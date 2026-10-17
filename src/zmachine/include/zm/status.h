/*
 * zm/status.h: result codes for operations that can fail in ordinary ways.
 *
 * The Z-Machine library never prints or exits on its own. Anything that can
 * go wrong because of the outside world (a missing file, a story file that
 * is not what it claims to be) is reported through one of these codes and
 * left to the front end to explain. Faults inside the running machine, such
 * as an instruction reading past the end of memory, are a different kind of
 * failure and go through zm/fatal.h instead.
 */

#ifndef ZM_STATUS_H
#define ZM_STATUS_H

typedef enum zm_status {
    ZM_OK = 0,

    /* The file could not be read. errno holds the reason. */
    ZM_ERROR_IO,

    /* malloc failed. */
    ZM_ERROR_OUT_OF_MEMORY,

    /* The file is shorter than the 64-byte header (Spec §1.1). */
    ZM_ERROR_TOO_SMALL,

    /* The file exceeds the size limit for its Version (Spec §1.1.4). */
    ZM_ERROR_TOO_LARGE,

    /* The Version byte is not 1 to 8 (Spec §11.1). */
    ZM_ERROR_BAD_VERSION,

    /* The static memory base in the header describes an impossible
     * memory map (Spec §1.1). */
    ZM_ERROR_BAD_MEMORY_MAP,

    /* Offered as a resource file, the bytes are not a Blorb (Blorb §0). */
    ZM_ERROR_NOT_A_BLORB,

    /* The Blorb file's resource index is damaged (Blorb §1). */
    ZM_ERROR_BAD_BLORB,

    /* The Blorb file holds no Z-code story (Blorb §5), so there is
     * nothing in it to run. */
    ZM_ERROR_BLORB_NO_STORY,

    /* Offered as a saved game, the bytes are not a Quetzal file at all
     * (Quetzal §2.1, §8.5). */
    ZM_ERROR_NOT_A_SAVE,

    /* The saved game belongs to a different story (Quetzal §5.3). */
    ZM_ERROR_WRONG_STORY,

    /* The saved game is malformed or too large for this interpreter
     * (Quetzal §3.5, §4.8). */
    ZM_ERROR_BAD_SAVE
} zm_status;

/* A short, human-readable description of a status code. */
const char *zm_status_message(zm_status status);

#endif /* ZM_STATUS_H */
