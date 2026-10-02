/*
    Plugin Manager for ARK-5
    text.h: text rendering with the PSP's firmware fonts (intraFont).
*/

#ifndef PM_TEXT_H
#define PM_TEXT_H

#include <psptypes.h>

enum {
    TEXT_LEFT = 0,
    TEXT_CENTER = 1,
    TEXT_RIGHT = 2,
    TEXT_BOLD = 4,
    TEXT_SHADOW = 8,
};

/* Loads the firmware fonts, or the built-in 8x8 font when they are missing
   (PPSSPP without an installed firmware). Returns < 0 only if neither works. */
int text_init(void);
void text_term(void);
/* Non-zero while drawing with the built-in 8x8 font. */
int text_fallback(void);

/* Width of `len` bytes of UTF-8 from a table made by text_init(). Unlike
   text_width(), it doesn't touch intraFont, so any thread may call it. */
float text_measure(const char *s, int len, float size, int flags);

/* Line height in pixels for a font size. */
float text_line_height(float size);

/* Draws UTF-8 text; `y` is the top of the line. Returns the width drawn. */
float text_draw(float x, float y, const char *s, float size, u32 color, int flags);
float text_width(const char *s, float size, int flags);
/* Like text_draw, left-aligned and without measuring the text first. */
void text_print(float x, float y, const char *s, float size, u32 color, int flags);

/* Draws text shortened with "..." so it fits in max_w pixels. */
void text_draw_fit(float x, float y, float max_w, const char *s, float size, u32 color, int flags);

/* Word-wraps `s` into lines no wider than `width`. Line starts/lengths are
   stored in `starts`/`lens` (at most `max` lines). Returns the number of lines. */
int text_wrap(const char *s, float size, float width, int flags, const char **starts, int *lens, int max);
void text_draw_n(float x, float y, const char *s, int len, float size, u32 color, int flags);

#endif
