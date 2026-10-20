/*
 * font.h: the font the SDL front end draws with, built into the program.
 *
 * Noto Sans Mono, from assets/fonts, under the SIL Open Font License,
 * whose text is beside it there. CMake turns the file into C source
 * (cmake/embed.cmake), so the program has nothing to find at run time.
 */

#ifndef QUIDDITY_FONT_H
#define QUIDDITY_FONT_H

extern const unsigned char font_regular[];
extern const unsigned long font_regular_size;

#endif /* QUIDDITY_FONT_H */
