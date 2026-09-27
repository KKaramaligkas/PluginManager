/*
    Plugin Manager for ARK-5
    ui.c: colors and reusable widgets.
*/

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"

unsigned int ui_frame;

u32 ui_category_color(store_category c)
{
    switch (c) {
    case CAT_PLUGIN: return RGB(138, 92, 232);
    case CAT_HOMEBREW: return RGB(28, 168, 158);
    case CAT_EMULATOR: return RGB(222, 84, 84);
    case CAT_GAME: return RGB(72, 178, 92);
    case CAT_UTILITY: return RGB(58, 128, 228);
    case CAT_THEME: return RGB(226, 98, 168);
    default: return RGB(110, 118, 132);
    }
}

const char *ui_category_label(store_category c)
{
    switch (c) {
    case CAT_PLUGIN: return "Plugin";
    case CAT_HOMEBREW: return "Homebrew";
    case CAT_EMULATOR: return "Emulator";
    case CAT_GAME: return "Game";
    case CAT_UTILITY: return "Utility";
    case CAT_THEME: return "Theme";
    default: return "Other";
    }
}

static u32 scale_color(u32 c, float f)
{
    int r = (int)((c & 0xFF) * f), g = (int)(((c >> 8) & 0xFF) * f), b = (int)(((c >> 16) & 0xFF) * f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (c & 0xFF000000) | (b << 16) | (g << 8) | r;
}

void ui_background(void)
{
    gfx_gradient(0, 0, SCREEN_W, SCREEN_H, C_BG_TOP, C_BG_BOTTOM);

    /* two slow XMB-like waves */
    float t = ui_frame * 0.012f;
    for (int w = 0; w < 2; w++) {
        float prev_x = 0, prev_y = 0;
        for (int i = 0; i <= 32; i++) {
            float x = i * (SCREEN_W / 32.0f);
            float y = 175 + w * 18 + sinf(x * 0.012f + t * (w ? 1.3f : 1.0f) + w * 1.7f) * (14 - w * 5);
            if (i) {
                gfx_triangle(prev_x, prev_y, x, y, prev_x, SCREEN_H, RGBA(90, 150, 255, 10 + w * 6));
                gfx_triangle(x, y, x, SCREEN_H, prev_x, SCREEN_H, RGBA(90, 150, 255, 10 + w * 6));
                gfx_line(prev_x, prev_y, x, y, RGBA(160, 200, 255, 26 + w * 10));
            }
            prev_x = x;
            prev_y = y;
        }
    }
}

float ui_glyph(float x, float y, int glyph, float alpha)
{
    int a = (int)(255 * alpha);
    float cx = x + 7, cy = y + 7;
    switch (glyph) {
    case GLYPH_CROSS:
    case GLYPH_CIRCLE:
    case GLYPH_TRIANGLE:
    case GLYPH_SQUARE:
        gfx_circle(cx, cy, 7, RGBA(40, 44, 54, a), 1);
        if (glyph == GLYPH_CROSS) {
            u32 c = RGBA(128, 176, 255, a);
            gfx_line(cx - 3, cy - 3, cx + 3.5f, cy + 3.5f, c);
            gfx_line(cx - 3, cy + 3, cx + 3.5f, cy - 3.5f, c);
            gfx_line(cx - 3, cy - 2, cx + 2.5f, cy + 3.5f, c);
            gfx_line(cx - 3, cy + 2, cx + 2.5f, cy - 3.5f, c);
        }
        else if (glyph == GLYPH_CIRCLE) {
            gfx_circle(cx, cy, 4.2f, RGBA(255, 106, 106, a), 0);
        }
        else if (glyph == GLYPH_TRIANGLE) {
            u32 c = RGBA(70, 226, 168, a);
            gfx_line(cx, cy - 4, cx + 4, cy + 3, c);
            gfx_line(cx + 4, cy + 3, cx - 4, cy + 3, c);
            gfx_line(cx - 4, cy + 3, cx, cy - 4, c);
        }
        else {
            u32 c = RGBA(255, 120, 238, a);
            gfx_line(cx - 3.5f, cy - 3.5f, cx + 3.5f, cy - 3.5f, c);
            gfx_line(cx + 3.5f, cy - 3.5f, cx + 3.5f, cy + 3.5f, c);
            gfx_line(cx + 3.5f, cy + 3.5f, cx - 3.5f, cy + 3.5f, c);
            gfx_line(cx - 3.5f, cy + 3.5f, cx - 3.5f, cy - 3.5f, c);
        }
        return 14;
    case GLYPH_UPDOWN:
    case GLYPH_LEFTRIGHT: {
        gfx_circle(cx, cy, 7, RGBA(40, 44, 54, a), 1);
        u32 c = RGBA(220, 226, 236, a);
        if (glyph == GLYPH_UPDOWN) {
            gfx_triangle(cx, cy - 5, cx + 3, cy - 1.5f, cx - 3, cy - 1.5f, c);
            gfx_triangle(cx, cy + 5, cx - 3, cy + 1.5f, cx + 3, cy + 1.5f, c);
        }
        else {
            gfx_triangle(cx - 5, cy, cx - 1.5f, cy - 3, cx - 1.5f, cy + 3, c);
            gfx_triangle(cx + 5, cy, cx + 1.5f, cy + 3, cx + 1.5f, cy - 3, c);
        }
        return 14;
    }
    default: {
        const char *label = glyph == GLYPH_L ? "L" : glyph == GLYPH_R ? "R" : glyph == GLYPH_START ? "START" : "SELECT";
        float tw = text_width(label, 0.45f, TEXT_BOLD);
        float w = tw + 8;
        gfx_round_rect(x, y + 1, w, 12, 4, RGBA(40, 44, 54, a));
        text_draw(x + 4, y + 2, label, 0.45f, RGBA(220, 226, 236, a), TEXT_BOLD);
        return w;
    }
    }
}

float ui_hint(float x, float y, int glyph, const char *label)
{
    float lw = text_width(label, 0.55f, 0);
    float gw = (glyph == GLYPH_L || glyph == GLYPH_R || glyph == GLYPH_START || glyph == GLYPH_SELECT)
               ? text_width(glyph == GLYPH_L ? "L" : glyph == GLYPH_R ? "R" : glyph == GLYPH_START ? "START" : "SELECT", 0.45f, TEXT_BOLD) + 8
               : 14;
    float start = x - lw - gw - 4;
    ui_glyph(start, y, glyph, 1.0f);
    text_draw(start + gw + 4, y + 1, label, 0.55f, C_DIM, 0);
    return start - 12;
}

void ui_panel(float x, float y, float w, float h, float r)
{
    gfx_round_rect(x, y, w, h, r, C_PANEL);
    gfx_round_frame(x, y, w, h, r, 1, C_LINE);
}

void ui_chip(float x, float y, const char *label, u32 color, float *out_w)
{
    float w = text_width(label, 0.5f, TEXT_BOLD) + 10;
    gfx_round_rect(x, y, w, 13, 5, COLOR_ALPHA(color, 60));
    gfx_round_frame(x, y, w, 13, 5, 1, COLOR_ALPHA(color, 150));
    text_draw(x + 5, y + 1.5f, label, 0.5f, scale_color(color, 1.35f), TEXT_BOLD);
    if (out_w) *out_w = w;
}

void ui_progress_bar(float x, float y, float w, float h, float fraction, u32 color)
{
    gfx_round_rect(x, y, w, h, h / 2, RGBA(255, 255, 255, 24));
    if (fraction < 0) {
        /* indeterminate: moving block */
        float bw = w * 0.25f;
        float pos = (ui_frame % 90) / 90.0f;
        float bx = x + (w - bw) * (0.5f - 0.5f * cosf(pos * 6.2831853f));
        gfx_round_rect(bx, y, bw, h, h / 2, color);
        return;
    }
    if (fraction > 1) fraction = 1;
    if (fraction * w >= h) gfx_round_rect(x, y, w * fraction, h, h / 2, color);
}

void ui_spinner(float cx, float cy, float r, u32 color)
{
    for (int i = 0; i < 8; i++) {
        float a = (ui_frame * 0.15f) + i * 0.785398f;
        int alpha = 40 + i * 26;
        gfx_circle(cx + cosf(a) * r, cy + sinf(a) * r, 2.2f, COLOR_ALPHA(color, alpha), 1);
    }
}

void ui_placeholder_icon(float x, float y, float w, float h, const char *title, store_category c)
{
    u32 base = ui_category_color(c);
    gfx_gradient(x, y, w, h, scale_color(base, 0.9f), scale_color(base, 0.45f));
    gfx_rect(x, y, w, 1, RGBA(255, 255, 255, 50));

    /* up to two initials */
    char init[8] = { 0 };
    int n = 0;
    for (const char *p = title; *p && n < 2; p++) {
        if (isalnum((unsigned char)*p) && (p == title || p[-1] == ' ' || p[-1] == '-' || p[-1] == '_'))
            init[n++] = (char)toupper((unsigned char)*p);
    }
    if (!n && title[0]) init[n++] = title[0];
    float size = h / 40.0f;
    text_draw(x + w / 2, y + h / 2 - text_line_height(size) / 2 - 2, init, size, RGBA(255, 255, 255, 235), TEXT_CENTER | TEXT_BOLD | TEXT_SHADOW);
    text_draw(x + w / 2, y + h - 12 * (h / 58.0f), ui_category_label(c), 0.42f * (h / 58.0f), RGBA(255, 255, 255, 170), TEXT_CENTER);
}

void ui_modal(float w, float h, const char *title, float *cx, float *cy, float *cw)
{
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_SHADE);
    float x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    gfx_round_rect(x + 2, y + 3, w, h, 8, RGBA(0, 0, 0, 90));
    gfx_round_rect(x, y, w, h, 8, RGB(30, 36, 52));
    gfx_round_frame(x, y, w, h, 8, 1, RGBA(255, 255, 255, 40));
    float ty = y + 10;
    if (title && *title) {
        text_draw_fit(x + 14, ty, w - 28, title, 0.72f, C_TEXT, TEXT_BOLD);
        gfx_rect(x + 14, ty + 18, w - 28, 1, C_LINE);
        ty += 26;
    }
    *cx = x + 14;
    *cy = ty;
    *cw = w - 28;
}
