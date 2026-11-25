/* Snake and Pong */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ================= Snake ================= */
#define SGW 26
#define SGH 18
#define SC 12
typedef struct {
    int x[SGW * SGH], y[SGW * SGH], len, dx, dy, ndx, ndy, ax, ay, score, best, dead, started, paused;
    uint64_t step_t, dead_t;
} Snake;
static int snake_best;

static void sn_apple(Snake *d) {
    for (;;) {
        int ax = rand() % SGW, ay = rand() % SGH, ok = 1;
        for (int i = 0; i < d->len; i++) if (d->x[i] == ax && d->y[i] == ay) { ok = 0; break; }
        if (ok) { d->ax = ax; d->ay = ay; return; }
    }
}
static void sn_reset(Snake *d) {
    d->len = 4;
    for (int i = 0; i < 4; i++) { d->x[i] = 8 - i; d->y[i] = SGH / 2; }
    d->dx = d->ndx = 1; d->dy = d->ndy = 0;
    d->score = 0; d->dead = 0; d->started = 0; d->paused = 0;
    srand((unsigned)now_ms);
    sn_apple(d);
}
static void sn_draw(Window *w, int x, int y, int cw, int ch) {
    Snake *d = w->data;
    (void)ch;
    fill(x, y, cw, 24, 0x1A2A1A);
    char b[64];
    snprintf(b, sizeof b, "Score: %d", d->score);
    text(&font_bold, x + 8, y + 6, b, 0x80FF80);
    snprintf(b, sizeof b, "Best: %d", snake_best);
    text(&font_bold, x + cw - 8 - text_w(&font_bold, b), y + 6, b, 0xFFE080);
    int gx = x, gy = y + 24;
    fill(gx, gy, SGW * SC, SGH * SC, 0x203820);
    for (int i = 0; i < SGW; i++) for (int j = 0; j < SGH; j++) if ((i + j) & 1) fill(gx + i * SC, gy + j * SC, SC, SC, 0x244024);
    fill_circle(gx + d->ax * SC + 6, gy + d->ay * SC + 6, 5, 0xE02020);
    fill(gx + d->ax * SC + 6, gy + d->ay * SC, 1, 3, 0x6A4A20);
    pset(gx + d->ax * SC + 4, gy + d->ay * SC + 4, 0xFFA0A0);
    for (int i = d->len - 1; i >= 0; i--) {
        uint32_t c = i == 0 ? 0x90FF60 : mix(0x60E040, 0x208020, i * 200 / d->len);
        round_fill(gx + d->x[i] * SC + 1, gy + d->y[i] * SC + 1, SC - 2, SC - 2, 3, c);
    }
    int hx = gx + d->x[0] * SC, hy = gy + d->y[0] * SC;
    fill(hx + 3 + d->dx * 2, hy + 3 + d->dy * 2, 2, 2, 0); fill(hx + 7 + d->dx * 2 - (d->dy ? 4 : 0), hy + 7 + d->dy * 2 - (d->dx ? 4 : 0), 2, 2, 0);
    const char *msg = NULL;
    if (d->dead) msg = "Game over! Press the D-Pad to play again";
    else if (!d->started) msg = "Press the D-Pad to start";
    else if (d->paused) msg = "Paused - click to resume";
    if (msg) {
        int tw = text_w(&font_bold, msg);
        fill_a(gx + (SGW * SC - tw) / 2 - 10, gy + 90, tw + 20, 28, 0, 160);
        text(&font_bold, gx + (SGW * SC - tw) / 2, gy + 97, msg, 0xFFFFFF);
    }
}
static void sn_pad(Window *w, uint32_t down, uint32_t held) {
    Snake *d = w->data;
    (void)held;
    int dx = 0, dy = 0;
    if (down & B_UP) dy = -1;
    else if (down & B_DOWN) dy = 1;
    else if (down & B_LEFT) dx = -1;
    else if (down & B_RIGHT) dx = 1;
    if (!dx && !dy) return;
    if (d->dead) { if (now_ms - d->dead_t > 700) sn_reset(d); return; }
    d->started = 1; d->paused = 0;
    if (dx == -d->dx && dy == -d->dy) return;
    d->ndx = dx; d->ndy = dy;
}
static void sn_tick(Window *w) {
    Snake *d = w->data;
    if (w != focus && d->started && !d->dead) d->paused = 1;
    if (!d->started || d->dead || d->paused) return;
    int speed = 140 - d->score * 3;
    if (speed < 60) speed = 60;
    if (now_ms - d->step_t < (uint64_t)speed) return;
    d->step_t = now_ms;
    d->dx = d->ndx; d->dy = d->ndy;
    int nx = d->x[0] + d->dx, ny = d->y[0] + d->dy;
    int hit = nx < 0 || ny < 0 || nx >= SGW || ny >= SGH;
    for (int i = 0; i < d->len - 1 && !hit; i++) if (d->x[i] == nx && d->y[i] == ny) hit = 1;
    if (hit) {
        d->dead = 1; d->dead_t = now_ms; sound_play(SND_ERROR);
        if (d->score > snake_best) { snake_best = d->score; notify("Snake", "New high score!"); }
        return;
    }
    int grow = nx == d->ax && ny == d->ay;
    if (grow && d->len < SGW * SGH) d->len++;
    for (int i = d->len - 1; i > 0; i--) { d->x[i] = d->x[i - 1]; d->y[i] = d->y[i - 1]; }
    d->x[0] = nx; d->y[0] = ny;
    if (grow) { d->score++; sound_play(SND_EAT); sn_apple(d); }
}
static void sn_mouse(Window *w, int ev, int mx, int my) {
    (void)mx; (void)my;
    Snake *d = w->data;
    if (ev == EV_DOWN && d->paused) d->paused = 0;
}
void open_snake(void) {
    Window *w = win_new("Snake", IC_SNAKE, SGW * SC, SGH * SC + 24);
    if (!w) return;
    Snake *d = calloc(1, sizeof(Snake));
    w->app = APP_SNAKE; w->no_max = 1; w->wants_pad = 1; w->data = d;
    w->draw = sn_draw; w->pad = sn_pad; w->tick = sn_tick; w->mouse = sn_mouse;
    sn_reset(d);
}

/* ================= Pong ================= */
#define PW 340
#define PH 220
typedef struct { float by, cy, bx, byy, vx, vy; int ps, cs, serve; uint32_t held; uint64_t serve_t; } Pong;

static void pg_serve(Pong *d, int dir) {
    d->bx = PW / 2; d->byy = PH / 2;
    d->vx = 3.2f * dir; d->vy = (float)((rand() % 300) - 150) / 100.f;
    d->serve_t = now_ms + 900;
}
static void pg_draw(Window *w, int x, int y, int cw, int ch) {
    Pong *d = w->data;
    (void)cw; (void)ch;
    fill(x, y, PW, PH, 0x101820);
    for (int j = 0; j < PH; j += 12) fill(x + PW / 2 - 1, y + j, 2, 6, 0x405060);
    char b[16];
    snprintf(b, sizeof b, "%d", d->ps);
    text(&font_huge, x + PW / 2 - 50 - text_w(&font_huge, b) / 2, y + 6, b, 0x5070A0);
    snprintf(b, sizeof b, "%d", d->cs);
    text(&font_huge, x + PW / 2 + 50 - text_w(&font_huge, b) / 2, y + 6, b, 0x5070A0);
    fill(x + 10, y + (int)d->by - 20, 6, 40, 0x60C0FF);
    fill(x + PW - 16, y + (int)d->cy - 20, 6, 40, 0xFF8060);
    if (now_ms >= d->serve_t || d->ps < 7) fill_circle(x + (int)d->bx, y + (int)d->byy, 4, 0xFFFFFF);
    if (d->ps >= 7 || d->cs >= 7) {
        const char *m = d->ps >= 7 ? "You win! D-Pad to play again" : "CPU wins! D-Pad to play again";
        fill_a(x + 50, y + 92, PW - 100, 30, 0, 170);
        text_center(&font_bold, x, y + 100, PW, m, 0xFFFFFF);
    } else if (!d->serve) text_center(&font_bold, x, y + 150, PW, "D-Pad Up/Down moves your bat", 0xA0B0C0);
}
static void pg_pad(Window *w, uint32_t down, uint32_t held) {
    Pong *d = w->data;
    d->held = held;
    if ((down & (B_UP | B_DOWN)) && (d->ps >= 7 || d->cs >= 7)) { d->ps = d->cs = 0; pg_serve(d, 1); }
    if (down & (B_UP | B_DOWN)) d->serve = 1;
}
static void pg_tick(Window *w) {
    Pong *d = w->data;
    if (w != focus) d->held = 0;
    if (d->held & B_UP) d->by -= 4.5f;
    if (d->held & B_DOWN) d->by += 4.5f;
    if (d->by < 20) d->by = 20;
    if (d->by > PH - 20) d->by = PH - 20;
    if (!d->serve || d->ps >= 7 || d->cs >= 7 || now_ms < d->serve_t || w != focus) return;
    /* CPU: follows the ball with limited speed */
    float target = d->vx > 0 ? d->byy : PH / 2.f;
    if (d->cy < target - 4) d->cy += 3.1f;
    else if (d->cy > target + 4) d->cy -= 3.1f;
    d->bx += d->vx; d->byy += d->vy;
    if (d->byy < 4) { d->byy = 4; d->vy = -d->vy; }
    if (d->byy > PH - 4) { d->byy = PH - 4; d->vy = -d->vy; }
    if (d->vx < 0 && d->bx < 20 && d->bx > 10 && d->byy > d->by - 24 && d->byy < d->by + 24) {
        d->vx = -d->vx * 1.05f; d->vy += (d->byy - d->by) / 8.f; sound_play(SND_CLICK);
    }
    if (d->vx > 0 && d->bx > PW - 20 && d->bx < PW - 10 && d->byy > d->cy - 24 && d->byy < d->cy + 24) {
        d->vx = -d->vx * 1.05f; d->vy += (d->byy - d->cy) / 8.f; sound_play(SND_CLICK);
    }
    if (d->vx > 7) d->vx = 7;
    if (d->vx < -7) d->vx = -7;
    if (d->bx < 0) { d->cs++; sound_play(SND_ERROR); pg_serve(d, -1); }
    if (d->bx > PW) { d->ps++; sound_play(SND_DING); pg_serve(d, 1); }
}
void open_pong(void) {
    Window *w = win_new("Pong", IC_PAD, PW, PH);
    if (!w) return;
    Pong *d = calloc(1, sizeof(Pong));
    d->by = d->cy = PH / 2;
    pg_serve(d, 1);
    w->app = APP_PONG; w->no_max = 1; w->wants_pad = 1; w->data = d;
    w->draw = pg_draw; w->pad = pg_pad; w->tick = pg_tick;
}
