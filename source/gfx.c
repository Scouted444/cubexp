#include "gfx.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

uint32_t fb[SW * SH] __attribute__((aligned(32)));

static int cx0 = 0, cy0 = 0, cx1 = SW, cy1 = SH;

void gfx_clip(int x, int y, int w, int h) {
    cx0 = x < 0 ? 0 : x; cy0 = y < 0 ? 0 : y;
    cx1 = x + w > SW ? SW : x + w; cy1 = y + h > SH ? SH : y + h;
    if (cx1 < cx0) cx1 = cx0;
    if (cy1 < cy0) cy1 = cy0;
}
void gfx_noclip(void) { cx0 = 0; cy0 = 0; cx1 = SW; cy1 = SH; }
void gfx_getclip(int *x, int *y, int *w, int *h) { *x = cx0; *y = cy0; *w = cx1 - cx0; *h = cy1 - cy0; }

uint32_t mix(uint32_t a, uint32_t b, int t) {
    int it = 256 - t;
    int r = (((a >> 16) & 255) * it + ((b >> 16) & 255) * t) >> 8;
    int g = (((a >> 8) & 255) * it + ((b >> 8) & 255) * t) >> 8;
    int bl = ((a & 255) * it + (b & 255) * t) >> 8;
    return RGB(r, g, bl);
}
uint32_t shade(uint32_t c, int pct) {
    int r = ((c >> 16) & 255) * pct / 100, g = ((c >> 8) & 255) * pct / 100, b = (c & 255) * pct / 100;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB(r, g, b);
}

void pset(int x, int y, uint32_t c) {
    if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) fb[y * SW + x] = c;
}
uint32_t pget(int x, int y) {
    if (x < 0 || y < 0 || x >= SW || y >= SH) return 0;
    return fb[y * SW + x];
}
static void pblend(int x, int y, uint32_t c, int a) {
    if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) {
        uint32_t *p = &fb[y * SW + x];
        *p = mix(*p, c, a);
    }
}

void fill(int x, int y, int w, int h, uint32_t c) {
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
    for (int j = y0; j < y1; j++) {
        uint32_t *p = &fb[j * SW + x0];
        for (int i = x0; i < x1; i++) *p++ = c;
    }
}
void fill_a(int x, int y, int w, int h, uint32_t c, int a) {
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
    for (int j = y0; j < y1; j++) {
        uint32_t *p = &fb[j * SW + x0];
        for (int i = x0; i < x1; i++, p++) *p = mix(*p, c, a);
    }
}
void hline(int x, int y, int w, uint32_t c) { fill(x, y, w, 1, c); }
void vline(int x, int y, int h, uint32_t c) { fill(x, y, 1, h, c); }
void rect(int x, int y, int w, int h, uint32_t c) {
    hline(x, y, w, c); hline(x, y + h - 1, w, c);
    vline(x, y, h, c); vline(x + w - 1, y, h, c);
}
void line(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        pset(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
void thick_line(int x0, int y0, int x1, int y1, int r, uint32_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        if (r <= 0) pset(x0, y0, c); else fill_circle(x0, y0, r, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
void grad_v(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    for (int j = 0; j < h; j++) hline(x, y + j, w, mix(c1, c2, h > 1 ? j * 256 / (h - 1) : 0));
}
void grad_h(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    for (int i = 0; i < w; i++) vline(x + i, y, h, mix(c1, c2, w > 1 ? i * 256 / (w - 1) : 0));
}
void fill_circle(int cx, int cy, int r, uint32_t c) {
    for (int j = -r; j <= r; j++) {
        int k = 0;
        while ((k + 1) * (k + 1) + j * j <= r * r + r) k++;
        hline(cx - k, cy + j, 2 * k + 1, c);
    }
}
void circle(int cx, int cy, int r, uint32_t c) {
    int x = r, y = 0, e = 1 - r;
    while (x >= y) {
        pset(cx + x, cy + y, c); pset(cx - x, cy + y, c); pset(cx + x, cy - y, c); pset(cx - x, cy - y, c);
        pset(cx + y, cy + x, c); pset(cx - y, cy + x, c); pset(cx + y, cy - x, c); pset(cx - y, cy - x, c);
        y++;
        if (e < 0) e += 2 * y + 1; else { x--; e += 2 * (y - x) + 1; }
    }
}
/* horizontal inset of row j (0..h-1) for a box with rounded corners of radius r */
static int corner_inset(int j, int h, int r) {
    int d;
    if (j < r) d = j;
    else if (j >= h - r) d = h - 1 - j;
    else return 0;
    float dy = r - d - 0.5f;
    return r - (int)(sqrtf((float)(r * r) - dy * dy) + 0.5f);
}
void round_fill(int x, int y, int w, int h, int r, uint32_t c) {
    for (int j = 0; j < h; j++) {
        int in = corner_inset(j, h, r);
        hline(x + in, y + j, w - 2 * in, c);
    }
}
void round_grad(int x, int y, int w, int h, int r, uint32_t c1, uint32_t c2, int top_only) {
    for (int j = 0; j < h; j++) {
        int in = (top_only && j >= r) ? 0 : corner_inset(j, h, r);
        hline(x + in, y + j, w - 2 * in, mix(c1, c2, h > 1 ? j * 256 / (h - 1) : 0));
    }
}
void bevel(int x, int y, int w, int h, int sunken) {
    uint32_t a = sunken ? C_SHADOW : C_WHITE, b = sunken ? C_WHITE : C_SHADOW;
    hline(x, y, w, a); vline(x, y, h, a);
    hline(x, y + h - 1, w, b); vline(x + w - 1, y, h, b);
}
void xp_button(int x, int y, int w, int h, const char *label, int pressed, int hot) {
    uint32_t top = pressed ? 0xE3E2DA : 0xFFFFFF, bot = pressed ? 0xF0F0EA : 0xE4E2D5;
    grad_v(x + 1, y + 1, w - 2, h - 2, top, bot);
    uint32_t border = 0x003C74;
    hline(x + 2, y, w - 4, border); hline(x + 2, y + h - 1, w - 4, border);
    vline(x, y + 2, h - 4, border); vline(x + w - 1, y + 2, h - 4, border);
    pset(x + 1, y + 1, border); pset(x + w - 2, y + 1, border);
    pset(x + 1, y + h - 2, border); pset(x + w - 2, y + h - 2, border);
    if (hot) { rect(x + 1, y + 1, w - 2, h - 2, 0xF8B330); rect(x + 2, y + 2, w - 4, h - 4, 0xFDD889); }
    if (label) {
        int tw = text_w(&font_ui, label);
        text(&font_ui, x + (w - tw) / 2 + pressed, y + (h - font_ui.height) / 2 + pressed, label, 0);
    }
}
void grayscale_screen(int amt) {
    for (int i = 0; i < SW * SH; i++) {
        uint32_t c = fb[i];
        int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
        int l = (r * 77 + g * 150 + b * 29) >> 8;
        fb[i] = mix(c, RGB(l, l, l), amt);
    }
}
void darken_screen(int a) {
    int k = 256 - a;
    for (int i = 0; i < SW * SH; i++) {
        uint32_t c = fb[i];
        fb[i] = RGB((((c >> 16) & 255) * k) >> 8, (((c >> 8) & 255) * k) >> 8, ((c & 255) * k) >> 8);
    }
}

/* ---- text ---- */
int text_n(const Font *f, int x, int y, const char *s, int n, uint32_t c) {
    int sx = x;
    for (int i = 0; i < n && s[i]; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch < 32 || ch > 126) ch = '?';
        const Glyph *g = &f->g[ch - 32];
        const unsigned char *p = f->px + g->off;
        for (int j = 0; j < g->h; j++)
            for (int k = 0; k < g->w; k++) {
                int a = p[j * g->w + k];
                if (a > 8) pblend(x + g->xo + k, y + g->yo + j, c, a >= 248 ? 256 : a);
            }
        x += g->adv;
    }
    return x - sx;
}
int text(const Font *f, int x, int y, const char *s, uint32_t c) { return text_n(f, x, y, s, 1 << 30, c); }
int text_wn(const Font *f, const char *s, int n) {
    int w = 0;
    for (int i = 0; i < n && s[i]; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch < 32 || ch > 126) ch = '?';
        w += f->g[ch - 32].adv;
    }
    return w;
}
int text_w(const Font *f, const char *s) { return text_wn(f, s, 1 << 30); }
void text_center(const Font *f, int x, int y, int w, const char *s, uint32_t c) {
    text(f, x + (w - text_w(f, s)) / 2, y, s, c);
}
void text_shadow(const Font *f, int x, int y, const char *s, uint32_t c) {
    text(f, x + 1, y + 1, s, 0x000000);
    text(f, x, y, s, c);
}
