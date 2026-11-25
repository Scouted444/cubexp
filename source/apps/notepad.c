/* Notepad: multi-line editor with word wrap, scrolling, SD save/open */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define NP_MAX 8192
#define NP_LINES 2048
typedef struct {
    char buf[NP_MAX];
    int len, cur, scroll, dirty, want_col;
    char fname[40];
    int starts[NP_LINES], nlines, cols, vis;
} NP;

static const char *np_menu[] = { "File", "Edit", "Help" };

static int ctx_of(Window *w) { return (int)(w - wins) | (w->gen << 8); }
static Window *win_of(int ctx) {
    Window *w = &wins[ctx & 255];
    return (w->used && w->app == APP_NOTEPAD && w->gen == (ctx >> 8)) ? w : NULL;
}

static void np_title(Window *w) {
    NP *d = w->data;
    snprintf(w->title, sizeof w->title, "%s%s - Notepad", d->dirty ? "*" : "", d->fname[0] ? d->fname : "Untitled");
}

static void layout(NP *d, int cols) {
    if (cols < 4) cols = 4;
    d->cols = cols;
    d->nlines = 0;
    int i = 0;
    d->starts[d->nlines++] = 0;
    int col = 0, last_space = -1;
    while (i < d->len && d->nlines < NP_LINES) {
        char c = d->buf[i];
        if (c == '\n') { d->starts[d->nlines++] = i + 1; col = 0; last_space = -1; i++; continue; }
        if (c == ' ') last_space = i;
        col++;
        if (col > cols) {
            int brk = (last_space > d->starts[d->nlines - 1]) ? last_space + 1 : i;
            d->starts[d->nlines++] = brk;
            col = i - brk + 1;
            last_space = -1;
        }
        i++;
    }
}
static int line_end(NP *d, int l) {
    int e = l + 1 < d->nlines ? d->starts[l + 1] : d->len;
    if (l + 1 < d->nlines && e > 0 && d->buf[e - 1] == '\n') e--;
    return e;
}
static int line_of(NP *d, int pos) {
    int l = 0;
    while (l + 1 < d->nlines && d->starts[l + 1] <= pos) l++;
    return l;
}

static void np_geom(Window *w, int *tx, int *ty, int *tw, int *th) {
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    *tx = 0; *ty = 20; *tw = cw - 16; *th = ch - 20;
}

static void np_draw(Window *w, int x, int y, int cw, int ch) {
    NP *d = w->data;
    int tx, ty, tw, th;
    np_geom(w, &tx, &ty, &tw, &th);
    ui_menubar(x, y, cw, np_menu, 3);
    fill(x + tx, y + ty, tw, th, 0xFFFFFF);
    int adv = font_mono.g['m' - 32].adv, lh = 14;
    layout(d, (tw - 8) / adv);
    d->vis = (th - 4) / lh;
    int cl = line_of(d, d->cur);
    if (cl < d->scroll) d->scroll = cl;
    if (cl >= d->scroll + d->vis) d->scroll = cl - d->vis + 1;
    gfx_clip(x + tx, y + ty, tw, th);
    for (int i = 0; i < d->vis && d->scroll + i < d->nlines; i++) {
        int l = d->scroll + i;
        text_n(&font_mono, x + tx + 4, y + ty + 3 + i * lh, d->buf + d->starts[l], line_end(d, l) - d->starts[l], 0);
    }
    if (w == focus && (now_ms / 500) % 2 == 0) {
        int col = d->cur - d->starts[cl];
        vline(x + tx + 4 + col * adv, y + ty + 3 + (cl - d->scroll) * lh, lh, 0);
    }
    gfx_noclip();
    ui_scrollbar(x + cw - 16, y + ty, th, d->nlines, d->vis, d->scroll);
}

static void insert(NP *d, char c) {
    if (d->len >= NP_MAX - 1) { sound_play(SND_ERROR); return; }
    memmove(d->buf + d->cur + 1, d->buf + d->cur, d->len - d->cur);
    d->buf[d->cur++] = c; d->len++; d->dirty = 1;
}

static void np_key(Window *w, int k) {
    NP *d = w->data;
    int cl = line_of(d, d->cur);
    switch (k) {
    case K_BKSP:
        if (d->cur > 0) { memmove(d->buf + d->cur - 1, d->buf + d->cur, d->len - d->cur); d->cur--; d->len--; d->dirty = 1; }
        break;
    case K_DEL:
        if (d->cur < d->len) { memmove(d->buf + d->cur, d->buf + d->cur + 1, d->len - d->cur - 1); d->len--; d->dirty = 1; }
        break;
    case K_LEFT: if (d->cur > 0) d->cur--; break;
    case K_RIGHT: if (d->cur < d->len) d->cur++; break;
    case K_HOME: d->cur = d->starts[cl]; break;
    case K_END: d->cur = line_end(d, cl); break;
    case K_UP: case K_DOWN: {
        int nl = cl + (k == K_UP ? -1 : 1);
        if (nl < 0 || nl >= d->nlines) break;
        int col = d->cur - d->starts[cl];
        int e = line_end(d, nl);
        d->cur = d->starts[nl] + col;
        if (d->cur > e) d->cur = e;
        break; }
    case K_ENTER: insert(d, '\n'); break;
    case K_TAB: for (int i = 0; i < 4; i++) insert(d, ' '); break;
    default: if (k >= 32 && k < 127) insert(d, (char)k);
    }
    np_title(w);
}

static void np_mouse(Window *w, int ev, int mx, int my) {
    NP *d = w->data;
    int tx, ty, tw, th;
    np_geom(w, &tx, &ty, &tw, &th);
    if (ev == EV_SCROLL) {
        d->scroll += my * 3;
        if (d->scroll > d->nlines - d->vis) d->scroll = d->nlines - d->vis;
        if (d->scroll < 0) d->scroll = 0;
        /* keep cursor visible so the auto-scroll doesn't fight */
        int cl = line_of(d, d->cur);
        if (cl < d->scroll) d->cur = d->starts[d->scroll];
        if (cl >= d->scroll + d->vis && d->scroll + d->vis - 1 < d->nlines) d->cur = d->starts[d->scroll + d->vis - 1];
        return;
    }
    if (ev != EV_DOWN) return;
    int m = ui_menubar_hit(mx, my, np_menu, 3);
    if (m >= 0) {
        int x, y, cw, ch;
        win_client(w, &x, &y, &cw, &ch);
        int px = x + 4;
        for (int i = 0; i < m; i++) px += text_w(&font_ui, np_menu[i]) + 12;
        extern void np_menu_cb(int, int);
        if (m == 0) {
            static const MItem it[] = { { "New", 1 }, { "Open...", 2 }, { "Save", 3 }, { "Save As...", 4 }, { NULL, 0 }, { "Exit", 5 } };
            shell_popup(px - 3, y + 19, it, 6, np_menu_cb, ctx_of(w));
        } else if (m == 1) {
            static const MItem it[] = { { "Time/Date", 6 }, { "Clear All", 7 }, { NULL, 0 }, { "Show Keyboard", 9 } };
            shell_popup(px - 3, y + 19, it, 4, np_menu_cb, ctx_of(w));
        } else {
            static const MItem it[] = { { "About Notepad", 8 } };
            shell_popup(px - 3, y + 19, it, 1, np_menu_cb, ctx_of(w));
        }
        return;
    }
    if (mx >= tw) {   /* scrollbar */
        d->scroll += my < ty + th / 2 ? -d->vis : d->vis;
        if (d->scroll > d->nlines - d->vis) d->scroll = d->nlines - d->vis;
        if (d->scroll < 0) d->scroll = 0;
        return;
    }
    if (my >= ty) {
        int adv = font_mono.g['m' - 32].adv;
        int l = d->scroll + (my - ty - 3) / 14;
        if (l >= d->nlines) l = d->nlines - 1;
        if (l < 0) l = 0;
        int col = (mx - 4 + adv / 2) / adv;
        if (col < 0) col = 0;
        int e = line_end(d, l);
        d->cur = d->starts[l] + col;
        if (d->cur > e) d->cur = e;
        if (!osk_visible()) osk_toggle();
    }
}

static int load_file(NP *d, const char *name) {
    char p[96];
    snprintf(p, sizeof p, "%s/%s", plat_storage_path(), name);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    d->len = (int)fread(d->buf, 1, NP_MAX - 1, f);
    fclose(f);
    int j = 0;   /* strip CRs */
    for (int i = 0; i < d->len; i++) if (d->buf[i] != '\r') d->buf[j++] = d->buf[i];
    d->len = j;
    d->cur = 0; d->scroll = 0; d->dirty = 0;
    strncpy(d->fname, name, sizeof d->fname - 1);
    return 1;
}
static void save_to(Window *w, const char *name) {
    NP *d = w->data;
    if (!plat_storage_ok()) {
        msgbox("Notepad", "No SD card was found, so the file can't be saved.\nSee Help and Support for how to add one.", IC_WARN);
        return;
    }
    char nm[40];
    strncpy(nm, name, 35); nm[35] = 0;
    if (!strchr(nm, '.')) strcat(nm, ".txt");
    char p[96];
    snprintf(p, sizeof p, "%s/%s", plat_storage_path(), nm);
    FILE *f = fopen(p, "wb");
    if (!f) { msgbox("Notepad", "The file could not be written.", IC_WARN); return; }
    fwrite(d->buf, 1, d->len, f);
    fclose(f);
    strcpy(d->fname, nm);
    d->dirty = 0;
    np_title(w);
    char m[80];
    snprintf(m, sizeof m, "Saved %s to the SD card.", nm);
    notify("Notepad", m);
}
static void saveas_cb(const char *name, int ctx) {
    Window *w = win_of(ctx);
    if (w && *name) save_to(w, name);
}
static void open_cb(const char *name, int ctx) {
    Window *w = win_of(ctx);
    if (!w) return;
    if (!load_file(w->data, name)) msgbox("Notepad", "The file could not be opened.", IC_WARN);
    np_title(w);
    win_focus(w);
}

void np_menu_cb(int id, int ctx) {
    Window *w = win_of(ctx);
    if (!w) return;
    NP *d = w->data;
    switch (id) {
    case 1: d->len = d->cur = d->scroll = 0; d->dirty = 0; d->fname[0] = 0; break;
    case 2: file_picker("Open", ".txt", open_cb, ctx); break;
    case 3: if (d->fname[0]) save_to(w, d->fname); else prompt_box("Save As", "File name:", "note.txt", saveas_cb, ctx); break;
    case 4: prompt_box("Save As", "File name:", d->fname[0] ? d->fname : "note.txt", saveas_cb, ctx); break;
    case 5: win_close(w); return;
    case 6: {
        time_t t = time(NULL);
        char b[40];
        strftime(b, sizeof b, cfg.clock24 ? "%H:%M %d/%m/%Y" : "%I:%M %p %m/%d/%Y", localtime(&t));
        for (char *p = b; *p; p++) insert(d, *p);
        break; }
    case 7: d->len = d->cur = d->scroll = 0; d->dirty = 1; break;
    case 8: msgbox("About Notepad", "CubeXP Notepad\n\nUp to 8 KB of text per file.\nFiles are saved to the SD card in /cubexp.", IC_NOTEPAD); break;
    case 9: if (!osk_visible()) osk_toggle(); break;
    }
    np_title(w);
}

static Window *np_new(void) {
    Window *w = win_new("Untitled - Notepad", IC_NOTEPAD, 400, 280);
    if (!w) return NULL;
    NP *d = calloc(1, sizeof(NP));
    if (!d) { win_close(w); return NULL; }
    w->app = APP_NOTEPAD; w->resizable = 1; w->wants_keys = 1; w->data = d;
    w->min_w = 220; w->min_h = 140;
    w->draw = np_draw; w->key = np_key; w->mouse = np_mouse;
    return w;
}

void open_notepad(void) {
    Window *w = np_new();
    if (!w) return;
    NP *d = w->data;
    if (plat_storage_ok() && load_file(d, "readme.txt")) { np_title(w); return; }
    const char *hello =
        "Welcome to CubeXP Notepad!\n\n"
        "Click the keys on the On-Screen Keyboard (press X) to type.\n"
        "L = Backspace, R = Space, Y = Enter, D-Pad = arrow keys.\n\n"
        "Use File > Save to keep your notes on the SD card.\n";
    strcpy(d->buf, hello);
    d->len = strlen(hello); d->cur = d->len;
    np_title(w);
}

void notepad_open_file(const char *name) {
    Window *w = np_new();
    if (!w) return;
    NP *d = w->data;
    if (name && !load_file(d, name)) msgbox("Notepad", "The file could not be opened.", IC_WARN);
    if (!name) strcpy(d->fname, "New Text Document.txt");
    np_title(w);
}
