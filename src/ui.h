/*
    Plugin Manager for ARK-5
    ui.h: colors and reusable widgets.
*/

#ifndef PM_UI_H
#define PM_UI_H

#include "gfx.h"
#include "store.h"
#include "text.h"

/* palette */
#define C_BG_TOP     RGB(22, 28, 44)
#define C_BG_BOTTOM  RGB(8, 10, 18)
#define C_PANEL      RGBA(255, 255, 255, 16)
#define C_PANEL_HI   RGBA(255, 255, 255, 30)
#define C_LINE       RGBA(255, 255, 255, 28)
#define C_TEXT       RGB(236, 240, 246)
#define C_DIM        RGB(158, 168, 184)
#define C_FAINT      RGB(104, 114, 132)
#define C_ACCENT     RGB(70, 160, 255)
#define C_ACCENT_DK  RGB(34, 92, 168)
#define C_OK         RGB(78, 206, 112)
#define C_WARN       RGB(255, 168, 46)
#define C_ERR        RGB(238, 86, 86)
#define C_SHADE      RGBA(0, 0, 0, 150)

enum {
    GLYPH_CROSS,
    GLYPH_CIRCLE,
    GLYPH_TRIANGLE,
    GLYPH_SQUARE,
    GLYPH_L,
    GLYPH_R,
    GLYPH_START,
    GLYPH_SELECT,
    GLYPH_UPDOWN,
    GLYPH_LEFTRIGHT,
};

u32 ui_category_color(store_category c);
const char *ui_category_label(store_category c);

void ui_background(void);
/* Button glyph, returns its width. */
float ui_glyph(float x, float y, int glyph, float alpha);
/* "glyph label" pairs laid out from the right edge; returns the left x. */
float ui_hint(float x, float y, int glyph, const char *label);

void ui_panel(float x, float y, float w, float h, float r);
void ui_chip(float x, float y, const char *label, u32 color, float *out_w);
void ui_progress_bar(float x, float y, float w, float h, float fraction, u32 color);
void ui_spinner(float cx, float cy, float r, u32 color);

/* Tile used by the grid when there is no icon. */
void ui_placeholder_icon(float x, float y, float w, float h, const char *title, store_category c);

/* Draws a centered modal box and returns its content rectangle. */
void ui_modal(float w, float h, const char *title, float *cx, float *cy, float *cw);

extern unsigned int ui_frame;

#endif
