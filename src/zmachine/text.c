/*
 * text.c: see zm/text.h. Implements Spec §3.
 *
 * The decoder is in two layers. The lower one pulls Z-characters out of
 * memory three at a time and knows nothing about what they mean. The
 * upper one is the state machine of §3.2 to §3.5: it holds the current
 * alphabet, interprets shifts, escapes, and abbreviations, and emits ZSCII.
 * Keeping them apart means the "three per word, top bit ends the string"
 * rule lives in exactly one place.
 */

#include "zm/text.h"

#include <stddef.h>

#include "zm/fatal.h"
#include "zm/header.h"

/* The three alphabets (Spec §3.2.1). */
enum { A0 = 0, A1 = 1, A2 = 2 };

/* Spec §3.5.3: what Z-characters 6 to 31 mean in each alphabet, for
 * Versions 2 to 4 (and 5 and later when the story supplies no table of
 * its own). Index with z - 6. The first two entries of A2 are never
 * consulted: Z-character 6 is the ZSCII escape (§3.4) and 7 is new-line,
 * both handled before the table is reached. They are filled with the
 * characters the Standard draws in that position so the rows line up
 * with the printed table. */
static const char alphabets[3][27] = {
    "abcdefghijklmnopqrstuvwxyz",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    " ^0123456789.,!?_#'\"/\\-:()"
};

/* Spec §3.5.4: Version 1 has no new-line in A2, which makes room for '<'
 * and shifts everything from '0' one place left. */
static const char alphabet_v1_a2[27] = " 0123456789.,!?_#'\"/\\<-:()";

/*
 * Reading Z-characters (Spec §3.2).
 */

typedef struct {
    const zm_memory *mem;
    uint32_t address;     /* of the next word to read */
    uint8_t chars[3];     /* the Z-characters of the word last read */
    unsigned remaining;   /* how many of them have not been handed out */
    int finished;         /* the word with the end bit has been read */
} zreader;

static void zreader_init(zreader *r, const zm_memory *mem, uint32_t address)
{
    r->mem = mem;
    r->address = address;
    r->remaining = 0;
    r->finished = 0;
}

/* Fetch the next Z-character into *z. Returns 0 once the string is over:
 * the end bit has been seen and the last word's three characters are
 * used up. */
static int zreader_next(zreader *r, uint8_t *z)
{
    if (r->remaining == 0) {
        uint16_t word;

        if (r->finished) {
            return 0;
        }

        word = zm_read_word(r->mem, r->address);
        r->address += 2;

        /* Spec §3.2: bit 15 marks the last word of the string; the three
         * five-bit Z-characters below it are stored most significant
         * first. */
        r->finished = (word & 0x8000u) != 0;
        r->chars[0] = (uint8_t)((word >> 10) & 0x1f);
        r->chars[1] = (uint8_t)((word >> 5) & 0x1f);
        r->chars[2] = (uint8_t)(word & 0x1f);
        r->remaining = 3;
    }

    *z = r->chars[3 - r->remaining];
    r->remaining--;
    return 1;
}

/*
 * Interpreting them (Spec §3.2 to §3.5).
 */

typedef struct {
    const zm_memory *mem;
    uint8_t version;

    /* Spec §3.5.5: byte address of the story's own alphabet table, or 0
     * to use the ones above. Only Version 5 and later may have one. */
    uint32_t alphabet_table;

    zm_text_sink sink;
    void *context;
} decoder;

static void decoder_init(decoder *d, const zm_memory *mem, zm_text_sink sink, void *context)
{
    d->mem = mem;
    d->version = mem->version;
    d->sink = sink;
    d->context = context;
    d->alphabet_table = mem->version >= 5 ? zm_read_word(mem, ZM_HEADER_ALPHABET_TABLE) : 0;
}

static void emit(const decoder *d, uint16_t zscii)
{
    d->sink(d->context, zscii);
}

/* Translate Z-character z (6 to 31) in the given alphabet (Spec §3.5). */
static uint16_t alphabet_lookup(const decoder *d, unsigned alphabet, uint8_t z)
{
    unsigned index = z - 6u;

    if (d->alphabet_table != 0) {
        /* Spec §3.5.5.1: 78 bytes, three blocks of 26 ZSCII values, one
         * block per alphabet. */
        return zm_read_byte(d->mem, d->alphabet_table + 26u * alphabet + index);
    }
    if (alphabet == A2 && d->version == 1) {
        return (uint8_t)alphabet_v1_a2[index];
    }
    return (uint8_t)alphabets[alphabet][index];
}

static void decode_string(const decoder *d, zreader *r, int in_abbreviation);

/* Spec §3.3: print abbreviation `number` (0 to 95). */
static void decode_abbreviation(const decoder *d, unsigned number)
{
    uint32_t table = zm_read_word(d->mem, ZM_HEADER_ABBREVIATIONS);
    uint16_t word_address = zm_read_word(d->mem, table + 2u * number);
    zreader r;

    /* The table holds word addresses, the only place in the Z-machine
     * that uses them (Spec §1.2.2): double to get a byte address. */
    zreader_init(&r, d->mem, 2u * word_address);
    decode_string(d, &r, 1);
}

static void decode_string(const decoder *d, zreader *r, int in_abbreviation)
{
    /* The alphabet to return to after a single character has been
     * printed. Only Versions 1 and 2 can change it, with shift locks
     * (Spec §3.2.2); from Version 3 on it is always A0 (Spec §3.2.3). */
    unsigned lock = A0;

    /* The alphabet the next character will be taken from. */
    unsigned current = A0;

    uint8_t z;

    while (zreader_next(r, &z)) {
        if (z == 0) {
            /* Spec §3.5.1. */
            emit(d, ZM_ZSCII_SPACE);
            current = lock;
        } else if (z == 1 && d->version == 1) {
            /* Spec §3.5.2. */
            emit(d, ZM_ZSCII_NEWLINE);
            current = lock;
        } else if (z == 1 || (z <= 3 && d->version >= 3)) {
            /* Spec §3.3: an abbreviation. Z-character 1 introduces one
             * from Version 2 on; 2 and 3 do so only from Version 3,
             * before which they are shifts. The next Z-character picks
             * the entry: 32(z-1)+x, so z chooses a bank of 32 and x the
             * entry within it. */
            uint8_t x;

            if (in_abbreviation) {
                /* Spec §3.3.1. */
                zm_fatal("abbreviation used inside abbreviation string at $%05x",
                         (unsigned)(r->address - 2));
            }
            if (!zreader_next(r, &x)) {
                /* Spec §3.6.1: the string ended with the construction
                 * half made; it is ignored. */
                break;
            }
            decode_abbreviation(d, 32u * (z - 1u) + x);

            /* The abbreviation is a string in its own right and does not
             * count as "the next character", so a shift that preceded
             * it still applies to whatever comes next. */
        } else if (z <= 3) {
            /* Spec §3.2.2, Versions 1 and 2 only: shift for the next
             * character. The table there reads, from the current
             * alphabet, A0 -> A1 -> A2 -> A0 for Z-character 2 and the
             * reverse for 3, which is a rotation by one or two places. */
            current = (current + (z == 2 ? 1u : 2u)) % 3u;
        } else if (z <= 5) {
            if (d->version <= 2) {
                /* Spec §3.2.2: shift lock, the same rotation but
                 * permanent. */
                lock = (current + (z == 4 ? 1u : 2u)) % 3u;
                current = lock;
            } else {
                /* Spec §3.2.3: 4 selects A1 and 5 selects A2, for one
                 * character only. */
                current = z - 3u;
            }
        } else if (current == A2 && z == 6) {
            /* Spec §3.4: ZSCII escape. The next two Z-characters are the
             * top and bottom five bits of a ten-bit ZSCII code. A custom
             * alphabet table does not change this (Spec §3.5.5.1). */
            uint8_t high, low;

            if (!zreader_next(r, &high) || !zreader_next(r, &low)) {
                /* Spec §3.6.1 again. */
                break;
            }
            emit(d, (uint16_t)((high << 5) | low));
            current = lock;
        } else if (current == A2 && z == 7 && d->version >= 2) {
            /* Spec §3.5.3: new-line, fixed even under a custom alphabet
             * table (Spec §3.5.5.1). Version 1 has '0' here instead
             * (Spec §3.5.4). */
            emit(d, ZM_ZSCII_NEWLINE);
            current = lock;
        } else {
            emit(d, alphabet_lookup(d, current, z));
            current = lock;
        }
    }
}

uint32_t zm_text_decode(const zm_memory *mem, uint32_t address, zm_text_sink sink, void *context)
{
    decoder d;
    zreader r;

    decoder_init(&d, mem, sink, context);
    zreader_init(&r, mem, address);
    decode_string(&d, &r, 0);

    /* The reader has consumed exactly the words of the string, so it is
     * sitting on the byte after the last one. */
    return r.address;
}

void zm_text_decode_abbreviation(const zm_memory *mem, unsigned number, zm_text_sink sink, void *context)
{
    decoder d;

    if (number >= 96) {
        zm_fatal("abbreviation %u does not exist; Spec §3.3 allows 0 to 95", number);
    }

    decoder_init(&d, mem, sink, context);
    decode_abbreviation(&d, number);
}

/*
 * ZSCII to Unicode (Spec §3.8).
 */

/* Spec §3.8.5.3, Table 1: the default translations for ZSCII 155 to 223.
 * These are the accented Latin letters, ligatures, and punctuation that
 * western European games need; the order is the Standard's, which is
 * not alphabetical, so it is listed by ZSCII range rather than by
 * meaning. */
static const uint16_t default_unicode[] = {
    /* 155-163: a" o" u" A" O" U" ss >> << */
    0x00e4, 0x00f6, 0x00fc, 0x00c4, 0x00d6, 0x00dc, 0x00df, 0x00bb, 0x00ab,
    /* 164-168: e" i" y" E" I" */
    0x00eb, 0x00ef, 0x00ff, 0x00cb, 0x00cf,
    /* 169-180: acute a e i o u y A E I O U Y */
    0x00e1, 0x00e9, 0x00ed, 0x00f3, 0x00fa, 0x00fd,
    0x00c1, 0x00c9, 0x00cd, 0x00d3, 0x00da, 0x00dd,
    /* 181-190: grave a e i o u A E I O U */
    0x00e0, 0x00e8, 0x00ec, 0x00f2, 0x00f9,
    0x00c0, 0x00c8, 0x00cc, 0x00d2, 0x00d9,
    /* 191-200: circumflex a e i o u A E I O U */
    0x00e2, 0x00ea, 0x00ee, 0x00f4, 0x00fb,
    0x00c2, 0x00ca, 0x00ce, 0x00d4, 0x00db,
    /* 201-204: a-ring A-ring o-slash O-slash */
    0x00e5, 0x00c5, 0x00f8, 0x00d8,
    /* 205-210: tilde a n o A N O */
    0x00e3, 0x00f1, 0x00f5, 0x00c3, 0x00d1, 0x00d5,
    /* 211-214: ae AE c-cedilla C-cedilla */
    0x00e6, 0x00c6, 0x00e7, 0x00c7,
    /* 215-218: thorn eth Thorn Eth */
    0x00fe, 0x00f0, 0x00de, 0x00d0,
    /* 219-223: pound oe OE inverted-! inverted-? */
    0x00a3, 0x0153, 0x0152, 0x00a1, 0x00bf
};

#define DEFAULT_UNICODE_COUNT (sizeof default_unicode / sizeof default_unicode[0])

uint16_t zm_zscii_to_unicode(const zm_memory *mem, uint16_t zscii)
{
    uint32_t table;
    unsigned index;

    /* Spec §3.8.3: this range is ASCII, and so also Unicode. */
    if (zscii >= 32 && zscii <= 126) {
        return zscii;
    }

    /* Everything outside the extra characters is a control code, an
     * input-only code, or undefined (Spec §3.8.2, §3.8.4, §3.8.6, §3.8.7).
     * None of them prints. */
    if (zscii < ZM_ZSCII_EXTRA_FIRST || zscii > ZM_ZSCII_EXTRA_LAST) {
        return 0;
    }
    index = zscii - ZM_ZSCII_EXTRA_FIRST;

    /* Spec §3.8.5.2: Version 5 and later may name a translation table in
     * word 3 of the header extension; everything else, including a
     * Version before 5, uses the default. zm_header_extension_word()
     * already answers 0 for those. */
    table = zm_header_extension_word(mem, ZM_HEADER_EXT_UNICODE_TABLE);
    if (table == 0) {
        if (index >= DEFAULT_UNICODE_COUNT) {
            return 0;
        }
        return default_unicode[index];
    }

    /* Spec §3.8.5.2.1: a count byte, then that many words. ZSCII codes
     * past the count are undefined (Spec §3.8.5.2.2). */
    if (index >= (unsigned)zm_read_byte(mem, table)) {
        return 0;
    }
    return zm_read_word(mem, table + 1u + 2u * index);
}

uint16_t zm_unicode_to_zscii(const zm_memory *mem, uint16_t unicode)
{
    if (unicode >= 32 && unicode <= 126) {
        return unicode;
    }
    if (unicode == 0) {
        return 0;
    }

    /* Spec §3.8.5: the extra characters, through the story's table. */
    for (uint16_t zscii = ZM_ZSCII_EXTRA_FIRST; zscii <= ZM_ZSCII_EXTRA_LAST; zscii++) {
        if (zm_zscii_to_unicode(mem, zscii) == unicode) {
            return zscii;
        }
    }
    return 0;
}

/*
 * Encoding (Spec §3.7): turning what the player typed into the form the
 * dictionary stores, so the two can be compared byte for byte.
 */

/* Which alphabet holds a ZSCII character, and as which Z-character.
 * Returns -1 if none does, in which case the ZSCII escape is needed. */
static int alphabet_find(const decoder *d, uint16_t zscii, uint8_t *z)
{
    for (unsigned alphabet = A0; alphabet <= A2; alphabet++) {
        /* Z-character 6 of A2 is the escape (Spec §3.4) and 7 is new-line
         * from Version 2 (Spec §3.5.3), whatever the table says there. */
        unsigned first = alphabet == A2 ? (d->version == 1 ? 7u : 8u) : 6u;

        for (unsigned candidate = first; candidate <= 31; candidate++) {
            if (alphabet_lookup(d, alphabet, (uint8_t)candidate) == zscii) {
                *z = (uint8_t)candidate;
                return (int)alphabet;
            }
        }
    }
    return -1;
}

/* The alphabet a character will be encoded from, with A2 for one that
 * needs the escape, or -1 for a space, which is Z-character 0 in any
 * alphabet. Used for the look-ahead of Spec §3.7.1. */
static int alphabet_of(const decoder *d, uint16_t zscii)
{
    uint8_t z;
    int alphabet;

    if (zscii == ZM_ZSCII_SPACE) {
        return -1;
    }
    alphabet = alphabet_find(d, zscii, &z);
    return alphabet < 0 ? A2 : alphabet;
}

typedef struct {
    uint8_t zchars[9];
    unsigned count;
    unsigned limit;

    /* Versions 1 and 2 only: the alphabet the shift lock is on. Later
     * Versions have no locks and always return to A0 (Spec §3.2.3). */
    unsigned locked;
} encoder;

static void put_zchar(encoder *e, uint8_t z)
{
    if (e->count < e->limit) {
        e->zchars[e->count++] = z;
    }
}

/* Emit whatever shift brings the next character from the given
 * alphabet. */
static void shift_to(encoder *e, const decoder *d, unsigned alphabet, int next_alphabet)
{
    if (d->version >= 3) {
        /* Spec §3.2.3: 4 and 5 are single shifts to A1 and A2. */
        if (alphabet == A1) {
            put_zchar(e, 4);
        } else if (alphabet == A2) {
            put_zchar(e, 5);
        }
        return;
    }

    /* Spec §3.2.2: in Versions 1 and 2, shifts are relative to the
     * current alphabet, 2 and 3 for one character, 4 and 5 to lock. Spec
     * §3.7.1: a dictionary word locks when the following character comes
     * from the same alphabet. */
    if (alphabet == e->locked) {
        return;
    }
    {
        int lock = next_alphabet == (int)alphabet;
        unsigned up = (e->locked + 1u) % 3u;

        if (alphabet == up) {
            put_zchar(e, lock ? 4 : 2);
        } else {
            put_zchar(e, lock ? 5 : 3);
        }
        if (lock) {
            e->locked = alphabet;
        }
    }
}

unsigned zm_text_encoded_length(const zm_memory *mem)
{
    return mem->version <= 3 ? 4u : 6u;
}

unsigned zm_text_encode(const zm_memory *mem, const uint16_t *zscii, unsigned length, uint8_t *out)
{
    decoder d;
    encoder e;
    unsigned words;

    decoder_init(&d, mem, NULL, NULL);
    e.count = 0;
    e.limit = mem->version <= 3 ? 6u : 9u;
    e.locked = A0;

    for (unsigned i = 0; i < length && e.count < e.limit; i++) {
        uint16_t c = zscii[i];
        uint8_t z;
        int alphabet;
        int next = i + 1 < length ? alphabet_of(&d, zscii[i + 1]) : -1;

        if (c == ZM_ZSCII_SPACE) {
            put_zchar(&e, 0);                       /* Spec §3.5.1 */
            continue;
        }

        alphabet = alphabet_find(&d, c, &z);
        if (alphabet < 0) {
            /* Spec §3.4: the ten-bit ZSCII code after A2's Z-character
             * 6, high five bits first. */
            shift_to(&e, &d, A2, next);
            put_zchar(&e, 6);
            put_zchar(&e, (uint8_t)(c >> 5));
            put_zchar(&e, (uint8_t)(c & 31u));
        } else {
            shift_to(&e, &d, (unsigned)alphabet, next);
            put_zchar(&e, z);
        }
    }

    /* Spec §3.7: pad with 5s. */
    while (e.count < e.limit) {
        e.zchars[e.count++] = 5;
    }

    /* Spec §3.2: three Z-characters to a word, end bit on the last. */
    words = e.limit / 3u;
    for (unsigned w = 0; w < words; w++) {
        uint16_t word = (uint16_t)((e.zchars[3 * w] << 10) | (e.zchars[3 * w + 1] << 5) | e.zchars[3 * w + 2]);

        if (w == words - 1) {
            word |= 0x8000u;
        }
        out[2 * w] = (uint8_t)(word >> 8);
        out[2 * w + 1] = (uint8_t)(word & 0xffu);
    }
    return 2u * words;
}
