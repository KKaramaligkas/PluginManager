/*
    Plugin Manager for ARK-5
    gfx.c: tiny 2D renderer on top of sceGu.
*/

#include <malloc.h>
#include <math.h>
#include <string.h>

#include <pspdisplay.h>
#include <pspgu.h>
#include <pspkernel.h>

#include "gfx.h"

#define BUF_W       512
#define FB_SIZE     (BUF_W * SCREEN_H * 4)
#define LIST_SIZE   (256 * 1024)

typedef struct {
    u32 color;
    float x, y, z;
} cvertex;

typedef struct {
    float u, v;
    u32 color;
    float x, y, z;
} tvertex;

static unsigned int __attribute__((aligned(64))) gu_list[LIST_SIZE / 4];

void gfx_init(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, gu_list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCREEN_W, SCREEN_H, (void *)FB_SIZE, BUF_W);
    sceGuDepthBuffer((void *)(FB_SIZE * 2), BUF_W);
    sceGuOffset(2048 - (SCREEN_W / 2), 2048 - (SCREEN_H / 2));
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    /* intraFont re-enables the depth test after drawing text: make it harmless */
    sceGuDepthFunc(GU_ALWAYS);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuTexScale(1.0f, 1.0f);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

void gfx_term(void)
{
    sceGuTerm();
}

void gfx_begin(void)
{
    sceGuStart(GU_DIRECT, gu_list);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuClearColor(RGB(0, 0, 0));
    sceGuClear(GU_COLOR_BUFFER_BIT);
}

void gfx_end(void)
{
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
}

void gfx_swap(void)
{
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

static cvertex *cverts(int n)
{
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);
    return (cvertex *)sceGuGetMemory(n * sizeof(cvertex));
}

static void setv(cvertex *v, float x, float y, u32 c)
{
    v->color = c;
    v->x = x;
    v->y = y;
    v->z = 0.0f;
}

#define CVERT_FMT (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)

void gfx_gradient(float x, float y, float w, float h, u32 top, u32 bottom)
{
    if (w <= 0 || h <= 0) return;
    cvertex *v = cverts(4);
    setv(&v[0], x, y, top);
    setv(&v[1], x + w, y, top);
    setv(&v[2], x, y + h, bottom);
    setv(&v[3], x + w, y + h, bottom);
    sceGuDrawArray(GU_TRIANGLE_STRIP, CVERT_FMT, 4, 0, v);
}

void gfx_gradient_h(float x, float y, float w, float h, u32 left, u32 right)
{
    if (w <= 0 || h <= 0) return;
    cvertex *v = cverts(4);
    setv(&v[0], x, y, left);
    setv(&v[1], x + w, y, right);
    setv(&v[2], x, y + h, left);
    setv(&v[3], x + w, y + h, right);
    sceGuDrawArray(GU_TRIANGLE_STRIP, CVERT_FMT, 4, 0, v);
}

void gfx_rect(float x, float y, float w, float h, u32 color)
{
    gfx_gradient(x, y, w, h, color, color);
}

#define CORNER_SEGS 5

/* Points of a rounded rectangle outline, clockwise, starting top-left. */
static int round_points(float x, float y, float w, float h, float r, float *px, float *py)
{
    static const float pi_2 = 1.5707963f;
    const float cx[4] = { x + w - r, x + w - r, x + r, x + r };
    const float cy[4] = { y + r, y + h - r, y + h - r, y + r };
    int n = 0;
    for (int c = 0; c < 4; c++) {
        float a0 = -pi_2 + c * pi_2;
        for (int s = 0; s <= CORNER_SEGS; s++) {
            float a = a0 + pi_2 * s / CORNER_SEGS;
            px[n] = cx[c] + cosf(a) * r;
            py[n] = cy[c] + sinf(a) * r;
            n++;
        }
    }
    return n;
}

void gfx_round_rect(float x, float y, float w, float h, float r, u32 color)
{
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 1.0f) {
        gfx_rect(x, y, w, h, color);
        return;
    }
    float px[4 * (CORNER_SEGS + 1)], py[4 * (CORNER_SEGS + 1)];
    int n = round_points(x, y, w, h, r, px, py);
    cvertex *v = cverts(n + 2);
    setv(&v[0], x + w / 2, y + h / 2, color);
    for (int i = 0; i < n; i++) setv(&v[i + 1], px[i], py[i], color);
    setv(&v[n + 1], px[0], py[0], color);
    sceGuDrawArray(GU_TRIANGLE_FAN, CVERT_FMT, n + 2, 0, v);
}

void gfx_round_frame(float x, float y, float w, float h, float r, float t, u32 color)
{
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < t) r = t;
    float ox[4 * (CORNER_SEGS + 1)], oy[4 * (CORNER_SEGS + 1)];
    float ix[4 * (CORNER_SEGS + 1)], iy[4 * (CORNER_SEGS + 1)];
    int n = round_points(x, y, w, h, r, ox, oy);
    round_points(x + t, y + t, w - 2 * t, h - 2 * t, r - t, ix, iy);
    cvertex *v = cverts(2 * n + 2);
    for (int i = 0; i <= n; i++) {
        int k = i % n;
        setv(&v[2 * i], ox[k], oy[k], color);
        setv(&v[2 * i + 1], ix[k], iy[k], color);
    }
    sceGuDrawArray(GU_TRIANGLE_STRIP, CVERT_FMT, 2 * n + 2, 0, v);
}

void gfx_line(float x0, float y0, float x1, float y1, u32 color)
{
    cvertex *v = cverts(2);
    setv(&v[0], x0, y0, color);
    setv(&v[1], x1, y1, color);
    sceGuDrawArray(GU_LINES, CVERT_FMT, 2, 0, v);
}

void gfx_circle(float cx, float cy, float r, u32 color, int filled)
{
    enum { SEGS = 20 };
    if (filled) {
        cvertex *v = cverts(SEGS + 2);
        setv(&v[0], cx, cy, color);
        for (int i = 0; i <= SEGS; i++) {
            float a = 6.2831853f * i / SEGS;
            setv(&v[i + 1], cx + cosf(a) * r, cy + sinf(a) * r, color);
        }
        sceGuDrawArray(GU_TRIANGLE_FAN, CVERT_FMT, SEGS + 2, 0, v);
    }
    else {
        /* 1.5px ring made of a triangle strip */
        cvertex *v = cverts(2 * SEGS + 2);
        for (int i = 0; i <= SEGS; i++) {
            float a = 6.2831853f * i / SEGS;
            float c = cosf(a), s = sinf(a);
            setv(&v[2 * i], cx + c * r, cy + s * r, color);
            setv(&v[2 * i + 1], cx + c * (r - 1.5f), cy + s * (r - 1.5f), color);
        }
        sceGuDrawArray(GU_TRIANGLE_STRIP, CVERT_FMT, 2 * SEGS + 2, 0, v);
    }
}

void gfx_triangle(float x0, float y0, float x1, float y1, float x2, float y2, u32 color)
{
    cvertex *v = cverts(3);
    setv(&v[0], x0, y0, color);
    setv(&v[1], x1, y1, color);
    setv(&v[2], x2, y2, color);
    sceGuDrawArray(GU_TRIANGLES, CVERT_FMT, 3, 0, v);
}

void gfx_clip(int x, int y, int w, int h)
{
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    sceGuScissor(x, y, w, h);
}

void gfx_noclip(void)
{
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
}

static int pow2(int v)
{
    int p = 8;
    while (p < v) p <<= 1;
    return p;
}

texture *tex_create(int w, int h)
{
    if (w <= 0 || h <= 0 || w > 512 || h > 512) return NULL;
    texture *t = calloc(1, sizeof(texture));
    if (!t) return NULL;
    t->w = w;
    t->h = h;
    t->tw = pow2(w);
    t->th = pow2(h);
    t->pixels = memalign(16, t->tw * t->th * 4);
    if (!t->pixels) {
        free(t);
        return NULL;
    }
    memset(t->pixels, 0, t->tw * t->th * 4);
    return t;
}

void tex_free(texture *t)
{
    if (!t) return;
    free(t->pixels);
    free(t);
}

void tex_flush(texture *t)
{
    if (t) sceKernelDcacheWritebackRange(t->pixels, t->tw * t->th * 4);
}

void gfx_texture(const texture *t, float x, float y, float w, float h, u32 tint)
{
    if (!t) return;
    sceGuEnable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, t->tw, t->th, t->tw, t->pixels);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFlush();

    tvertex *v = (tvertex *)sceGuGetMemory(2 * sizeof(tvertex));
    v[0].u = 0.0f;
    v[0].v = 0.0f;
    v[0].color = tint;
    v[0].x = x;
    v[0].y = y;
    v[0].z = 0.0f;
    v[1].u = (float)t->w;
    v[1].v = (float)t->h;
    v[1].color = tint;
    v[1].x = x + w;
    v[1].y = y + h;
    v[1].z = 0.0f;
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
    sceGuDisable(GU_TEXTURE_2D);
}
