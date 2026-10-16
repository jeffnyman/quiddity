/*
 * zm/dictionary.h: the dictionary and lexical analysis.
 *
 * Implements Spec §13. The dictionary is a sorted table of encoded words
 * in static memory, found through the header word at $08, and lexing is
 * the business of cutting the player's typed line into words, encoding
 * each as Spec §3.7 says, looking it up, and writing a parse table that
 * the game's own parser then reads. The read and tokenise opcodes are the
 * two callers (Spec §13.6); tokenise may supply a dictionary of its own.
 */

#ifndef ZM_DICTIONARY_H
#define ZM_DICTIONARY_H

#include <stdint.h>

#include "zm/memory.h"

typedef struct zm_dictionary {
    /* Byte address of the table's header (Spec §13.2). */
    uint32_t address;

    /* The word separators: ZSCII codes that end a word and count as one
     * (Spec §13.2, §13.6.1). separators is the byte address of the list. */
    uint8_t separator_count;
    uint32_t separators;

    /* Bytes per entry: the encoded word plus data the game uses (Spec
     * §13.3, §13.4). */
    uint8_t entry_length;

    /* How many entries, and whether they are in the numerical order Spec
     * §13.5 requires. A user dictionary given to tokenise may declare a
     * negative count to mean "this many, unsorted" (Spec §15 tokenise). */
    uint16_t entry_count;
    int sorted;

    /* Byte address of the first entry. */
    uint32_t entries;
} zm_dictionary;

/* Read a dictionary header. Faults if the entry length is too short to
 * hold an encoded word (Spec §13.2). */
void zm_dictionary_read(const zm_memory *mem, uint32_t address, zm_dictionary *dict);

/* The game's own dictionary, from the header (Spec §13.1). */
void zm_dictionary_read_main(const zm_memory *mem, zm_dictionary *dict);

/* Is a ZSCII character one of the dictionary's word separators? */
int zm_dictionary_is_separator(const zm_memory *mem, const zm_dictionary *dict, uint16_t zscii);

/* Find an encoded word (zm_text_encoded_length() bytes, as
 * zm_text_encode() writes it). Returns the byte address of its entry, or
 * 0 if it is not there (Spec §13.6.2, §15 read). */
uint32_t zm_dictionary_lookup(const zm_memory *mem, const zm_dictionary *dict, const uint8_t *encoded);

/* Lexical analysis (Spec §13.6, §15 read, §15 tokenise). The text buffer
 * is laid out as read leaves it: in Versions 1 to 4 the text starts at
 * byte 1 and ends with a zero; from Version 5 byte 1 holds its length and
 * the text starts at byte 2. Byte 0 of the parse buffer is the most words
 * it can take; byte 1 receives the number found, and then four bytes per
 * word: the dictionary address (or 0), the word's length, and its
 * position in the text buffer. With keep_unrecognised set, words not in
 * the dictionary leave their four bytes untouched. */
void zm_lexer_tokenise(zm_memory *mem, uint32_t text_buffer, uint32_t parse_buffer,
                       const zm_dictionary *dict, int keep_unrecognised);

#endif /* ZM_DICTIONARY_H */
