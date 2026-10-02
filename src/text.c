/*
    Plugin Manager for ARK-5
    text.c: text rendering with the PSP's firmware fonts (intraFont).

    PPSSPP has no flash0:/font unless a PSP firmware is installed in it.
    Without the fonts, text is drawn with the PSP SDK's 8x8 font (the one
    pspDebugScreen uses), which covers ASCII.
*/

#include <stdlib.h>
#include <string.h>

#include <intraFont.h>
#include <pspgu.h>

#include "gfx.h"
#include "text.h"

/* ltn0 = sans-serif regular, ltn4 = sans-serif bold. The Latin fonts have no
   Greek: kr0 (Korean, which includes Greek and Cyrillic letters) draws what
   they lack. */
static intraFont *font_regular;
static intraFont *font_bold;
static intraFont *font_other;

/* metrics of ltn0/ltn4 at size 1.0 */
#define LINE_HEIGHT 17.0f
#define ASCENT      13.0f

/* Advances at size 1.0, in 1/64 pixel, for the code points below ADVANCES
   (Latin, Greek and Cyrillic). text_measure() reads only this table, so the
   browser's worker thread can lay out pages while the main thread draws. */
#define ADVANCES    0x0530
static unsigned short advance[2][ADVANCES];
static unsigned short advance_other[2];
/* Letters no font has, drawn as a close one: accented Greek as the bare letter. */
static unsigned short substitute[ADVANCES];

extern unsigned char msx[];     /* the SDK's 8x8 font, 256 glyphs */
static texture *fallback;       /* msx as a 16x16 grid of 8x8 glyphs */

typedef struct {
    float u, v;
    u32 color;
    float x, y, z;
} glyph_vertex;

static intraFont *load(const char *path)
{
    /* INTRAFONT_CACHE_ASCII would drop every non-ASCII glyph, so use the
       large dynamic cache to keep accented, Greek and Cyrillic letters. */
    intraFont *f = intraFontLoad(path, INTRAFONT_CACHE_LARGE | INTRAFONT_STRING_UTF8);
    if (f) intraFontSetEncoding(f, INTRAFONT_STRING_UTF8);
    return f;
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

/* Decodes one code point from at most `len` bytes; returns its length. */
static int utf8_decode(const char *s, int len, unsigned *cp)
{
    const unsigned char *p = (const unsigned char *)s;
    int n = utf8_len(s);
    if (n > len) n = 1;
    if (n == 1) { *cp = p[0] < 0x80 ? p[0] : '?'; return 1; }
    unsigned c = p[0] & (n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07);
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *cp = '?'; return 1; }
        c = (c << 6) | (p[i] & 0x3F);
    }
    *cp = c;
    return n;
}

static int encode(unsigned cp, char *out)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}

static void find_substitutes(void)
{
    static const unsigned short greek[][2] = {
        {0x386, 0x391}, {0x388, 0x395}, {0x389, 0x397}, {0x38A, 0x399}, {0x38C, 0x39F}, {0x38E, 0x3A5}, {0x38F, 0x3A9},
        {0x390, 0x3B9}, {0x3AA, 0x399}, {0x3AB, 0x3A5}, {0x3AC, 0x3B1}, {0x3AD, 0x3B5}, {0x3AE, 0x3B7}, {0x3AF, 0x3B9},
        {0x3B0, 0x3C5}, {0x3C2, 0x3C3}, {0x3CA, 0x3B9}, {0x3CB, 0x3C5}, {0x3CC, 0x3BF}, {0x3CD, 0x3C5}, {0x3CE, 0x3C9}};
    for (size_t i = 0; i < sizeof(greek) / sizeof(*greek); i++)
        if (!advance[0][greek[i][0]] && advance[0][greek[i][1]]) {
            substitute[greek[i][0]] = greek[i][1];
            advance[0][greek[i][0]] = advance[0][greek[i][1]];
            advance[1][greek[i][0]] = advance[1][greek[i][1]];
        }
}

/* Returns `s`, or a copy in `buf` with substitute letters. */
static const char *substituted(const char *s, char *buf, size_t size)
{
    const char *p;
    for (p = s; *p; p++) if ((unsigned char)*p == 0xCE || (unsigned char)*p == 0xCF) break;
    if (!*p) return s;
    size_t n = 0;
    int len = (int)strlen(s);
    for (int i = 0; i < len && n + 4 < size;) {
        unsigned cp;
        int k = utf8_decode(s + i, len - i, &cp);
        if (cp < ADVANCES && substitute[cp]) n += encode(substitute[cp], buf + n);
        else { memcpy(buf + n, s + i, (size_t)k); n += k; }
        i += k;
    }
    buf[n] = 0;
    return buf;
}

static void measure_advances(void)
{
    for (int bold = 0; bold < 2; bold++) {
        intraFont *f = bold ? font_bold : font_regular;
        intraFontSetStyle(f, 1.0f, 0xFFFFFFFF, 0, 0.0f, 0);
        if (font_other) intraFontSetStyle(font_other, 1.0f, 0xFFFFFFFF, 0, 0.0f, 0);
        char s[4];
        for (unsigned cp = 32; cp < ADVANCES; cp++) {
            s[encode(cp, s)] = 0;
            float w = intraFontMeasureText(f, s);
            advance[bold][cp] = (unsigned short)(w > 0 && w < 1000 ? w * 64 + 0.5f : 0);
        }
        advance_other[bold] = advance[bold]['n'] ? advance[bold]['n'] : 8 * 64;
    }
}

static int load_fallback(void)
{
    fallback = tex_create(128, 128);
    if (!fallback) return -1;
    for (int c = 0; c < 256; c++)
        for (int row = 0; row < 8; row++)
            for (int col = 0; col < 8; col++)
                if (msx[c * 8 + row] & (0x80 >> col))
                    fallback->pixels[((c / 16) * 8 + row) * fallback->tw + (c % 16) * 8 + col] = 0xFFFFFFFF;
    tex_flush(fallback);
    return 0;
}

/* The 8x8 font: whole pixels only, twice as large for big text. */
static int fallback_scale(float size) { return size >= 0.85f ? 2 : 1; }

static unsigned fallback_glyph(unsigned cp)
{
    static const char latin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
    if (cp >= 32 && cp < 127) return cp;
    if (cp >= 0xC0 && cp <= 0xFF) return (unsigned char)latin1[cp - 0xC0];
    return '?';
}

static float fallback_width(const char *s, int len, float size)
{
    int n = 0;
    for (int i = 0; i < len && s[i]; i += utf8_len(s + i)) n++;
    return (float)(n * 6 * fallback_scale(size));
}

static void fallback_draw(float x, float y, const char *s, float size, u32 color)
{
    int g = fallback_scale(size), n = 0, len = (int)strlen(s);
    for (int i = 0; i < len; i += utf8_len(s + i)) n++;
    if (!n) return;
    glyph_vertex *v = (glyph_vertex *)sceGuGetMemory(2 * n * sizeof(glyph_vertex));
    float top = (float)(int)(y + (LINE_HEIGHT * size - 8 * g) / 2 + 0.5f);
    x = (float)(int)(x + 0.5f);
    for (int i = 0, k = 0; i < len && k < n; k++) {
        unsigned cp;
        i += utf8_decode(s + i, len - i, &cp);
        unsigned c = fallback_glyph(cp);
        float u = (float)((c % 16) * 8), t = (float)((c / 16) * 8);
        v[2 * k] = (glyph_vertex){ u, t, color, x + k * 6 * g, top, 0.0f };
        v[2 * k + 1] = (glyph_vertex){ u + 8, t + 8, color, x + k * 6 * g + 8 * g, top + 8 * g, 0.0f };
    }
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, fallback->tw, fallback->th, fallback->tw, fallback->pixels);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexFlush();
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2 * n, 0, v);
    sceGuDisable(GU_TEXTURE_2D);
}

int text_init(void)
{
    intraFontInit();
    font_regular = load("flash0:/font/ltn0.pgf");
    font_bold = load("flash0:/font/ltn4.pgf");
    if (!font_bold) font_bold = font_regular;
    if (font_regular) {
        font_other = intraFontLoad("flash0:/font/kr0.pgf", INTRAFONT_STRING_UTF8);
        if (font_other) {
            intraFontSetEncoding(font_other, INTRAFONT_STRING_UTF8);
            intraFontSetAltFont(font_regular, font_other);
            if (font_bold != font_regular) intraFontSetAltFont(font_bold, font_other);
        }
        measure_advances();
        find_substitutes();
        return 0;
    }
    return load_fallback();
}

int text_fallback(void)
{
    return !font_regular;
}

void text_term(void)
{
    if (font_bold && font_bold != font_regular) intraFontUnload(font_bold);
    if (font_regular) intraFontUnload(font_regular);
    if (font_other) intraFontUnload(font_other);
    font_regular = font_bold = font_other = NULL;
    tex_free(fallback);
    fallback = NULL;
    intraFontShutdown();
}

static intraFont *pick(int flags)
{
    return (flags & TEXT_BOLD) ? font_bold : font_regular;
}

/* intraFont draws a letter from the alternative font with that font's own
   size and color, so kr0 gets the same style (by default it's white, size 1). */
static void style(intraFont *f, float size, u32 color)
{
    intraFontSetStyle(f, size, color, 0, 0.0f, 0);
    if (font_other) intraFontSetStyle(font_other, size, color, 0, 0.0f, 0);
}

float text_line_height(float size)
{
    return LINE_HEIGHT * size;
}

float text_measure(const char *s, int len, float size, int flags)
{
    if (!font_regular) return fallback_width(s, len, size);
    int bold = (flags & TEXT_BOLD) ? 1 : 0;
    unsigned total = 0;
    for (int i = 0; i < len && s[i];) {
        unsigned cp;
        i += utf8_decode(s + i, len - i, &cp);
        total += cp < ADVANCES ? (cp < 32 ? 0 : advance[bold][cp]) : advance_other[bold];
    }
    return total / 64.0f * size;
}

float text_width(const char *s, float size, int flags)
{
    if (!s || !*s) return 0.0f;
    if (!font_regular) return fallback ? fallback_width(s, (int)strlen(s), size) : 0.0f;
    intraFont *f = pick(flags);
    char buf[512];
    style(f, size, 0xFFFFFFFF);
    return intraFontMeasureText(f, substituted(s, buf, sizeof(buf)));
}

float text_draw(float x, float y, const char *s, float size, u32 color, int flags)
{
    intraFont *f = pick(flags);
    if (!s || !*s || (!f && !fallback)) return 0.0f;

    float w = text_width(s, size, flags);
    if (!f) {
        if (flags & TEXT_CENTER) x -= w / 2;
        else if (flags & TEXT_RIGHT) x -= w;
        if (flags & TEXT_SHADOW) fallback_draw(x + 1, y + 1, s, size, COLOR_ALPHA(0, ((color >> 24) & 0xFF) / 2));
        fallback_draw(x, y, s, size, color);
        if (flags & TEXT_BOLD) fallback_draw(x + 1, y, s, size, color);
        return w;
    }
    if (flags & TEXT_CENTER) x -= w / 2;
    else if (flags & TEXT_RIGHT) x -= w;

    char buf[512];
    const char *shown = substituted(s, buf, sizeof(buf));
    if (flags & TEXT_SHADOW) {
        style(f, size, COLOR_ALPHA(0, ((color >> 24) & 0xFF) / 2));
        intraFontPrint(f, x + 1, y + ASCENT * size + 1, shown);
    }
    style(f, size, color);
    intraFontPrint(f, x, y + ASCENT * size, shown);
    return w;
}

void text_print(float x, float y, const char *s, float size, u32 color, int flags)
{
    intraFont *f = pick(flags);
    if (!s || !*s) return;
    if (!f) {
        if (!fallback) return;
        fallback_draw(x, y, s, size, color);
        if (flags & TEXT_BOLD) fallback_draw(x + 1, y, s, size, color);
        return;
    }
    char buf[512];
    style(f, size, color);
    intraFontPrint(f, x, y + ASCENT * size, substituted(s, buf, sizeof(buf)));
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
