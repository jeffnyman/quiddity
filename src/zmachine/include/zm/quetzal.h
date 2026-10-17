/*
 * zm/quetzal.h: saved games in the Quetzal format.
 *
 * Spec §6.1.1.1 leaves the format of a saved game to the interpreter, and
 * Quetzal (Martin Frost, 1997; mirrored at specs/zmachine/quetzal.html)
 * is the format the interpreters then agreed on, so a game saved here can
 * be restored by Frotz and the others, and theirs here. A Quetzal file is
 * an IFF FORM of type IFZS holding three chunks: IFhd names the story the
 * game belongs to (Quetzal §5), CMem or UMem carries dynamic memory
 * (§3), and Stks the stack and the routine call state (§4). Between them
 * that is the whole state of play of Spec §6.1; the program counter
 * travels in IFhd.
 *
 * Neither function here touches a file. The machine asks its front end to
 * keep and fetch the bytes (write_file and read_file in zm/io.h), so the
 * core stays clear of file names, prompts, and directories, which differ
 * from one front end to the next.
 */

#ifndef ZM_QUETZAL_H
#define ZM_QUETZAL_H

#include <stdint.h>

#include "zm/machine.h"
#include "zm/status.h"

/* Write the state of play as a Quetzal file into a buffer obtained from
 * malloc, which the caller frees. pc is the program counter the file
 * records, which by Quetzal §5.8 is the address of the save instruction's
 * branch data (Versions 1 to 3) or store byte (Version 4 and later), so
 * that a restore can act on them again. */
zm_status zm_quetzal_write(const zm_machine *m, uint32_t pc, uint8_t **data, uint32_t *size);

/* Read a Quetzal file and make it the state of play, as Spec §6.1.2 asks:
 * everything is written back except Flags 2, and the header fields the
 * interpreter owns are rewritten. The PC becomes the one the file
 * recorded; the caller completes the save instruction found there.
 *
 * Returns ZM_ERROR_NOT_A_SAVE when the bytes are not an IFZS form,
 * ZM_ERROR_WRONG_STORY when they were saved from a different story
 * (Quetzal §5.3), and ZM_ERROR_BAD_SAVE when the file is malformed or
 * does not fit this machine (Quetzal §3.5, §4.8). On any failure the
 * machine is left exactly as it was. */
zm_status zm_quetzal_read(zm_machine *m, const uint8_t *data, uint32_t size);

#endif /* ZM_QUETZAL_H */
