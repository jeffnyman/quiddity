/*
 * test_blorb.c: resource files.
 *
 * Covers zm/blorb.h: reading the container of Blorb §0 and §1, measuring
 * the pictures of §2, lifting a story out of the executable chunk of §5,
 * matching the game identifier of §6, and the scaling rules of §11.2.
 * The resource file is built by hand, chunk by chunk, so every offset
 * in it is known.
 */

#include <string.h>

#include "zm/blorb.h"
#include "zm/header.h"
#include "zm/memory.h"

#include "check.h"

/* A buffer being written as an IFF file (Blorb §15). */
typedef struct {
    uint8_t bytes[2048];
    uint32_t size;
} iff;

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

/* Append a chunk, padded to an even length, and return its offset. */
static uint32_t chunk(iff *f, const char *type, const uint8_t *data, uint32_t length)
{
    uint32_t offset = f->size;

    memcpy(f->bytes + f->size, type, 4);
    put_u32(f->bytes + f->size + 4, length);
    memcpy(f->bytes + f->size + 8, data, length);
    f->size += 8 + length;
    if (length & 1) {
        f->bytes[f->size++] = 0;
    }
    return offset;
}

/* The smallest Version 5 story: a header and a quit, 128 bytes long,
 * release 5, serial 260101, with the checksum Spec §15 verify defines
 * (the sum of the bytes from $40, here the single quit byte). */
#define STORY_SIZE 128u
#define STORY_CHECKSUM 0xbau

static void build_story(uint8_t *story)
{
    memset(story, 0, STORY_SIZE);
    story[ZM_HEADER_VERSION] = 5;
    story[ZM_HEADER_RELEASE + 1] = 5;
    story[ZM_HEADER_HIGH_BASE + 1] = 0x40;
    story[ZM_HEADER_INITIAL_PC + 1] = 0x40;
    story[ZM_HEADER_STATIC_BASE + 1] = 0x40;
    memcpy(story + ZM_HEADER_SERIAL, "260101", 6);
    story[ZM_HEADER_FILE_LENGTH + 1] = STORY_SIZE / 4;
    story[ZM_HEADER_CHECKSUM + 1] = STORY_CHECKSUM;
    story[0x40] = 0xba;
}

/* A resource file with three pictures and the story: Blorb §1 says the
 * index comes first, and the other chunks follow in any order. */
static void build_blorb(iff *f, int with_story)
{
    uint8_t index[4 + 4 * 12];
    uint8_t png[33];
    uint8_t rect[8];
    uint8_t jpeg[40];
    uint8_t story[STORY_SIZE];
    uint8_t reso[24 + 2 * 28];
    uint8_t ifhd[13];
    uint8_t reln[2] = { 0, 7 };
    uint32_t entries = with_story ? 4 : 3;
    uint32_t offsets[4];

    memset(f, 0, sizeof *f);
    memcpy(f->bytes, "FORM", 4);
    memcpy(f->bytes + 8, "IFRS", 4);
    f->size = 12;

    /* The index is written once the chunks it points at are placed, so
     * a chunk of the right size stands in for it. */
    memset(index, 0, sizeof index);
    chunk(f, "RIdx", index, 4 + entries * 12);

    /* Blorb §2.1: a PNG of 40 by 20, as far as its signature and IHDR. */
    memset(png, 0, sizeof png);
    memcpy(png, "\x89PNG\r\n\x1a\n", 8);
    put_u32(png + 8, 13);
    memcpy(png + 12, "IHDR", 4);
    put_u32(png + 16, 40);
    put_u32(png + 20, 20);
    offsets[0] = chunk(f, "PNG ", png, sizeof png);

    /* Blorb §2.3: a placeholder 12 wide and 6 high. */
    put_u32(rect, 12);
    put_u32(rect + 4, 6);
    offsets[1] = chunk(f, "Rect", rect, sizeof rect);

    /* Blorb §2.2: a JPEG of 50 by 30: start of image, an APP0 segment
     * to step over, then a baseline frame header. */
    memset(jpeg, 0, sizeof jpeg);
    jpeg[0] = 0xff; jpeg[1] = 0xd8;
    jpeg[2] = 0xff; jpeg[3] = 0xe0; jpeg[4] = 0; jpeg[5] = 16;
    memcpy(jpeg + 6, "JFIF", 5);
    jpeg[20] = 0xff; jpeg[21] = 0xc0; jpeg[22] = 0; jpeg[23] = 17;
    jpeg[24] = 8;
    jpeg[25] = 0; jpeg[26] = 30;
    jpeg[27] = 0; jpeg[28] = 50;
    offsets[2] = chunk(f, "JPEG", jpeg, sizeof jpeg);

    if (with_story) {
        build_story(story);
        offsets[3] = chunk(f, "ZCOD", story, STORY_SIZE);
    }

    chunk(f, "RelN", reln, 2);

    /* Blorb §11.2: a standard window of 320 by 200; picture 1 scales
     * freely at its own size, picture 3 is pinned at one pixel per
     * pixel, picture 2 has no entry and does not scale. */
    memset(reso, 0, sizeof reso);
    put_u32(reso, 320);
    put_u32(reso + 4, 200);
    put_u32(reso + 24, 1);
    put_u32(reso + 28, 1);
    put_u32(reso + 32, 1);
    put_u32(reso + 52, 3);
    put_u32(reso + 56, 2);
    put_u32(reso + 60, 1);
    put_u32(reso + 64, 1);
    put_u32(reso + 68, 1);
    put_u32(reso + 72, 1);
    put_u32(reso + 76, 1);
    chunk(f, "Reso", reso, sizeof reso);

    /* Blorb §6, Quetzal §5.4: release, serial, checksum, and a PC that
     * means nothing here. */
    memset(ifhd, 0, sizeof ifhd);
    ifhd[1] = 5;
    memcpy(ifhd + 2, "260101", 6);
    ifhd[9] = STORY_CHECKSUM;
    chunk(f, "IFhd", ifhd, sizeof ifhd);

    put_u32(f->bytes + 4, f->size - 8);

    /* Now the index: usage, number, offset for each resource. */
    put_u32(f->bytes + 20, entries);
    memcpy(f->bytes + 24, "Pict", 4);
    put_u32(f->bytes + 28, 1);
    put_u32(f->bytes + 32, offsets[0]);
    memcpy(f->bytes + 36, "Pict", 4);
    put_u32(f->bytes + 40, 2);
    put_u32(f->bytes + 44, offsets[1]);
    memcpy(f->bytes + 48, "Pict", 4);
    put_u32(f->bytes + 52, 3);
    put_u32(f->bytes + 56, offsets[2]);
    if (with_story) {
        memcpy(f->bytes + 60, "Exec", 4);
        put_u32(f->bytes + 64, 0);
        put_u32(f->bytes + 68, offsets[3]);
    }
}

static void test_container_is_read(void)
{
    iff f;
    zm_blorb b;
    uint8_t plain[64];

    build_blorb(&f, 1);
    CHECK(zm_blorb_looks_like(f.bytes, f.size));
    CHECK_EQ_INT(zm_blorb_load_bytes(&b, f.bytes, f.size), ZM_OK);
    CHECK_EQ_INT(b.count, 4);
    CHECK_EQ_INT(zm_blorb_picture_count(&b), 3);
    CHECK_EQ_INT(zm_blorb_release(&b), 7);
    CHECK(b.resolution != 0);
    CHECK(b.identifier != 0);
    zm_blorb_free(&b);

    /* Not a Blorb at all, and a Blorb whose index claims more entries
     * than it holds. */
    memset(plain, 0, sizeof plain);
    CHECK(!zm_blorb_looks_like(plain, sizeof plain));
    CHECK_EQ_INT(zm_blorb_load_bytes(&b, plain, sizeof plain), ZM_ERROR_NOT_A_BLORB);
    build_blorb(&f, 1);
    put_u32(f.bytes + 20, 1000);
    CHECK_EQ_INT(zm_blorb_load_bytes(&b, f.bytes, f.size), ZM_ERROR_BAD_BLORB);
}

static void test_pictures_are_measured(void)
{
    iff f;
    zm_blorb b;
    zm_picture p;

    build_blorb(&f, 0);
    CHECK_EQ_INT(zm_blorb_load_bytes(&b, f.bytes, f.size), ZM_OK);

    CHECK(zm_blorb_picture(&b, 1, &p));
    CHECK_EQ_INT(p.format, ZM_PICTURE_PNG);
    CHECK_EQ_INT(p.width, 40);
    CHECK_EQ_INT(p.height, 20);
    CHECK_EQ_INT(p.size, 33);
    CHECK(p.data != NULL && p.data[1] == 'P');
    CHECK_EQ_INT(p.standard_width, 320);
    CHECK_EQ_INT(p.standard_height, 200);
    CHECK_EQ_INT(p.ratio_num, 1);
    CHECK_EQ_INT(p.ratio_den, 1);
    CHECK_EQ_INT(p.min_den, 0);
    CHECK_EQ_INT(p.max_den, 0);

    CHECK(zm_blorb_picture(&b, 2, &p));
    CHECK_EQ_INT(p.format, ZM_PICTURE_RECT);
    CHECK_EQ_INT(p.width, 12);
    CHECK_EQ_INT(p.height, 6);
    CHECK(p.data == NULL);
    CHECK_EQ_INT(p.standard_width, 0);

    CHECK(zm_blorb_picture(&b, 3, &p));
    CHECK_EQ_INT(p.format, ZM_PICTURE_JPEG);
    CHECK_EQ_INT(p.width, 50);
    CHECK_EQ_INT(p.height, 30);
    CHECK_EQ_INT(p.ratio_num, 2);
    CHECK_EQ_INT(p.min_num, 1);
    CHECK_EQ_INT(p.max_num, 1);

    CHECK(!zm_blorb_picture(&b, 4, &p));
    CHECK(!zm_blorb_picture(&b, 0, &p));
    zm_blorb_free(&b);
}

static void test_pictures_are_scaled_to_the_screen(void)
{
    iff f;
    zm_blorb b;
    zm_picture p;
    unsigned width;
    unsigned height;

    build_blorb(&f, 0);
    CHECK_EQ_INT(zm_blorb_load_bytes(&b, f.bytes, f.size), ZM_OK);

    /* Blorb §11.2: a screen twice the standard window doubles a freely
     * scalable picture; half of it halves it; a screen with less room
     * in one direction is held back by that direction. */
    zm_blorb_picture(&b, 1, &p);
    zm_picture_scaled_size(&p, 640, 400, &width, &height);
    CHECK_EQ_INT(width, 80);
    CHECK_EQ_INT(height, 40);
    zm_picture_scaled_size(&p, 160, 100, &width, &height);
    CHECK_EQ_INT(width, 20);
    CHECK_EQ_INT(height, 10);
    zm_picture_scaled_size(&p, 320, 50, &width, &height);
    CHECK_EQ_INT(width, 10);
    CHECK_EQ_INT(height, 5);

    /* A picture that would scale to nothing keeps a pixel. */
    zm_picture_scaled_size(&p, 4, 2, &width, &height);
    CHECK_EQ_INT(width, 1);
    CHECK_EQ_INT(height, 1);

    /* A picture with no resolution entry is never scaled. */
    zm_blorb_picture(&b, 2, &p);
    zm_picture_scaled_size(&p, 640, 400, &width, &height);
    CHECK_EQ_INT(width, 12);
    CHECK_EQ_INT(height, 6);

    /* Equal minimum and maximum ratios pin the scale, whatever the
     * screen or the standard ratio say. */
    zm_blorb_picture(&b, 3, &p);
    zm_picture_scaled_size(&p, 640, 400, &width, &height);
    CHECK_EQ_INT(width, 50);
    CHECK_EQ_INT(height, 30);
    zm_picture_scaled_size(&p, 32, 20, &width, &height);
    CHECK_EQ_INT(width, 50);
    CHECK_EQ_INT(height, 30);
    zm_blorb_free(&b);
}

static void test_story_is_lifted_out_of_the_blorb(void)
{
    iff f;
    zm_blorb b;
    zm_memory mem;
    uint32_t offset;
    uint32_t length;

    build_blorb(&f, 1);
    CHECK_EQ_INT(zm_blorb_find_story(f.bytes, f.size, &offset, &length), ZM_OK);
    CHECK_EQ_INT(length, STORY_SIZE);
    CHECK_EQ_INT(f.bytes[offset], 5);

    /* The memory loader takes a Blorb as it takes a story file, and what
     * it loads is the story alone. */
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, f.bytes, f.size), ZM_OK);
    CHECK_EQ_INT(mem.version, 5);
    CHECK_EQ_INT(mem.size, STORY_SIZE);
    CHECK_EQ_INT(zm_read_byte(&mem, 0x40), 0xba);
    CHECK_EQ_INT(zm_header_compute_checksum(&mem), STORY_CHECKSUM);

    /* Blorb §6: the identifier names this story; a different release
     * is a different story. */
    CHECK_EQ_INT(zm_blorb_load_bytes(&b, f.bytes, f.size), ZM_OK);
    CHECK(zm_blorb_matches(&b, &mem));
    f.bytes[b.identifier + 1] = 6;
    zm_blorb_free(&b);
    CHECK_EQ_INT(zm_blorb_load_bytes(&b, f.bytes, f.size), ZM_OK);
    CHECK(!zm_blorb_matches(&b, &mem));
    zm_blorb_free(&b);
    zm_memory_free(&mem);

    /* A resource file with pictures but no story cannot be run; it is
     * still a resource file. */
    build_blorb(&f, 0);
    CHECK_EQ_INT(zm_blorb_find_story(f.bytes, f.size, &offset, &length), ZM_ERROR_BLORB_NO_STORY);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, f.bytes, f.size), ZM_ERROR_BLORB_NO_STORY);
    CHECK_EQ_INT(zm_blorb_load_bytes(&b, f.bytes, f.size), ZM_OK);
    CHECK_EQ_INT(zm_blorb_picture_count(&b), 3);
    zm_blorb_free(&b);
}

int main(void)
{
    RUN_TEST(test_container_is_read);
    RUN_TEST(test_pictures_are_measured);
    RUN_TEST(test_pictures_are_scaled_to_the_screen);
    RUN_TEST(test_story_is_lifted_out_of_the_blorb);
    return CHECK_SUMMARY();
}
