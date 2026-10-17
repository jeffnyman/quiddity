/*
 * blorb.c: see zm/blorb.h. Reads the IFF container of Blorb §0, §1, and
 * §15, the picture chunks of §2, the executable chunk of §5, the game
 * identifier of §6, and the Z-machine chunks of §11.1 and §11.2.
 */

#include "zm/blorb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zm/header.h"
#include "zm/memory.h"

/*
 * IFF (Blorb §15): a file is a FORM chunk whose contents are chunks; each
 * chunk is a four-letter type, a four-byte big-endian length, and that
 * many bytes, padded to an even length. The resource index points at
 * chunks by their offset from the start of the file.
 */

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static int has_type(const uint8_t *p, const char *type)
{
    return memcmp(p, type, 4) == 0;
}

int zm_blorb_looks_like(const uint8_t *bytes, uint32_t size)
{
    return size >= 12 && has_type(bytes, "FORM") && has_type(bytes + 8, "IFRS");
}

/* The length of the chunk at an offset, or -1 if no chunk fits there. */
static int64_t chunk_length(const uint8_t *bytes, uint32_t size, uint32_t offset)
{
    uint32_t length;

    if (offset > size || size - offset < 8) {
        return -1;
    }
    length = get_u32(bytes + offset + 4);
    if (length > size - offset - 8) {
        return -1;
    }
    return (int64_t)length;
}

/* Blorb §0, §1: find the resource index. It must be the first chunk, but
 * a file that put it later would be no harder to read, so the whole file
 * is searched. Returns the offset of the index's entries and their
 * count, or ZM_ERROR_BAD_BLORB. */
static zm_status find_index(const uint8_t *bytes, uint32_t size, uint32_t *index, uint32_t *count)
{
    uint32_t offset = 12;

    while (offset + 8 <= size) {
        int64_t length = chunk_length(bytes, size, offset);

        if (length < 0) {
            return ZM_ERROR_BAD_BLORB;
        }
        if (has_type(bytes + offset, "RIdx")) {
            uint32_t entries;

            if (length < 4) {
                return ZM_ERROR_BAD_BLORB;
            }
            entries = get_u32(bytes + offset + 8);
            if ((uint64_t)entries * 12u > (uint64_t)length - 4u) {
                return ZM_ERROR_BAD_BLORB;
            }
            *index = offset + 12;
            *count = entries;
            return ZM_OK;
        }
        offset += 8u + (uint32_t)length + ((uint32_t)length & 1u);
    }
    return ZM_ERROR_BAD_BLORB;
}

/* Blorb §1: the chunk offset of the resource with this usage and number,
 * or 0 if the index has no such entry. Checked to be a chunk. */
static uint32_t find_resource(const uint8_t *bytes, uint32_t size, uint32_t index, uint32_t count,
                              const char *usage, uint32_t number)
{
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *entry = bytes + index + 12u * i;

        if (has_type(entry, usage) && get_u32(entry + 4) == number) {
            uint32_t start = get_u32(entry + 8);

            return chunk_length(bytes, size, start) >= 0 ? start : 0;
        }
    }
    return 0;
}

zm_status zm_blorb_find_story(const uint8_t *bytes, uint32_t size, uint32_t *offset, uint32_t *length)
{
    uint32_t index;
    uint32_t count;
    uint32_t start;
    zm_status status;

    if (!zm_blorb_looks_like(bytes, size)) {
        return ZM_ERROR_NOT_A_BLORB;
    }
    status = find_index(bytes, size, &index, &count);
    if (status != ZM_OK) {
        return status;
    }

    /* Blorb §5: the executable is usage "Exec", number 0, and for the
     * Z-machine its chunk type is "ZCOD". */
    start = find_resource(bytes, size, index, count, "Exec", 0);
    if (start == 0 || !has_type(bytes + start, "ZCOD")) {
        return ZM_ERROR_BLORB_NO_STORY;
    }
    *offset = start + 8;
    *length = (uint32_t)chunk_length(bytes, size, start);
    return ZM_OK;
}

/*
 * Loading.
 */

static zm_status establish(zm_blorb *b, uint8_t *bytes, uint32_t size)
{
    uint32_t offset = 12;
    zm_status status;

    memset(b, 0, sizeof *b);
    if (!zm_blorb_looks_like(bytes, size)) {
        return ZM_ERROR_NOT_A_BLORB;
    }
    status = find_index(bytes, size, &b->index, &b->count);
    if (status != ZM_OK) {
        return status;
    }
    b->data = bytes;
    b->size = size;

    /* The optional chunks (Blorb §0) may be anywhere after the index. */
    while (offset + 8 <= size) {
        int64_t length = chunk_length(bytes, size, offset);

        if (length < 0) {
            break;
        }
        if (has_type(bytes + offset, "RelN") && length >= 2) {
            b->release = get_u16(bytes + offset + 8);                   /* Blorb §11.1 */
        } else if (has_type(bytes + offset, "Reso") && length >= 24) {
            b->resolution = offset;                                     /* Blorb §11.2 */
        } else if (has_type(bytes + offset, "IFhd") && length >= 13) {
            b->identifier = offset + 8;                                 /* Blorb §6 */
        }
        offset += 8u + (uint32_t)length + ((uint32_t)length & 1u);
    }
    return ZM_OK;
}

zm_status zm_blorb_load_bytes(zm_blorb *b, const uint8_t *bytes, uint32_t size)
{
    uint8_t *copy = malloc(size > 0 ? size : 1);
    zm_status status;

    if (copy == NULL) {
        memset(b, 0, sizeof *b);
        return ZM_ERROR_OUT_OF_MEMORY;
    }
    memcpy(copy, bytes, size);
    status = establish(b, copy, size);
    if (status != ZM_OK) {
        free(copy);
        memset(b, 0, sizeof *b);
    }
    return status;
}

zm_status zm_blorb_load_file(zm_blorb *b, const char *path)
{
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *bytes;
    zm_status status;

    memset(b, 0, sizeof *b);
    if (file == NULL) {
        return ZM_ERROR_IO;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return ZM_ERROR_IO;
    }
    bytes = malloc(length > 0 ? (size_t)length : 1);
    if (bytes == NULL) {
        fclose(file);
        return ZM_ERROR_OUT_OF_MEMORY;
    }
    if (fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return ZM_ERROR_IO;
    }
    fclose(file);
    status = establish(b, bytes, (uint32_t)length);
    if (status != ZM_OK) {
        free(bytes);
        memset(b, 0, sizeof *b);
    }
    return status;
}

void zm_blorb_free(zm_blorb *b)
{
    free(b->data);
    memset(b, 0, sizeof *b);
}

/*
 * The story the file belongs to.
 */

int zm_blorb_matches(const zm_blorb *b, const struct zm_memory *mem)
{
    const uint8_t *id;
    uint16_t checksum;

    if (b->identifier == 0) {
        return 1;
    }

    /* Blorb §6 defers to Quetzal §5: release, serial code, checksum. The
     * story's own values are read from the pristine copy of its header,
     * and a story too early to carry a checksum is matched on the sum
     * computed for it. */
    id = b->data + b->identifier;
    checksum = get_u16(mem->original + ZM_HEADER_CHECKSUM);
    if (get_u16(id) != get_u16(mem->original + ZM_HEADER_RELEASE)) {
        return 0;
    }
    if (memcmp(id + 2, mem->original + ZM_HEADER_SERIAL, 6) != 0) {
        return 0;
    }
    return get_u16(id + 8) == checksum
        || (checksum == 0 && get_u16(id + 8) == zm_header_compute_checksum(mem));
}

/*
 * Pictures.
 */

unsigned zm_blorb_picture_count(const zm_blorb *b)
{
    unsigned count = 0;

    for (uint32_t i = 0; i < b->count; i++) {
        if (has_type(b->data + b->index + 12u * i, "Pict")) {
            count++;
        }
    }
    return count;
}

uint16_t zm_blorb_release(const zm_blorb *b)
{
    return b->release;
}

/* A PNG file is an eight-byte signature and then chunks, the first of
 * which must be IHDR, whose first two words are the width and height. */
static void measure_png(const uint8_t *data, uint32_t size, zm_picture *p)
{
    static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };

    if (size >= 24 && memcmp(data, signature, 8) == 0 && has_type(data + 12, "IHDR")) {
        p->width = get_u32(data + 16);
        p->height = get_u32(data + 20);
    }
}

/* A JPEG file is a sequence of marker segments, each $FF, a marker byte,
 * and for most markers a two-byte length that counts itself. The frame
 * header (markers $C0 to $CF, less the ones that are not frames) carries
 * the sample precision, then the height and width. */
static void measure_jpeg(const uint8_t *data, uint32_t size, zm_picture *p)
{
    uint32_t pos = 2;

    if (size < 4 || data[0] != 0xff || data[1] != 0xd8) {
        return;
    }
    while (pos + 4 <= size && data[pos] == 0xff) {
        uint8_t marker = data[pos + 1];
        uint32_t length;

        if (marker == 0xff) {
            pos++;                              /* padding before a marker */
            continue;
        }
        if (marker == 0xd8 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) {
            pos += 2;                           /* markers with no segment */
            continue;
        }
        length = get_u16(data + pos + 2);
        if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            if (pos + 9 <= size) {
                p->height = get_u16(data + pos + 5);
                p->width = get_u16(data + pos + 7);
            }
            return;
        }
        if (marker == 0xda || marker == 0xd9) {
            return;                             /* scan data or end: no frame header */
        }
        pos += 2u + length;
    }
}

/* Blorb §11.2: the resolution entry for a picture, if it has one. */
static void find_resolution(const zm_blorb *b, zm_picture *p)
{
    const uint8_t *reso;
    uint32_t length;
    uint32_t entries;

    if (b->resolution == 0) {
        return;
    }
    reso = b->data + b->resolution + 8;
    length = get_u32(b->data + b->resolution + 4);
    entries = (length - 24u) / 28u;
    for (uint32_t i = 0; i < entries; i++) {
        const uint8_t *entry = reso + 24 + 28u * i;

        if (get_u32(entry) == p->number) {
            p->standard_width = get_u32(reso);
            p->standard_height = get_u32(reso + 4);
            p->ratio_num = get_u32(entry + 4);
            p->ratio_den = get_u32(entry + 8);
            p->min_num = get_u32(entry + 12);
            p->min_den = get_u32(entry + 16);
            p->max_num = get_u32(entry + 20);
            p->max_den = get_u32(entry + 24);
            return;
        }
    }
}

int zm_blorb_picture(const zm_blorb *b, unsigned number, zm_picture *p)
{
    uint32_t start = find_resource(b->data, b->size, b->index, b->count, "Pict", number);
    const uint8_t *chunk;
    uint32_t length;

    memset(p, 0, sizeof *p);
    if (start == 0) {
        return 0;
    }
    chunk = b->data + start;
    length = get_u32(chunk + 4);
    p->number = number;
    if (has_type(chunk, "PNG ")) {
        p->format = ZM_PICTURE_PNG;
        p->data = chunk + 8;
        p->size = length;
        measure_png(p->data, length, p);
    } else if (has_type(chunk, "JPEG")) {
        p->format = ZM_PICTURE_JPEG;
        p->data = chunk + 8;
        p->size = length;
        measure_jpeg(p->data, length, p);
    } else if (has_type(chunk, "Rect") && length >= 8) {
        p->format = ZM_PICTURE_RECT;
        p->width = get_u32(chunk + 8);
        p->height = get_u32(chunk + 12);
    } else {
        p->format = ZM_PICTURE_OTHER;
        p->data = chunk + 8;
        p->size = length;
    }
    find_resolution(b, p);
    return 1;
}

static double ratio(uint32_t num, uint32_t den)
{
    return den == 0 ? 0.0 : (double)num / (double)den;
}

void zm_picture_scaled_size(const zm_picture *p, unsigned screen_width, unsigned screen_height,
                            unsigned *width, unsigned *height)
{
    double scale = 1.0;

    if (p->standard_width != 0 && p->standard_height != 0) {
        /* Blorb §11.2: the Elbow Room Factor is how many times the
         * standard window fits in the screen, in whichever direction has
         * less room; the picture's standard ratio scales that, within
         * its limits, and equal limits pin it. */
        double erf_x = (double)screen_width / (double)p->standard_width;
        double erf_y = (double)screen_height / (double)p->standard_height;
        double erf = erf_x < erf_y ? erf_x : erf_y;
        double minimum = ratio(p->min_num, p->min_den);
        double maximum = ratio(p->max_num, p->max_den);

        scale = erf * (p->ratio_den == 0 ? 1.0 : ratio(p->ratio_num, p->ratio_den));
        if (p->min_den != 0 && scale < minimum) {
            scale = minimum;
        }
        if (p->max_den != 0 && scale > maximum) {
            scale = maximum;
        }
    }
    *width = (unsigned)((double)p->width * scale + 0.5);
    *height = (unsigned)((double)p->height * scale + 0.5);
    if (*width == 0 && p->width != 0) {
        *width = 1;
    }
    if (*height == 0 && p->height != 0) {
        *height = 1;
    }
}
