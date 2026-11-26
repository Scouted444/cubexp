/* Windows Explorer for the virtual disk */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    VNode *dir;
    VNode *back[16];
    int nback;
    int sel, scroll, view;      /* view 0 = icons, 1 = details */
    int last_changes;
} EX;

static VNode *clip_node;
static int clip_cut;

void vfs_open(VNode *n) {
    if (!n) return;
    char p[200];
    vfs_path(n, p, sizeof p);
    if (n->dir) { explorer_open_path(p); return; }
    if (n->app >= 0) { app_launch(n->app); return; }
    const char *e = vfs_ext(n);
    if (!strcasecmp(e, ".bmp")) viewer_open_file(p);
    else if (!strcasecmp(e, ".song")) music_open_file(p);
    else if (!strcasecmp(e, ".sh") || !strcasecmp(e, ".bat")) { char c[220]; snprintf(c, sizeof c, "sh \"%s\"", p); terminal_run(c); }
    else notepad_open_file(p);
}

static int ctx_of(Window *w) { return (int)(w - wins) | (w->gen << 8); }
static Window *win_of(int ctx) {
    Window *w = &wins[ctx & 255];
    return (w->used && w->app == APP_EXPLORER && w->gen == (ctx >> 8)) ? w : NULL;
}

static VNode *item(EX *d, int i) { VNode *c = d->dir->child; while (c && i--) c = c->next; return c; }

static void set_title(Window *w) {
    EX *d = w->data;
    const char *t = d->dir == vfs_root ? DRIVE_NAME : d->dir == vfs_docs ? "My Documents" : d->dir == vfs_pics ? "My Pictures" :
                    d->dir == vfs_music ? "My Music" : d->dir->name;
    snprintf(w->title, sizeof w->title, "%s", t);
    w->icon = d->dir == vfs_root ? IC_DRIVE : vfs_icon(d->dir);
}
static void go(Window *w, VNode *dir, int push) {
    EX *d = w->data;
    if (!dir || !dir->dir || dir == d->dir) return;
    if (push) {
        if (d->nback == 16) { memmove(d->back, d->back + 1, sizeof(VNode *) * 15); d->nback--; }
        d->back[d->nback++] = d->dir;
    }
    d->dir = dir; d->sel = -1; d->scroll = 0;
    set_title(w);
}

/* layout */
#define TB_H 34
#define AD_H 26
#define PANE 168
#define ST_H 20
static void content_rect(Window *w, int *x, int *y, int *cw, int *ch) {
    int X, Y, CW, CH;
    win_client(w, &X, &Y, &CW, &CH);
    *x = PANE; *y = TB_H + AD_H; *cw = CW - PANE; *ch = CH - TB_H - AD_H - ST_H;
}

static const char *tb_labels[] = { "Back", "Up", "New Folder", "Delete", "Views" };
static const int tb_icons[] = { IC_BACK, IC_UP, IC_NEWFOLDER, IC_DELETE, IC_SHOWDESK };
static int tb_x(int i) { int x = 6; for (int k = 0; k < i; k++) x += 26 + text_w(&font_ui, tb_labels[k]) + 10; return x; }

static const char *place_names[] = { "Desktop", "My Documents", "My Pictures", "My Music", DRIVE_NAME, "Recycle Bin" };
static VNode **place_nodes[] = { &vfs_desktop, &vfs_docs, &vfs_pics, &vfs_music, &vfs_root, &vfs_recycle };
static const char *task_names[] = { "Make a new folder", "New text document", "Rename this item", "Delete this item", "Copy this item", "Paste here" };
static const int task_icons[] = { IC_NEWFOLDER, IC_TEXTFILE, IC_FILE, IC_DELETE, IC_DOCS, IC_FOLDER };

static void pane_box(int x, int y, int w, int h, const char *title) {
    round_fill(x, y, w, h, 4, 0xD6DFF7);
    round_grad(x, y, w, 22, 4, 0xFFFFFF, 0xC6D3F7, 1);
    text(&font_bold, x + 10, y + 5, title, 0x215DC6);
}

static void ex_draw(Window *w, int x, int y, int cw, int ch) {
    EX *d = w->data;
    if (!vfs_alive(d->dir)) { d->dir = vfs_root; set_title(w); }
    if (d->last_changes != vfs_changes) { d->last_changes = vfs_changes; if (!item(d, d->sel)) d->sel = -1; }
    /* toolbar */
    grad_v(x, y, cw, TB_H, 0xFCFCFE, 0xE8E6DC);
    hline(x, y + TB_H - 1, cw, C_SHADOW);
    for (int i = 0; i < 5; i++) {
        int bx = x + tb_x(i), bw = 26 + text_w(&font_ui, tb_labels[i]) + 4;
        int dis = (i == 0 && !d->nback) || (i == 1 && !d->dir->parent) || (i == 3 && d->sel < 0);
        if (!dis && w == focus && inside(mouse_x, mouse_y, bx, y + 4, bw, 26)) { round_fill(bx, y + 4, bw, 26, 3, 0xC4C2B4); round_fill(bx + 1, y + 5, bw - 2, 24, 3, 0xFFFFFF); }
        draw_icon_a(tb_icons[i], bx + 4, y + 9, 16, dis ? 90 : 256);
        text(&font_ui, bx + 24, y + 11, tb_labels[i], dis ? 0xA0A0A0 : 0);
    }
    /* address bar */
    int ay = y + TB_H;
    fill(x, ay, cw, AD_H, C_FACE);
    hline(x, ay + AD_H - 1, cw, C_SHADOW);
    text(&font_ui, x + 8, ay + 6, "Address", 0x606060);
    fill(x + 60, ay + 3, cw - 68, 20, 0xFFFFFF); rect(x + 60, ay + 3, cw - 68, 20, 0x7F9DB9);
    draw_icon(d->dir == vfs_root ? IC_DRIVE : vfs_icon(d->dir), x + 63, ay + 5, 16);
    char p[200], disp[210];
    vfs_path(d->dir, p, sizeof p);
#ifdef CUBELINUX
    snprintf(disp, sizeof disp, "%s%s", getenv("HOME") ? getenv("HOME") : "/home/player", strcmp(p, "/") ? p : "");
#else
    snprintf(disp, sizeof disp, "C:%s", p);
    for (char *c = disp; *c; c++) if (*c == '/') *c = '\\';
#endif
    text(&font_ui, x + 84, ay + 6, disp, 0);
    /* task pane */
    int py = ay + AD_H;
    int ph = ch - TB_H - AD_H;
    grad_v(x, py, PANE, ph, 0x7BA2E7, 0x6375D6);
    gfx_clip(x, py, PANE, ph);
    int bx = x + 10, by = py + 10;
    pane_box(bx, by, PANE - 20, 26 + 6 * 18, "File and Folder Tasks");
    for (int i = 0; i < 6; i++) {
        int ty = by + 28 + i * 18;
        int dis = (i >= 2 && i <= 4 && d->sel < 0) || (i == 5 && !clip_node);
        int hot = !dis && inside(mouse_x, mouse_y, bx + 6, ty, PANE - 32, 16);
        draw_icon_a(task_icons[i], bx + 6, ty, 16, dis ? 100 : 256);
        text(&font_ui, bx + 26, ty + 2, task_names[i], dis ? 0x8A9AC8 : hot ? 0x428EFF : 0x215DC6);
        if (hot) hline(bx + 26, ty + 15, text_w(&font_ui, task_names[i]), 0x428EFF);
    }
    by += 26 + 6 * 18 + 12;
    pane_box(bx, by, PANE - 20, 26 + 6 * 18, "Other Places");
    for (int i = 0; i < 6; i++) {
        int ty = by + 28 + i * 18;
        VNode *n = *place_nodes[i];
        int hot = inside(mouse_x, mouse_y, bx + 6, ty, PANE - 32, 16);
        draw_icon(n == vfs_root ? IC_DRIVE : vfs_icon(n), bx + 6, ty, 16);
        text(&font_ui, bx + 26, ty + 2, place_names[i], hot ? 0x428EFF : 0x215DC6);
    }
    gfx_noclip();
    /* content */
    int cx, cy, ccw, cch;
    content_rect(w, &cx, &cy, &ccw, &cch);
    cx += x; cy += y;
    fill(cx, cy, ccw, cch, 0xFFFFFF);
    gfx_clip(cx, cy, ccw, cch);
    int i = 0;
    if (d->view == 0) {
        int per = ccw / 86;
        if (per < 1) per = 1;
        for (VNode *n = d->dir->child; n; n = n->next, i++) {
            int row = i / per - d->scroll, col = i % per;
            if (row < 0) continue;
            int ix = cx + 6 + col * 86, iy = cy + 6 + row * 74;
            if (iy > cy + cch) break;
            int s = i == d->sel;
            draw_icon(vfs_icon(n), ix + 27, iy + 2, 32);
            if (s) fill_a(ix + 27, iy + 2, 32, 32, C_SEL, 90);
            char nm[40];
            snprintf(nm, sizeof nm, "%s", n->name);
            while (text_w(&font_ui, nm) > 82 && strlen(nm) > 4) { int L = strlen(nm); nm[L - 4] = '.'; nm[L - 3] = '.'; nm[L - 2] = 0; }
            int tw = text_w(&font_ui, nm);
            if (s) fill(ix + 43 - tw / 2 - 2, iy + 38, tw + 4, 15, C_SEL);
            text(&font_ui, ix + 43 - tw / 2, iy + 39, nm, s ? 0xFFFFFF : 0);
        }
    } else {
        fill(cx, cy, ccw, 18, C_FACE);
        hline(cx, cy + 18, ccw, C_SHADOW);
        static const char *hd[] = { "Name", "Size", "Type", "Modified" };
        static const int hx[] = { 4, 200, 270, 360 };
        for (int k = 0; k < 4; k++) { text(&font_ui, cx + hx[k] + 2, cy + 3, hd[k], 0); vline(cx + hx[k] - 2, cy + 2, 14, C_SHADOW); }
        for (VNode *n = d->dir->child; n; n = n->next, i++) {
            int row = i - d->scroll;
            if (row < 0) continue;
            int iy = cy + 20 + row * 19;
            if (iy > cy + cch) break;
            int s = i == d->sel;
            if (s) fill(cx + 2, iy, ccw - 4, 19, C_SEL);
            uint32_t fg = s ? 0xFFFFFF : 0;
            draw_icon(vfs_icon(n), cx + 6, iy + 1, 16);
            gfx_clip(cx, cy, 196, cch);
            text(&font_ui, cx + 26, iy + 3, n->name, fg);
            gfx_clip(cx, cy, ccw, cch);
            char b[32];
            if (n->dir) b[0] = 0; else if (n->size < 1024) snprintf(b, sizeof b, "%d B", n->size); else snprintf(b, sizeof b, "%d KB", (n->size + 1023) / 1024);
            text(&font_ui, cx + 202, iy + 3, b, fg);
            const char *e = vfs_ext(n);
            snprintf(b, sizeof b, "%s", n->dir ? "File Folder" : n->app >= 0 ? "Application" : *e ? e + 1 : "File");
            text(&font_ui, cx + 272, iy + 3, b, fg);
            time_t t = n->mtime;
            strftime(b, sizeof b, "%m/%d/%Y %H:%M", localtime(&t));
            text(&font_ui, cx + 362, iy + 3, b, fg);
        }
    }
    if (!d->dir->child) text(&font_ui, cx + 14, cy + (d->view ? 28 : 14), d->dir == vfs_recycle ? "The Recycle Bin is empty." : "This folder is empty.", 0x909090);
    gfx_noclip();
    /* status bar */
    int sy = y + ch - ST_H;
    fill(x + PANE, sy, cw - PANE, ST_H, C_FACE);
    hline(x + PANE, sy, cw - PANE, C_SHADOW);
    char st[96];
    VNode *s = item(d, d->sel);
    if (s && !s->dir) snprintf(st, sizeof st, "%s   %d bytes", s->name, s->size);
    else snprintf(st, sizeof st, "%d object%s", vfs_count(d->dir), vfs_count(d->dir) == 1 ? "" : "s");
    text(&font_ui, x + PANE + 6, sy + 4, st, 0);
    text(&font_ui, x + cw - 110, sy + 4, "My Computer", 0x606060);
}

/* ---- actions ---- */
static void rename_cb(const char *name, int ctx) {
    Window *w = win_of(ctx);
    if (!w) return;
    EX *d = w->data;
    VNode *n = item(d, d->sel);
    if (!n || !*name) return;
    if (!vfs_move(n, n->parent, name)) msgbox("Rename", "That name isn't valid, or another item already uses it.", IC_WARN);
}
static void ex_new_folder(Window *w) {
    EX *d = w->data;
    char nm[40];
    vfs_unique_name(d->dir, "New Folder", "", nm, sizeof nm);
    if (!vfs_mkdir(d->dir, nm, 0)) { msgbox("Explorer", "Can't create a folder here.", IC_WARN); return; }
    int i = 0;
    for (VNode *c = d->dir->child; c; c = c->next, i++) if (!strcmp(c->name, nm)) d->sel = i;
    prompt_box("Rename", "Name for the new folder:", nm, rename_cb, ctx_of(w));
}
static void ex_new_text(Window *w) {
    EX *d = w->data;
    char nm[40];
    vfs_unique_name(d->dir, "New Text Document", ".txt", nm, sizeof nm);
    VNode *f = vfs_create(d->dir, nm);
    if (!f) { msgbox("Explorer", "Can't create a file here.", IC_WARN); return; }
    int i = 0;
    for (VNode *c = d->dir->child; c; c = c->next, i++) if (c == f) d->sel = i;
    prompt_box("Rename", "Name for the new document:", nm, rename_cb, ctx_of(w));
}
static void ex_delete(Window *w) {
    EX *d = w->data;
    VNode *n = item(d, d->sel);
    if (!n) return;
    int in_bin = vfs_is_inside(n, vfs_recycle);
    char nm[40];
    snprintf(nm, sizeof nm, "%s", n->name);
    if (!vfs_remove(n, !in_bin)) { msgbox("Delete", "This item can't be deleted.", IC_WARN); return; }
    d->sel = -1;
    char m[96];
    snprintf(m, sizeof m, in_bin ? "'%s' was permanently deleted." : "'%s' was moved to the Recycle Bin.", nm);
    notify("Deleted", m);
    sound_play(SND_CLICK);
}
static void ex_paste(Window *w) {
    EX *d = w->data;
    if (!clip_node || !vfs_alive(clip_node)) { clip_node = NULL; return; }
    int ok = clip_cut ? vfs_move(clip_node, d->dir, NULL) : vfs_copy(clip_node, d->dir, NULL) != NULL;
    if (!ok) msgbox("Paste", "The item couldn't be pasted here.\n(Is a file with that name already here?)", IC_WARN);
    if (clip_cut && ok) clip_node = NULL;
}
static void ex_props(VNode *n) {
    char p[200], m[360], when[40];
    vfs_path(n, p, sizeof p);
    time_t t = n->mtime;
    strftime(when, sizeof when, "%A, %B %d, %Y %H:%M", localtime(&t));
    snprintf(m, sizeof m, "%s\n\nType:      %s\nLocation:  C:%s\nSize:      %ld bytes%s\nModified:  %s", n->name,
             n->dir ? "File Folder" : n->app >= 0 ? "Application shortcut" : "File", p, vfs_total_size(n),
             n->dir ? "" : "", when);
    if (n->dir) { char c[40]; snprintf(c, sizeof c, "\nContains:  %d items", vfs_count(n)); strncat(m, c, sizeof m - strlen(m) - 1); }
    msgbox("Properties", m, vfs_icon(n));
}

static void ex_menu_cb(int id, int ctx) {
    Window *w = win_of(ctx);
    if (!w) return;
    EX *d = w->data;
    VNode *n = item(d, d->sel);
    switch (id) {
    case 1: if (n) vfs_open(n); break;
    case 2: if (n) { clip_node = n; clip_cut = 1; } break;
    case 3: if (n) { clip_node = n; clip_cut = 0; } break;
    case 4: if (n) prompt_box("Rename", "New name:", n->name, rename_cb, ctx); break;
    case 5: ex_delete(w); break;
    case 6: if (n) ex_props(n); break;
    case 7: d->view = 0; break;
    case 8: d->view = 1; break;
    case 9: ex_new_folder(w); break;
    case 10: ex_new_text(w); break;
    case 11: ex_paste(w); break;
    case 12: if (n) vfs_restore(n); break;
    case 13: if (n && !n->dir) { char p[200]; vfs_path(n, p, sizeof p); notepad_open_file(p); } break;
    case 14: if (n) { char p[200], c[220]; vfs_path(n, p, sizeof p); snprintf(c, sizeof c, "sh \"%s\"", p); terminal_run(c); } break;
    case 15: { char p[200], c[220]; vfs_path(d->dir, p, sizeof p); snprintf(c, sizeof c, "cd \"%s\"", p); terminal_run(c); break; }
    }
}

static void ex_mouse(Window *w, int ev, int mx, int my) {
    EX *d = w->data;
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    int cx, cy, ccw, cch;
    content_rect(w, &cx, &cy, &ccw, &cch);
    if (ev == EV_SCROLL) { d->scroll += my; if (d->scroll < 0) d->scroll = 0; return; }
    if (ev != EV_DOWN && ev != EV_DBL && ev != EV_RDOWN) return;
    if (my < TB_H) {
        if (ev != EV_DOWN) return;
        for (int i = 0; i < 5; i++) {
            int bx = tb_x(i), bw = 26 + text_w(&font_ui, tb_labels[i]) + 4;
            if (!inside(mx, my, bx, 4, bw, 26)) continue;
            if (i == 0) { while (d->nback && !vfs_alive(d->back[d->nback - 1])) d->nback--; if (d->nback) go(w, d->back[--d->nback], 0); }
            else if (i == 1) go(w, d->dir->parent, 1);
            else if (i == 2) ex_new_folder(w);
            else if (i == 3) ex_delete(w);
            else d->view = !d->view;
            return;
        }
        return;
    }
    if (mx < PANE && my > TB_H + AD_H) {
        if (ev != EV_DOWN) return;
        int by = TB_H + AD_H + 10;
        for (int i = 0; i < 6; i++)
            if (inside(mx, my, 16, by + 28 + i * 18, PANE - 32, 16)) {
                if (i == 0) ex_new_folder(w);
                else if (i == 1) ex_new_text(w);
                else if (i == 2) ex_menu_cb(4, ctx_of(w));
                else if (i == 3) ex_delete(w);
                else if (i == 4) ex_menu_cb(3, ctx_of(w));
                else ex_paste(w);
                return;
            }
        by += 26 + 6 * 18 + 12;
        for (int i = 0; i < 6; i++)
            if (inside(mx, my, 16, by + 28 + i * 18, PANE - 32, 16)) { go(w, *place_nodes[i], 1); return; }
        return;
    }
    if (!inside(mx, my, cx, cy, ccw, cch)) return;
    int idx = -1;
    if (d->view == 0) {
        int per = ccw / 86;
        if (per < 1) per = 1;
        int col = (mx - cx - 6) / 86, row = (my - cy - 6) / 74 + d->scroll;
        if (col < per && mx - cx - 6 >= 0 && my - cy - 6 >= 0) idx = row * per + col;
    } else if (my - cy >= 20) idx = (my - cy - 20) / 19 + d->scroll;
    VNode *n = idx >= 0 ? item(d, idx) : NULL;
    d->sel = n ? idx : -1;
    if (ev == EV_DBL && n) { if (n->dir) go(w, n, 1); else vfs_open(n); return; }
    if (ev == EV_RDOWN) {
        if (n) {
            if (vfs_is_inside(n, vfs_recycle)) {
                static const MItem it[] = { { "Restore", 12 }, { NULL, 0 }, { "Delete", 5 }, { NULL, 0 }, { "Properties", 6 } };
                shell_popup(mouse_x, mouse_y, it, 5, ex_menu_cb, ctx_of(w));
            } else if (!strcasecmp(vfs_ext(n), ".sh")) {
                static const MItem it[] = { { "Run in Terminal", 14 }, { "Edit", 13 }, { NULL, 0 }, { "Cut", 2 }, { "Copy", 3 }, { NULL, 0 }, { "Rename", 4 }, { "Delete", 5 }, { NULL, 0 }, { "Properties", 6 } };
                shell_popup(mouse_x, mouse_y, it, 10, ex_menu_cb, ctx_of(w));
            } else {
                static const MItem it[] = { { "Open", 1 }, { "Edit as text", 13 }, { NULL, 0 }, { "Cut", 2 }, { "Copy", 3 }, { NULL, 0 }, { "Rename", 4 }, { "Delete", 5 }, { NULL, 0 }, { "Properties", 6 } };
                shell_popup(mouse_x, mouse_y, it, 10, ex_menu_cb, ctx_of(w));
            }
        } else {
            static const MItem it[] = { { "View: Icons", 7 }, { "View: Details", 8 }, { NULL, 0 }, { "New Folder", 9 }, { "New Text Document", 10 },
                                        { "Paste", 11 }, { NULL, 0 }, { "Open Terminal Here", 15 } };
            shell_popup(mouse_x, mouse_y, it, 8, ex_menu_cb, ctx_of(w));
        }
    }
}
static void ex_key(Window *w, int k) {
    EX *d = w->data;
    int n = vfs_count(d->dir);
    if (k == K_DEL) ex_delete(w);
    else if (k == K_ENTER) { VNode *s = item(d, d->sel); if (s) { if (s->dir) go(w, s, 1); else vfs_open(s); } }
    else if (k == K_BKSP) go(w, d->dir->parent, 1);
    else if (k == K_RIGHT || k == K_DOWN) { if (d->sel < n - 1) d->sel++; }
    else if (k == K_LEFT || k == K_UP) { if (d->sel > 0) d->sel--; }
}

static Window *ex_new(VNode *dir) {
    Window *w = win_new("Explorer", IC_EXPLORER, 580, 360);
    if (!w) return NULL;
    EX *d = calloc(1, sizeof(EX));
    if (!d) { win_close(w); return NULL; }
    d->dir = dir; d->sel = -1;
    w->app = APP_EXPLORER; w->resizable = 1; w->wants_keys = 1; w->data = d;
    w->min_w = 420; w->min_h = 300;
    w->draw = ex_draw; w->mouse = ex_mouse; w->key = ex_key;
    w->x = 20 + (w->x % 40); w->y = 8 + (w->y % 30);
    if (w->x + w->w > SW) w->x = SW - w->w;
    set_title(w);
    return w;
}
void open_explorer(void) { ex_new(vfs_docs); }
void explorer_open_path(const char *path) {
    VNode *n = vfs_lookup(NULL, path);
    if (!n || !n->dir) n = vfs_root;
    ex_new(n);
}
