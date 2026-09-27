/*
    Plugin Manager for ARK-5
    gfx.h: tiny 2D renderer on top of sceGu.
*/

#ifndef PM_GFX_H
#define PM_GFX_H

#include <psptypes.h>

#define SCREEN_W 480
#define SCREEN_H 272

/* GU colors are 0xAABBGGRR */
#define RGBA(r, g, b, a) ((u32)(((a) & 0xFF) << 24 | ((b) & 0xFF) << 16 | ((g) & 0xFF) << 8 | ((r) & 0xFF)))
#define RGB(r, g, b) RGBA(r, g, b, 255)
#define COLOR_ALPHA(c, a) (((c) & 0x00FFFFFF) | ((u32)((a) & 0xFF) << 24))

typedef struct {
    int w, h;           /* image size */
    int tw, th;         /* power of two texture size */
    u32 *pixels;        /* RGBA8888, 16 byte aligned */
} texture;

void gfx_init(void);
void gfx_term(void);

void gfx_begin(void);           /* start a frame (clears the screen) */
void gfx_end(void);             /* finish the display list (utility dialogs draw after this) */
void gfx_swap(void);            /* wait for vblank and flip */

void gfx_rect(float x, float y, float w, float h, u32 color);
void gfx_gradient(float x, float y, float w, float h, u32 top, u32 bottom);
void gfx_gradient_h(float x, float y, float w, float h, u32 left, u32 right);
void gfx_round_rect(float x, float y, float w, float h, float r, u32 color);
void gfx_round_frame(float x, float y, float w, float h, float r, float thickness, u32 color);
void gfx_line(float x0, float y0, float x1, float y1, u32 color);
void gfx_circle(float cx, float cy, float r, u32 color, int filled);
void gfx_triangle(float x0, float y0, float x1, float y1, float x2, float y2, u32 color);

void gfx_clip(int x, int y, int w, int h);
void gfx_noclip(void);

texture *tex_create(int w, int h);
void tex_free(texture *t);
void tex_flush(texture *t);     /* call after writing pixels */
void gfx_texture(const texture *t, float x, float y, float w, float h, u32 tint);

#endif
