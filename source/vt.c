/* VT100 / ANSI terminal emulator shared by the terminal windows */
#include "vt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct VT {
    Cell s[VR][VC];
    Cell sb[SBL][VC];
    int sbn, sbhead, view;
    int cx, cy, wrap, top, bot, scx, scy, cursor_on;
    uint8_t fg, bg, fl;
    int st, np, p[16], priv;
    int utf8_skip;
    void (*reply)(void *ctx, const char *s);
    void *ctx;
};

static void vt_clear_cells(VT *vt, Cell *c, int n) { for (int i = 0; i < n; i++) { c[i].c = ' '; c[i].fg = vt->fg; c[i].bg = vt->bg; c[i].fl = 0; } }
void vt_reset(VT *vt) {
    void (*rp)(void *, const char *) = vt->reply; void *ctx = vt->ctx;
    memset(vt, 0, sizeof *vt);
    vt->reply = rp; vt->ctx = ctx;
    vt->fg = 7; vt->bg = 0; vt->bot = VR - 1; vt->cursor_on = 1;
    for (int r = 0; r < VR; r++) vt_clear_cells(vt, vt->s[r], VC);
}
static void vt_scroll_up(VT *vt, int top, int bot, int n) {
    for (int k = 0; k < n; k++) {
        if (top == 0 && bot == VR - 1) {   /* keep scrollback */
            memcpy(vt->sb[vt->sbhead], vt->s[0], sizeof vt->s[0]);
            vt->sbhead = (vt->sbhead + 1) % SBL;
            if (vt->sbn < SBL) vt->sbn++;
        }
        memmove(vt->s[top], vt->s[top + 1], sizeof vt->s[0] * (bot - top));
        vt_clear_cells(vt, vt->s[bot], VC);
    }
}
static void vt_scroll_down(VT *vt, int top, int bot, int n) {
    for (int k = 0; k < n; k++) {
        memmove(vt->s[top + 1], vt->s[top], sizeof vt->s[0] * (bot - top));
        vt_clear_cells(vt, vt->s[top], VC);
    }
}
static void vt_lf(VT *vt) {
    if (vt->cy == vt->bot) vt_scroll_up(vt, vt->top, vt->bot, 1);
    else if (vt->cy < VR - 1) vt->cy++;
}
static int P(VT *vt, int i, int def) { return i < vt->np && vt->p[i] > 0 ? vt->p[i] : def; }
static void clampc(VT *vt) {
    if (vt->cx < 0) vt->cx = 0;
    if (vt->cx >= VC) vt->cx = VC - 1;
    if (vt->cy < 0) vt->cy = 0;
    if (vt->cy >= VR) vt->cy = VR - 1;
}
static void vt_sgr(VT *vt) {
    if (!vt->np) { vt->fg = 7; vt->bg = 0; vt->fl = 0; return; }
    for (int i = 0; i < vt->np; i++) {
        int v = vt->p[i];
        if (v == 0) { vt->fg = 7; vt->bg = 0; vt->fl = 0; }
        else if (v == 1) vt->fl |= 1;
        else if (v == 4) vt->fl |= 4;
        else if (v == 7) vt->fl |= 2;
        else if (v == 22) vt->fl &= ~1;
        else if (v == 24) vt->fl &= ~4;
        else if (v == 27) vt->fl &= ~2;
        else if (v >= 30 && v <= 37) vt->fg = v - 30;
        else if (v == 39) vt->fg = 7;
        else if (v >= 40 && v <= 47) vt->bg = v - 40;
        else if (v == 49) vt->bg = 0;
        else if (v >= 90 && v <= 97) vt->fg = v - 90 + 8;
        else if (v >= 100 && v <= 107) vt->bg = v - 100 + 8;
        else if ((v == 38 || v == 48) && i + 2 < vt->np && vt->p[i + 1] == 5) {   /* 256-colour: map to 16 */
            int c = vt->p[i + 2] & 15;
            if (v == 38) vt->fg = c; else vt->bg = c;
            i += 2;
        }
    }
}
static void vt_csi(VT *vt, char f) {
    int n = P(vt, 0, 1);
    switch (f) {
    case 'A': vt->cy -= n; if (vt->cy < vt->top) vt->cy = vt->top; break;
    case 'B': case 'e': vt->cy += n; if (vt->cy > vt->bot) vt->cy = vt->bot; break;
    case 'C': case 'a': vt->cx += n; break;
    case 'D': vt->cx -= n; break;
    case 'E': vt->cy += n; vt->cx = 0; break;
    case 'F': vt->cy -= n; vt->cx = 0; break;
    case 'G': case '`': vt->cx = n - 1; break;
    case 'd': vt->cy = n - 1; break;
    case 'H': case 'f': vt->cy = P(vt, 0, 1) - 1; vt->cx = P(vt, 1, 1) - 1; break;
    case 'J': {
        int m = vt->np ? vt->p[0] : 0;
        if (m == 0) { vt_clear_cells(vt, &vt->s[vt->cy][vt->cx], VC - vt->cx); for (int r = vt->cy + 1; r < VR; r++) vt_clear_cells(vt, vt->s[r], VC); }
        else if (m == 1) { for (int r = 0; r < vt->cy; r++) vt_clear_cells(vt, vt->s[r], VC); vt_clear_cells(vt, vt->s[vt->cy], vt->cx + 1); }
        else for (int r = 0; r < VR; r++) vt_clear_cells(vt, vt->s[r], VC);
        break; }
    case 'K': {
        int m = vt->np ? vt->p[0] : 0;
        if (m == 0) vt_clear_cells(vt, &vt->s[vt->cy][vt->cx], VC - vt->cx);
        else if (m == 1) vt_clear_cells(vt, vt->s[vt->cy], vt->cx + 1);
        else vt_clear_cells(vt, vt->s[vt->cy], VC);
        break; }
    case 'X': { int k = n; if (vt->cx + k > VC) k = VC - vt->cx; vt_clear_cells(vt, &vt->s[vt->cy][vt->cx], k); break; }
    case 'P': { int k = n; if (vt->cx + k > VC) k = VC - vt->cx;
        memmove(&vt->s[vt->cy][vt->cx], &vt->s[vt->cy][vt->cx + k], sizeof(Cell) * (VC - vt->cx - k));
        vt_clear_cells(vt, &vt->s[vt->cy][VC - k], k); break; }
    case '@': { int k = n; if (vt->cx + k > VC) k = VC - vt->cx;
        memmove(&vt->s[vt->cy][vt->cx + k], &vt->s[vt->cy][vt->cx], sizeof(Cell) * (VC - vt->cx - k));
        vt_clear_cells(vt, &vt->s[vt->cy][vt->cx], k); break; }
    case 'L': if (vt->cy >= vt->top && vt->cy <= vt->bot) vt_scroll_down(vt, vt->cy, vt->bot, n); break;
    case 'M': if (vt->cy >= vt->top && vt->cy <= vt->bot) vt_scroll_up(vt, vt->cy, vt->bot, n); break;
    case 'S': vt_scroll_up(vt, vt->top, vt->bot, n); break;
    case 'T': vt_scroll_down(vt, vt->top, vt->bot, n); break;
    case 'r': vt->top = P(vt, 0, 1) - 1; vt->bot = P(vt, 1, VR) - 1;
        if (vt->top < 0) vt->top = 0;
        if (vt->bot >= VR) vt->bot = VR - 1;
        if (vt->top >= vt->bot) { vt->top = 0; vt->bot = VR - 1; }
        vt->cx = 0; vt->cy = 0; break;
    case 'm': vt_sgr(vt); break;
    case 's': vt->scx = vt->cx; vt->scy = vt->cy; break;
    case 'u': vt->cx = vt->scx; vt->cy = vt->scy; break;
    case 'h': case 'l':
        if (vt->priv && vt->np && vt->p[0] == 25) vt->cursor_on = f == 'h';
        if (vt->priv && vt->np && (vt->p[0] == 1049 || vt->p[0] == 47 || vt->p[0] == 1047)) {
            for (int r = 0; r < VR; r++) vt_clear_cells(vt, vt->s[r], VC);
            vt->cx = vt->cy = 0;
        }
        break;
    case 'n':
        if (vt->np && vt->p[0] == 6) { char b[24]; snprintf(b, sizeof b, "\x1b[%d;%dR", vt->cy + 1, vt->cx + 1); if (vt->reply) vt->reply(vt->ctx, b); }
        else if (vt->np && vt->p[0] == 5) if (vt->reply) vt->reply(vt->ctx, "\x1b[0n");
        break;
    case 'c': if (!vt->priv) if (vt->reply) vt->reply(vt->ctx, "\x1b[?1;2c"); break;
    }
    vt->wrap = 0;
    clampc(vt);
}

void vt_put(VT *vt, int ch) {
    if (!vt) return;
    unsigned char c = (unsigned char)ch;
    if (vt->st == 1) {   /* after ESC */
        vt->st = 0;
        switch (c) {
        case '[': vt->st = 2; vt->np = 0; vt->priv = 0; memset(vt->p, 0, sizeof vt->p); return;
        case ']': vt->st = 4; return;   /* OSC: skip to BEL / ST */
        case '(': case ')': case '#': vt->st = 5; return;
        case '7': vt->scx = vt->cx; vt->scy = vt->cy; return;
        case '8': vt->cx = vt->scx; vt->cy = vt->scy; return;
        case 'D': vt_lf(vt); return;
        case 'E': vt->cx = 0; vt_lf(vt); return;
        case 'M': if (vt->cy == vt->top) vt_scroll_down(vt, vt->top, vt->bot, 1); else if (vt->cy) vt->cy--; return;
        case 'c': vt_reset(vt); return;
        default: return;
        }
    }
    if (vt->st == 2) {   /* CSI parameters */
        if (c == '?' || c == '>' || c == '=') { vt->priv = 1; return; }
        if (c >= '0' && c <= '9') { if (!vt->np) vt->np = 1; vt->p[vt->np - 1] = vt->p[vt->np - 1] * 10 + (c - '0'); return; }
        if (c == ';') { if (!vt->np) vt->np = 1; if (vt->np < 16) vt->np++; return; }
        if (c >= 0x40 && c <= 0x7e) { vt->st = 0; vt_csi(vt, (char)c); return; }
        if (c < 0x20) { /* control inside CSI: execute */ } else return;
    }
    if (vt->st == 4) { if (c == 7) vt->st = 0; else if (c == 0x1b) vt->st = 6; return; }
    if (vt->st == 6) { vt->st = 0; return; }
    if (vt->st == 5) { vt->st = 0; return; }
    if (vt->utf8_skip > 0) { if ((c & 0xC0) == 0x80) { vt->utf8_skip--; return; } vt->utf8_skip = 0; }
    switch (c) {
    case 0x1b: vt->st = 1; return;
    case '\r': vt->cx = 0; vt->wrap = 0; return;
    case '\n': case 0x0b: case 0x0c: vt_lf(vt); vt->wrap = 0; return;
    case '\b': if (vt->cx > 0) vt->cx--; vt->wrap = 0; return;
    case '\t': vt->cx = (vt->cx / 8 + 1) * 8; if (vt->cx >= VC) vt->cx = VC - 1; return;
    case 7: sound_play(SND_CLICK); return;
    }
    if (c < 0x20 || c == 0x7f) return;
    if (c >= 0x80) {   /* UTF-8: show one placeholder per character */
        if ((c & 0xE0) == 0xC0) vt->utf8_skip = 1;
        else if ((c & 0xF0) == 0xE0) vt->utf8_skip = 2;
        else if ((c & 0xF8) == 0xF0) vt->utf8_skip = 3;
        else return;
        c = (vt->utf8_skip == 2) ? '-' : '?';   /* box-drawing (3-byte) chars look best as dashes */
    }
    if (vt->wrap) { vt->cx = 0; vt_lf(vt); vt->wrap = 0; }
    Cell *cl = &vt->s[vt->cy][vt->cx];
    cl->c = (char)c; cl->fg = vt->fg; cl->bg = vt->bg; cl->fl = vt->fl;
    if (vt->cx == VC - 1) vt->wrap = 1; else vt->cx++;
    vt->view = 0;
}


VT *vt_create(void (*reply)(void *ctx, const char *s), void *ctx) {
    VT *vt = calloc(1, sizeof(VT));
    if (!vt) return NULL;
    vt->reply = reply; vt->ctx = ctx;
    vt_reset(vt);
    return vt;
}
void vt_destroy(VT *vt) { free(vt); }
void vt_write(VT *vt, const char *s, int n) { for (int i = 0; i < n; i++) vt_put(vt, (unsigned char)s[i]); }
int vt_cursor_x(VT *vt) { return vt->cx; }
int vt_cursor_y(VT *vt) { return vt->cy; }

static const uint32_t pal[16] = {
    0x101014, 0xCC3333, 0x4E9A06, 0xC4A000, 0x3465A4, 0x75507B, 0x06989A, 0xD3D7CF,
    0x555753, 0xEF2929, 0x8AE234, 0xFCE94F, 0x729FCF, 0xAD7FA8, 0x34E2E2, 0xFFFFFF,
};
uint32_t vt_bg_color(void) { return pal[0]; }

void vt_scroll_view(VT *vt, int delta) {
    vt->view += delta;
    if (vt->view < 0) vt->view = 0;
    if (vt->view > vt->sbn) vt->view = vt->sbn;
}
int vt_viewing_history(VT *vt) { return vt->view; }

void vt_draw(VT *vt, int x, int y, int cursor) {
    int adv = font_mono.g['m' - 32].adv, lh = 13;
    int ox = x, oy = y;
    for (int r = 0; r < VR; r++) {
        const Cell *row;
        int srow = r - vt->view;
        if (srow >= 0) row = vt->s[srow];
        else {
            int k = vt->sbn + srow;
            if (k < 0) continue;
            row = vt->sb[(vt->sbhead - vt->sbn + k + SBL) % SBL];
        }
        for (int c = 0; c < VC; c++) {
            const Cell *cl = &row[c];
            int bgc = cl->fl & 2 ? cl->fg : cl->bg;
            if (bgc) fill(ox + c * adv, oy + r * lh, adv, lh, pal[bgc & 15]);
        }
        int c = 0;
        while (c < VC) {
            const Cell *cl = &row[c];
            int fgc = cl->fl & 2 ? cl->bg : cl->fg;
            if (cl->fl & 1 && fgc < 8) fgc += 8;
            int e = c;
            char buf[VC + 1];
            while (e < VC) {
                const Cell *k = &row[e];
                int f2 = k->fl & 2 ? k->bg : k->fg;
                if (k->fl & 1 && f2 < 8) f2 += 8;
                if (f2 != fgc) break;
                buf[e - c] = k->c ? k->c : ' ';
                e++;
            }
            int n = e - c;
            while (n > 0 && buf[n - 1] == ' ') n--;
            if (n) text_n(&font_mono, ox + c * adv, oy + r * lh, buf, n, pal[fgc & 15]);
            for (int u = c; u < e; u++) if (row[u].fl & 4) hline(ox + u * adv, oy + r * lh + 11, adv, pal[fgc & 15]);
            c = e;
        }
    }
    if (cursor && vt->view == 0 && vt->cursor_on)
        fill_a(ox + vt->cx * adv, oy + vt->cy * lh, adv, lh, 0xE0E0E0, 200);
}
