/*
 * utf8.c: see utf8.h.
 */

#include "utf8.h"

unsigned utf8_encode(uint16_t c, char *out)
{
    if (c < 0x80) {
        out[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        out[0] = (char)(0xc0 | (c >> 6));
        out[1] = (char)(0x80 | (c & 0x3f));
        return 2;
    }
    out[0] = (char)(0xe0 | (c >> 12));
    out[1] = (char)(0x80 | ((c >> 6) & 0x3f));
    out[2] = (char)(0x80 | (c & 0x3f));
    return 3;
}

unsigned utf8_encode_pair(uint16_t high, uint16_t low, char *out)
{
    uint32_t c = 0x10000u + (((uint32_t)high - 0xd800u) << 10) + ((uint32_t)low - 0xdc00u);

    out[0] = (char)(0xf0 | (c >> 18));
    out[1] = (char)(0x80 | ((c >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((c >> 6) & 0x3f));
    out[3] = (char)(0x80 | (c & 0x3f));
    return 4;
}

void utf8_put(FILE *out, uint16_t c)
{
    char bytes[3];
    unsigned count = utf8_encode(c, bytes);

    fwrite(bytes, 1, count, out);
}

static int is_high_surrogate(uint16_t unit)
{
    return unit >= 0xd800 && unit <= 0xdbff;
}

static int is_low_surrogate(uint16_t unit)
{
    return unit >= 0xdc00 && unit <= 0xdfff;
}

void utf8_put_unit(FILE *out, uint16_t unit, uint16_t *held)
{
    char bytes[4];

    if (*held != 0) {
        uint16_t high = *held;

        *held = 0;
        if (is_low_surrogate(unit)) {
            fwrite(bytes, 1, utf8_encode_pair(high, unit, bytes), out);
            return;
        }
        putc('?', out);
    }
    if (is_high_surrogate(unit)) {
        *held = unit;
        return;
    }
    if (is_low_surrogate(unit)) {
        putc('?', out);
        return;
    }
    fwrite(bytes, 1, utf8_encode(unit, bytes), out);
}

void utf8_put_end(FILE *out, uint16_t *held)
{
    if (*held != 0) {
        *held = 0;
        putc('?', out);
    }
}

void utf8_put_units(FILE *out, const uint16_t *units, size_t count)
{
    uint16_t held = 0;

    for (size_t i = 0; i < count; i++) {
        utf8_put_unit(out, units[i], &held);
    }
    utf8_put_end(out, &held);
}

unsigned utf8_units(uint32_t c, uint16_t out[2])
{
    if (c < 0x10000u) {
        out[0] = (uint16_t)c;
        return 1;
    }
    c -= 0x10000u;
    out[0] = (uint16_t)(0xd800u + (c >> 10));
    out[1] = (uint16_t)(0xdc00u + (c & 0x3ffu));
    return 2;
}

uint32_t utf8_next(const unsigned char *s, size_t *i)
{
    unsigned char lead = s[*i];
    uint32_t c;
    unsigned extra;

    (*i)++;
    if (lead < 0x80) {
        return lead;
    }
    if ((lead & 0xe0) == 0xc0) {
        c = lead & 0x1fu;
        extra = 1;
    } else if ((lead & 0xf0) == 0xe0) {
        c = lead & 0x0fu;
        extra = 2;
    } else if ((lead & 0xf8) == 0xf0) {
        c = lead & 0x07u;
        extra = 3;
    } else {
        return 0xfffd;
    }
    for (unsigned k = 0; k < extra; k++) {
        if ((s[*i] & 0xc0) != 0x80) {
            return 0xfffd;
        }
        c = (c << 6) | (s[*i] & 0x3fu);
        (*i)++;
    }
    return c;
}
