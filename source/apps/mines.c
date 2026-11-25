/* Minesweeper */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAXW 16
#define MAXH 16
#define CS 16
typedef struct {
    int w, h, mines, level;
    unsigned char mine[MAXH][MAXW], open[MAXH][MAXW], flag[MAXH][MAXW];
    int started, over, won, flags, opened;
    uint64_t t0, t1;
    int press_face;
} Mines;

static const char *mn_menu[] = { "Game", "Help" };
#define TOP 20
#define HDR 40

static void seg_digit(int x, int y, int d) {
    static const unsigned char segs[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
    unsigned char s = d < 0 ? 0x40 : segs[d];
    uint32_t on = 0xFF0000, off = 0x400000;
    fill(x + 2, y, 7, 2, s & 1 ? on : off);
    fill(x + 9, y + 2, 2, 8, s & 2 ? on : off);
    fill(x + 9, y + 12, 2, 8, s & 4 ? on : off);
    fill(x + 2, y + 20, 7, 2, s & 8 ? on : off);
    fill(x, y + 12, 2, 8, s & 16 ? on : off);
    fill(x, y + 2, 2, 8, s & 32 ? on : off);
    fill(x + 2, y + 10, 7, 2, s & 64 ? on : off);
}
static void counter(int x, int y, int v) {
    fill(x, y, 41, 25, 0);
    rect(x - 1, y - 1, 43, 27, 0x808080);
    if (v > 999) v = 999;
    int neg = v < 0;
    if (neg) v = -v;
    seg_digit(x + 2, y + 2, neg ? -1 : (v / 100) % 10);
    seg_digit(x + 15, y + 2, (v / 10) % 10);
    seg_digit(x + 28, y + 2, v % 10);
}

static int face_x(Mines *d, int cw) { (void)d; return cw / 2 - 13; }

static void mn_draw(Window *w, int x, int y, int cw, int ch) {
    Mines *d = w->data;
    (void)ch;
    ui_menubar(x, y, cw, mn_menu, 2);
    fill(x, y + TOP, cw, ch - TOP, 0xC0C0C0);
    bevel(x + 6, y + TOP + 6, cw - 12, HDR - 6, 1);
    counter(x + 14, y + TOP + 12, d->mines - d->flags);
    int secs = d->started ? (int)(((d->over ? d->t1 : now_ms) - d->t0) / 1000) : 0;
    counter(x + cw - 56, y + TOP + 12, secs);
    /* smiley */
    int fx = x + face_x(d, cw), fy = y + TOP + 11;
    fill(fx, fy, 26, 26, 0xC0C0C0);
    bevel(fx, fy, 26, 26, d->press_face);
    fill_circle(fx + 13, fy + 13, 9, 0x000000);
    fill_circle(fx + 13, fy + 13, 8, 0xFFFF00);
    if (d->over && !d->won) {
        line(fx + 8, fy + 8, fx + 11, fy + 11, 0); line(fx + 11, fy + 8, fx + 8, fy + 11, 0);
        line(fx + 15, fy + 8, fx + 18, fy + 11, 0); line(fx + 18, fy + 8, fx + 15, fy + 11, 0);
        hline(fx + 9, fy + 17, 9, 0); pset(fx + 8, fy + 18, 0); pset(fx + 18, fy + 18, 0);
    } else if (d->won) {
        fill(fx + 6, fy + 9, 15, 3, 0); fill(fx + 8, fy + 9, 4, 4, 0); fill(fx + 15, fy + 9, 4, 4, 0);
        for (int i = 0; i < 7; i++) pset(fx + 10 + i, fy + 17 + (i == 0 || i == 6 ? -1 : 0), 0);
    } else {
        fill(fx + 9, fy + 9, 2, 2, 0); fill(fx + 16, fy + 9, 2, 2, 0);
        if (mouse_btn && w == focus) circle(fx + 13, fy + 17, 2, 0);
        else for (int i = 0; i < 7; i++) pset(fx + 10 + i, fy + 17 + (i == 0 || i == 6 ? -1 : 0), 0);
    }
    /* grid */
    int gx = x + 10, gy = y + TOP + HDR + 8;
    bevel(gx - 3, gy - 3, d->w * CS + 6, d->h * CS + 6, 1);
    static const uint32_t ncol[9] = { 0, 0x0000FF, 0x008000, 0xFF0000, 0x000080, 0x800000, 0x008080, 0x000000, 0x808080 };
    for (int r = 0; r < d->h; r++)
        for (int c = 0; c < d->w; c++) {
            int cx = gx + c * CS, cy = gy + r * CS;
            if (d->open[r][c] || (d->over && d->mine[r][c])) {
                fill(cx, cy, CS, CS, (d->open[r][c] == 2) ? 0xFF0000 : 0xC0C0C0);
                hline(cx, cy, CS, 0x808080); vline(cx, cy, CS, 0x808080);
                if (d->mine[r][c]) {
                    fill_circle(cx + 8, cy + 8, 4, 0); line(cx + 2, cy + 8, cx + 14, cy + 8, 0); line(cx + 8, cy + 2, cx + 8, cy + 14, 0);
                    fill(cx + 6, cy + 6, 2, 2, 0xFFFFFF);
                } else {
                    int n = 0;
                    for (int dr = -1; dr <= 1; dr++) for (int dc = -1; dc <= 1; dc++) {
                        int rr = r + dr, cc = c + dc;
                        if (rr >= 0 && cc >= 0 && rr < d->h && cc < d->w) n += d->mine[rr][cc];
                    }
                    if (n) { char b[2] = { (char)('0' + n), 0 }; text_center(&font_bold, cx, cy + 2, CS, b, ncol[n]); }
                }
                if (d->over && d->flag[r][c] && !d->mine[r][c]) { line(cx + 2, cy + 2, cx + 13, cy + 13, 0xFF0000); line(cx + 13, cy + 2, cx + 2, cy + 13, 0xFF0000); }
            } else {
                fill(cx, cy, CS, CS, 0xC0C0C0);
                hline(cx, cy, CS - 1, 0xFFFFFF); vline(cx, cy, CS - 1, 0xFFFFFF);
                hline(cx, cy + 1, CS - 2, 0xFFFFFF); vline(cx + 1, cy, CS - 2, 0xFFFFFF);
                hline(cx + 1, cy + CS - 1, CS - 1, 0x808080); vline(cx + CS - 1, cy + 1, CS - 1, 0x808080);
                hline(cx + 2, cy + CS - 2, CS - 3, 0x808080); vline(cx + CS - 2, cy + 2, CS - 3, 0x808080);
                if (d->flag[r][c]) {
                    fill(cx + 4, cy + 3, 6, 5, 0xFF0000); vline(cx + 9, cy + 3, 9, 0); fill(cx + 5, cy + 11, 7, 2, 0);
                }
            }
        }
}

static void reset(Mines *d) {
    memset(d->mine, 0, sizeof d->mine); memset(d->open, 0, sizeof d->open); memset(d->flag, 0, sizeof d->flag);
    d->started = d->over = d->won = d->flags = d->opened = 0;
}
static void place(Mines *d, int sr, int sc) {
    srand((unsigned)now_ms);
    int n = 0;
    while (n < d->mines) {
        int r = rand() % d->h, c = rand() % d->w;
        if (d->mine[r][c] || (abs(r - sr) <= 1 && abs(c - sc) <= 1)) continue;
        d->mine[r][c] = 1; n++;
    }
}
static int count(Mines *d, int r, int c) {
    int n = 0;
    for (int dr = -1; dr <= 1; dr++) for (int dc = -1; dc <= 1; dc++) {
        int rr = r + dr, cc = c + dc;
        if (rr >= 0 && cc >= 0 && rr < d->h && cc < d->w) n += d->mine[rr][cc];
    }
    return n;
}
static void reveal(Mines *d, int r, int c) {
    if (r < 0 || c < 0 || r >= d->h || c >= d->w || d->open[r][c] || d->flag[r][c]) return;
    if (d->mine[r][c]) {
        d->open[r][c] = 2; d->over = 1; d->t1 = now_ms;
        sound_play(SND_EXPLODE);
        return;
    }
    d->open[r][c] = 1; d->opened++;
    if (!count(d, r, c))
        for (int dr = -1; dr <= 1; dr++) for (int dc = -1; dc <= 1; dc++) reveal(d, r + dr, c + dc);
}
static void check_win(Mines *d) {
    if (!d->over && d->opened == d->w * d->h - d->mines) {
        d->over = d->won = 1; d->t1 = now_ms;
        for (int r = 0; r < d->h; r++) for (int c = 0; c < d->w; c++) if (d->mine[r][c] && !d->flag[r][c]) { d->flag[r][c] = 1; d->flags++; }
        char m[64];
        snprintf(m, sizeof m, "You cleared the field in %d seconds!", (int)((d->t1 - d->t0) / 1000));
        notify("Minesweeper", m);
    }
}

static void set_level(Window *w, int lvl) {
    Mines *d = w->data;
    static const int L[3][3] = { { 9, 9, 10 }, { 12, 12, 24 }, { 16, 16, 40 } };
    d->level = lvl; d->w = L[lvl][0]; d->h = L[lvl][1]; d->mines = L[lvl][2];
    reset(d);
    int cw = d->w * CS + 20, ch = TOP + HDR + 8 + d->h * CS + 10;
    if (cw < 190) cw = 190;
    w->w = cw + 2 * BORDER; w->h = ch + TITLE_H + BORDER;
    if (w->y + w->h > SH - TASKBAR_H) w->y = SH - TASKBAR_H - w->h;
    if (w->y < 0) w->y = 0;
}

static int ctx_of(Window *w) { return (int)(w - wins) | (w->gen << 8); }
static void mn_menu_cb(int id, int ctx) {
    Window *w = &wins[ctx & 255];
    if (!w->used || w->gen != (ctx >> 8)) return;
    if (id == 1) reset(w->data);
    else if (id >= 2 && id <= 4) set_level(w, id - 2);
    else if (id == 5) win_close(w);
    else if (id == 6) msgbox("How to play", "Reveal every square that isn't a mine.\nA = reveal, B = flag.\nNumbers show how many mines touch a square.\nClick a number to clear around it once its\nmines are flagged.", IC_MINES);
}

static void mn_mouse(Window *w, int ev, int mx, int my) {
    Mines *d = w->data;
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    if (ev == EV_UP) {
        if (d->press_face && inside(mx, my, face_x(d, cw), TOP + 11, 26, 26)) reset(d);
        d->press_face = 0;
        return;
    }
    if (ev != EV_DOWN && ev != EV_RDOWN) return;
    int m = ui_menubar_hit(mx, my, mn_menu, 2);
    if (m >= 0 && ev == EV_DOWN) {
        if (m == 0) {
            static const MItem it[] = { { "New", 1 }, { NULL, 0 }, { "Beginner", 2 }, { "Intermediate", 3 }, { "Expert (16x16)", 4 }, { NULL, 0 }, { "Exit", 5 } };
            shell_popup(x + 1, y + 19, it, 7, mn_menu_cb, ctx_of(w));
        } else {
            static const MItem it[] = { { "How to Play", 6 } };
            shell_popup(x + 4 + text_w(&font_ui, "Game") + 9, y + 19, it, 1, mn_menu_cb, ctx_of(w));
        }
        return;
    }
    if (inside(mx, my, face_x(d, cw), TOP + 11, 26, 26) && ev == EV_DOWN) { d->press_face = 1; return; }
    int gx = 10, gy = TOP + HDR + 8;
    int c = (mx - gx) / CS, r = (my - gy) / CS;
    if (mx < gx || my < gy || c >= d->w || r >= d->h || d->over) return;
    if (ev == EV_RDOWN) {
        if (d->open[r][c]) return;
        d->flag[r][c] = !d->flag[r][c];
        d->flags += d->flag[r][c] ? 1 : -1;
        return;
    }
    if (!d->started) { place(d, r, c); d->started = 1; d->t0 = now_ms; }
    if (d->open[r][c]) {   /* chord */
        int f = 0;
        for (int dr = -1; dr <= 1; dr++) for (int dc = -1; dc <= 1; dc++) {
            int rr = r + dr, cc = c + dc;
            if (rr >= 0 && cc >= 0 && rr < d->h && cc < d->w) f += d->flag[rr][cc];
        }
        if (f == count(d, r, c))
            for (int dr = -1; dr <= 1; dr++) for (int dc = -1; dc <= 1; dc++) reveal(d, r + dr, c + dc);
    } else reveal(d, r, c);
    check_win(d);
}

void open_mines(void) {
    Window *w = win_new("Minesweeper", IC_MINES, 170, 200);
    if (!w) return;
    w->app = APP_MINES; w->no_max = 1;
    w->data = calloc(1, sizeof(Mines));
    w->draw = mn_draw; w->mouse = mn_mouse;
    set_level(w, 0);
}
