/*
 * status.c: text for the result codes in zm/status.h.
 */

#include "zm/status.h"

const char *zm_status_message(zm_status status)
{
    switch (status) {
    case ZM_OK:
        return "no error";
    case ZM_ERROR_IO:
        return "cannot read the story file";
    case ZM_ERROR_OUT_OF_MEMORY:
        return "out of memory";
    case ZM_ERROR_TOO_SMALL:
        return "story file is too small to contain a header";
    case ZM_ERROR_TOO_LARGE:
        return "story file is larger than its Version allows";
    case ZM_ERROR_BAD_VERSION:
        return "story file has an unknown Version number";
    case ZM_ERROR_BAD_MEMORY_MAP:
        return "story file header describes an impossible memory map";
    case ZM_ERROR_BLORB_UNSUPPORTED:
        return "Blorb files are not supported yet";
    case ZM_ERROR_NOT_A_SAVE:
        return "not a Quetzal saved game";
    case ZM_ERROR_WRONG_STORY:
        return "saved game is from a different story";
    case ZM_ERROR_BAD_SAVE:
        return "saved game is damaged";
    }

    return "unknown error";
}
