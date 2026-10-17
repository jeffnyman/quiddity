/*
 * zm/blorb.h: resource files (Blorb 2.0.4, mirrored in specs/zmachine).
 *
 * Spec §8.8.6: pictures "are not stored in the story file (or the
 * Z-machine) itself, and the interpreter is simply expected to know where
 * to find them." Blorb is where. It is an IFF container whose FORM type is
 * IFRS (Blorb §0): a resource index first (§1), then one chunk per
 * resource, which for a Z-machine game means pictures (§2), sounds (§3),
 * and sometimes the story file itself (§5). A story file packed inside
 * its Blorb is a .zblorb; an Infocom story kept as a bare .z6 has its
 * pictures in a .blb beside it.
 *
 * This module reads the container. It does not decode a PNG or a JPEG:
 * it hands the picture's bytes and its size in pixels to the front end,
 * which draws them however it can. What it does decide is the size a
 * picture has on the screen, by the scaling rules of Blorb §11.2, so that
 * picture_data (Spec §15) gets the same answer on every front end.
 */

#ifndef ZM_BLORB_H
#define ZM_BLORB_H

#include <stdint.h>

#include "zm/status.h"

struct zm_memory;

/* Blorb §2: how a picture is stored. */
typedef enum zm_picture_format {
    ZM_PICTURE_PNG,
    ZM_PICTURE_JPEG,
    ZM_PICTURE_RECT,    /* Blorb §2.3: a placeholder with a size and no image */
    ZM_PICTURE_OTHER
} zm_picture_format;

typedef struct zm_picture {
    unsigned number;
    zm_picture_format format;

    /* The picture file's bytes, inside the resource file, and their
     * count; NULL and 0 for a placeholder rectangle. */
    const uint8_t *data;
    uint32_t size;

    /* The picture's own size in pixels (Blorb §2.1, §2.2, §2.3). Zero
     * when the file could not be measured. */
    unsigned width;
    unsigned height;

    /* Blorb §11.2: the standard window size the picture was drawn for,
     * and its standard, minimum, and maximum scaling ratios. A picture
     * with no entry in the resolution chunk, or in a file without one,
     * is not scalable: standard_width and standard_height are 0 and it
     * is shown one pixel per pixel. A ratio of 0/0 means no limit. */
    unsigned standard_width;
    unsigned standard_height;
    uint32_t ratio_num, ratio_den;
    uint32_t min_num, min_den;
    uint32_t max_num, max_den;
} zm_picture;

typedef struct zm_blorb {
    uint8_t *data;
    uint32_t size;

    /* Blorb §1: where the resource index's entries begin, and how many
     * there are. */
    uint32_t index;
    uint32_t count;

    /* Blorb §11.2: the resolution chunk's contents, or 0 if there is
     * none. */
    uint32_t resolution;

    /* Blorb §11.1: the release number of the resource file; 0 if it
     * gave none. */
    uint16_t release;

    /* Blorb §6: the game identifier, in Quetzal's IFhd layout, or 0 if
     * there is none. */
    uint32_t identifier;
} zm_blorb;

/* Whether a buffer begins like a Blorb file: "FORM", a length, "IFRS". */
int zm_blorb_looks_like(const uint8_t *bytes, uint32_t size);

/* Read a resource file. On success the Blorb owns a copy of the bytes
 * until zm_blorb_free(). A file that is not a Blorb at all gives
 * ZM_ERROR_NOT_A_BLORB; one whose index is damaged, ZM_ERROR_BAD_BLORB;
 * for ZM_ERROR_IO, errno holds the system's reason. */
zm_status zm_blorb_load_file(zm_blorb *b, const char *path);
zm_status zm_blorb_load_bytes(zm_blorb *b, const uint8_t *bytes, uint32_t size);
void zm_blorb_free(zm_blorb *b);

/* Find the Z-code story inside a Blorb (Blorb §5: usage "Exec", number
 * 0, chunk type "ZCOD") without building a zm_blorb, for the loader in
 * zm/memory.h. Returns ZM_OK with the story's offset and length,
 * ZM_ERROR_BAD_BLORB if the index is damaged, or ZM_ERROR_BLORB_NO_STORY
 * if there is no Z-code in it. */
zm_status zm_blorb_find_story(const uint8_t *bytes, uint32_t size, uint32_t *offset, uint32_t *length);

/* Blorb §6: whether the resource file belongs to this story, judged by
 * the release number, serial code, and checksum of its IFhd chunk. A
 * file without one is taken to belong. */
int zm_blorb_matches(const zm_blorb *b, const struct zm_memory *mem);

/* How many pictures the file holds (Blorb §1), and its release number
 * (Blorb §11.1): what picture_data 0 reports (Spec §15). */
unsigned zm_blorb_picture_count(const zm_blorb *b);
uint16_t zm_blorb_release(const zm_blorb *b);

/* Describe a picture by number. Returns 0 if there is no such picture. */
int zm_blorb_picture(const zm_blorb *b, unsigned number, zm_picture *picture);

/* The size a picture has on a screen of the given size, in that screen's
 * pixels, by Blorb §11.2: a scalable picture is scaled by the Elbow Room
 * Factor, the smaller of the screen's width and height over the standard
 * window's, times its standard ratio, held within its minimum and
 * maximum; any other picture keeps its own size. A picture that would
 * vanish keeps a pixel in each direction. */
void zm_picture_scaled_size(const zm_picture *picture, unsigned screen_width, unsigned screen_height,
                            unsigned *width, unsigned *height);

#endif /* ZM_BLORB_H */
