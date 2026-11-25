#include "gfx.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

uint32_t fb[SW * SH] __attribute__((aligned(32)));

/* current render target: buffer covering the absolute rect (tx,ty,tw,th) */
static uint32_t *tb = fb;
static int tx = 0, ty = 0, tw = SW, th = SH;
static int cx0 = 0, cy0 = 0, cx1 = SW, cy1 = SH;
#define PX(x, y) tb[((y) - ty) * tw + ((x) - tx)]

void gfx_target(uint32_t *buf, int x, int y, int w, int h) {
    tb = buf; tx = x; ty = y; tw = w; th = h;
    gfx_noclip();
}
void gfx_target_screen(void) { gfx_target(fb, 0, 0, SW, SH); }

void gfx_clip(int x, int y, int w, int h) {
    cx0 = x < tx ? tx : x; cy0 = y < ty ? ty : y;
    cx1 = x + w > tx + tw ? tx + tw : x + w; cy1 = y + h > ty + th ? ty + th : y + h;
    if (cx1 < cx0) cx1 = cx0;
    if (cy1 < cy0) cy1 = cy0;
}
void gfx_noclip(void) { cx0 = tx; cy0 = ty; cx1 = tx + tw; cy1 = ty + th; }
void gfx_getclip(int *x, int *y, int *w, int *h) { *x = cx0; *y = cy0; *w = cx1 - cx0; *h = cy1 - cy0; }

/* ---- layers: draw into an offscreen copy, then blend back with alpha ---- */
static uint32_t layer_buf[SW * SH];
static int lx, ly, lw, lh, layer_on;
void layer_begin(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SW) w = SW - x;
    if (y + h > SH) h = SH - y;
    if (w <= 0 || h <= 0) { w = h = 1; x = y = 0; }
    lx = x; ly = y; lw = w; lh = h; layer_on = 1;
    for (int j = 0; j < h; j++) memcpy(&layer_buf[j * w], &fb[(y + j) * SW + x], w * 4);
    gfx_target(layer_buf, x, y, w, h);
}
void layer_end(int alpha) {
    if (!layer_on) return;
    layer_on = 0;
    gfx_target_screen();
    if (alpha >= 256) {
        for (int j = 0; j < lh; j++) memcpy(&fb[(ly + j) * SW + lx], &layer_buf[j * lw], lw * 4);
        return;
    }
    if (alpha <= 0) return;
    for (int j = 0; j < lh; j++) {
        uint32_t *d = &fb[(ly + j) * SW + lx], *s = &layer_buf[j * lw];
        for (int i = 0; i < lw; i++) d[i] = mix(d[i], s[i], alpha);
    }
}

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
    if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) PX(x, y) = c;
}
uint32_t pget(int x, int y) {
    if (x < tx || y < ty || x >= tx + tw || y >= ty + th) return 0;
    return PX(x, y);
}
static void pblend(int x, int y, uint32_t c, int a) {
    if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) {
        uint32_t *p = &PX(x, y);
        *p = mix(*p, c, a);
    }
}

void fill(int x, int y, int w, int h, uint32_t c) {
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
    for (int j = y0; j < y1; j++) {
        uint32_t *p = &PX(x0, j);
        for (int i = x0; i < x1; i++) *p++ = c;
    }
}
void fill_a(int x, int y, int w, int h, uint32_t c, int a) {
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
    for (int j = y0; j < y1; j++) {
        uint32_t *p = &PX(x0, j);
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
void round_fill_a(int x, int y, int w, int h, int r, uint32_t c, int a) {
    for (int j = 0; j < h; j++) {
        int in = corner_inset(j, h, r);
        fill_a(x + in, y + j, w - 2 * in, 1, c, a);
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
void blit_argb(const uint32_t *src, int sw, int sh, int x, int y, int alpha) {
    for (int j = 0; j < sh; j++) {
        int py = y + j;
        if (py < cy0 || py >= cy1) continue;
        for (int i = 0; i < sw; i++) {
            int px = x + i;
            if (px < cx0 || px >= cx1) continue;
            uint32_t s = src[j * sw + i];
            int a = s >> 24;
            if (!a) continue;
            a = a * alpha >> 8;
            uint32_t *d = &PX(px, py);
            *d = a >= 255 ? (s & 0xFFFFFF) : mix(*d, s & 0xFFFFFF, a + (a >> 7));
        }
    }
}
void shadow_rect(int x, int y, int w, int h, int r, int strength) {
    /* soft drop shadow drawn as fading rings (cheap: only the visible edge pixels) */
    int ox = 2, oy = 3;
    for (int k = 1; k <= r; k++) {
        int a = strength * (r + 1 - k) / (r + 1) / 2;
        int X = x + ox - k, Y = y + oy - k, W = w + 2 * k, H = h + 2 * k;
        fill_a(X + k, Y, W - 2 * k, 1, 0, a);
        fill_a(X + k, Y + H - 1, W - 2 * k, 1, 0, a);
        fill_a(X, Y + k, 1, H - 2 * k, 0, a);
        fill_a(X + W - 1, Y + k, 1, H - 2 * k, 0, a);
    }
    fill_a(x + w, y + oy, ox, h, 0, strength / 2);
    fill_a(x + ox, y + h, w - ox, oy, 0, strength / 2);
}
void gfx_push(GfxState *s) { s->b = tb; s->x = tx; s->y = ty; s->w = tw; s->h = th; s->c[0] = cx0; s->c[1] = cy0; s->c[2] = cx1; s->c[3] = cy1; }
void gfx_pop(const GfxState *s) { tb = s->b; tx = s->x; ty = s->y; tw = s->w; th = s->h; cx0 = s->c[0]; cy0 = s->c[1]; cx1 = s->c[2]; cy1 = s->c[3]; }
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
