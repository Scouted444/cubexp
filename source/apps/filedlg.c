/* Common Open / Save As dialog for the virtual disk */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    int mode, sel, scroll;
    char ext[12];
    VNode *dir;
    TField name;
    int name_active;
    void (*cb)(const char *, int);
    int ctx;
} FD;

static const char *places[] = { "Desktop", "Documents", "Pictures", "Music", "C:\\" };
static int place_icon[] = { IC_DESKTOP, IC_DOCS, IC_PICS, IC_MUSICF, IC_DRIVE };

static int shown(FD *d, VNode *n) {
    if (n->dir) return n != vfs_recycle && n != vfs_programs;
    if (n->app >= 0) return 0;
    return !d->ext[0] || !strcasecmp(vfs_ext(n), d->ext);
}
static VNode *item(FD *d, int idx) {
    for (VNode *c = d->dir->child; c; c = c->next) if (shown(d, c) && !idx--) return c;
    return NULL;
}

#define LX 86
#define LY 34
static void fd_geom(Window *w, int *lw, int *lh) {
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    *lw = cw - LX - 10; *lh = ch - LY - 70;
}

static void fd_draw(Window *w, int x, int y, int cw, int ch) {
    FD *d = w->data;
    if (!vfs_alive(d->dir)) d->dir = vfs_docs;
    char p[160];
    vfs_path(d->dir, p, sizeof p);
    text(&font_ui, x + 10, y + 12, d->mode == FD_SAVE ? "Save in:" : "Look in:", 0);
    fill(x + LX, y + 8, cw - LX - 70, 21, 0xFFFFFF); rect(x + LX, y + 8, cw - LX - 70, 21, 0x7F9DB9);
    draw_icon(d->dir == vfs_root ? IC_DRIVE : vfs_icon(d->dir), x + LX + 3, y + 10, 16);
    gfx_clip(x + LX, y + 8, cw - LX - 72, 21);
    text(&font_ui, x + LX + 23, y + 12, d->dir == vfs_root ? DRIVE_NAME : p, 0);
    gfx_noclip();
    ui_button(x + cw - 62, y + 7, 24, 23, NULL, w); draw_icon(IC_UP, x + cw - 58, y + 10, 16);
    ui_button(x + cw - 34, y + 7, 24, 23, NULL, w); draw_icon(IC_NEWFOLDER, x + cw - 30, y + 10, 16);
    /* places bar */
    fill(x + 8, y + LY, 70, ch - LY - 70, 0x7A96DF);
    for (int i = 0; i < 5; i++) {
        int py = y + LY + 4 + i * 44;
        if (py + 42 > y + ch - 70) break;
        int hot = inside(mouse_x, mouse_y, x + 10, py, 66, 42);
        if (hot) round_fill(x + 11, py, 64, 42, 3, 0x9CB4EC);
        draw_icon(place_icon[i], x + 31, py + 3, 24);
        text_center(&font_small, x + 8, py + 29, 70, places[i], 0xFFFFFF);
    }
    int lw, lh;
    fd_geom(w, &lw, &lh);
    ui_panel_sunken(x + LX, y + LY, lw, lh, 0xFFFFFF);
    gfx_clip(x + LX + 1, y + LY + 1, lw - 2, lh - 2);
    int colw = 150, rows = (lh - 4) / 20, i = 0;
    if (rows < 1) rows = 1;
    for (VNode *c = d->dir->child; c; c = c->next) {
        if (!shown(d, c)) continue;
        int k = i - d->scroll * rows;
        i++;
        if (k < 0) continue;
        int ix = x + LX + 3 + (k / rows) * colw, iy = y + LY + 3 + (k % rows) * 20;
        if (ix > x + LX + lw) break;
        int s = i - 1 == d->sel;
        if (s) fill(ix + 19, iy, text_w(&font_ui, c->name) + 6, 19, C_SEL);
        draw_icon(vfs_icon(c), ix, iy + 1, 16);
        text(&font_ui, ix + 22, iy + 3, c->name, s ? 0xFFFFFF : 0);
    }
    if (!i) text(&font_ui, x + LX + 8, y + LY + 8, d->ext[0] ? "(no matching files here)" : "(empty folder)", 0x909090);
    gfx_noclip();
    int fy = y + ch - 60;
    text(&font_ui, x + LX, fy + 4, "File name:", 0);
    tf_draw(&d->name, x + LX + 70, fy, cw - LX - 170, w == focus);
    text(&font_ui, x + LX, fy + 32, "Files of type:", 0);
    char t[40];
    snprintf(t, sizeof t, "%s", d->ext[0] ? d->ext + 1 : "All files");
    for (char *c = t; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    text(&font_bold, x + LX + 84, fy + 32, t, 0x404040);
    ui_button(x + cw - 90, fy - 1, 80, 23, d->mode == FD_SAVE ? "Save" : "Open", w);
    ui_button(x + cw - 90, fy + 27, 80, 23, "Cancel", w);
}

static void fd_finish(Window *w) {
    FD *d = w->data;
    char nm[64];
    snprintf(nm, sizeof nm, "%s", d->name.s);
    char *s = nm;
    while (*s == ' ') s++;
    if (!*s) return;
    VNode *hit = vfs_lookup(d->dir, s);
    if (hit && hit->dir) { d->dir = hit; d->sel = -1; d->scroll = 0; tf_set(&d->name, ""); return; }
    if (d->mode == FD_OPEN && (!hit)) { msgbox("Open", "That file doesn't exist.\nCheck the name and try again.", IC_WARN); return; }
    char full[200], base[160];
    vfs_path(d->dir, base, sizeof base);
    if (s[0] == '/' || s[0] == '\\') snprintf(full, sizeof full, "%s", s);
    else snprintf(full, sizeof full, "%s/%s", strcmp(base, "/") ? base : "", s);
    if (d->mode == FD_SAVE) {
        if (!strchr(s, '.') && d->ext[0] && strlen(full) + strlen(d->ext) < sizeof full) strcat(full, d->ext);
        char leaf[40];
        if (!vfs_parent_for(NULL, full, leaf) || !vfs_valid_name(leaf)) {
            msgbox("Save As", "That file name isn't valid.\nNames can't contain / \\ : * ? \" < > |", IC_WARN);
            return;
        }
    }
    void (*cb)(const char *, int) = d->cb;
    int ctx = d->ctx;
    win_close(w);
    cb(full, ctx);
}

static void fd_mouse(Window *w, int ev, int mx, int my) {
    FD *d = w->data;
    int x, y, cw, ch, lw, lh;
    win_client(w, &x, &y, &cw, &ch);
    fd_geom(w, &lw, &lh);
    int rows = (lh - 4) / 20;
    if (rows < 1) rows = 1;
    if (ev == EV_SCROLL) { d->scroll += my; if (d->scroll < 0) d->scroll = 0; return; }
    if (ev != EV_DOWN && ev != EV_DBL) return;
    if (inside(mx, my, cw - 62, 7, 24, 23) && ev == EV_DOWN) { if (d->dir->parent) { d->dir = d->dir->parent; d->sel = -1; d->scroll = 0; } return; }
    if (inside(mx, my, cw - 34, 7, 24, 23) && ev == EV_DOWN) {
        char nm[40];
        vfs_unique_name(d->dir, "New Folder", "", nm, sizeof nm);
        vfs_mkdir(d->dir, nm, 0);
        return;
    }
    for (int i = 0; i < 5; i++) {
        int py = LY + 4 + i * 44;
        if (inside(mx, my, 10, py, 66, 42) && ev == EV_DOWN) {
            VNode *t[] = { vfs_desktop, vfs_docs, vfs_pics, vfs_music, vfs_root };
            d->dir = t[i]; d->sel = -1; d->scroll = 0;
            return;
        }
    }
    if (inside(mx, my, LX, LY, lw, lh)) {
        int col = (mx - LX - 3) / 150, row = (my - LY - 3) / 20;
        if (row >= rows) return;
        int idx = d->scroll * rows + col * rows + row;
        VNode *n = item(d, idx);
        d->sel = n ? idx : -1;
        if (!n) return;
        if (!n->dir) tf_set(&d->name, n->name);
        if (ev == EV_DBL) {
            if (n->dir) { d->dir = n; d->sel = -1; d->scroll = 0; }
            else fd_finish(w);
        }
        return;
    }
    int fy = ch - 60;
    if (inside(mx, my, cw - 90, fy - 1, 80, 23) && ev == EV_DOWN) { fd_finish(w); return; }
    if (inside(mx, my, cw - 90, fy + 27, 80, 23) && ev == EV_DOWN) { win_close(w); return; }
    if (inside(mx, my, LX + 70, fy, cw - LX - 170, 21) && !osk_visible()) osk_toggle();
}
static void fd_key(Window *w, int k) {
    FD *d = w->data;
    if (k == K_ENTER) fd_finish(w);
    else if (k == K_ESC) win_close(w);
    else tf_key(&d->name, k);
}

void file_dialog(int mode, const char *title, const char *ext, const char *start_dir, const char *name,
                 void (*cb)(const char *, int), int ctx) {
    FD *d = calloc(1, sizeof(FD));
    if (!d) return;
    d->mode = mode; d->cb = cb; d->ctx = ctx; d->sel = -1;
    snprintf(d->ext, sizeof d->ext, "%s", ext ? ext : "");
    d->dir = start_dir ? vfs_lookup(NULL, start_dir) : NULL;
    if (!d->dir || !d->dir->dir) d->dir = vfs_docs;
    tf_set(&d->name, name ? name : "");
    Window *w = win_new(title, mode == FD_SAVE ? IC_DRIVE : IC_FOLDER, 470, 330);
    if (!w) { free(d); return; }
    w->app = APP_MSGBOX; w->no_max = 1; w->wants_keys = 1; w->data = d;
    w->draw = fd_draw; w->mouse = fd_mouse; w->key = fd_key;
    win_center(w);
    w->y = 20;
}
