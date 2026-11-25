/* Calculator (standard mode) */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

typedef struct {
    char disp[32];
    double acc, mem;
    int op, fresh, err;
} Calc;

static const char *keys[5][6] = {
    { NULL, NULL, "Backspace", NULL, "CE", "C" },
    { "MC", "7", "8", "9", "/", "sqrt" },
    { "MR", "4", "5", "6", "*", "%" },
    { "MS", "1", "2", "3", "-", "1/x" },
    { "M+", "0", "+/-", ".", "+", "=" },
};
#define KW 36
#define KH 28

static void key_rect(int r, int c, int *x, int *y, int *w) {
    *y = 46 + r * (KH + 5);
    *w = KW;
    if (r == 0) {   /* Backspace/CE/C wide row */
        static const int xs[] = { 0, 0, 52, 0, 148, 200 };
        static const int ws[] = { 0, 0, 92, 0, 48, 48 };
        *x = xs[c]; *w = ws[c];
        return;
    }
    *x = c == 0 ? 8 : 52 + (c - 1) * (KW + 4);
}

static void show(Calc *d, double v) {
    if (isnan(v) || isinf(v)) { strcpy(d->disp, "Cannot divide by zero."); d->err = 1; return; }
    snprintf(d->disp, sizeof d->disp, "%.12g", v);
}
static double val(Calc *d) { return atof(d->disp); }

static double apply(double a, double b, int op, int *err) {
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': if (b == 0) { *err = 1; return NAN; } return a / b;
    }
    return b;
}

static void press(Calc *d, const char *k) {
    if (d->err && strcmp(k, "C") && strcmp(k, "CE")) return;
    if ((k[0] >= '0' && k[0] <= '9' && !k[1]) || !strcmp(k, ".")) {
        if (d->fresh) { strcpy(d->disp, "0"); d->fresh = 0; }
        if (k[0] == '.' && strchr(d->disp, '.')) return;
        if (strlen(d->disp) >= 16) return;
        if (!strcmp(d->disp, "0") && k[0] != '.') strcpy(d->disp, k);
        else strcat(d->disp, k);
    } else if (!strcmp(k, "Backspace")) {
        if (d->fresh) return;
        int n = strlen(d->disp);
        if (n > 1) d->disp[n - 1] = 0; else strcpy(d->disp, "0");
        if (!strcmp(d->disp, "-")) strcpy(d->disp, "0");
    } else if (!strcmp(k, "CE")) { strcpy(d->disp, "0"); d->err = 0; }
    else if (!strcmp(k, "C")) { strcpy(d->disp, "0"); d->acc = 0; d->op = 0; d->err = 0; d->fresh = 0; }
    else if (strchr("+-*/", k[0]) && !k[1]) {
        if (d->op && !d->fresh) { int e = 0; d->acc = apply(d->acc, val(d), d->op, &e); show(d, d->acc); if (e) return; }
        else if (!d->op) d->acc = val(d);
        d->op = k[0]; d->fresh = 1;
    } else if (!strcmp(k, "=")) {
        if (d->op) { int e = 0; double r = apply(d->acc, val(d), d->op, &e); show(d, r); d->acc = r; d->op = 0; }
        d->fresh = 1;
    } else if (!strcmp(k, "+/-")) {
        if (!strcmp(d->disp, "0")) return;
        if (d->disp[0] == '-') memmove(d->disp, d->disp + 1, strlen(d->disp));
        else { memmove(d->disp + 1, d->disp, strlen(d->disp) + 1); d->disp[0] = '-'; }
    } else if (!strcmp(k, "sqrt")) {
        double v = val(d);
        if (v < 0) { strcpy(d->disp, "Invalid input for function."); d->err = 1; return; }
        show(d, sqrt(v)); d->fresh = 1;
    } else if (!strcmp(k, "%")) { show(d, d->acc * val(d) / 100); d->fresh = 1; }
    else if (!strcmp(k, "1/x")) {
        double v = val(d);
        if (v == 0) { strcpy(d->disp, "Cannot divide by zero."); d->err = 1; return; }
        show(d, 1 / v); d->fresh = 1;
    }
    else if (!strcmp(k, "MC")) d->mem = 0;
    else if (!strcmp(k, "MR")) { show(d, d->mem); d->fresh = 1; }
    else if (!strcmp(k, "MS")) { d->mem = val(d); d->fresh = 1; }
    else if (!strcmp(k, "M+")) { d->mem += val(d); d->fresh = 1; }
}

static void calc_draw(Window *w, int x, int y, int cw, int ch) {
    Calc *d = w->data;
    (void)ch;
    fill(x + 8, y + 8, cw - 16, 26, 0xFFFFFF);
    rect(x + 8, y + 8, cw - 16, 26, 0x7F9DB9);
    int tw = text_w(&font_big, d->disp);
    const Font *f = tw > cw - 28 ? &font_bold : &font_big;
    text(f, x + cw - 14 - text_w(f, d->disp), y + (f == &font_big ? 10 : 15), d->disp, 0);
    /* memory indicator box */
    fill(x + 10, y + 46, 32, KH, C_FACE);
    rect(x + 10, y + 46, 32, KH, C_SHADOW);
    if (d->mem != 0) text_center(&font_bold, x + 10, y + 53, 32, "M", 0);
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 6; c++) {
            if (!keys[r][c]) continue;
            int kx, ky, kw;
            key_rect(r, c, &kx, &ky, &kw);
            int hot = w == focus && inside(mouse_x, mouse_y, x + kx, y + ky, kw, KH);
            xp_button(x + kx, y + ky, kw, KH, NULL, hot && mouse_btn, hot);
            uint32_t col = (c == 0 || r == 0) ? 0xC00000 : (strchr("+-*/=", keys[r][c][0]) && !keys[r][c][1]) ? 0xC00000 : 0x0000C0;
            if (!strcmp(keys[r][c], "+/-")) col = 0x0000C0;
            text_center(&font_ui, x + kx, y + ky + 8, kw, keys[r][c], col);
        }
}
static void calc_mouse(Window *w, int ev, int mx, int my) {
    if (ev != EV_DOWN) return;
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 6; c++) {
            if (!keys[r][c]) continue;
            int kx, ky, kw;
            key_rect(r, c, &kx, &ky, &kw);
            if (inside(mx, my, kx, ky, kw, KH)) { press(w->data, keys[r][c]); return; }
        }
}
static void calc_key(Window *w, int k) {
    char s[2] = { (char)k, 0 };
    if ((k >= '0' && k <= '9') || strchr("+-*/.=%", k)) press(w->data, s);
    else if (k == K_ENTER) press(w->data, "=");
    else if (k == K_BKSP) press(w->data, "Backspace");
    else if (k == K_ESC || k == 'c' || k == 'C') press(w->data, "C");
}
void open_calc(void) {
    Window *w = win_new("Calculator", IC_CALC, 256, 210);
    if (!w) return;
    Calc *d = calloc(1, sizeof(Calc));
    strcpy(d->disp, "0");
    w->app = APP_CALC; w->no_max = 1; w->wants_keys = 1; w->data = d;
    w->draw = calc_draw; w->mouse = calc_mouse; w->key = calc_key;
}
