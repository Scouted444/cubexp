/* Blocks: falling-block puzzle. D-Pad: move/rotate/drop, B (right click) = hard drop */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BW 10
#define BH 20
#define BC 16
static const uint16_t shapes[7][4] = {   /* 4x4 bitmasks for each rotation */
    { 0x0F00, 0x2222, 0x00F0, 0x4444 },  /* I */
    { 0x8E00, 0x6440, 0x0E20, 0x44C0 },  /* J */
    { 0x2E00, 0x4460, 0x0E80, 0xC440 },  /* L */
    { 0x6600, 0x6600, 0x6600, 0x6600 },  /* O */
    { 0x6C00, 0x4620, 0x06C0, 0x8C40 },  /* S */
    { 0x4E00, 0x4640, 0x0E40, 0x4C40 },  /* T */
    { 0xC600, 0x2640, 0x0C60, 0x4C80 },  /* Z */
};
static const uint32_t pcol[7] = { 0x30C8F0, 0x3A6AE8, 0xF09A30, 0xF0D030, 0x50D060, 0xA060E8, 0xE84850 };
static int best_score;

typedef struct {
    uint8_t g[BH][BW];
    int piece, rot, px, py, next, score, lines, level, over, paused;
    uint64_t fall_t, rep_t;
    uint32_t held;
    int flash_rows, flash[4];
    uint64_t flash_t;
} Blk;

static int cell(int p, int r, int x, int y) { return (shapes[p][r] >> (15 - (y * 4 + x))) & 1; }
static int fits(Blk *d, int p, int r, int px, int py) {
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
        if (!cell(p, r, x, y)) continue;
        int gx = px + x, gy = py + y;
        if (gx < 0 || gx >= BW || gy >= BH) return 0;
        if (gy >= 0 && d->g[gy][gx]) return 0;
    }
    return 1;
}
static void spawn(Blk *d) {
    d->piece = d->next; d->next = rand() % 7; d->rot = 0; d->px = 3; d->py = -1;
    if (!fits(d, d->piece, d->rot, d->px, d->py)) {
        d->over = 1; sound_play(SND_ERROR);
        if (d->score > best_score) { best_score = d->score; notify("Blocks", "New high score!"); }
    }
}
static void reset(Blk *d) {
    memset(d->g, 0, sizeof d->g);
    d->score = d->lines = 0; d->level = 1; d->over = 0; d->paused = 0;
    srand((unsigned)now_ms);
    d->next = rand() % 7;
    spawn(d);
    d->fall_t = now_ms;
}
static void lock_piece(Blk *d) {
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++)
        if (cell(d->piece, d->rot, x, y) && d->py + y >= 0) d->g[d->py + y][d->px + x] = (uint8_t)(d->piece + 1);
    int cleared = 0;
    for (int y = BH - 1; y >= 0; y--) {
        int full = 1;
        for (int x = 0; x < BW; x++) if (!d->g[y][x]) full = 0;
        if (!full) continue;
        memmove(d->g[1], d->g[0], sizeof d->g[0] * y);
        memset(d->g[0], 0, sizeof d->g[0]);
        cleared++; y++;
    }
    static const int pts[5] = { 0, 100, 300, 500, 800 };
    if (cleared) { d->score += pts[cleared] * d->level; d->lines += cleared; d->level = 1 + d->lines / 10; sound_play(SND_EAT); d->flash_t = now_ms; }
    else sound_play(SND_CLICK);
    spawn(d);
}
static void drop_hard(Blk *d) {
    while (fits(d, d->piece, d->rot, d->px, d->py + 1)) { d->py++; d->score += 2; }
    lock_piece(d);
}

static void block(int x, int y, uint32_t c, int ghost) {
    if (ghost) { rect(x + 1, y + 1, BC - 2, BC - 2, mix(c, 0x101020, 120)); return; }
    fill(x + 1, y + 1, BC - 2, BC - 2, c);
    hline(x + 1, y + 1, BC - 2, mix(c, 0xFFFFFF, 140)); vline(x + 1, y + 1, BC - 2, mix(c, 0xFFFFFF, 100));
    hline(x + 1, y + BC - 2, BC - 2, shade(c, 60)); vline(x + BC - 2, y + 1, BC - 2, shade(c, 70));
}

static void bk_draw(Window *w, int x, int y, int cw, int ch) {
    Blk *d = w->data;
    (void)ch;
    grad_v(x, y, cw, ch, 0x1C1C34, 0x0C0C18);
    int gx = x + 10, gy = y + 10;
    fill(gx - 2, gy - 2, BW * BC + 4, BH * BC + 4, 0x3A3A60);
    fill(gx, gy, BW * BC, BH * BC, 0x0A0A14);
    for (int i = 1; i < BW; i++) vline(gx + i * BC, gy, BH * BC, 0x14142A);
    for (int j = 1; j < BH; j++) hline(gx, gy + j * BC, BW * BC, 0x14142A);
    for (int j = 0; j < BH; j++) for (int i = 0; i < BW; i++) if (d->g[j][i]) block(gx + i * BC, gy + j * BC, pcol[d->g[j][i] - 1], 0);
    if (!d->over) {
        int gyy = d->py;
        while (fits(d, d->piece, d->rot, d->px, gyy + 1)) gyy++;
        for (int yy = 0; yy < 4; yy++) for (int xx = 0; xx < 4; xx++) {
            if (!cell(d->piece, d->rot, xx, yy)) continue;
            if (gyy + yy >= 0) block(gx + (d->px + xx) * BC, gy + (gyy + yy) * BC, pcol[d->piece], 1);
            if (d->py + yy >= 0) block(gx + (d->px + xx) * BC, gy + (d->py + yy) * BC, pcol[d->piece], 0);
        }
    }
    if (now_ms - d->flash_t < 150) fill_a(gx, gy, BW * BC, BH * BC, 0xFFFFFF, 60);
    /* side panel */
    int sx = gx + BW * BC + 14;
    text(&font_bold, sx, y + 12, "NEXT", 0x9090C0);
    fill(sx, y + 30, 4 * BC + 8, 3 * BC + 4, 0x0A0A14);
    for (int yy = 0; yy < 4; yy++) for (int xx = 0; xx < 4; xx++) if (cell(d->next, 0, xx, yy)) block(sx + 4 + xx * BC, y + 34 + yy * BC, pcol[d->next], 0);
    char b[32];
    const char *lab[] = { "SCORE", "LINES", "LEVEL", "BEST" };
    int val[] = { d->score, d->lines, d->level, best_score > d->score ? best_score : d->score };
    for (int i = 0; i < 4; i++) {
        text(&font_bold, sx, y + 100 + i * 40, lab[i], 0x9090C0);
        snprintf(b, sizeof b, "%d", val[i]);
        text(&font_big, sx, y + 116 + i * 40, b, 0xFFFFFF);
    }
    text(&font_small, sx, y + 270, "D-Pad: move", 0x7070A0);
    text(&font_small, sx, y + 283, "Up: rotate", 0x7070A0);
    text(&font_small, sx, y + 296, "B: drop", 0x7070A0);
    int hot = w == focus && inside(mouse_x, mouse_y, sx, y + 312, 76, 22);
    round_fill(sx, y + 312, 76, 22, 4, hot ? 0x5A5A90 : 0x3E3E68);
    text_center(&font_ui, sx, y + 316, 76, "New Game", 0xFFFFFF);
    const char *m = d->over ? "GAME OVER" : d->paused ? "PAUSED" : NULL;
    if (m) {
        fill_a(gx, gy + BH * BC / 2 - 24, BW * BC, 48, 0, 190);
        text_center(&font_big, gx, gy + BH * BC / 2 - 16, BW * BC, m, 0xFFFFFF);
        text_center(&font_small, gx, gy + BH * BC / 2 + 8, BW * BC, d->over ? "click New Game" : "click to resume", 0xB0B0D0);
    }
}
static void try_move(Blk *d, int dx, int dy) {
    if (fits(d, d->piece, d->rot, d->px + dx, d->py + dy)) { d->px += dx; d->py += dy; }
    else if (dy) lock_piece(d);
}
static void rotate(Blk *d) {
    int r = (d->rot + 1) % 4;
    static const int kick[] = { 0, -1, 1, -2, 2 };
    for (int k = 0; k < 5; k++) if (fits(d, d->piece, r, d->px + kick[k], d->py)) { d->rot = r; d->px += kick[k]; return; }
}
static void bk_pad(Window *w, uint32_t down, uint32_t held) {
    Blk *d = w->data;
    d->held = held;
    if (d->over || d->paused) return;
    if (down & B_UP) rotate(d);
    if (down & B_LEFT) { try_move(d, -1, 0); d->rep_t = now_ms + 170; }
    if (down & B_RIGHT) { try_move(d, 1, 0); d->rep_t = now_ms + 170; }
    if (down & B_DOWN) { try_move(d, 0, 1); d->score++; d->rep_t = now_ms + 120; }
    (void)w;
}
static void bk_mouse(Window *w, int ev, int mx, int my) {
    Blk *d = w->data;
    int sx = 10 + BW * BC + 14;
    if (ev == EV_DOWN && inside(mx, my, sx, 312, 76, 22)) { reset(d); return; }
    if (ev == EV_DOWN && d->paused) d->paused = 0;
    if (ev == EV_RDOWN && !d->over && !d->paused) drop_hard(d);
}
static void bk_tick(Window *w) {
    Blk *d = w->data;
    if (w != focus && !d->over) d->paused = 1;
    if (d->over || d->paused) { d->fall_t = now_ms; return; }
    if (now_ms >= d->rep_t) {
        if (d->held & B_LEFT) { try_move(d, -1, 0); d->rep_t = now_ms + 50; }
        else if (d->held & B_RIGHT) { try_move(d, 1, 0); d->rep_t = now_ms + 50; }
        else if (d->held & B_DOWN) { try_move(d, 0, 1); d->score++; d->rep_t = now_ms + 40; }
    }
    int speed = 800 - (d->level - 1) * 70;
    if (speed < 90) speed = 90;
    if (now_ms - d->fall_t >= (uint64_t)speed) { d->fall_t = now_ms; try_move(d, 0, 1); }
}
void open_blocks(void) {
    Window *w = win_new("Blocks", IC_BLOCKS, 10 + BW * BC + 14 + 96, BH * BC + 20);
    if (!w) return;
    Blk *d = calloc(1, sizeof(Blk));
    w->app = APP_BLOCKS; w->no_max = 1; w->wants_pad = 1; w->data = d;
    w->draw = bk_draw; w->pad = bk_pad; w->mouse = bk_mouse; w->tick = bk_tick;
    reset(d);
    w->y = 4;
}
