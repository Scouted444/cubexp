/* Solitaire (Klondike, draw one) with drag and drop and the bouncing-card victory */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CWD 46
#define CHT 64
#define GAP 8
#define TOPY 10
#define TABY 92
typedef struct { int c[52], n, down; } Pile;   /* down = number of face-down cards at the bottom */
enum { P_STOCK, P_WASTE, P_F0, P_F1, P_F2, P_F3, P_T0 };  /* tableau P_T0..P_T0+6 */
#define NP 13
typedef struct {
    Pile p[NP];
    int drag[20], ndrag, from, dx, dy, gx, gy;
    int score, won, moves;
    uint64_t t0;
    /* victory animation */
    uint32_t *canvas;
    int cw, ch, anim_card, anim_f;
    float ax, ay, vx, vy;
} Sol;

static int suit(int c) { return c / 13; }
static int rank(int c) { return c % 13; }
static int red(int c) { return suit(c) == 1 || suit(c) == 2; }
static int pile_x(int p) {
    if (p == P_STOCK) return GAP;
    if (p == P_WASTE) return GAP + (CWD + GAP);
    if (p >= P_F0 && p <= P_F3) return GAP + (3 + p - P_F0) * (CWD + GAP);
    return GAP + (p - P_T0) * (CWD + GAP);
}
static int pile_y(int p) { return p >= P_T0 ? TABY : TOPY; }
static int card_y(Pile *pl, int i) {   /* offset of card i inside a tableau pile */
    int y = 0;
    for (int k = 0; k < i; k++) y += k < pl->down ? 5 : 15;
    return y;
}

static void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t c) {
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2), maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2), maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    for (int y = miny; y <= maxy; y++)
        for (int x = minx; x <= maxx; x++) {
            int d1 = (x - x1) * (y0 - y1) - (x0 - x1) * (y - y1), d2 = (x - x2) * (y1 - y2) - (x1 - x2) * (y - y2), d3 = (x - x0) * (y2 - y0) - (x2 - x0) * (y - y0);
            if (!((d1 < 0 || d2 < 0 || d3 < 0) && (d1 > 0 || d2 > 0 || d3 > 0))) pset(x, y, c);
        }
}
static void suit_sym(int s, int cx, int cy, int r, uint32_t c) {   /* r = half size */
    if (s == 1) { fill_circle(cx - r / 2, cy - r / 4, r / 2 + 1, c); fill_circle(cx + r / 2, cy - r / 4, r / 2 + 1, c); tri(cx - r - 1, cy, cx + r + 1, cy, cx, cy + r + 1, c); }
    else if (s == 2) { tri(cx, cy - r - 1, cx - r, cy, cx + r, cy, c); tri(cx - r, cy, cx + r, cy, cx, cy + r + 1, c); }
    else if (s == 0) { fill_circle(cx - r / 2, cy + r / 4, r / 2 + 1, c); fill_circle(cx + r / 2, cy + r / 4, r / 2 + 1, c); tri(cx - r - 1, cy, cx + r + 1, cy, cx, cy - r - 1, c); tri(cx, cy + r / 3, cx - r / 2, cy + r + 1, cx + r / 2, cy + r + 1, c); }
    else { fill_circle(cx, cy - r / 2, r / 2 + 1, c); fill_circle(cx - r / 2 - 1, cy + r / 4, r / 2 + 1, c); fill_circle(cx + r / 2 + 1, cy + r / 4, r / 2 + 1, c); tri(cx, cy, cx - r / 2, cy + r + 1, cx + r / 2, cy + r + 1, c); }
}
static void draw_card(int x, int y, int c, int up) {
    round_fill(x, y, CWD, CHT, 4, 0x404040);
    if (!up) {
        round_fill(x + 1, y + 1, CWD - 2, CHT - 2, 3, 0xFFFFFF);
        round_fill(x + 3, y + 3, CWD - 6, CHT - 6, 2, 0x1A4AA8);
        for (int j = 0; j < CHT - 8; j += 4) for (int i = (j / 4) % 2 * 2; i < CWD - 8; i += 4) pset(x + 4 + i, y + 4 + j, 0x4A7AE0);
        draw_icon(IC_LOGO, x + CWD / 2 - 10, y + CHT / 2 - 10, 20);
        return;
    }
    round_fill(x + 1, y + 1, CWD - 2, CHT - 2, 3, 0xFFFFFF);
    static const char *rk[] = { "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K" };
    uint32_t col = red(c) ? 0xD01818 : 0x101010;
    text(&font_bold, x + 4, y + 3, rk[rank(c)], col);
    suit_sym(suit(c), x + 9, y + 22, 4, col);
    if (rank(c) >= 10) {
        round_fill(x + 15, y + 14, CWD - 20, CHT - 20, 3, red(c) ? 0xFDE6C8 : 0xDCE6F8);
        text_center(&font_big, x + 15, y + 22, CWD - 20, rk[rank(c)], col);
        suit_sym(suit(c), x + 15 + (CWD - 20) / 2, y + CHT - 16, 4, col);
    } else suit_sym(suit(c), x + CWD / 2 + 4, y + CHT / 2 + 4, 9, col);
}
static void empty_slot(int x, int y, const char *lab) {
    round_fill(x, y, CWD, CHT, 4, 0x1E6A2E);
    round_fill(x + 1, y + 1, CWD - 2, CHT - 2, 3, 0x2E8A3E);
    if (lab) text_center(&font_big, x, y + 20, CWD, lab, 0x4EAA5E);
}

static void deal(Sol *d) {
    int deck[52];
    for (int i = 0; i < 52; i++) deck[i] = i;
    srand((unsigned)now_ms ^ 0x5A5A);
    for (int i = 51; i > 0; i--) { int j = rand() % (i + 1), t = deck[i]; deck[i] = deck[j]; deck[j] = t; }
    memset(d->p, 0, sizeof d->p);
    int k = 0;
    for (int t = 0; t < 7; t++) {
        for (int i = 0; i <= t; i++) d->p[P_T0 + t].c[i] = deck[k++];
        d->p[P_T0 + t].n = t + 1; d->p[P_T0 + t].down = t;
    }
    while (k < 52) { Pile *s = &d->p[P_STOCK]; s->c[s->n++] = deck[k++]; }
    d->p[P_STOCK].down = d->p[P_STOCK].n;
    d->score = 0; d->won = 0; d->ndrag = 0; d->moves = 0; d->t0 = now_ms;
    free(d->canvas); d->canvas = NULL;
}

static void sol_draw(Window *w, int x, int y, int cw, int ch) {
    Sol *d = w->data;
    if (d->won && d->canvas && d->cw == cw && d->ch == ch - 20) {
        for (int j = 0; j < d->ch; j++) for (int i = 0; i < d->cw; i++) pset(x + i, y + j, d->canvas[j * d->cw + i]);
    } else {
        grad_v(x, y, cw, ch - 20, 0x2E9A44, 0x1E7A30);
        Pile *st = &d->p[P_STOCK];
        if (st->n) { draw_card(x + pile_x(P_STOCK), y + TOPY, 0, 0); if (st->n > 10) draw_card(x + pile_x(P_STOCK) + 2, y + TOPY - 2, 0, 0); }
        else { empty_slot(x + pile_x(P_STOCK), y + TOPY, NULL); circle(x + pile_x(P_STOCK) + CWD / 2, y + TOPY + CHT / 2, 10, 0x4EAA5E); }
        Pile *wa = &d->p[P_WASTE];
        if (wa->n - (d->from == P_WASTE ? d->ndrag : 0) > 0) draw_card(x + pile_x(P_WASTE), y + TOPY, wa->c[wa->n - 1 - (d->from == P_WASTE ? d->ndrag : 0)], 1);
        for (int f = P_F0; f <= P_F3; f++) {
            Pile *p = &d->p[f];
            int n = p->n - (d->from == f ? d->ndrag : 0);
            if (n > 0) draw_card(x + pile_x(f), y + TOPY, p->c[n - 1], 1);
            else empty_slot(x + pile_x(f), y + TOPY, "A");
        }
        for (int t = 0; t < 7; t++) {
            Pile *p = &d->p[P_T0 + t];
            int n = p->n - (d->from == P_T0 + t ? d->ndrag : 0);
            if (!n) { empty_slot(x + pile_x(P_T0 + t), y + TABY, "K"); continue; }
            for (int i = 0; i < n; i++) draw_card(x + pile_x(P_T0 + t), y + TABY + card_y(p, i), p->c[i], i >= p->down);
        }
        for (int i = 0; i < d->ndrag; i++) {
            shadow_rect(x + d->gx, y + d->gy + i * 15, CWD, CHT, 3, 60);
            draw_card(x + d->gx, y + d->gy + i * 15, d->drag[i], 1);
        }
    }
    fill(x, y + ch - 20, cw, 20, C_FACE);
    hline(x, y + ch - 20, cw, C_SHADOW);
    char b[80];
    int secs = (int)((d->won ? d->t0 : now_ms - d->t0) / 1000);
    snprintf(b, sizeof b, "Score: %d   Time: %d   Moves: %d", d->score, secs, d->moves);
    text(&font_ui, x + cw - text_w(&font_ui, b) - 8, y + ch - 16, b, 0);
    text(&font_small, x + 6, y + ch - 15, d->won ? "You win! Click to deal again." : "Double-click: send home", 0x505050);
}

static int can_stack(int c, Pile *t) {   /* onto tableau */
    if (!t->n) return rank(c) == 12;
    int top = t->c[t->n - 1];
    if (t->n <= t->down) return 0;
    return rank(c) == rank(top) - 1 && red(c) != red(top);
}
static int can_found(int c, Pile *f) {
    if (!f->n) return rank(c) == 0;
    int top = f->c[f->n - 1];
    return suit(c) == suit(top) && rank(c) == rank(top) + 1;
}
static void flip_top(Sol *d, Pile *p) {
    if (p->n && p->down >= p->n) { p->down = p->n - 1; d->score += 5; }
}
static void check_win(Window *w, Sol *d) {
    int n = 0;
    for (int f = P_F0; f <= P_F3; f++) n += d->p[f].n;
    if (n < 52 || d->won) return;
    d->won = 1;
    d->t0 = now_ms - d->t0;
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    d->cw = cw; d->ch = ch - 20;
    d->canvas = malloc(sizeof(uint32_t) * d->cw * d->ch);
    if (d->canvas) {
        GfxState st;
        gfx_push(&st);
        gfx_target(d->canvas, 0, 0, d->cw, d->ch);
        grad_v(0, 0, d->cw, d->ch, 0x2E9A44, 0x1E7A30);
        for (int f = P_F0; f <= P_F3; f++) draw_card(pile_x(f), TOPY, d->p[f].c[12], 1);
        gfx_pop(&st);
    }
    d->anim_card = 51; d->anim_f = 0;
    d->ax = -1;
    sound_play(SND_STARTUP);
    notify("Solitaire", "Congratulations! You won!");
}

/* move run starting at index i of pile src to dst (validated by caller) */
static void move_cards(Sol *d, int src, int i, int dst) {
    Pile *s = &d->p[src], *t = &d->p[dst];
    int n = s->n - i;
    for (int k = 0; k < n; k++) t->c[t->n++] = s->c[i + k];
    s->n = i;
    if (s->down > s->n) s->down = s->n;
    if (src >= P_T0) flip_top(d, s);
    d->moves++;
    if (dst >= P_F0 && dst <= P_F3) d->score += 10;
    else if (src == P_WASTE) d->score += 5;
    else if (src >= P_F0 && src <= P_F3) d->score -= 15;
    if (d->score < 0) d->score = 0;
}

static int auto_home(Sol *d, int src) {
    Pile *s = &d->p[src];
    if (!s->n || (src >= P_T0 && s->n <= s->down)) return 0;
    int c = s->c[s->n - 1];
    for (int f = P_F0; f <= P_F3; f++)
        if (can_found(c, &d->p[f])) { move_cards(d, src, s->n - 1, f); sound_play(SND_CLICK); return 1; }
    return 0;
}

static int hit_pile(Sol *d, int mx, int my, int *idx) {
    for (int p = 0; p < NP; p++) {
        int px = pile_x(p), py = pile_y(p);
        if (mx < px || mx >= px + CWD) continue;
        if (p < P_T0) { if (my >= py && my < py + CHT) { *idx = d->p[p].n - 1; return p; } continue; }
        Pile *pl = &d->p[p];
        if (!pl->n) { if (my >= py && my < py + CHT) { *idx = -1; return p; } continue; }
        for (int i = pl->n - 1; i >= 0; i--) {
            int cy = py + card_y(pl, i);
            if (my >= cy && my < cy + (i == pl->n - 1 ? CHT : (i < pl->down ? 5 : 15))) { *idx = i; return p; }
        }
    }
    return -1;
}

static void sol_mouse(Window *w, int ev, int mx, int my) {
    Sol *d = w->data;
    if (d->won) { if (ev == EV_DOWN) deal(d); return; }
    if (ev == EV_DOWN || ev == EV_DBL) {
        int idx, p = hit_pile(d, mx, my, &idx);
        if (p < 0) return;
        if (ev == EV_DBL) { d->ndrag = 0; d->from = -1; if (p == P_WASTE || p >= P_T0) { auto_home(d, p); check_win(w, d); } return; }
        if (p == P_STOCK) {
            Pile *s = &d->p[P_STOCK], *wa = &d->p[P_WASTE];
            if (s->n) { wa->c[wa->n++] = s->c[--s->n]; s->down = s->n; }
            else { while (wa->n) s->c[s->n++] = wa->c[--wa->n]; s->down = s->n; d->score -= 20; if (d->score < 0) d->score = 0; }
            sound_play(SND_CLICK);
            return;
        }
        if (idx < 0) return;
        Pile *pl = &d->p[p];
        if (p >= P_T0 && idx < pl->down) return;
        if (p < P_T0) idx = pl->n - 1;
        if (idx < 0) return;
        d->ndrag = pl->n - idx;
        for (int k = 0; k < d->ndrag; k++) d->drag[k] = pl->c[idx + k];
        d->from = p;
        int cy = pile_y(p) + (p >= P_T0 ? card_y(pl, idx) : 0);
        d->dx = mx - pile_x(p); d->dy = my - cy;
        d->gx = pile_x(p); d->gy = cy;
        return;
    }
    if (ev == EV_DRAG && d->ndrag) { d->gx = mx - d->dx; d->gy = my - d->dy; return; }
    if (ev == EV_UP && d->ndrag) {
        int cx = d->gx + CWD / 2, cy = d->gy + 10, best = -1;
        for (int p = P_F0; p < NP; p++) {
            if (p == d->from) continue;
            Pile *t = &d->p[p];
            int px = pile_x(p), py = pile_y(p), ph = p >= P_T0 ? card_y(t, t->n) + CHT : CHT;
            if (cx >= px - 6 && cx < px + CWD + 6 && cy >= py - 10 && cy < py + ph + 10) { best = p; break; }
        }
        int ok = 0;
        Pile *src = &d->p[d->from];
        int i = src->n - d->ndrag;
        if (best >= P_F0 && best <= P_F3 && d->ndrag == 1 && can_found(d->drag[0], &d->p[best])) ok = 1;
        if (best >= P_T0 && can_stack(d->drag[0], &d->p[best])) ok = 1;
        if (ok) { move_cards(d, d->from, i, best); sound_play(SND_CLICK); }
        d->ndrag = 0; d->from = -1;
        check_win(w, d);
    }
}

static void sol_tick(Window *w) {
    Sol *d = w->data;
    if (!d->won || !d->canvas || d->anim_card < 0) return;
    for (int step = 0; step < 2; step++) {
        if (d->ax < 0) {   /* launch next card from a foundation */
            int f = -1;
            for (int k = 0; k < 4; k++) { int c = P_F0 + (d->anim_f + k) % 4; if (d->p[c].n) { f = c; break; } }
            if (f < 0) { d->anim_card = -1; return; }
            Pile *p = &d->p[f];
            d->ax = pile_x(f); d->ay = TOPY;
            d->vx = (rand() % 2 ? 1 : -1) * (2.0f + (rand() % 30) / 10.0f);
            d->vy = -(rand() % 40) / 10.0f;
            d->drag[0] = p->c[--p->n];
            d->anim_f = (f - P_F0 + 1) % 4;
            d->anim_card--;
        }
        d->ax += d->vx; d->ay += d->vy; d->vy += 0.45f;
        if (d->ay + CHT > d->ch) { d->ay = d->ch - CHT; d->vy = -d->vy * 0.78f; }
        GfxState st;
        gfx_push(&st);
        gfx_target(d->canvas, 0, 0, d->cw, d->ch);
        draw_card((int)d->ax, (int)d->ay, d->drag[0], 1);
        gfx_pop(&st);
        if (d->ax < -CWD || d->ax > d->cw) d->ax = -1;
    }
}
static void sol_close(Window *w) { Sol *d = w->data; free(d->canvas); d->canvas = NULL; }

void open_solitaire(void) {
    Window *w = win_new("Solitaire", IC_SOLITAIRE, 7 * (CWD + GAP) + GAP, 380);
    if (!w) return;
    Sol *d = calloc(1, sizeof(Sol));
    if (!d) { win_close(w); return; }
    d->from = -1;
    deal(d);
    w->app = APP_SOLITAIRE; w->no_max = 1; w->data = d;
    w->draw = sol_draw; w->mouse = sol_mouse; w->tick = sol_tick; w->on_close = sol_close;
    w->y = 6;
}
