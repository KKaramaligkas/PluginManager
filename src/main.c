/*
    Plugin Manager for ARK-5
    main.c: screens, input handling and job orchestration.

    A Universal-Updater-like store for the PSP: browse plugins and homebrew in
    a grid, install/update/remove them over Wi-Fi, and toggle plugins. The
    XMB "Plugins" category (XMBControl) launches this app, optionally with a
    request to open a specific plugin (data/launch.txt).
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pspkernel.h>
#include <psppower.h>
#include <psprtc.h>
#include <psputility.h>

#include <systemctrl.h>

#include "app.h"
#include "entropy.h"
#include "fs.h"
#include "gfx.h"
#include "image.h"
#include "input.h"
#include "net.h"
#include "pluginstxt.h"
#include "text.h"
#include "ui.h"
#include "version.h"
#include "worker.h"

PSP_MODULE_INFO("PluginManager", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
/* The heap gets all the free memory but this much, which the system needs
   while the app runs: the network libraries (~450 KB), the TCP/IP buffers,
   their threads and the worker thread's stack are all allocated next to the
   app. Don't use a negative PSP_HEAP_SIZE_KB: it leaves only 512 KB, so the
   network can't start on a real PSP (emulators don't load those libraries,
   so they don't show the problem). */
PSP_HEAP_THRESHOLD_SIZE_KB(4 * 1024);

/* ------------------------------------------------------------------------ */
/* layout */

#define HEADER_H    26
#define TABS_Y      28
#define GRID_Y      56
#define GRID_COLS   4
#define CELL_W      114
#define TILE_W      104
#define TILE_H      58
#define ROW_H       82
#define GRID_X      ((SCREEN_W - GRID_COLS * CELL_W) / 2 + (CELL_W - TILE_W) / 2)
#define GRID_BOTTOM 216
#define HINTS_Y     256
#define MAX_TEXTURES 28

enum { SCR_GRID, SCR_DETAILS, SCR_SETTINGS };
enum { MODAL_NONE, MODAL_MESSAGE, MODAL_CONFIRM, MODAL_PROGRESS, MODAL_MENU };
enum { F_ALL, F_PLUGINS, F_APPS, F_EMUS, F_GAMES, F_INSTALLED, F_UPDATES, F_COUNT };
enum { SORT_NAME, SORT_CATEGORY, SORT_UPDATED, SORT_COUNT };
enum {
    ACT_INSTALL, ACT_UPDATE, ACT_REINSTALL, ACT_UNINSTALL,
    ACT_ENABLE, ACT_DISABLE, ACT_REMOVE_LOCAL,
};
enum {
    MENU_REFRESH, MENU_SEARCH, MENU_SORT, MENU_UPDATE_ALL, MENU_SETTINGS, MENU_ABOUT, MENU_EXIT,
    MENU_STORE_DEFAULT, MENU_STORE_CUSTOM,
};
enum {
    SET_STORE, SET_ROOT, SET_XMB, SET_TLS, SET_AUTO, SET_ICONS, SET_ABOUT, SET_COUNT,
};

static const char *filter_names[F_COUNT] = {
    "All", "Plugins", "Apps", "Emulators", "Games", "Installed", "Updates",
};
static const char *sort_names[SORT_COUNT] = { "Name", "Category", "Recently updated" };

typedef void (*confirm_fn)(void);

static struct {
    int screen;

    /* grid */
    int tabs[F_COUNT];
    int n_tabs;
    int tab;
    int *list;
    int list_n;
    int sel;
    float scroll, scroll_target;
    float hl_x, hl_y;           /* animated selection highlight */
    char search[64];
    int sort;

    /* details */
    char detail_id[64];
    char detail_path[PM_PATH_MAX];  /* local plugins are identified by path */
    int actions[6];
    int n_actions;
    int action_sel;
    const char *line_start[96];
    int line_len[96];
    int n_lines;
    float desc_scroll;
    char local_desc[768];

    /* settings */
    int set_sel;

    /* modal */
    int modal;
    char modal_title[96];
    char modal_text[900];
    int modal_sel;
    confirm_fn on_yes;
    int menu_ids[12];
    const char *menu_labels[12];
    char menu_buf[12][64];
    int menu_n;
    int menu_sel;

    /* jobs */
    char queue[64][64];
    int queue_n, queue_total;
    int icons_seen;
    int installs_done;
    int vsh_plugin_changed;

    char toast[128];
    unsigned int toast_until;
} ui;

static volatile int exit_requested;
/* set by a "run" step: started instead of returning to the XMB */
static char launch_path[256];
static unsigned int tex_loaded;

/* ------------------------------------------------------------------------ */
/* callbacks */

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    exit_requested = 1;
    worker_cancel();
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int thid = sceKernelCreateThread("pm_callbacks", callback_thread, 0x11, 0xFA0, PSP_THREAD_ATTR_USER, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
}

/* ------------------------------------------------------------------------ */
/* helpers */

static void toast(const char *msg)
{
    pm_strlcpy(ui.toast, msg, sizeof(ui.toast));
    ui.toast_until = sceKernelGetSystemTimeLow() + 2500 * 1000;
}

static void message(const char *title, const char *text)
{
    ui.modal = MODAL_MESSAGE;
    pm_strlcpy(ui.modal_title, title, sizeof(ui.modal_title));
    pm_strlcpy(ui.modal_text, text, sizeof(ui.modal_text));
    ui.modal_sel = 0;
}

static void confirm(const char *title, const char *text, confirm_fn yes)
{
    message(title, text);
    ui.modal = MODAL_CONFIRM;
    ui.on_yes = yes;
    ui.modal_sel = 0;
}

static void menu_open(const char *title)
{
    ui.modal = MODAL_MENU;
    pm_strlcpy(ui.modal_title, title, sizeof(ui.modal_title));
    ui.menu_n = 0;
    ui.menu_sel = 0;
}

static void menu_add(int id, const char *label)
{
    if (ui.menu_n >= (int)NELEMS(ui.menu_ids)) return;
    pm_strlcpy(ui.menu_buf[ui.menu_n], label, sizeof(ui.menu_buf[0]));
    ui.menu_ids[ui.menu_n] = id;
    ui.menu_labels[ui.menu_n] = ui.menu_buf[ui.menu_n];
    ui.menu_n++;
}

static item_t *sel_item(void)
{
    if (ui.sel < 0 || ui.sel >= ui.list_n) return NULL;
    return &app.items[ui.list[ui.sel]];
}

static item_t *detail_item(void)
{
    if (ui.detail_id[0]) return app_find_item(ui.detail_id);
    if (ui.detail_path[0]) return app_find_local(ui.detail_path);
    return NULL;
}

static int is_vsh_item(const item_t *it)
{
    return strstr(it->runlevels, "vsh") || strstr(it->runlevels, "xmb") || strstr(it->runlevels, "always");
}

static const char *item_author(const item_t *it)
{
    if (it->entry) return it->entry->author;
    if (it->status == ST_LOCAL) return "Added manually";
    return "Installed package";
}

static const char *item_version(const item_t *it)
{
    if (it->entry) return it->entry->version;
    return it->installed_version[0] ? it->installed_version : "";
}

/* ------------------------------------------------------------------------ */
/* list building */

static int item_matches(const item_t *it, int filter)
{
    int store = it->entry != NULL;
    switch (filter) {
    case F_ALL: return store;
    case F_PLUGINS: return it->category == CAT_PLUGIN && (store || it->status != ST_AVAILABLE);
    case F_APPS:
        return store && (it->category == CAT_HOMEBREW || it->category == CAT_UTILITY ||
                         it->category == CAT_THEME || it->category == CAT_OTHER);
    case F_EMUS: return store && it->category == CAT_EMULATOR;
    case F_GAMES: return store && it->category == CAT_GAME;
    case F_INSTALLED: return it->status != ST_AVAILABLE;
    case F_UPDATES: return it->status == ST_UPDATE;
    }
    return 0;
}

static const char *strcasestr_ascii(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (!n) return hay;
    for (; *hay; hay++)
        if (pm_strncasecmp(hay, needle, n) == 0) return hay;
    return NULL;
}

static int search_matches(const item_t *it)
{
    if (!ui.search[0]) return 1;
    if (strcasestr_ascii(it->title, ui.search)) return 1;
    if (it->entry && (strcasestr_ascii(it->entry->author, ui.search) || strcasestr_ascii(it->entry->description, ui.search)))
        return 1;
    return 0;
}

static int cmp_items(const void *a, const void *b)
{
    const item_t *x = &app.items[*(const int *)a], *y = &app.items[*(const int *)b];
    if (ui.sort == SORT_CATEGORY && x->category != y->category) return (int)x->category - (int)y->category;
    if (ui.sort == SORT_UPDATED) {
        const char *ux = x->entry && x->entry->updated ? x->entry->updated : "";
        const char *uy = y->entry && y->entry->updated ? y->entry->updated : "";
        int c = strcmp(uy, ux);
        if (c) return c;
    }
    return pm_strcasecmp(x->title, y->title);
}

static void rebuild_list(void)
{
    /* visible tabs: always All + Installed, the rest when not empty */
    ui.n_tabs = 0;
    for (int f = 0; f < F_COUNT; f++) {
        int count = 0;
        for (int i = 0; i < app.n_items && !count; i++) count += item_matches(&app.items[i], f);
        if (f == F_ALL || f == F_INSTALLED || count) ui.tabs[ui.n_tabs++] = f;
    }
    if (ui.tab >= ui.n_tabs) ui.tab = 0;

    int filter = ui.tabs[ui.tab];
    free(ui.list);
    ui.list = malloc(sizeof(int) * (app.n_items ? app.n_items : 1));
    ui.list_n = 0;
    for (int i = 0; i < app.n_items; i++) {
        if (item_matches(&app.items[i], filter) && search_matches(&app.items[i])) ui.list[ui.list_n++] = i;
    }
    qsort(ui.list, ui.list_n, sizeof(int), cmp_items);
    if (ui.sel >= ui.list_n) ui.sel = ui.list_n - 1;
    if (ui.sel < 0) ui.sel = 0;
}

static void select_tab(int filter)
{
    for (int t = 0; t < ui.n_tabs; t++)
        if (ui.tabs[t] == filter) ui.tab = t;
}

static void refresh_items(void)
{
    char sel_id[64] = "";
    item_t *it = sel_item();
    if (it) pm_strlcpy(sel_id, it->id[0] ? it->id : it->title, sizeof(sel_id));

    app_rebuild_items();
    tex_loaded = 0;
    rebuild_list();

    /* keep the selection on the same item */
    for (int i = 0; sel_id[0] && i < ui.list_n; i++) {
        const item_t *x = &app.items[ui.list[i]];
        if (!strcmp(x->id[0] ? x->id : x->title, sel_id)) ui.sel = i;
    }
}

/* ------------------------------------------------------------------------ */
/* icons */

static void free_lru_icon(void)
{
    int oldest = -1;
    for (int i = 0; i < app.n_items; i++) {
        item_t *it = &app.items[i];
        if (it->icon && (oldest < 0 || it->icon_used < app.items[oldest].icon_used)) oldest = i;
    }
    if (oldest >= 0) {
        tex_free(app.items[oldest].icon);
        app.items[oldest].icon = NULL;
        app.items[oldest].icon_state = 0;
        tex_loaded--;
    }
}

static texture *item_icon(item_t *it, int *budget)
{
    it->icon_used = ui_frame;
    if (it->icon) return it->icon;
    if (it->icon_state < 0 || !it->id[0] || !it->entry || *budget <= 0) return NULL;

    (*budget)--;
    char path[288];
    snprintf(path, sizeof(path), "%s%s.png", app.icons_dir, it->id);
    if (!fs_exists(path)) {
        it->icon_state = -1;
        return NULL;
    }
    if (tex_loaded >= MAX_TEXTURES) free_lru_icon();
    it->icon = image_load_png(path);
    it->icon_state = it->icon ? 1 : -1;
    if (it->icon) tex_loaded++;
    return it->icon;
}

/* ------------------------------------------------------------------------ */
/* drawing */

static void draw_check(float cx, float cy, u32 color)
{
    gfx_line(cx - 3, cy, cx - 1, cy + 2.5f, color);
    gfx_line(cx - 1, cy + 2.5f, cx + 3.5f, cy - 2.5f, color);
    gfx_line(cx - 3, cy + 0.8f, cx - 1, cy + 3.3f, color);
    gfx_line(cx - 1, cy + 3.3f, cx + 3.5f, cy - 1.7f, color);
}

static void draw_badge(const item_t *it, float x, float y)
{
    float cx = x + TILE_W - 8, cy = y + 8;
    if (it->status == ST_UPDATE) {
        gfx_circle(cx, cy, 7, C_WARN, 1);
        gfx_triangle(cx, cy - 4, cx + 3.5f, cy, cx - 3.5f, cy, RGB(255, 255, 255));
        gfx_rect(cx - 1.2f, cy, 2.4f, 4, RGB(255, 255, 255));
    }
    else if (it->status == ST_INSTALLED || it->status == ST_LOCAL) {
        gfx_circle(cx, cy, 7, it->status == ST_LOCAL ? RGB(138, 92, 232) : C_OK, 1);
        draw_check(cx, cy, RGB(255, 255, 255));
    }
    if (it->n_lines && !it->enabled) {
        gfx_round_rect(x + 3, y + 3, 24, 12, 4, RGBA(0, 0, 0, 170));
        text_draw(x + 15, y + 3.5f, "OFF", 0.42f, RGB(255, 190, 190), TEXT_CENTER | TEXT_BOLD);
    }
}

static void draw_header(void)
{
    gfx_gradient(0, 0, SCREEN_W, HEADER_H, RGBA(0, 0, 0, 90), RGBA(0, 0, 0, 40));
    gfx_rect(0, HEADER_H, SCREEN_W, 1, C_LINE);

    /* logo: a puzzle piece */
    gfx_round_rect(10, 6, 14, 14, 3, C_ACCENT);
    gfx_circle(24, 13, 3.5f, C_ACCENT, 1);
    gfx_circle(17, 6, 3.5f, C_ACCENT, 1);
    gfx_circle(17, 13, 2.5f, RGB(22, 28, 44), 1);
    text_draw(32, 5, "Plugin Manager", 0.7f, C_TEXT, TEXT_BOLD);

    /* right side: network, clock, battery */
    char buf[32];
    float x = SCREEN_W - 10;
    ScePspDateTime t;
    if (sceRtcGetCurrentClockLocalTime(&t) >= 0) {
        snprintf(buf, sizeof(buf), "%02d:%02d", t.hour, t.minute);
        x -= text_draw(x, 7, buf, 0.58f, C_DIM, TEXT_RIGHT) + 10;
    }
    if (scePowerIsBatteryExist()) {
        int pct = scePowerGetBatteryLifePercent();
        if (pct >= 0 && pct <= 100) {
            float bx = x - 22;
            gfx_round_frame(bx, 8, 18, 10, 2, 1, C_DIM);
            gfx_rect(bx + 18, 11, 2, 4, C_DIM);
            gfx_rect(bx + 2, 10, 14 * pct / 100.0f, 6, pct <= 15 ? C_ERR : C_DIM);
            x = bx - 10;
        }
    }
    int online = net_is_connected();
    const char *status = online ? "Online" : net_wlan_switch_on() ? "Offline" : "Wi-Fi off";
    x -= text_draw(x, 7, status, 0.55f, online ? C_OK : C_FAINT, TEXT_RIGHT);
    gfx_circle(x - 7, 13, 3, online ? C_OK : C_FAINT, 1);

    if (job.running && job.type == JOB_ICONS) ui_spinner(x - 22, 13, 4, C_DIM);
}

static void draw_tabs(void)
{
    float widths[F_COUNT], total = 0;
    for (int t = 0; t < ui.n_tabs; t++) {
        widths[t] = text_width(filter_names[ui.tabs[t]], 0.62f, TEXT_BOLD) + 18;
        total += widths[t];
    }
    float x = (SCREEN_W - total) / 2;
    for (int t = 0; t < ui.n_tabs; t++) {
        int selected = t == ui.tab;
        const char *name = filter_names[ui.tabs[t]];
        text_draw(x + widths[t] / 2, TABS_Y + 3, name, 0.62f, selected ? C_TEXT : C_FAINT,
                  TEXT_CENTER | (selected ? TEXT_BOLD : 0));
        if (selected) gfx_round_rect(x + 8, TABS_Y + 19, widths[t] - 16, 2.5f, 1, C_ACCENT);
        x += widths[t];
    }
    ui_glyph(8, TABS_Y + 3, GLYPH_L, 0.9f);
    ui_glyph(SCREEN_W - 19, TABS_Y + 3, GLYPH_R, 0.9f);
}

static void draw_hints(const int *glyphs, const char **labels, int n)
{
    gfx_rect(0, HINTS_Y - 3, SCREEN_W, 1, C_LINE);
    float x = SCREEN_W - 8;
    for (int i = 0; i < n; i++) x = ui_hint(x, HINTS_Y, glyphs[i], labels[i]);
}

static void draw_grid(void)
{
    draw_tabs();

    if (ui.search[0]) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Search: \"%s\"", ui.search);
        float w;
        ui_chip(12, TABS_Y + 24, buf, C_ACCENT, &w);
        text_draw(20 + w, TABS_Y + 25, "(press [] again to clear)", 0.48f, C_FAINT, 0);
    }

    int budget = 2;     /* PNG decodes per frame, keeps scrolling smooth */
    int grid_y = GRID_Y + (ui.search[0] ? 12 : 0);

    if (ui.list_n == 0) {
        const char *msg = ui.search[0] ? "Nothing matches your search" :
                          ui.tabs[ui.tab] == F_UPDATES ? "Everything is up to date" :
                          ui.tabs[ui.tab] == F_INSTALLED ? "Nothing installed yet" : "The store is empty";
        text_draw(SCREEN_W / 2, 118, msg, 0.8f, C_DIM, TEXT_CENTER | TEXT_BOLD);
        if (!ui.search[0] && ui.tabs[ui.tab] == F_ALL)
            text_draw(SCREEN_W / 2, 142, "Open the menu with triangle and choose Refresh store", 0.55f, C_FAINT, TEXT_CENTER);
    }
    else {
        ui.scroll += (ui.scroll_target - ui.scroll) * 0.25f;
        if (fabsf(ui.scroll_target - ui.scroll) < 0.5f) ui.scroll = ui.scroll_target;

        gfx_clip(0, grid_y - 6, SCREEN_W, GRID_BOTTOM - grid_y + 8);
        int first_row = (int)(ui.scroll / ROW_H);
        for (int i = first_row * GRID_COLS; i < ui.list_n && i < (first_row + 4) * GRID_COLS; i++) {
            item_t *it = &app.items[ui.list[i]];
            int row = i / GRID_COLS, col = i % GRID_COLS;
            float x = GRID_X + col * CELL_W;
            float y = grid_y + row * ROW_H - ui.scroll;
            if (y > GRID_BOTTOM || y + ROW_H < grid_y - 6) continue;

            int selected = i == ui.sel && ui.screen == SCR_GRID;
            if (selected) {
                ui.hl_x += (x - ui.hl_x) * 0.35f;
                ui.hl_y += (y - ui.hl_y) * 0.35f;
            }

            texture *tex = item_icon(it, &budget);
            gfx_round_rect(x + 2, y + 3, TILE_W, TILE_H, 4, RGBA(0, 0, 0, 80));
            if (tex) gfx_texture(tex, x, y, TILE_W, TILE_H, 0xFFFFFFFF);
            else ui_placeholder_icon(x, y, TILE_W, TILE_H, it->title, it->category);
            draw_badge(it, x, y);

            text_draw_fit(x + TILE_W / 2, y + TILE_H + 5, TILE_W, it->title, 0.55f,
                          selected ? C_TEXT : C_DIM, TEXT_CENTER | (selected ? TEXT_BOLD : 0));
        }

        /* selection highlight */
        float pulse = 0.5f + 0.5f * sinf(ui_frame * 0.1f);
        gfx_round_frame(ui.hl_x - 4, ui.hl_y - 4, TILE_W + 8, TILE_H + 8, 6, 2.5f, C_ACCENT);
        gfx_round_frame(ui.hl_x - 6, ui.hl_y - 6, TILE_W + 12, TILE_H + 12, 8, 2, COLOR_ALPHA(C_ACCENT, 40 + (int)(50 * pulse)));
        gfx_noclip();

        /* scroll bar */
        int rows = (ui.list_n + GRID_COLS - 1) / GRID_COLS;
        if (rows > 2) {
            float track = GRID_BOTTOM - grid_y;
            float h = track * 2 / rows;
            float y = grid_y + (track - h) * (ui.scroll / ((rows - 2) * ROW_H));
            gfx_round_rect(SCREEN_W - 5, grid_y, 3, track, 1.5f, RGBA(255, 255, 255, 18));
            gfx_round_rect(SCREEN_W - 5, y, 3, h, 1.5f, RGBA(255, 255, 255, 90));
        }
    }

    /* info panel */
    item_t *it = sel_item();
    ui_panel(8, 220, SCREEN_W - 16, 32, 6);
    if (it) {
        float x = 16;
        x += text_draw(x, 223, it->title, 0.64f, C_TEXT, TEXT_BOLD) + 6;
        const char *ver = item_version(it);
        if (ver[0] && strcmp(ver, "?")) {
            char v[48];
            snprintf(v, sizeof(v), "v%s", ver);
            text_draw(x, 224.5f, v, 0.52f, C_DIM, 0);
        }

        const char *chip;
        u32 color;
        char upd[96];
        switch (it->status) {
        case ST_UPDATE:
            snprintf(upd, sizeof(upd), "Update %s > %s", it->installed_version, item_version(it));
            chip = upd;
            color = C_WARN;
            break;
        case ST_INSTALLED: chip = "Installed"; color = C_OK; break;
        case ST_LOCAL: chip = "Manual plugin"; color = RGB(138, 92, 232); break;
        default: chip = "Available"; color = C_ACCENT; break;
        }
        float cw = text_width(chip, 0.5f, TEXT_BOLD) + 10;
        ui_chip(SCREEN_W - 16 - cw, 224, chip, color, NULL);

        char line[160];
        char size[24] = "";
        if (it->entry && it->entry->size > 0) {
            char s[16];
            pm_format_size(it->entry->size, s, sizeof(s));
            snprintf(size, sizeof(size), "  |  %s", s);
        }
        if (it->n_lines)
            snprintf(line, sizeof(line), "%s  |  %s  |  %s: %s%s", item_author(it), ui_category_label(it->category),
                     it->enabled ? "On" : "Off", it->runlevels, size);
        else
            snprintf(line, sizeof(line), "%s  |  %s%s", item_author(it), ui_category_label(it->category), size);
        text_draw_fit(16, 238, SCREEN_W - 32, line, 0.5f, C_FAINT, 0);
    }

    static const int g[] = { GLYPH_START, GLYPH_SQUARE, GLYPH_TRIANGLE, GLYPH_CROSS };
    static const char *l[] = { "Settings", "Search", "Menu", "Open" };
    draw_hints(g, l, 4);
    ui_hint(8 + text_width("Category", 0.55f, 0) + 40, HINTS_Y, GLYPH_R, "Category");
    ui_glyph(8, HINTS_Y, GLYPH_L, 1.0f);
}

/* details */

static const char *action_label(int a, const item_t *it)
{
    switch (a) {
    case ACT_INSTALL: return "Install";
    case ACT_UPDATE: return "Update";
    case ACT_REINSTALL: return "Reinstall";
    case ACT_UNINSTALL: return "Uninstall";
    case ACT_ENABLE: return "Enable";
    case ACT_DISABLE: return "Disable";
    case ACT_REMOVE_LOCAL: return "Remove from PLUGINS.TXT";
    }
    (void)it;
    return "?";
}

static void build_details(void)
{
    item_t *it = detail_item();
    ui.n_actions = 0;
    ui.n_lines = 0;
    if (!it) return;

    int self = !strcmp(it->id, "pluginmanager");
    int external = it->entry && it->entry->version_file;   /* ARK itself */
    /* reinstalling ARK runs the store's updater: never offer it over a newer ARK */
    int newer = external && pm_version_compare(it->entry->version, it->installed_version) < 0;
    if (it->status == ST_AVAILABLE) ui.actions[ui.n_actions++] = ACT_INSTALL;
    if (it->status == ST_UPDATE) ui.actions[ui.n_actions++] = ACT_UPDATE;
    if (it->status == ST_INSTALLED && it->entry && !newer) ui.actions[ui.n_actions++] = ACT_REINSTALL;
    if (it->n_lines) ui.actions[ui.n_actions++] = it->enabled ? ACT_DISABLE : ACT_ENABLE;
    if ((it->status == ST_INSTALLED || it->status == ST_UPDATE) && !self && !external && db_find(&app.db, it->id))
        ui.actions[ui.n_actions++] = ACT_UNINSTALL;
    if (it->status == ST_LOCAL) ui.actions[ui.n_actions++] = ACT_REMOVE_LOCAL;
    if (ui.action_sel >= ui.n_actions) ui.action_sel = 0;

    const char *text;
    if (it->entry) {
        snprintf(ui.local_desc, sizeof(ui.local_desc), "%s%s%s", it->entry->description,
                 it->entry->notes ? "\n\nNote: " : "", it->entry->notes ? it->entry->notes : "");
        text = ui.local_desc;
    }
    else if (it->status == ST_LOCAL) {
        snprintf(ui.local_desc, sizeof(ui.local_desc),
                 "This plugin was added to PLUGINS.TXT without the Plugin Manager.\n\n"
                 "File: %s\nListed in: %s\nLoads in: %s",
                 it->plugin_path, it->plugin_file, it->runlevels);
        text = ui.local_desc;
    }
    else {
        snprintf(ui.local_desc, sizeof(ui.local_desc),
                 "This package was installed from a store that doesn't list it anymore. "
                 "You can still enable, disable or uninstall it.");
        text = ui.local_desc;
    }
    ui.n_lines = text_wrap(text, 0.56f, 286, 0, ui.line_start, ui.line_len, NELEMS(ui.line_start));
}

static void open_details(item_t *it)
{
    ui.detail_id[0] = 0;
    ui.detail_path[0] = 0;
    if (it->id[0]) pm_strlcpy(ui.detail_id, it->id, sizeof(ui.detail_id));
    else pm_strlcpy(ui.detail_path, it->plugin_path, sizeof(ui.detail_path));
    ui.action_sel = 0;
    ui.desc_scroll = 0;
    ui.screen = SCR_DETAILS;
    build_details();
}

static void draw_details(void)
{
    item_t *it = detail_item();
    if (!it) {
        ui.screen = SCR_GRID;
        return;
    }

    int budget = 1;
    texture *tex = item_icon(it, &budget);
    gfx_round_rect(18, 40, 144, 80, 5, RGBA(0, 0, 0, 90));
    if (tex) gfx_texture(tex, 16, 36, 144, 80, 0xFFFFFFFF);
    else ui_placeholder_icon(16, 36, 144, 80, it->title, it->category);
    draw_badge(it, 16 + 144 - TILE_W, 36);

    /* facts */
    float y = 126;
    char buf[96];
    const char *ver = item_version(it);
    struct { const char *k; const char *v; } facts[7];
    int nf = 0;
    if (ver[0]) facts[nf++] = (typeof(facts[0])){ "Version", ver };
    if (it->installed_version[0] && it->status != ST_LOCAL) facts[nf++] = (typeof(facts[0])){ "Installed", it->installed_version };
    if (it->entry && it->entry->size > 0) {
        pm_format_size(it->entry->size, buf, sizeof(buf));
        facts[nf++] = (typeof(facts[0])){ "Download", buf };
    }
    if (it->entry && it->entry->license) facts[nf++] = (typeof(facts[0])){ "License", it->entry->license };
    if (it->entry && it->entry->updated) facts[nf++] = (typeof(facts[0])){ "Released", it->entry->updated };
    char loads[112];
    if (it->runlevels[0]) {
        snprintf(loads, sizeof(loads), "%s%s", it->runlevels, it->enabled ? "" : " (off)");
        facts[nf++] = (typeof(facts[0])){ "Loads in", loads };
    }
    else if (it->entry && it->entry->runlevel) facts[nf++] = (typeof(facts[0])){ "Loads in", it->entry->runlevel };
    for (int i = 0; i < nf; i++) {
        text_draw(16, y, facts[i].k, 0.5f, C_FAINT, 0);
        text_draw_fit(70, y, 92, facts[i].v, 0.5f, C_DIM, 0);
        y += 13;
    }

    /* title block */
    text_draw_fit(172, 34, 292, it->title, 0.86f, C_TEXT, TEXT_BOLD);
    snprintf(buf, sizeof(buf), "by %s", item_author(it));
    text_draw_fit(172, 53, 220, buf, 0.56f, C_DIM, 0);

    const char *chip = ui_category_label(it->category);
    float cw;
    ui_chip(172, 69, chip, ui_category_color(it->category), &cw);
    if (it->status == ST_UPDATE) ui_chip(172 + cw + 6, 69, "Update available", C_WARN, NULL);
    else if (it->status == ST_INSTALLED) ui_chip(172 + cw + 6, 69, "Installed", C_OK, NULL);
    else if (it->status == ST_LOCAL) ui_chip(172 + cw + 6, 69, "Manual plugin", RGB(138, 92, 232), NULL);

    /* description */
    const float box_y = 88, box_h = 124, lh = 12.5f;
    ui_panel(168, box_y, 300, box_h, 6);
    int visible = (int)((box_h - 8) / lh);
    float max_scroll = ui.n_lines > visible ? (ui.n_lines - visible) * lh : 0;
    if (ui.desc_scroll > max_scroll) ui.desc_scroll = max_scroll;
    if (ui.desc_scroll < 0) ui.desc_scroll = 0;
    gfx_clip(168, (int)box_y + 3, 300, (int)box_h - 6);
    for (int i = 0; i < ui.n_lines; i++) {
        float ly = box_y + 5 + i * lh - ui.desc_scroll;
        if (ly < box_y - lh || ly > box_y + box_h) continue;
        u32 color = C_DIM;
        if (ui.line_len[i] >= 5 && !strncmp(ui.line_start[i], "Note:", 5)) color = C_WARN;
        text_draw_n(176, ly, ui.line_start[i], ui.line_len[i], 0.56f, color, 0);
    }
    gfx_noclip();
    if (max_scroll > 0) {
        float h = (box_h - 12) * visible / ui.n_lines;
        float sy = box_y + 6 + (box_h - 12 - h) * (ui.desc_scroll / max_scroll);
        gfx_round_rect(462, sy, 3, h, 1.5f, RGBA(255, 255, 255, 90));
    }

    /* actions */
    float x = 168;
    for (int i = 0; i < ui.n_actions; i++) {
        const char *label = action_label(ui.actions[i], it);
        float w = text_width(label, 0.6f, TEXT_BOLD) + 26;
        int selected = i == ui.action_sel;
        int danger = ui.actions[i] == ACT_UNINSTALL || ui.actions[i] == ACT_REMOVE_LOCAL;
        u32 bg = selected ? (danger ? C_ERR : C_ACCENT) : RGBA(255, 255, 255, 22);
        gfx_round_rect(x, 222, w, 22, 11, bg);
        if (!selected) gfx_round_frame(x, 222, w, 22, 11, 1, C_LINE);
        text_draw(x + w / 2, 226, label, 0.6f, selected ? RGB(255, 255, 255) : C_DIM, TEXT_CENTER | TEXT_BOLD);
        x += w + 8;
    }

    static const int g[] = { GLYPH_CIRCLE, GLYPH_CROSS, GLYPH_UPDOWN };
    static const char *l[] = { "Back", "Select", "Scroll" };
    draw_hints(g, l, 3);
}

/* settings */

static void setting_value(int i, char *out, int size)
{
    switch (i) {
    case SET_STORE:
        pm_strlcpy(out, strcmp(app.cfg.store_url, PM_DEFAULT_STORE) ? app.cfg.store_url : "Default store", size);
        break;
    case SET_ROOT:
        pm_strlcpy(out, !strcmp(app.cfg.root, "ef0:/") ? "Internal storage (ef0)" : "Memory Stick (ms0)", size);
        break;
    case SET_XMB: pm_strlcpy(out, app_xmb_category_enabled() ? "On" : "Off", size); break;
    case SET_TLS: pm_strlcpy(out, app.cfg.verify_tls ? "On" : "Off", size); break;
    case SET_AUTO: pm_strlcpy(out, app.cfg.auto_refresh ? "On" : "Off", size); break;
    default: out[0] = 0; break;
    }
}

static const char *setting_label(int i)
{
    switch (i) {
    case SET_STORE: return "Store address";
    case SET_ROOT: return "Install to";
    case SET_XMB: return "\"Plugins\" category in the XMB";
    case SET_TLS: return "Verify HTTPS certificates";
    case SET_AUTO: return "Refresh the store at startup";
    case SET_ICONS: return "Clear downloaded icons";
    case SET_ABOUT: return "About Plugin Manager";
    }
    return "";
}

static const char *setting_help(int i)
{
    switch (i) {
    case SET_STORE: return "Where the list of plugins and homebrew is downloaded from.";
    case SET_ROOT: return "Storage used for new installs on a PSP Go.";
    case SET_XMB: return "Lists your plugins in the XMB, in place of the PlayStation Network column.";
    case SET_TLS: return "Keep this on: it protects downloads from tampering.";
    case SET_AUTO: return "Connect and download the latest store when the app starts.";
    case SET_ICONS: return "Frees space; icons are downloaded again when needed.";
    case SET_ABOUT: return "Version and credits.";
    }
    return "";
}

static int setting_visible(int i)
{
    return i != SET_ROOT || (app.has_ms && app.has_ef);
}

static void draw_settings(void)
{
    text_draw(16, 34, "Settings", 0.86f, C_TEXT, TEXT_BOLD);
    float y = 60;
    for (int i = 0; i < SET_COUNT; i++) {
        if (!setting_visible(i)) continue;
        int selected = i == ui.set_sel;
        if (selected) {
            gfx_round_rect(10, y - 3, SCREEN_W - 20, 22, 6, COLOR_ALPHA(C_ACCENT, 60));
            gfx_round_frame(10, y - 3, SCREEN_W - 20, 22, 6, 1, COLOR_ALPHA(C_ACCENT, 160));
        }
        text_draw(20, y, setting_label(i), 0.62f, selected ? C_TEXT : C_DIM, selected ? TEXT_BOLD : 0);
        char v[256];
        setting_value(i, v, sizeof(v));
        if (v[0]) {
            u32 c = !strcmp(v, "On") ? C_OK : !strcmp(v, "Off") ? C_FAINT : C_DIM;
            float max = 190;
            float w = text_width(v, 0.58f, 0);
            text_draw_fit(SCREEN_W - 20 - (w < max ? w : max), y + 1, max, v, 0.58f, c, 0);
        }
        y += 24;
    }
    ui_panel(8, 226, SCREEN_W - 16, 24, 6);
    text_draw_fit(16, 231, SCREEN_W - 32, setting_help(ui.set_sel), 0.52f, C_FAINT, 0);

    static const int g[] = { GLYPH_CIRCLE, GLYPH_CROSS };
    static const char *l[] = { "Back", "Change" };
    draw_hints(g, l, 2);
}

/* modal dialogs */

static void draw_modal(void)
{
    float cx, cy, cw;
    const char *starts[16];
    int lens[16];

    switch (ui.modal) {
    case MODAL_MESSAGE:
    case MODAL_CONFIRM: {
        int n = text_wrap(ui.modal_text, 0.58f, 312, 0, starts, lens, 16);
        float h = 26 + n * 13 + 44;
        if (h < 100) h = 100;
        ui_modal(340, h, ui.modal_title, &cx, &cy, &cw);
        for (int i = 0; i < n; i++) text_draw_n(cx, cy + i * 13, starts[i], lens[i], 0.58f, C_DIM, 0);
        float by = (SCREEN_H + h) / 2 - 30;
        const char *labels[2] = { ui.modal == MODAL_CONFIRM ? "Yes" : "OK", "No" };
        int nb = ui.modal == MODAL_CONFIRM ? 2 : 1;
        float bx = (SCREEN_W + 340) / 2 - 14;
        for (int i = nb - 1; i >= 0; i--) {
            float w = 70;
            bx -= w;
            int sel = i == ui.modal_sel;
            gfx_round_rect(bx, by, w, 20, 10, sel ? C_ACCENT : RGBA(255, 255, 255, 22));
            text_draw(bx + w / 2, by + 3, labels[i], 0.6f, sel ? RGB(255, 255, 255) : C_DIM, TEXT_CENTER | TEXT_BOLD);
            bx -= 8;
        }
        break;
    }
    case MODAL_PROGRESS: {
        char title[128];
        const char *verb = job.type == JOB_REFRESH ? "Updating the store" :
                           job.type == JOB_UNINSTALL ? "Removing" : "Installing";
        if (job.type == JOB_REFRESH) snprintf(title, sizeof(title), "%s", verb);
        else snprintf(title, sizeof(title), "%s %s", verb, job.title);
        ui_modal(340, 112, title, &cx, &cy, &cw);

        if (ui.queue_total > 1) {
            char q[48];
            snprintf(q, sizeof(q), "%d of %d", ui.queue_total - ui.queue_n, ui.queue_total);
            text_draw(cx + cw, cy - 24, q, 0.5f, C_FAINT, TEXT_RIGHT);
        }
        text_draw_fit(cx, cy, cw, job.stage[0] ? job.stage : "Please wait...", 0.58f, C_DIM, 0);

        int64_t cur = job.cur, total = job.total;
        float frac = total > 0 ? (float)cur / (float)total : -1.0f;
        ui_progress_bar(cx, cy + 20, cw, 8, frac, C_ACCENT);

        char info[96] = "";
        if (cur > 0) {
            char a[16], b[16];
            pm_format_size(cur, a, sizeof(a));
            if (total > 0) {
                pm_format_size(total, b, sizeof(b));
                snprintf(info, sizeof(info), "%s / %s  (%d%%)", a, b, (int)(frac * 100));
            }
            else {
                snprintf(info, sizeof(info), "%s", a);
            }
        }
        text_draw(cx, cy + 34, info, 0.52f, C_FAINT, 0);
        ui_hint(cx + cw, cy + 52, GLYPH_CIRCLE, job.cancel ? "Cancelling..." : "Cancel");
        break;
    }
    case MODAL_MENU: {
        float h = 30 + ui.menu_n * 20 + 8;
        ui_modal(260, h, ui.modal_title, &cx, &cy, &cw);
        for (int i = 0; i < ui.menu_n; i++) {
            float y = cy + i * 20;
            if (i == ui.menu_sel) gfx_round_rect(cx - 6, y - 2, cw + 12, 19, 6, COLOR_ALPHA(C_ACCENT, 90));
            text_draw(cx, y, ui.menu_labels[i], 0.6f, i == ui.menu_sel ? C_TEXT : C_DIM, i == ui.menu_sel ? TEXT_BOLD : 0);
        }
        break;
    }
    }
}

static void draw_toast(void)
{
    if (!ui.toast[0] || (int)(sceKernelGetSystemTimeLow() - ui.toast_until) > 0) return;
    float w = text_width(ui.toast, 0.58f, 0) + 24;
    float x = (SCREEN_W - w) / 2, y = 196;
    gfx_round_rect(x, y, w, 20, 10, RGBA(10, 12, 20, 225));
    gfx_round_frame(x, y, w, 20, 10, 1, COLOR_ALPHA(C_ACCENT, 140));
    text_draw(SCREEN_W / 2, y + 3.5f, ui.toast, 0.58f, C_TEXT, TEXT_CENTER);
}

static void draw_scene(void *unused)
{
    (void)unused;
    ui_background();
    draw_header();
    switch (ui.screen) {
    case SCR_GRID: draw_grid(); break;
    case SCR_DETAILS: draw_details(); break;
    case SCR_SETTINGS: draw_settings(); break;
    }
}

static void render(void)
{
    gfx_begin();
    draw_scene(NULL);
    draw_toast();
    draw_modal();
    gfx_end();
    gfx_swap();
    ui_frame++;
}

/* ------------------------------------------------------------------------ */
/* on-screen keyboard */

static int osk_input(const char *title, const char *initial, char *out, int outlen)
{
    static unsigned short desc[64], intext[256], outtext[256];
    int i;
    for (i = 0; title[i] && i < 63; i++) desc[i] = (unsigned char)title[i];
    desc[i] = 0;
    for (i = 0; initial[i] && i < 255; i++) intext[i] = (unsigned char)initial[i];
    intext[i] = 0;
    memset(outtext, 0, sizeof(outtext));

    SceUtilityOskData data;
    memset(&data, 0, sizeof(data));
    data.language = PSP_UTILITY_OSK_LANGUAGE_DEFAULT;
    data.lines = 1;
    data.unk_24 = 1;
    data.inputtype = PSP_UTILITY_OSK_INPUTTYPE_ALL;
    data.desc = desc;
    data.intext = intext;
    data.outtextlength = 255;
    data.outtextlimit = outlen - 1 < 255 ? outlen - 1 : 255;
    data.outtext = outtext;

    SceUtilityOskParams params;
    memset(&params, 0, sizeof(params));
    params.base.size = sizeof(params);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &params.base.language);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_UNKNOWN, &params.base.buttonSwap);
    params.base.graphicsThread = 17;
    params.base.accessThread = 19;
    params.base.fontThread = 18;
    params.base.soundThread = 16;
    params.datacount = 1;
    params.data = &data;

    if (sceUtilityOskInitStart(&params) < 0) return 0;
    for (int done = 0; !done && !exit_requested;) {
        gfx_begin();
        draw_scene(NULL);
        gfx_end();
        switch (sceUtilityOskGetStatus()) {
        case PSP_UTILITY_DIALOG_VISIBLE: sceUtilityOskUpdate(1); break;
        case PSP_UTILITY_DIALOG_QUIT: sceUtilityOskShutdownStart(); break;
        case PSP_UTILITY_DIALOG_NONE: done = 1; break;
        default: break;
        }
        gfx_swap();
        ui_frame++;
    }
    input_flush();
    if (data.result != PSP_UTILITY_OSK_RESULT_CHANGED) return 0;

    /* UCS-2 -> UTF-8 */
    int o = 0;
    for (i = 0; outtext[i] && o < outlen - 4; i++) {
        unsigned short c = outtext[i];
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) {
            out[o++] = (char)(0xC0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 0x3F));
        }
        else {
            out[o++] = (char)(0xE0 | (c >> 12));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[o] = 0;
    return 1;
}

/* ------------------------------------------------------------------------ */
/* jobs */

/* shows the system connection dialog; errors are reported, a cancelled
   dialog isn't (the dialog itself shows why a connection failed) */
static int connect_wifi(void)
{
    int r = net_connect_dialog(draw_scene, NULL);
    input_flush();
    if (r < 0) message("Can't connect", net_last_error());
    return r == NET_CONNECTED;
}

static int ensure_online(void)
{
    if (net_is_connected()) return 1;
    if (!net_wlan_switch_on()) {
        message("Wireless is off", "Turn on the WLAN switch of your PSP and try again.");
        return 0;
    }
    return connect_wifi();
}

/* waits for the background icon download to stop */
static void wait_worker_idle(void)
{
    if (!worker_busy()) return;
    if (job.type == JOB_ICONS) worker_cancel();
    while (job.running && !exit_requested) render();
}

static void on_job_finished(void);

static char pending_run[256];

static void do_run(void)
{
    pm_strlcpy(launch_path, pending_run, sizeof(launch_path));
    exit_requested = 1;
}

static int start_job(job_type type, const char *id, const char *title)
{
    wait_worker_idle();
    if (worker_collect()) on_job_finished();
    if (worker_submit(type, id, title) < 0) return -1;
    if (type != JOB_ICONS) ui.modal = MODAL_PROGRESS;
    return 0;
}

static void start_icons(void)
{
    if (app.have_store && net_is_connected() && !worker_busy()) {
        ui.icons_seen = 0;
        start_job(JOB_ICONS, NULL, NULL);
    }
}

static void refresh_store(void)
{
    if (!ensure_online()) return;
    start_job(JOB_REFRESH, NULL, NULL);
}

static void install_item(item_t *it)
{
    if (!it || !it->entry) return;
    if (!ensure_online()) return;
    start_job(JOB_INSTALL, it->id, it->title);
}

static void run_queue(void)
{
    while (ui.queue_n > 0) {
        char id[64];
        pm_strlcpy(id, ui.queue[0], sizeof(id));
        memmove(ui.queue[0], ui.queue[1], sizeof(ui.queue[0]) * (ui.queue_n - 1));
        ui.queue_n--;
        item_t *it = app_find_item(id);
        if (it && it->entry) {
            start_job(JOB_INSTALL, it->id, it->title);
            return;
        }
    }
}

static void on_job_finished(void)
{
    char buf[900];
    if (ui.modal == MODAL_PROGRESS) ui.modal = MODAL_NONE;

    switch (job.type) {
    case JOB_REFRESH:
        if (job.result == 0 && job.new_store) {
            app_set_store(job.new_store, "online");
            free(job.new_store);
            job.new_store = NULL;
            refresh_items();
            if (ui.screen == SCR_DETAILS) build_details();
            snprintf(buf, sizeof(buf), "Store updated: %d items", app.store.count);
            for (int i = 0; i < app.n_items; i++) {
                const item_t *it = &app.items[i];
                if (it->status == ST_UPDATE && it->entry && it->entry->version_file)
                    snprintf(buf, sizeof(buf), "%s %s is available: see Updates", it->title, it->entry->version);
            }
            toast(buf);
            start_icons();
        }
        else if (!job.cancel) {
            snprintf(buf, sizeof(buf), "%s\n\nThe copy saved on your memory stick is shown instead.", job.error);
            message("Couldn't update the store", buf);
        }
        break;

    case JOB_ICONS:
        for (int i = 0; i < app.n_items; i++)
            if (app.items[i].icon_state < 0) app.items[i].icon_state = 0;
        break;

    case JOB_INSTALL:
    case JOB_UNINSTALL: {
        int install = job.type == JOB_INSTALL;
        char title[96];
        pm_strlcpy(title, job.title, sizeof(title));
        refresh_items();
        if (ui.screen == SCR_DETAILS) build_details();

        if (job.result == 0) {
            item_t *it = app_find_item(job.id);
            if (it && is_vsh_item(it)) ui.vsh_plugin_changed = 1;
            if (!install) ui.vsh_plugin_changed = 1;
            ui.installs_done++;
            if (ui.queue_n > 0) {
                run_queue();
                return;
            }
            if (ui.queue_total > 1) {
                snprintf(buf, sizeof(buf), "%d updates were installed.", ui.queue_total);
                ui.queue_total = 0;
                message("Updates installed", buf);
                break;
            }
            ui.queue_total = 0;
            if (install && job.run_path[0]) {
                pm_strlcpy(pending_run, job.run_path, sizeof(pending_run));
                const char *name = job.run_title[0] ? job.run_title : title;
                snprintf(buf, sizeof(buf), "%s is ready. Start it now? The Plugin Manager closes.%s%s\n\n"
                         "You can also start it later: it's %s in the Game column of the XMB.",
                         name, job.messages[0] ? "\n\n" : "", job.messages, name);
                confirm("Start", buf, do_run);
            }
            else if (install) {
                snprintf(buf, sizeof(buf), "%s was installed.%s%s%s", title,
                         job.messages[0] ? "\n\n" : "", job.messages,
                         it && it->n_lines ? "\n\nPlugins are loaded when the XMB or a game starts. Exit the Plugin Manager to reload the XMB." : "");
                message("Done", buf);
            }
            else {
                snprintf(buf, sizeof(buf), "%s was removed.", title);
                toast(buf);
            }
        }
        else {
            ui.queue_n = 0;
            ui.queue_total = 0;
            if (job.cancel) toast("Cancelled");
            else {
                snprintf(buf, sizeof(buf), "%s: %s", title, job.error);
                message(install ? "Installation failed" : "Couldn't remove it", buf);
            }
        }
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* actions */

static void do_uninstall(void)
{
    item_t *it = detail_item();
    if (it) start_job(JOB_UNINSTALL, it->id, it->title);
}

static void do_remove_local(void)
{
    item_t *it = detail_item();
    if (!it) return;
    if (app_remove_local_plugin(it) < 0) {
        message("Error", "Couldn't update PLUGINS.TXT.");
        return;
    }
    ui.vsh_plugin_changed = 1;
    toast("Removed from PLUGINS.TXT");
    ui.screen = SCR_GRID;
    refresh_items();
}

static void do_disable_tls(void)
{
    app.cfg.verify_tls = 0;
    net_set_tls(app.ca_file, 0);
    app_settings_save();
}

static void do_exit(void)
{
    exit_requested = 1;
}

static void run_action(int a)
{
    item_t *it = detail_item();
    if (!it) return;
    char buf[512];
    switch (a) {
    case ACT_INSTALL:
    case ACT_UPDATE:
    case ACT_REINSTALL:
        install_item(it);
        break;
    case ACT_UNINSTALL:
        snprintf(buf, sizeof(buf), "Uninstall %s? Its files will be deleted from your %s.", it->title,
                 !strcmp(app.cfg.root, "ef0:/") ? "internal storage" : "memory stick");
        confirm("Uninstall", buf, do_uninstall);
        break;
    case ACT_ENABLE:
    case ACT_DISABLE:
        if (app_set_plugin_enabled(it, a == ACT_ENABLE) < 0) {
            message("Error", "Couldn't update PLUGINS.TXT.");
            break;
        }
        if (is_vsh_item(it)) ui.vsh_plugin_changed = 1;
        snprintf(buf, sizeof(buf), "%s %s", it->title, a == ACT_ENABLE ? "enabled" : "disabled");
        toast(buf);
        refresh_items();
        build_details();
        break;
    case ACT_REMOVE_LOCAL:
        snprintf(buf, sizeof(buf), "Remove %s from PLUGINS.TXT? The plugin file itself stays on the memory stick.", it->title);
        confirm("Remove plugin", buf, do_remove_local);
        break;
    }
}

static void about(void)
{
    char buf[700];
    snprintf(buf, sizeof(buf),
             "Plugin Manager %s for ARK-5\n"
             "Install plugins and homebrew without a PC. Store: %s (%s, revision %d).\n"
             "Console: %s. Install device: %s.\n\n"
             "Uses libcurl, mbedTLS, cJSON, unarr, zlib, libpng and intraFont. "
             "Distributed under the GPLv3.",
             PM_VERSION, app.have_store ? app.store.title : "none", app.store_origin,
             app.have_store ? app.store.revision : 0, app_model_name(app.model), app.cfg.root);
    message("About", buf);
}

static int count_updates(void)
{
    int n = 0;
    for (int i = 0; i < app.n_items; i++)
        n += app.items[i].status == ST_UPDATE && app.items[i].entry && !app.items[i].entry->runs;
    return n;
}

static void update_all(void)
{
    ui.queue_n = 0;
    for (int i = 0; i < app.n_items && ui.queue_n < (int)NELEMS(ui.queue); i++) {
        item_t *it = &app.items[i];
        /* entries that end by starting a program (ARK) are updated from their page */
        if (it->status == ST_UPDATE && it->entry && !it->entry->runs)
            pm_strlcpy(ui.queue[ui.queue_n++], it->id, sizeof(ui.queue[0]));
    }
    ui.queue_total = ui.queue_n;
    if (ui.queue_n && ensure_online()) run_queue();
    else ui.queue_n = ui.queue_total = 0;
}

static void open_main_menu(void)
{
    char buf[64];
    menu_open("Menu");
    menu_add(MENU_REFRESH, "Refresh store");
    menu_add(MENU_SEARCH, "Search...");
    snprintf(buf, sizeof(buf), "Sort by: %s", sort_names[ui.sort]);
    menu_add(MENU_SORT, buf);
    int n = count_updates();
    if (n) {
        snprintf(buf, sizeof(buf), "Update all (%d)", n);
        menu_add(MENU_UPDATE_ALL, buf);
    }
    menu_add(MENU_SETTINGS, "Settings");
    menu_add(MENU_ABOUT, "About");
    menu_add(MENU_EXIT, "Exit");
}

static void do_search(void)
{
    char q[64];
    if (osk_input("Search plugins and homebrew", ui.search, q, sizeof(q))) {
        pm_strlcpy(ui.search, pm_trim(q), sizeof(ui.search));
        ui.sel = 0;
        ui.scroll = ui.scroll_target = 0;
        rebuild_list();
    }
}

static void menu_choose(int id)
{
    ui.modal = MODAL_NONE;
    switch (id) {
    case MENU_REFRESH: refresh_store(); break;
    case MENU_SEARCH: do_search(); break;
    case MENU_SORT:
        ui.sort = (ui.sort + 1) % SORT_COUNT;
        rebuild_list();
        open_main_menu();
        ui.menu_sel = 2;
        break;
    case MENU_UPDATE_ALL: update_all(); break;
    case MENU_SETTINGS:
        ui.screen = SCR_SETTINGS;
        ui.set_sel = 0;
        break;
    case MENU_ABOUT: about(); break;
    case MENU_EXIT: do_exit(); break;
    case MENU_STORE_DEFAULT:
        pm_strlcpy(app.cfg.store_url, PM_DEFAULT_STORE, sizeof(app.cfg.store_url));
        app_settings_save();
        refresh_store();
        break;
    case MENU_STORE_CUSTOM: {
        char url[256];
        if (osk_input("Store address (https://...)", app.cfg.store_url, url, sizeof(url))) {
            char *u = pm_trim(url);
            if (pm_starts_with(u, "https://")) {
                pm_strlcpy(app.cfg.store_url, u, sizeof(app.cfg.store_url));
                app_settings_save();
                refresh_store();
            }
            else {
                message("Invalid address", "The store address must start with https://");
            }
        }
        break;
    }
    }
}

static int clear_icon_cb(void *ud, const char *name, int is_dir)
{
    (void)ud;
    if (!is_dir) {
        char path[256];
        snprintf(path, sizeof(path), "%s%s", app.icons_dir, name);
        fs_remove(path);
    }
    return 0;
}

static void settings_change(int i)
{
    switch (i) {
    case SET_STORE:
        menu_open("Store address");
        menu_add(MENU_STORE_DEFAULT, "Use the default store");
        menu_add(MENU_STORE_CUSTOM, "Enter another address...");
        break;
    case SET_ROOT:
        pm_strlcpy(app.cfg.root, !strcmp(app.cfg.root, "ef0:/") ? "ms0:/" : "ef0:/", sizeof(app.cfg.root));
        app_settings_save();
        break;
    case SET_XMB:
        app_set_xmb_category(!app_xmb_category_enabled());
        toast("Takes effect when the XMB restarts");
        break;
    case SET_TLS:
        if (app.cfg.verify_tls)
            confirm("Disable certificate checks?",
                    "Downloads could then be modified by anyone on your network. Only do this if "
                    "the date of your PSP can't be set correctly.", do_disable_tls);
        else {
            app.cfg.verify_tls = 1;
            net_set_tls(app.ca_file, 1);
            app_settings_save();
        }
        break;
    case SET_AUTO:
        app.cfg.auto_refresh = !app.cfg.auto_refresh;
        app_settings_save();
        break;
    case SET_ICONS:
        for (int k = 0; k < app.n_items; k++) {
            tex_free(app.items[k].icon);
            app.items[k].icon = NULL;
            app.items[k].icon_state = 0;
        }
        tex_loaded = 0;
        fs_list(app.icons_dir, clear_icon_cb, NULL);
        toast("Icons cleared");
        break;
    case SET_ABOUT:
        about();
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* input */

static void move_grid(int delta)
{
    int n = ui.sel + delta;
    if (n < 0 || n >= ui.list_n) {
        /* horizontal moves wrap within the row edges only */
        if (delta == GRID_COLS && ui.sel / GRID_COLS < (ui.list_n - 1) / GRID_COLS) n = ui.list_n - 1;
        else return;
    }
    ui.sel = n;
    int row = ui.sel / GRID_COLS;
    int first = (int)(ui.scroll_target / ROW_H + 0.5f);
    if (row < first) ui.scroll_target = row * ROW_H;
    if (row > first + 1) ui.scroll_target = (row - 1) * ROW_H;
}

static void handle_grid(const input_state *in)
{
    if (in->repeat & PSP_CTRL_RIGHT) move_grid(1);
    if (in->repeat & PSP_CTRL_LEFT) move_grid(-1);
    if (in->repeat & PSP_CTRL_DOWN) move_grid(GRID_COLS);
    if (in->repeat & PSP_CTRL_UP) move_grid(-GRID_COLS);
    if (in->repeat & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) {
        int d = (in->repeat & PSP_CTRL_RTRIGGER) ? 1 : -1;
        ui.tab = (ui.tab + d + ui.n_tabs) % ui.n_tabs;
        ui.sel = 0;
        ui.scroll = ui.scroll_target = 0;
        rebuild_list();
    }
    if (in->pressed & BTN_CONFIRM) {
        item_t *it = sel_item();
        if (it) open_details(it);
    }
    if (in->pressed & PSP_CTRL_TRIANGLE) open_main_menu();
    if (in->pressed & PSP_CTRL_SQUARE) {
        if (ui.search[0]) {
            ui.search[0] = 0;
            rebuild_list();
        }
        else do_search();
    }
    if (in->pressed & PSP_CTRL_START) {
        ui.screen = SCR_SETTINGS;
        ui.set_sel = 0;
    }
    if (in->pressed & BTN_CANCEL) confirm("Exit", "Exit the Plugin Manager?", do_exit);
}

static void handle_details(const input_state *in)
{
    if (in->repeat & PSP_CTRL_RIGHT && ui.action_sel < ui.n_actions - 1) ui.action_sel++;
    if (in->repeat & PSP_CTRL_LEFT && ui.action_sel > 0) ui.action_sel--;
    if (in->held & PSP_CTRL_DOWN) ui.desc_scroll += 2.5f;
    if (in->held & PSP_CTRL_UP) ui.desc_scroll -= 2.5f;
    if (in->pressed & BTN_CONFIRM && ui.n_actions) run_action(ui.actions[ui.action_sel]);
    if (in->pressed & BTN_CANCEL) ui.screen = SCR_GRID;
}

static void handle_settings(const input_state *in)
{
    if (in->repeat & PSP_CTRL_DOWN) {
        do ui.set_sel = (ui.set_sel + 1) % SET_COUNT;
        while (!setting_visible(ui.set_sel));
    }
    if (in->repeat & PSP_CTRL_UP) {
        do ui.set_sel = (ui.set_sel + SET_COUNT - 1) % SET_COUNT;
        while (!setting_visible(ui.set_sel));
    }
    if (in->pressed & BTN_CONFIRM) settings_change(ui.set_sel);
    if (in->pressed & (BTN_CANCEL | PSP_CTRL_START)) ui.screen = SCR_GRID;
}

static void handle_modal(const input_state *in)
{
    switch (ui.modal) {
    case MODAL_MESSAGE:
        if (in->pressed & (BTN_CONFIRM | BTN_CANCEL)) ui.modal = MODAL_NONE;
        break;
    case MODAL_CONFIRM:
        if (in->repeat & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) ui.modal_sel ^= 1;
        if (in->pressed & BTN_CANCEL) ui.modal = MODAL_NONE;
        if (in->pressed & BTN_CONFIRM) {
            ui.modal = MODAL_NONE;
            if (ui.modal_sel == 0 && ui.on_yes) ui.on_yes();
        }
        break;
    case MODAL_PROGRESS:
        if (in->pressed & BTN_CANCEL) worker_cancel();
        break;
    case MODAL_MENU:
        if (in->repeat & PSP_CTRL_DOWN) ui.menu_sel = (ui.menu_sel + 1) % ui.menu_n;
        if (in->repeat & PSP_CTRL_UP) ui.menu_sel = (ui.menu_sel + ui.menu_n - 1) % ui.menu_n;
        if (in->pressed & (BTN_CANCEL | PSP_CTRL_TRIANGLE)) ui.modal = MODAL_NONE;
        if (in->pressed & BTN_CONFIRM) menu_choose(ui.menu_ids[ui.menu_sel]);
        break;
    }
}

/* XMB "Plugins" category: open the page of the plugin the user picked */
static void handle_launch_request(void)
{
    char kind[16], value[256];
    if (!app_read_launch_request(kind, sizeof(kind), value, sizeof(value))) return;

    item_t *it = NULL;
    if (!strcmp(kind, "entry")) {
        it = app_find_item(value);
    }
    else if (!strcmp(kind, "plugin")) {
        it = app_find_local(value);
        for (int i = 0; !it && i < app.db.count; i++) {
            const db_package *p = &app.db.pkgs[i];
            for (int j = 0; j < p->n_plugins; j++)
                if (ptxt_same_path(p->plugins[j].path, value)) it = app_find_item(p->id);
        }
    }
    if (!it) return;

    select_tab(F_INSTALLED);
    rebuild_list();
    for (int i = 0; i < ui.list_n; i++)
        if (&app.items[ui.list[i]] == it) ui.sel = i;
    move_grid(0);
    open_details(it);
}

/* Starts an EBOOT.PBP the way the XMB starts homebrew. Doesn't return on success. */
static void start_program(const char *path)
{
    struct SceKernelLoadExecVSHParam param;
    memset(&param, 0, sizeof(param));
    param.size = sizeof(param);
    param.args = strlen(path) + 1;
    param.argp = (void *)path;
    param.key = "game";
    /* 0x152: homebrew on the internal storage, 0x141: on the memory stick */
    sctrlKernelLoadExecVSHWithApitype(pm_starts_with(path, "ef0:") ? 0x152 : 0x141, path, &param);
}

/* ------------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);

    entropy_init();
    gfx_init();
    text_init();
    input_init();

    app_init_paths(argc > 0 ? argv[0] : NULL);
    app_settings_load();
    app_db_load();
    net_set_tls(app.ca_file, app.cfg.verify_tls);

    app_load_offline_store();
    rebuild_list();
    worker_start();

    int from_xmb = fs_exists(app.launch_file);
    handle_launch_request();

    /* first frames before a possible Wi-Fi dialog */
    for (int i = 0; i < 10; i++) render();

    if (app.cfg.auto_refresh && !from_xmb && net_wlan_switch_on() && connect_wifi())
        start_job(JOB_REFRESH, NULL, NULL);

    input_state in;
    while (!exit_requested) {
        input_update(&in);
        if (worker_collect()) on_job_finished();

        if (ui.modal != MODAL_NONE) handle_modal(&in);
        else if (ui.screen == SCR_GRID) handle_grid(&in);
        else if (ui.screen == SCR_DETAILS) handle_details(&in);
        else if (ui.screen == SCR_SETTINGS) handle_settings(&in);

        if (job.icons_done != ui.icons_seen) {
            ui.icons_seen = job.icons_done;
            for (int i = 0; i < app.n_items; i++)
                if (app.items[i].icon_state < 0) app.items[i].icon_state = 0;
        }
        render();
    }

    worker_stop();
    net_term();
    text_term();
    gfx_term();
    if (launch_path[0]) start_program(launch_path);
    sceKernelExitGame();
    return 0;
}
