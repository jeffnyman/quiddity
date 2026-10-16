/*
 * dictionary.c: see zm/dictionary.h. Implements Spec §13.
 */

#include "zm/dictionary.h"

#include <string.h>

#include "zm/fatal.h"
#include "zm/header.h"
#include "zm/text.h"

void zm_dictionary_read(const zm_memory *mem, uint32_t address, zm_dictionary *dict)
{
    uint16_t count;

    /* Spec §13.2: a count of separators, the separators, the entry
     * length, and the number of entries. */
    dict->address = address;
    dict->separator_count = zm_read_byte(mem, address);
    dict->separators = address + 1u;
    dict->entry_length = zm_read_byte(mem, dict->separators + dict->separator_count);
    count = zm_read_word(mem, dict->separators + dict->separator_count + 1u);
    dict->entries = dict->separators + dict->separator_count + 3u;

    /* Spec §15 tokenise: a negative count means unsorted. */
    if (count & 0x8000u) {
        dict->sorted = 0;
        dict->entry_count = (uint16_t)(0x10000u - count);
    } else {
        dict->sorted = 1;
        dict->entry_count = count;
    }

    if (dict->entry_length < zm_text_encoded_length(mem)) {
        zm_fatal("dictionary at $%05x has entries of %u bytes, too short for a word in Version %u",
                 (unsigned)address, (unsigned)dict->entry_length, (unsigned)mem->version);
    }
}

void zm_dictionary_read_main(const zm_memory *mem, zm_dictionary *dict)
{
    zm_dictionary_read(mem, zm_read_word(mem, ZM_HEADER_DICTIONARY), dict);
}

int zm_dictionary_is_separator(const zm_memory *mem, const zm_dictionary *dict, uint16_t zscii)
{
    for (unsigned i = 0; i < dict->separator_count; i++) {
        if (zm_read_byte(mem, dict->separators + i) == zscii) {
            return 1;
        }
    }
    return 0;
}

/* Compare an encoded word with the entry at an index, as the unsigned
 * big-endian numbers Spec §13.5 orders them by; memcmp on the bytes is
 * exactly that comparison. */
static int compare_entry(const zm_memory *mem, const zm_dictionary *dict, unsigned length, const uint8_t *encoded, uint32_t index)
{
    uint32_t entry = dict->entries + index * dict->entry_length;
    uint8_t bytes[ZM_TEXT_ENCODED_MAX];

    for (unsigned i = 0; i < length; i++) {
        bytes[i] = zm_read_byte(mem, entry + i);
    }
    return memcmp(encoded, bytes, length);
}

uint32_t zm_dictionary_lookup(const zm_memory *mem, const zm_dictionary *dict, const uint8_t *encoded)
{
    unsigned length = zm_text_encoded_length(mem);

    if (dict->sorted) {
        /* Spec §13.5 and its Remarks: the order exists so that this
         * binary chop is possible. */
        uint32_t low = 0;
        uint32_t high = dict->entry_count;

        while (low < high) {
            uint32_t middle = low + (high - low) / 2u;
            int order = compare_entry(mem, dict, length, encoded, middle);

            if (order == 0) {
                return dict->entries + middle * dict->entry_length;
            }
            if (order < 0) {
                high = middle;
            } else {
                low = middle + 1u;
            }
        }
        return 0;
    }

    for (uint32_t i = 0; i < dict->entry_count; i++) {
        if (compare_entry(mem, dict, length, encoded, i) == 0) {
            return dict->entries + i * dict->entry_length;
        }
    }
    return 0;
}

/*
 * Lexing (Spec §13.6).
 */

void zm_lexer_tokenise(zm_memory *mem, uint32_t text_buffer, uint32_t parse_buffer,
                       const zm_dictionary *dict, int keep_unrecognised)
{
    uint32_t text;          /* byte address of the first character */
    unsigned length;        /* characters in the buffer */
    unsigned max_words = zm_read_byte(mem, parse_buffer);
    unsigned words = 0;
    unsigned i = 0;

    /* Spec §15 read: where the text is depends on the Version. In
     * Versions 1 to 4 it runs to a zero byte, and never past the room
     * byte 0 promised. */
    if (mem->version <= 4) {
        unsigned room = zm_read_byte(mem, text_buffer);

        text = text_buffer + 1u;
        length = 0;
        while (length < room && zm_read_byte(mem, text + length) != 0) {
            length++;
        }
    } else {
        text = text_buffer + 2u;
        length = zm_read_byte(mem, text_buffer + 1u);
    }

    while (i < length && words < max_words) {
        unsigned start;
        unsigned word_length;
        uint16_t zscii[256];
        uint8_t encoded[ZM_TEXT_ENCODED_MAX];
        uint32_t entry;
        uint32_t block;

        /* Spec §13.6.1: spaces divide words and are otherwise ignored;
         * a separator divides words and is a word by itself. */
        if (zm_read_byte(mem, text + i) == ZM_ZSCII_SPACE) {
            i++;
            continue;
        }
        start = i;
        if (zm_dictionary_is_separator(mem, dict, zm_read_byte(mem, text + i))) {
            i++;
        } else {
            while (i < length) {
                uint8_t c = zm_read_byte(mem, text + i);

                if (c == ZM_ZSCII_SPACE || zm_dictionary_is_separator(mem, dict, c)) {
                    break;
                }
                i++;
            }
        }
        word_length = i - start;

        /* Spec §13.6.2: encode and look up. */
        for (unsigned k = 0; k < word_length; k++) {
            zscii[k] = zm_read_byte(mem, text + start + k);
        }
        zm_text_encode(mem, zscii, word_length, encoded);
        entry = zm_dictionary_lookup(mem, dict, encoded);

        /* Spec §13.6.3, §15 read: four bytes per word. The position is
         * counted from the start of the text buffer, so the game can
         * index the buffer with it directly. */
        block = parse_buffer + 2u + 4u * words;
        if (entry != 0 || !keep_unrecognised) {
            zm_write_word(mem, block, (uint16_t)entry);
            zm_write_byte(mem, block + 2u, (uint8_t)word_length);
            zm_write_byte(mem, block + 3u, (uint8_t)(text - text_buffer + start));
        }
        words++;
    }

    zm_write_byte(mem, parse_buffer + 1u, (uint8_t)words);
}
