/* Sticky Notes: small yellow notes that remember their text (saved in /System/Notes) */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SN_MAX 600
typedef struct { char t[SN_MAX + 1]; int len, cur, color; char path[96]; } Note;
static const uint32_t note_col[4][2] = { { 0xFFF7A0, 0xF8E46A }, { 0xC8F4C0, 0x98DC8C }, { 0xFFD0E4, 0xF4A4C4 }, { 0xCDE6FF, 0x9CC8F4 } };

static void sn_save(Note *d) {
    VNode *f = vfs_create(NULL, d->path);
    if (f) { char hdr[4] = { '#', (char)('0' + d->color), '\n', 0 }; vfs_write(f, hdr, 3); vfs_append(f, d->t, d->len); }
}
static void sn_draw(Window *w, int x, int y, int cw, int ch) {
    Note *d = w->data;
    grad_v(x, y, cw, ch, note_col[d->color][0], mix(note_col[d->color][0], note_col[d->color][1], 120));
    fill(x, y, cw, 18, note_col[d->color][1]);
    for (int i = 0; i < 4; i++) { fill_circle(x + 10 + i * 14, y + 9, 5, 0x606060); fill_circle(x + 10 + i * 14, y + 9, 4, note_col[i][0]); }
    text(&font_small, x + cw - 50, y + 3, "colour", 0x806A20);
    /* word-wrapped text */
    int lx = x + 8, ly = y + 24, maxw = cw - 16, row = 0, cur_x = lx, cur_y = ly;
    const char *s = d->t;
    int i = 0;
    while (i <= d->len) {
        int start = i, last_sp = -1, wpx = 0;
        while (i < d->len && d->t[i] != '\n') {
            int a = font_ui.g[(unsigned char)d->t[i] - 32 < 95 ? (unsigned char)d->t[i] - 32 : 0].adv;
            if (wpx + a > maxw) break;
            if (d->t[i] == ' ') last_sp = i;
            wpx += a; i++;
        }
        int end = i;
        if (i < d->len && d->t[i] != '\n' && last_sp > start) end = i = last_sp + 1;
        text_n(&font_ui, lx, ly + row * 15, s + start, end - start, 0x303020);
        if (d->cur >= start && d->cur <= end) { cur_x = lx + text_wn(&font_ui, s + start, d->cur - start); cur_y = ly + row * 15; }
        row++;
        if (i < d->len && d->t[i] == '\n') { i++; if (i == d->len) { if (d->cur == d->len) { cur_x = lx; cur_y = ly + row * 15; } row++; break; } continue; }
        if (i >= d->len) break;
    }
    if (w == focus && (now_ms / 500) % 2 == 0) vline(cur_x, cur_y - 1, 15, 0x303020);
}
static void sn_key(Window *w, int k) {
    Note *d = w->data;
    if (k == K_BKSP) { if (d->cur) { memmove(d->t + d->cur - 1, d->t + d->cur, d->len - d->cur + 1); d->cur--; d->len--; } }
    else if (k == K_DEL) { if (d->cur < d->len) { memmove(d->t + d->cur, d->t + d->cur + 1, d->len - d->cur); d->len--; } }
    else if (k == K_LEFT) { if (d->cur) d->cur--; }
    else if (k == K_RIGHT) { if (d->cur < d->len) d->cur++; }
    else if (k == K_HOME) d->cur = 0;
    else if (k == K_END) d->cur = d->len;
    else if ((k == K_ENTER || (k >= 32 && k < 127)) && d->len < SN_MAX) {
        memmove(d->t + d->cur + 1, d->t + d->cur, d->len - d->cur + 1);
        d->t[d->cur++] = k == K_ENTER ? '\n' : (char)k; d->len++;
    } else return;
    sn_save(d);
}
static void sn_mouse(Window *w, int ev, int mx, int my) {
    Note *d = w->data;
    if (ev != EV_DOWN) return;
    if (my < 18) { for (int i = 0; i < 4; i++) if (abs(mx - 10 - i * 14) <= 6) { d->color = i; sn_save(d); } return; }
    d->cur = d->len;
    if (!osk_visible()) osk_toggle();
}
static void sn_close(Window *w) {
    Note *d = w->data;
    if (d->len == 0) { VNode *f = vfs_lookup(NULL, d->path); if (f) vfs_remove(f, 0); }
}
static Window *sn_window(const char *path) {
    Window *w = win_new("Sticky Note", IC_STICKY, 200, 160);
    if (!w) return NULL;
    Note *d = calloc(1, sizeof(Note));
    snprintf(d->path, sizeof d->path, "%s", path);
    VNode *f = vfs_lookup(NULL, path);
    if (f && f->size >= 3 && f->data[0] == '#') {
        d->color = (f->data[1] - '0') & 3;
        d->len = f->size - 3 > SN_MAX ? SN_MAX : f->size - 3;
        memcpy(d->t, f->data + 3, d->len);
        d->cur = d->len;
    }
    w->app = APP_STICKY; w->resizable = 1; w->no_max = 1; w->wants_keys = 1; w->data = d;
    w->min_w = 140; w->min_h = 100;
    w->draw = sn_draw; w->key = sn_key; w->mouse = sn_mouse; w->on_close = sn_close;
    w->x = SW - w->w - 20 - (w->x % 80); w->y = 20 + (w->y % 60);
    return w;
}
void open_sticky(void) {
    VNode *dir = vfs_mkdir(vfs_system, "Notes", 1);
    if (!dir) return;
    char nm[40];
    vfs_unique_name(dir, "note", ".txt", nm, sizeof nm);
    char p[96];
    snprintf(p, sizeof p, "/System/Notes/%s", nm);
    Window *w = sn_window(p);
    if (w) sn_save(w->data);
}
/* reopen saved notes at log on */
void sticky_restore_all(void) {
    VNode *dir = vfs_lookup(NULL, "/System/Notes");
    if (!dir) return;
    for (VNode *c = dir->child; c; c = c->next) {
        if (c->dir || c->size <= 3) continue;
        char p[96];
        snprintf(p, sizeof p, "/System/Notes/%s", c->name);
        sn_window(p);
    }
}
