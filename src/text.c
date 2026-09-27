/*
    Plugin Manager for ARK-5
    text.c: text rendering with the PSP's firmware fonts (intraFont).
*/

#include <stdlib.h>
#include <string.h>

#include <intraFont.h>

#include "gfx.h"
#include "text.h"

/* ltn0 = sans-serif regular, ltn4 = sans-serif bold */
static intraFont *font_regular;
static intraFont *font_bold;

/* metrics of ltn0/ltn4 at size 1.0 */
#define LINE_HEIGHT 17.0f
#define ASCENT      13.0f

static intraFont *load(const char *path)
{
    /* INTRAFONT_CACHE_ASCII would drop every non-ASCII glyph, so use the
       large dynamic cache to keep accented, Greek and Cyrillic letters. */
    intraFont *f = intraFontLoad(path, INTRAFONT_CACHE_LARGE | INTRAFONT_STRING_UTF8);
    if (f) intraFontSetEncoding(f, INTRAFONT_STRING_UTF8);
    return f;
}

int text_init(void)
{
    intraFontInit();
    font_regular = load("flash0:/font/ltn0.pgf");
    font_bold = load("flash0:/font/ltn4.pgf");
    if (!font_bold) font_bold = font_regular;
    return font_regular ? 0 : -1;
}

void text_term(void)
{
    if (font_bold && font_bold != font_regular) intraFontUnload(font_bold);
    if (font_regular) intraFontUnload(font_regular);
    font_regular = font_bold = NULL;
    intraFontShutdown();
}

static intraFont *pick(int flags)
{
    return (flags & TEXT_BOLD) ? font_bold : font_regular;
}

float text_line_height(float size)
{
    return LINE_HEIGHT * size;
}

float text_width(const char *s, float size, int flags)
{
    intraFont *f = pick(flags);
    if (!f || !s || !*s) return 0.0f;
    intraFontSetStyle(f, size, 0xFFFFFFFF, 0, 0.0f, 0);
    return intraFontMeasureText(f, s);
}

float text_draw(float x, float y, const char *s, float size, u32 color, int flags)
{
    intraFont *f = pick(flags);
    if (!f || !s || !*s) return 0.0f;

    float w = text_width(s, size, flags);
    if (flags & TEXT_CENTER) x -= w / 2;
    else if (flags & TEXT_RIGHT) x -= w;

    if (flags & TEXT_SHADOW) {
        intraFontSetStyle(f, size, COLOR_ALPHA(0, ((color >> 24) & 0xFF) / 2), 0, 0.0f, 0);
        intraFontPrint(f, x + 1, y + ASCENT * size + 1, s);
    }
    intraFontSetStyle(f, size, color, 0, 0.0f, 0);
    intraFontPrint(f, x, y + ASCENT * size, s);
    return w;
}

void text_draw_n(float x, float y, const char *s, int len, float size, u32 color, int flags)
{
    char buf[512];
    if (len <= 0) return;
    if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, s, len);
    buf[len] = 0;
    text_draw(x, y, buf, size, color, flags);
}

/* length in bytes of the UTF-8 sequence starting at s */
static int utf8_len(const char *s)
{
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

void text_draw_fit(float x, float y, float max_w, const char *s, float size, u32 color, int flags)
{
    if (!s || !*s) return;
    if (text_width(s, size, flags) <= max_w) {
        text_draw(x, y, s, size, color, flags);
        return;
    }
    char buf[256];
    float ell = text_width("...", size, flags);
    int n = 0, best = 0;
    while (s[n] && n < (int)sizeof(buf) - 4) {
        int cl = utf8_len(s + n);
        memcpy(buf, s, n + cl);
        buf[n + cl] = 0;
        if (text_width(buf, size, flags) + ell > max_w) break;
        n += cl;
        best = n;
    }
    memcpy(buf, s, best);
    strcpy(buf + best, "...");
    text_draw(x, y, buf, size, color, flags);
}

static float word_width(const char *s, int len, float size, int flags)
{
    char buf[256];
    if (len <= 0) return 0.0f;
    if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, s, len);
    buf[len] = 0;
    return text_width(buf, size, flags);
}

int text_wrap(const char *s, float size, float width, int flags, const char **starts, int *lens, int max)
{
    int n = 0;
    float space = text_width(" ", size, flags);
    if (space <= 0) space = 4.0f * size;
    const char *p = s;

    while (*p && n < max) {
        const char *line = p;
        const char *line_end = p;
        float w = 0.0f;
        int words = 0;

        while (*p && *p != '\n') {
            /* next word */
            const char *ws = p;
            while (*p && *p != ' ' && *p != '\n') p++;
            float ww = word_width(ws, (int)(p - ws), size, flags);

            if (words && w + space + ww > width) {
                p = ws;     /* word goes to the next line */
                break;
            }
            if (!words && ww > width) {
                /* a single word longer than the line: cut it */
                const char *q = ws;
                float cw = 0.0f;
                while (q < p) {
                    int cl = utf8_len(q);
                    float c = word_width(q, cl, size, flags);
                    if (cw + c > width && q > ws) break;
                    cw += c;
                    q += cl;
                }
                p = q;
                line_end = q;
                words++;
                break;
            }
            w += (words ? space : 0.0f) + ww;
            words++;
            line_end = p;
            while (*p == ' ') p++;
        }

        starts[n] = line;
        lens[n] = (int)(line_end - line);
        n++;
        if (*p == '\n') p++;
        while (*p == ' ') p++;
    }
    return n;
}
