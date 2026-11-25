/* Picture and Fax Viewer: shows BMP pictures from the virtual disk */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct { char path[160]; uint32_t *px; int w, h, zoom; } PV;
static const char *btn_tips[] = { "Previous", "Next", "Zoom", "Set as background", "Edit in Paint", "Delete" };
static const int btn_icons[] = { IC_BACK, IC_BACK, IC_VIEWER, IC_DESKTOP, IC_PAINT, IC_DELETE };
#define BAR 40

static void pv_load(Window *w, const char *path) {
    PV *d = w->data;
    free(d->px); d->px = NULL;
    snprintf(d->path, sizeof d->path, "%s", path);
    VNode *f = vfs_lookup(NULL, path);
    if (f && !f->dir) d->px = bmp_decode(f->data, f->size, &d->w, &d->h);
    const char *nm = strrchr(d->path, '/');
    snprintf(w->title, sizeof w->title, "%.28s - Picture Viewer", nm ? nm + 1 : d->path);
}

static void pv_draw(Window *w, int x, int y, int cw, int ch) {
    PV *d = w->data;
    int ah = ch - BAR;
    fill(x, y, cw, ah, 0xFAFAFA);
    if (d->px) {
        float s = d->zoom ? 1.0f : (float)(cw - 20) / d->w;
        if (!d->zoom && (float)(ah - 20) / d->h < s) s = (float)(ah - 20) / d->h;
        if (!d->zoom && s > 3) s = 3;
        int dw = (int)(d->w * s), dh = (int)(d->h * s);
        int ox = x + (cw - dw) / 2, oy = y + (ah - dh) / 2;
        shadow_rect(ox, oy, dw, dh, 4, 120);
        gfx_clip(x, y, cw, ah);
        int cx0, cy0, cww, chh;
        gfx_getclip(&cx0, &cy0, &cww, &chh);
        for (int j = 0; j < dh; j++) {
            int py = oy + j;
            if (py < cy0 || py >= cy0 + chh) continue;
            const uint32_t *row = d->px + (int)(j / s) * d->w;
            for (int i = 0; i < dw; i++) {
                int px = ox + i;
                if (px < cx0 || px >= cx0 + cww) continue;
                pset(px, py, row[(int)(i / s)]);
            }
        }
        gfx_noclip();
    } else text_center(&font_ui, x, y + ah / 2 - 6, cw, "No preview available - open a .bmp picture.", 0x808080);
    /* toolbar */
    int by = y + ah;
    grad_v(x, by, cw, BAR, 0xF6F6F0, 0xDAD8CC);
    hline(x, by, cw, C_SHADOW);
    int tot = 6 * 34, bx = x + (cw - tot) / 2;
    for (int i = 0; i < 6; i++) {
        int px = bx + i * 34, hot = w == focus && inside(mouse_x, mouse_y, px, by + 5, 30, 30);
        if (hot) { round_fill(px, by + 5, 30, 30, 15, 0xA8C4F0); round_fill(px + 1, by + 6, 28, 28, 14, 0xEAF2FF); }
        if (i == 1) {   /* mirrored "back" arrow = next */
            fill_circle(px + 15, by + 20, 10, 0x2A8A2A); fill_circle(px + 15, by + 19, 9, 0x48B848);
            for (int k = 0; k < 8; k++) vline(px + 20 - k, by + 12 + k, 16 - 2 * k, 0xFFFFFF);
            fill(px + 8, by + 17, 8, 5, 0xFFFFFF);
        } else draw_icon(btn_icons[i], px + 7, by + 12, 16);
    }
    for (int i = 0; i < 6; i++) {
        int px = bx + i * 34;
        if (w == focus && inside(mouse_x, mouse_y, px, by + 5, 30, 30)) {
            int tw = text_w(&font_small, btn_tips[i]) + 8;
            round_fill(px + 15 - tw / 2, by - 18, tw, 16, 3, 0xFFFFE1);
            text(&font_small, px + 19 - tw / 2, by - 16, btn_tips[i], 0);
        }
    }
    if (d->px) {
        char b[40];
        snprintf(b, sizeof b, "%d x %d", d->w, d->h);
        text(&font_small, x + 8, by + 15, b, 0x707070);
    }
}

static void step(Window *w, int dir) {
    PV *d = w->data;
    char parent[160];
    snprintf(parent, sizeof parent, "%s", d->path);
    char *sl = strrchr(parent, '/');
    if (!sl) return;
    if (sl == parent) sl[1] = 0; else *sl = 0;
    VNode *dn = vfs_lookup(NULL, parent);
    if (!dn) return;
    VNode *list[64];
    int n = 0, cur = -1;
    VNode *me = vfs_lookup(NULL, d->path);
    for (VNode *c = dn->child; c && n < 64; c = c->next)
        if (!c->dir && !strcasecmp(vfs_ext(c), ".bmp")) { if (c == me) cur = n; list[n++] = c; }
    if (!n) return;
    cur = (cur + dir + n) % n;
    char p[160];
    vfs_path(list[cur], p, sizeof p);
    pv_load(w, p);
}

static void pv_mouse(Window *w, int ev, int mx, int my) {
    PV *d = w->data;
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    if (ev == EV_SCROLL) { step(w, my > 0 ? 1 : -1); return; }
    if (ev == EV_DBL && my < ch - BAR) { d->zoom = !d->zoom; return; }
    if (ev != EV_DOWN) return;
    int bx = (cw - 6 * 34) / 2;
    for (int i = 0; i < 6; i++) {
        if (!inside(mx, my, bx + i * 34, ch - BAR + 5, 30, 30)) continue;
        switch (i) {
        case 0: step(w, -1); break;
        case 1: step(w, 1); break;
        case 2: d->zoom = !d->zoom; break;
        case 3: if (d->px) { set_custom_wallpaper(d->px, d->w, d->h); settings_save(); notify("Desktop", "Background changed."); } break;
        case 4: paint_open_file(d->path); break;
        case 5: {
            VNode *f = vfs_lookup(NULL, d->path);
            if (f && vfs_remove(f, 1)) { notify("Picture Viewer", "Moved to the Recycle Bin."); win_close(w); }
            break; }
        }
        return;
    }
}
static void pv_pad(Window *w, uint32_t down, uint32_t held) {
    (void)held;
    if (down & B_LEFT) step(w, -1);
    if (down & B_RIGHT) step(w, 1);
}
static void pv_close(Window *w) { PV *d = w->data; free(d->px); d->px = NULL; }

void viewer_open_file(const char *path) {
    Window *w = win_new("Picture Viewer", IC_VIEWER, 460, 340);
    if (!w) return;
    w->data = calloc(1, sizeof(PV));
    w->app = APP_VIEWER; w->resizable = 1; w->wants_pad = 1;
    w->draw = pv_draw; w->mouse = pv_mouse; w->pad = pv_pad; w->on_close = pv_close;
    if (path) pv_load(w, path);
}
void open_viewer(void) {
    VNode *first = NULL;
    for (VNode *c = vfs_pics->child; c; c = c->next) if (!c->dir && !strcasecmp(vfs_ext(c), ".bmp")) { first = c; break; }
    if (first) { char p[160]; vfs_path(first, p, sizeof p); viewer_open_file(p); }
    else viewer_open_file(NULL);
}
