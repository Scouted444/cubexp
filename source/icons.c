/* Icons: drawn procedurally on a 32x32 design grid, rendered 4x supersampled
   into an anti-aliased ARGB cache, then alpha-blitted. */
#include "gfx.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int X0, Y0, SZ;
static inline int S(float v) { return (int)(v * SZ / 32.0f + 0.5f); }
static void F(float x, float y, float w, float h, uint32_t c) { fill(X0 + S(x), Y0 + S(y), S(x + w) - S(x), S(y + h) - S(y), c); }
static void R(float x, float y, float w, float h, uint32_t c) {
    int t = SZ / 32 > 1 ? SZ / 32 : 1;   /* outline thickness = 1 design unit */
    int ax = X0 + S(x), ay = Y0 + S(y), aw = S(x + w) - S(x), ah = S(y + h) - S(y);
    fill(ax, ay, aw, t, c); fill(ax, ay + ah - t, aw, t, c); fill(ax, ay, t, ah, c); fill(ax + aw - t, ay, t, ah, c);
}
static void GV(float x, float y, float w, float h, uint32_t a, uint32_t b) { grad_v(X0 + S(x), Y0 + S(y), S(x + w) - S(x), S(y + h) - S(y), a, b); }
static void GH(float x, float y, float w, float h, uint32_t a, uint32_t b) { grad_h(X0 + S(x), Y0 + S(y), S(x + w) - S(x), S(y + h) - S(y), a, b); }
static void C(float x, float y, float r, uint32_t c) { fill_circle(X0 + S(x), Y0 + S(y), S(r), c); }
static void RING(float x, float y, float r, float t, uint32_t c) {
    int cx = X0 + S(x), cy = Y0 + S(y), ro = S(r), ri = S(r - t);
    for (int j = -ro; j <= ro; j++)
        for (int i = -ro; i <= ro; i++) {
            int d = i * i + j * j;
            if (d <= ro * ro && d > ri * ri) pset(cx + i, cy + j, c);
        }
}
static void TL(float a, float b, float c, float d, float r, uint32_t col) { thick_line(X0 + S(a), Y0 + S(b), X0 + S(c), Y0 + S(d), S(r), col); }
static void RF(float x, float y, float w, float h, float r, uint32_t c) { round_fill(X0 + S(x), Y0 + S(y), S(x + w) - S(x), S(y + h) - S(y), S(r), c); }
static void RG(float x, float y, float w, float h, float r, uint32_t a, uint32_t b) { round_grad(X0 + S(x), Y0 + S(y), S(x + w) - S(x), S(y + h) - S(y), S(r), a, b, 0); }
static void TRI(float ax, float ay, float bx, float by, float cx, float cy, uint32_t col) {
    float x0 = X0 + ax * SZ / 32, y0 = Y0 + ay * SZ / 32, x1 = X0 + bx * SZ / 32, y1 = Y0 + by * SZ / 32, x2 = X0 + cx * SZ / 32, y2 = Y0 + cy * SZ / 32;
    int minx = (int)fminf(x0, fminf(x1, x2)), maxx = (int)fmaxf(x0, fmaxf(x1, x2)) + 1;
    int miny = (int)fminf(y0, fminf(y1, y2)), maxy = (int)fmaxf(y0, fmaxf(y1, y2)) + 1;
    for (int y = miny; y <= maxy; y++)
        for (int x = minx; x <= maxx; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float d1 = (px - x1) * (y0 - y1) - (x0 - x1) * (py - y1);
            float d2 = (px - x2) * (y1 - y2) - (x1 - x2) * (py - y2);
            float d3 = (px - x0) * (y2 - y0) - (x2 - x0) * (py - y0);
            int neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
            if (!(neg && pos)) pset(x, y, col);
        }
}

/* ---- extra drawing helpers ---- */
static void E(float cx, float cy, float rx, float ry, uint32_t c) {   /* filled ellipse */
    int x0 = X0 + S(cx - rx), x1 = X0 + S(cx + rx), y0 = Y0 + S(cy - ry), y1 = Y0 + S(cy + ry);
    float fcx = X0 + cx * SZ / 32, fcy = Y0 + cy * SZ / 32, frx = rx * SZ / 32, fry = ry * SZ / 32;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float dx = (x + 0.5f - fcx) / frx, dy = (y + 0.5f - fcy) / fry;
            if (dx * dx + dy * dy <= 1) pset(x, y, c);
        }
}
static void EG(float cx, float cy, float rx, float ry, uint32_t a, uint32_t b) {   /* ellipse, vertical gradient */
    int y0 = Y0 + S(cy - ry), y1 = Y0 + S(cy + ry), x0 = X0 + S(cx - rx), x1 = X0 + S(cx + rx);
    float fcx = X0 + cx * SZ / 32, fcy = Y0 + cy * SZ / 32, frx = rx * SZ / 32, fry = ry * SZ / 32;
    for (int y = y0; y <= y1; y++) {
        uint32_t c = mix(a, b, y1 > y0 ? (y - y0) * 256 / (y1 - y0) : 0);
        for (int x = x0; x <= x1; x++) {
            float dx = (x + 0.5f - fcx) / frx, dy = (y + 0.5f - fcy) / fry;
            if (dx * dx + dy * dy <= 1) pset(x, y, c);
        }
    }
}
/* white highlight fading downwards (gloss) */
static void GLOSS(float x, float y, float w, float h, int a) {
    int ax = X0 + S(x), ay = Y0 + S(y), aw = S(x + w) - S(x), ah = S(y + h) - S(y);
    for (int j = 0; j < ah; j++) fill_a(ax, ay + j, aw, 1, 0xFFFFFF, a * (ah - j) / ah);
}
static void QUAD(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy, uint32_t c) {
    TRI(ax, ay, bx, by, cx, cy, c); TRI(ax, ay, cx, cy, dx, dy, c);
}
/* rounded box with outline, vertical gradient and gloss: the basic XP "tile" */
static void TILE(float x, float y, float w, float h, float r, uint32_t edge, uint32_t c1, uint32_t c2, int gloss) {
    RF(x, y, w, h, r, edge);
    RG(x + 1, y + 1, w - 2, h - 2, r - 0.5f, c1, c2);
    if (gloss) GLOSS(x + 1.5f, y + 1.2f, w - 3, h * 0.42f, gloss);
}
static void ARC(float cx, float cy, float r, float a0, float a1, float t, uint32_t c);

/* ---- reusable parts ---- */
static void monitor(uint32_t s1, uint32_t s2) {
    QUAD(11, 26, 21, 26, 23, 29.5f, 9, 29.5f, 0x6E6C62);          /* foot */
    QUAD(11.5f, 26, 20.5f, 26, 22, 28.6f, 10, 28.6f, 0xC8C5B6);
    GV(13.5f, 22, 5, 4.5f, 0xA9A694, 0x86836F);                    /* neck */
    TILE(1.5f, 2, 29, 21.5f, 2.6f, 0x4A483E, 0xF8F7F1, 0xC9C5B0, 0);
    RF(4, 4.5f, 24, 15, 1, 0x56544A);
    GV(5, 5.5f, 22, 13, s1, s2);
    TRI(5, 5.5f, 19, 5.5f, 5, 14, 0xFFFFFF);  /* screen glare (drawn faint below) */
    GV(5, 5.5f, 22, 13, s1, s2);
    { int ax = X0 + S(5), ay = Y0 + S(5.5f), aw = S(27) - S(5), ah = S(18.5f) - S(5.5f);
      for (int j = 0; j < ah; j++) fill_a(ax, ay + j, aw - j * aw / ah / 2 - aw / 3, 1, 0xFFFFFF, 46 - j * 46 / ah); }
    C(26.5f, 21.3f, 0.7f, 0x4AD040);
}
static void folder(uint32_t a, uint32_t b, uint32_t edge) {
    /* back panel with tab */
    RF(2, 5, 12, 6, 1.6f, edge);
    RF(2.6f, 5.6f, 10.8f, 5, 1.2f, shade(a, 88));
    RF(2, 8, 28, 20, 1.6f, edge);
    RG(2.6f, 8.6f, 26.8f, 18.8f, 1.2f, shade(a, 92), shade(b, 80));
    F(4, 10, 23, 1, mix(a, 0xFFFFFF, 120));
    /* paper peeking out */
    F(5, 9.5f, 21, 4, 0xFFFFFF); F(5, 13, 21, 0.6f, 0xD8D8D8);
    /* front flap, slightly skewed for depth */
    QUAD(3.5f, 12.5f, 31, 12.5f, 29.4f, 28, 2, 28, edge);
    QUAD(4.2f, 13.2f, 30, 13.2f, 28.6f, 27.3f, 2.8f, 27.3f, a);
    { int y0 = Y0 + S(13.2f), y1 = Y0 + S(27.3f);
      for (int y = y0; y < y1; y++) {
          float t = (float)(y - y0) / (y1 - y0);
          int xl = X0 + S(4.2f - t * 1.4f), xr = X0 + S(30 - t * 1.4f);
          hline(xl, y, xr - xl, mix(a, b, (int)(t * 256)));
      } }
    QUAD(4.2f, 13.2f, 30, 13.2f, 29.8f, 14.2f, 4.1f, 14.2f, mix(a, 0xFFFFFF, 150));
}
static void page(uint32_t corner) {
    (void)corner;
    QUAD(6, 1.5f, 21.5f, 1.5f, 27, 7, 27, 30.5f, 0x7A7A7A);
    TRI(6, 1.5f, 27, 30.5f, 6, 30.5f, 0x7A7A7A);
    QUAD(6.8f, 2.3f, 21, 2.3f, 26.2f, 7.5f, 26.2f, 29.7f, 0xFFFFFF);
    TRI(6.8f, 2.3f, 26.2f, 29.7f, 6.8f, 29.7f, 0xFFFFFF);
    GH(6.8f, 8, 19.4f, 21.7f, 0xFFFFFF, 0xE6EAF0);
    TRI(21, 2.3f, 21, 7.5f, 26.2f, 7.5f, 0xC8CCD4);           /* folded corner */
    TL(21, 2.3f, 21, 7.5f, 0.3f, 0x9A9A9A); TL(21, 7.5f, 26.2f, 7.5f, 0.3f, 0x9A9A9A);
}
static void glyph_i(float cx, float cy, uint32_t c) { C(cx, cy - 7, 2, c); RF(cx - 1.8f, cy - 3, 3.6f, 11, 1, c); }
static void glyph_ex(float cx, float cy, uint32_t c) { RF(cx - 2, cy - 9, 4, 11, 1.5f, c); C(cx, cy + 5.5f, 2.2f, c); }
static void sphere(uint32_t edge, uint32_t c1, uint32_t c2) {
    C(16, 16, 14.5f, edge);
    EG(16, 16, 13.4f, 13.4f, c1, c2);
    EG(16, 9.6f, 9.6f, 6, 0xFFFFFF, mix(c1, 0xFFFFFF, 60));
    EG(16, 9.6f, 9.6f, 6, mix(c1, 0xFFFFFF, 170), mix(c1, c2, 60));
}
static void card(float x, float y, float w, float h, int back) {
    RF(x, y, w, h, 2, 0x5A5A5A);
    RF(x + 0.7f, y + 0.7f, w - 1.4f, h - 1.4f, 1.6f, 0xFFFFFF);
    if (back) {
        RG(x + 2, y + 2, w - 4, h - 4, 1, 0x3A6CD8, 0x163E9E);
        for (int i = 0; i < 5; i++) TL(x + 2.5f, y + 3 + i * 4, x + w - 2.5f, y + 6 + i * 4, 0.3f, 0x6A9AF0);
    }
}
static void heart(float cx, float cy, float r, uint32_t c) {
    C(cx - r / 2, cy - r / 4, r / 2 + 0.4f, c); C(cx + r / 2, cy - r / 4, r / 2 + 0.4f, c);
    TRI(cx - r - 0.2f, cy, cx + r + 0.2f, cy, cx, cy + r + 0.6f, c);
}

static void icon_raw(int id) {
    switch (id) {
    case IC_COMPUTER:
        monitor(0x7DBDFF, 0x2A62D0);
        QUAD(5, 14, 27, 12, 27, 18.5f, 5, 18.5f, 0x4CB040);
        QUAD(5, 15.5f, 27, 13.6f, 27, 14.5f, 5, 16.5f, 0x7ADA60);
        break;
    case IC_NOTEPAD:
        RF(6.5f, 4, 21, 27, 1.5f, 0x5A6A80);
        RG(7.3f, 4.8f, 19.4f, 25.4f, 1, 0xFFFFFF, 0xE8EEF6);
        RG(7.3f, 4.8f, 19.4f, 5, 1, 0x9CCAF4, 0x3A78C8);
        GLOSS(7.5f, 5, 19, 2.4f, 120);
        for (int i = 0; i < 5; i++) { RF(9.4f + i * 3.7f, 2, 1.6f, 5.4f, 0.8f, 0x6A7080); F(9.8f + i * 3.7f, 2.6f, 0.6f, 3.6f, 0xD8DCE4); }
        for (int i = 0; i < 6; i++) F(10, 13 + i * 2.9f, i == 5 ? 8 : 14, 0.9f, 0x8EA0C0);
        F(9, 11, 0.6f, 18, 0xF0A0A0);
        break;
    case IC_TEXTFILE:
        page(0);
        for (int i = 0; i < 6; i++) F(9.5f, 11 + i * 3, i == 5 ? 8 : 13.5f, 1, 0x6E82AA);
        break;
    case IC_CALC:
        TILE(6, 1.5f, 20, 29, 3, 0x2E343C, 0xE8ECF2, 0x9AA2B0, 70);
        RF(8.4f, 4, 15.2f, 6.6f, 1.2f, 0x4A5A48);
        RG(9, 4.6f, 14, 5.4f, 0.8f, 0xE4F8D8, 0xA4CE94);
        F(15, 6, 6, 1.6f, 0x3A4A38); F(19, 8.2f, 2, 0.8f, 0x3A4A38);
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 3; c++) {
                uint32_t k = (r == 3 && c == 2) ? 0xF07A30 : (c == 2 ? 0x5A6A80 : 0x3E4654);
                float bx = 8.8f + c * 5, by = 12.8f + r * 4.2f;
                RF(bx, by, 4, 3.2f, 0.8f, shade(k, 70));
                RG(bx + 0.3f, by + 0.2f, 3.4f, 2.6f, 0.6f, shade(k, 140), k);
            }
        break;
    case IC_PAINT:
        E(15, 17.5f, 13.5f, 12, 0x7A5A2E);
        EG(15, 17.2f, 12.5f, 11, 0xFCEACA, 0xE0C48A);
        C(21.5f, 22.5f, 3.4f, 0x7A5A2E); C(21.5f, 22.5f, 2.5f, 0xFFFFFF);
        static const uint32_t pc[5] = { 0xE23030, 0x2A62E6, 0x2EB044, 0xF4C418, 0x9A3AC8 };
        static const float px[5] = { 8, 13.5f, 20, 7.5f, 13 }, py[5] = { 13.5f, 9.5f, 11, 20.5f, 24 };
        for (int i = 0; i < 5; i++) { C(px[i], py[i], 2.9f, shade(pc[i], 70)); C(px[i], py[i], 2.4f, pc[i]); C(px[i] - 0.8f, py[i] - 0.8f, 0.8f, 0xFFFFFF); }
        TL(17, 29, 27.5f, 7, 1.5f, 0x7A4A18); TL(17.2f, 28.4f, 27.2f, 7.2f, 0.6f, 0xC08A40);
        TL(27, 8, 29.2f, 3.6f, 1.6f, 0xB8BCC8); TL(28.8f, 4.4f, 30, 2, 1.2f, 0x2A2A40);
        break;
    case IC_MINES:
        TILE(2, 2, 28, 28, 2.5f, 0x5A5A5A, 0xF2F2F2, 0xB0B0B0, 60);
        for (int a = 0; a < 8; a++) { float an = a * 0.785398f; TL(16, 16, 16 + 11 * sinf(an), 16 - 11 * cosf(an), 1, 0x141414); }
        C(16, 16, 8.2f, 0x141414); EG(16, 16, 7.2f, 7.2f, 0x4A4A4A, 0x101010);
        C(13.2f, 13.2f, 2.4f, 0xFFFFFF); C(12.6f, 12.6f, 1, 0xFFFFFF);
        break;
    case IC_CMD:
        RF(1.5f, 4, 29, 25, 2.2f, 0x18284A);
        RG(2.3f, 4.8f, 27.4f, 5.6f, 1.6f, 0x5A9AF8, 0x0A4AD0);
        GLOSS(2.5f, 5, 27, 2.4f, 110);
        F(2.3f, 10.4f, 27.4f, 17.8f, 0x000000);
        F(4.5f, 13, 2, 2, 0xD0D0D0); F(7.5f, 13, 7, 2, 0xD0D0D0); F(15.5f, 13, 4, 2, 0xD0D0D0);
        F(4.5f, 17.5f, 10, 2, 0xD0D0D0); F(15.5f, 17.5f, 3, 2, 0xFFFFFF);
        F(4.5f, 22, 5, 2, 0x909090);
        break;
    case IC_TERMINAL:
        RF(1.5f, 3.5f, 29, 26, 3.4f, 0x0E0A18);
        RG(2.3f, 4.3f, 27.4f, 24.4f, 2.8f, 0x3E3266, 0x14101E);
        GLOSS(2.5f, 4.4f, 27, 5, 50);
        for (int i = 0; i < 3; i++) { C(5.5f + i * 3.2f, 7.4f, 1.1f, i == 0 ? 0xFF6058 : i == 1 ? 0xFFBD2E : 0x28C940); }
        TL(7, 12.5f, 12.5f, 16.5f, 1.4f, 0x6CF08C); TL(12.5f, 16.5f, 7, 20.5f, 1.4f, 0x6CF08C);
        RF(14.5f, 19.5f, 9, 2.3f, 0.6f, 0xE8E8F8);
        break;
    case IC_CONTROL:
        TILE(1.5f, 3, 29, 26, 3, 0x3A5A98, 0xF2F6FF, 0x9EBCEC, 90);
        for (int a = 0; a < 8; a++) {
            float an = a * 0.785398f;
            C(16 + 9.2f * sinf(an), 16 - 9.2f * cosf(an), 3, 0x4E5E78);
        }
        C(16, 16, 9, 0x4E5E78);
        EG(16, 16, 8, 8, 0xC4D0E2, 0x7A8AA4);
        C(16, 16, 3.8f, 0x4E5E78); C(16, 16, 3, 0xEEF4FF);
        ARC(16, 16, 6.2f, -60, 30, 0.6f, 0xFFFFFF);
        break;
    case IC_TASKMGR:
        monitor(0x0C180C, 0x001E00);
        for (int i = 0; i < 4; i++) F(5, 8 + i * 3, 22, 0.4f, 0x0E4A0E);
        TL(6, 15, 10, 11, 0.7f, 0x40FF40); TL(10, 11, 13.5f, 14, 0.7f, 0x40FF40); TL(13.5f, 14, 17.5f, 7.5f, 0.7f, 0x40FF40);
        TL(17.5f, 7.5f, 21, 12, 0.7f, 0x40FF40); TL(21, 12, 26, 9, 0.7f, 0x40FF40);
        break;
    case IC_SNAKE:
        TILE(1.5f, 1.5f, 29, 29, 3.5f, 0x0E2A0E, 0x2E5A2A, 0x183A18, 40);
        TL(6, 24.5f, 20.5f, 24.5f, 2.6f, 0x2E9A2E); TL(20.5f, 24.5f, 20.5f, 12.5f, 2.6f, 0x2E9A2E); TL(20.5f, 12.5f, 10, 12.5f, 2.6f, 0x2E9A2E);
        TL(6, 24, 20.5f, 24, 1.8f, 0x5ED05A); TL(20, 24.5f, 20, 12.5f, 1.8f, 0x5ED05A); TL(20.5f, 12, 10, 12, 1.8f, 0x5ED05A);
        E(10, 8.2f, 4, 3.6f, 0x2E9A2E); E(10, 7.8f, 3.4f, 3, 0x7CF06C);
        C(8.6f, 7.2f, 0.9f, 0xFFFFFF); C(11.4f, 7.2f, 0.9f, 0xFFFFFF); C(8.8f, 7.4f, 0.45f, 0); C(11.6f, 7.4f, 0.45f, 0);
        EG(25, 7.5f, 3.6f, 3.4f, 0xFF5050, 0xB01010); C(24, 6.3f, 1, 0xFFB0B0); TL(25, 4.2f, 26.4f, 2, 0.5f, 0x6A4A20);
        E(27, 3.4f, 1.6f, 0.8f, 0x3AA030);
        break;
    case IC_CLOCK:
        C(16, 16, 14.5f, 0x0E2E70);
        EG(16, 16, 13.6f, 13.6f, 0x4A7EE0, 0x123E96);
        C(16, 16, 11.4f, 0x9AA8C0);
        EG(16, 16, 10.8f, 10.8f, 0xFFFFFF, 0xE4E8F0);
        for (int i = 0; i < 12; i++) { float a = i * 0.5236f; C(16 + 9.2f * sinf(a), 16 - 9.2f * cosf(a), i % 3 ? 0.55f : 0.95f, 0x303848); }
        TL(16, 16, 16, 8.5f, 1, 0x141820); TL(16, 16, 21.5f, 18.5f, 0.8f, 0x141820); TL(16, 16, 11, 21, 0.35f, 0xD02020);
        C(16, 16, 1.4f, 0xD02020);
        EG(16, 10, 9, 5, 0xFFFFFF, 0xF4F6FA);
        for (int i = 0; i < 12; i++) { float a = i * 0.5236f; if (16 - 9.2f * cosf(a) < 15) C(16 + 9.2f * sinf(a), 16 - 9.2f * cosf(a), i % 3 ? 0.55f : 0.95f, 0x303848); }
        TL(16, 16, 16, 8.5f, 1, 0x141820);
        GLOSS(7, 6, 18, 6, 60);
        break;
    case IC_RECYCLE: case IC_RECYCLE_FULL: {
        /* tapered bin with ribs; rim ellipse on top */
        if (id == IC_RECYCLE_FULL) {
            QUAD(8, 3, 17, 1.5f, 18.5f, 9, 9.5f, 10, 0x8A8A8A); QUAD(8.6f, 3.6f, 16.6f, 2.2f, 17.8f, 8.6f, 10, 9.4f, 0xFFFFFF);
            for (int i = 0; i < 3; i++) TL(10.5f, 4.6f + i * 1.6f, 16, 3.8f + i * 1.6f, 0.3f, 0x8AA0C8);
            QUAD(15, 2.5f, 24, 4, 22.5f, 10, 13.6f, 9, 0x8A8A8A); QUAD(15.6f, 3.2f, 23.2f, 4.6f, 21.8f, 9.4f, 14.4f, 8.6f, 0xFFF4C0);
        }
        int x0 = X0 + S(7), x1 = X0 + S(25), y0 = Y0 + S(8.5f), y1 = Y0 + S(29.5f);
        for (int y = y0; y < y1; y++) {
            float t = (float)(y - y0) / (y1 - y0);
            int l = x0 + (int)(t * S(2)), r = x1 - (int)(t * S(2));
            for (int x = l; x < r; x++) {
                float u = (float)(x - l) / (r - l);
                float sh = 0.55f + 0.45f * sinf(u * 3.1416f * 0.85f + 0.35f);
                int rib = ((int)(u * 7 * 2)) % 2;
                uint32_t c = mix(0x5A6A58, 0xDCE8D8, (int)(sh * 256));
                if (rib) c = shade(c, 92);
                pset(x, y, c);
            }
        }
        E(16, 29.3f, 7, 1.2f, 0x4A5A48);
        E(16, 8.5f, 9.6f, 2.5f, 0x4A5A48);
        EG(16, 8.3f, 8.8f, 1.9f, 0xF0F6EE, 0x9AAA98);
        E(16, 8.6f, 7.4f, 1.2f, id == IC_RECYCLE_FULL ? 0xE8E0C0 : 0x3A4A38);
        break; }
    case IC_HELP: case IC_INFO:
        sphere(0x0A3A9E, 0x5A9AF4, 0x1450C8);
        if (id == IC_INFO) glyph_i(16, 17, 0xFFFFFF);
        else {
            ARC(16, 12.5f, 4.8f, -80, 150, 1.5f, 0xFFFFFF);
            TL(18.4f, 16.7f, 16, 19, 1.5f, 0xFFFFFF);
            TL(16, 19, 16, 20.5f, 1.5f, 0xFFFFFF);
            C(16, 24.6f, 2, 0xFFFFFF);
        }
        break;
    case IC_WARN:
        TRI(16, 1.5f, 31, 29, 1, 29, 0x7A5600);
        TRI(16, 4.2f, 29, 27.6f, 3, 27.6f, 0xFFD436);
        TRI(16, 4.2f, 23, 17, 9, 17, 0xFFE878);
        glyph_ex(16, 18.5f, 0x141414);
        break;
    case IC_ERROR:
        sphere(0x7A0A0A, 0xF0584A, 0xB81A12);
        TL(10.5f, 10.5f, 21.5f, 21.5f, 1.9f, 0xFFFFFF); TL(21.5f, 10.5f, 10.5f, 21.5f, 1.9f, 0xFFFFFF);
        break;
    case IC_FOLDER: folder(0xFFE89C, 0xEDB040, 0xA87A1C); break;
    case IC_DOCS:
        folder(0xFFE89C, 0xEDB040, 0xA87A1C);
        F(10, 15.5f, 13, 9.5f, 0xFFFFFF); R(10, 15.5f, 13, 9.5f, 0x9A9A9A);
        for (int i = 0; i < 4; i++) F(11.8f, 17.5f + i * 1.9f, 9.4f, 0.7f, 0x6E82AA);
        break;
    case IC_PICS:
        folder(0xFFE89C, 0xEDB040, 0xA87A1C);
        F(9.5f, 15, 14, 11, 0xFFFFFF); GV(10.5f, 16, 12, 9, 0x6CB4FF, 0xC0E4FF);
        TRI(10.5f, 25, 15, 19, 19.5f, 25, 0x3E9A3A); TRI(15, 25, 19, 21, 22.5f, 25, 0x2A7A26); C(20, 18.2f, 1.3f, 0xFFF070);
        break;
    case IC_MUSICF:
        folder(0xFFE89C, 0xEDB040, 0xA87A1C);
        C(12.5f, 23.5f, 2.4f, 0x2A46B8); C(20, 22, 2.4f, 0x2A46B8);
        TL(14.6f, 23.5f, 14.6f, 15, 0.6f, 0x2A46B8); TL(22.1f, 22, 22.1f, 13.5f, 0.6f, 0x2A46B8); TL(14.6f, 15, 22.1f, 13.5f, 1, 0x2A46B8);
        break;
    case IC_DESKTOP:
        TILE(1.5f, 4, 29, 21, 2, 0x1E3A7A, 0x3E6AC8, 0x2A50A8, 0);
        GV(3, 5.5f, 26, 14, 0x5A9AF0, 0xB0D8FF);
        QUAD(3, 15, 29, 13, 29, 19.5f, 3, 19.5f, 0x4CB040);
        F(3, 19.5f, 26, 3.6f, 0x2A5AD0); RF(3, 19.5f, 7, 3.6f, 1, 0x3CA03C);
        GV(12.5f, 25, 7, 3, 0x8A8A8A, 0x5A5A5A); RF(8.5f, 28, 15, 2, 1, 0x4A4A4A);
        break;
    case IC_SHOWDESK:
        TILE(3, 5, 26, 20, 2, 0x1E3A7A, 0xE6F0FF, 0x9AB8E8, 0);
        TILE(6, 8.5f, 12, 9, 1, 0x2A4A90, 0xFFFFFF, 0xE8EEF8, 0);
        RG(6.6f, 9.1f, 10.8f, 2.2f, 0.6f, 0x5A8AF0, 0x2A5AD0);
        TL(13, 27, 23, 19, 1.4f, 0x2A8A2A); TRI(19, 17.5f, 25.5f, 17, 24.5f, 23.5f, 0x2A8A2A);
        break;
    case IC_EXPLORER:
        folder(0xFFE89C, 0xEDB040, 0xA87A1C);
        TL(23.2f, 23.2f, 29, 29, 2, 0x5A3A10); TL(23.2f, 23.2f, 28.6f, 28.6f, 1.1f, 0xA86A28);
        C(19, 19, 6.6f, 0x24489A); EG(19, 19, 5.2f, 5.2f, 0xD8EEFF, 0x8AB8E8);
        C(17.4f, 17.2f, 1.6f, 0xFFFFFF);
        break;
    case IC_LOGO: {
        TRI(16, 2.5f, 29.5f, 9.5f, 16, 16.5f, 0xF8B04A); TRI(16, 2.5f, 2.5f, 9.5f, 16, 16.5f, 0xF59A23);
        TRI(2.5f, 9.5f, 16, 16.5f, 2.5f, 23.5f, 0x4CC85A); TRI(16, 16.5f, 16, 30, 2.5f, 23.5f, 0x2A8A34);
        TRI(29.5f, 9.5f, 16, 16.5f, 29.5f, 23.5f, 0x4A94F4); TRI(16, 16.5f, 16, 30, 29.5f, 23.5f, 0x1A54B8);
        TRI(16, 4.5f, 23, 8.2f, 16, 11.8f, 0xFFD088);
        TL(16, 16.5f, 16, 30, 0.35f, 0xFFFFFF); TL(2.5f, 9.5f, 16, 16.5f, 0.35f, 0xFFFFFF); TL(29.5f, 9.5f, 16, 16.5f, 0.35f, 0xFFFFFF);
        break; }
    case IC_DRIVE:
        QUAD(4, 9, 28, 9, 30.5f, 14, 1.5f, 14, 0x6A6A6A);
        QUAD(4.6f, 9.6f, 27.4f, 9.6f, 29.4f, 13.6f, 2.6f, 13.6f, 0xF4F4F4);
        TILE(1.5f, 13.5f, 29, 11, 1.5f, 0x5A5A5A, 0xE4E4E4, 0x9A9A9A, 0);
        F(4, 18, 14, 0.8f, 0x707070); F(4, 20, 14, 0.8f, 0x707070);
        C(26, 19, 1.6f, 0x206020); C(26, 19, 1.1f, 0x40F040); C(25.7f, 18.6f, 0.4f, 0xD0FFD0);
        break;
    case IC_SDCARD:
        QUAD(8, 2, 26, 2, 26, 30, 8, 30, 0x1E2A4E); TRI(8, 2, 13, 2, 8, 7, 0);
        QUAD(8.7f, 2.7f, 25.3f, 2.7f, 25.3f, 29.3f, 8.7f, 29.3f, 0x34487E); TRI(8.7f, 2.7f, 13, 2.7f, 8.7f, 7, 0);
        TRI(8, 1.5f, 13.6f, 1.5f, 8, 7.6f, 0);
        for (int i = 0; i < 5; i++) F(14 + i * 2.3f, 3, 1.4f, 4.4f, 0xE8C860);
        RF(10.5f, 12, 13, 13, 1, 0xF8F8F8); F(12.5f, 15, 9, 1.4f, 0x34487E); F(12.5f, 18.5f, 6, 1.4f, 0x34487E);
        GLOSS(9, 8, 16, 4, 60);
        break;
    case IC_DISC:
        C(16, 16, 14.5f, 0x7A7A8A);
        for (int r = 13; r > 3; r--) C(16, 16, r + 0.4f, mix(0xC8D4F0, 0xF0D8F0, (r * 37) & 255));
        QUAD(16, 16, 27, 7, 29, 10, 16, 16, 0xFFFFFF);
        QUAD(16, 16, 5, 25, 3, 22, 16, 16, 0xFFF0FF);
        C(16, 16, 4, 0x8A8A9A); C(16, 16, 3, 0xF4F4F8); C(16, 16, 1.8f, 0x8A8A9A);
        break;
    case IC_PAD:
        E(9, 20, 7, 8.5f, 0x22185E); E(23, 20, 7, 8.5f, 0x22185E);
        RF(4, 10, 24, 14, 6, 0x22185E);
        EG(9, 19.5f, 6.2f, 7.8f, 0x7262D8, 0x3C2E98); EG(23, 19.5f, 6.2f, 7.8f, 0x7262D8, 0x3C2E98);
        RG(5, 10.8f, 22, 12.4f, 5.2f, 0x7A6AE0, 0x4A3AA8);
        GLOSS(6, 11, 20, 4, 90);
        C(9.5f, 17, 4, 0x2E2478); RF(7, 16, 5, 2, 0.4f, 0xE8E8F4); RF(8.5f, 14.5f, 2, 5, 0.4f, 0xE8E8F4);
        C(22.5f, 17.5f, 3, 0x1E8A40); C(22.5f, 17.2f, 2.5f, 0x3ED470); C(26.5f, 14, 1.5f, 0xE84848); C(18.8f, 21, 1.4f, 0xF4C820);
        break;
    case IC_POWER: case IC_RESTART: case IC_LOGOFF: {
        uint32_t e = id == IC_POWER ? 0x8A1E08 : id == IC_RESTART ? 0x1E6A14 : 0x9A5A04;
        uint32_t c1 = id == IC_POWER ? 0xF8987A : id == IC_RESTART ? 0x9CE48A : 0xFFDC84;
        uint32_t c2 = id == IC_POWER ? 0xC8340E : id == IC_RESTART ? 0x2E9A22 : 0xE08A0A;
        TILE(1.5f, 1.5f, 29, 29, 5.5f, e, c1, c2, 110);
        if (id == IC_POWER) { ARC(16, 17, 8, 35, 325, 2.2f, 0xFFFFFF); RF(14.7f, 5, 2.6f, 12, 1.2f, 0xFFFFFF); }
        else if (id == IC_RESTART) { ARC(16, 16.5f, 8, 60, 330, 2.2f, 0xFFFFFF); TRI(16, 3.5f, 23.5f, 8.5f, 16, 13.5f, 0xFFFFFF); }
        else { RING(11.5f, 16, 5, 2.2f, 0xFFFFFF); TL(16.5f, 16, 27, 16, 1.1f, 0xFFFFFF); TL(23, 16, 23, 21, 1.1f, 0xFFFFFF); TL(26, 16, 26, 20, 1, 0xFFFFFF); }
        break; }
    case IC_RUN:
        RF(1.5f, 4.5f, 29, 23, 2, 0x163A82);
        RG(2.3f, 5.3f, 27.4f, 4.4f, 1.4f, 0x5A9AF8, 0x1A56D8);
        F(2.3f, 9.7f, 27.4f, 17, 0xFFFFFF);
        GV(2.3f, 9.7f, 27.4f, 17, 0xFFFFFF, 0xE4EAF4);
        TL(8, 23.5f, 18, 14.5f, 1.6f, 0x1E8A20); TRI(13.5f, 11.5f, 22.5f, 10.5f, 21.5f, 19.5f, 0x1E8A20);
        TL(8.4f, 22.8f, 17.6f, 14.6f, 0.7f, 0x5ACA50);
        break;
    case IC_KEYBOARD:
        QUAD(3, 9, 29, 9, 31, 24, 1, 24, 0x4A4A4A);
        QUAD(3.6f, 9.6f, 28.4f, 9.6f, 30, 23.2f, 2, 23.2f, 0xD8D8D8);
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 8; c++) {
                float kx = 4.5f + c * 3.05f + r * 0.5f - (r == 2 ? 0.3f : 0), ky = 11 + r * 3.4f;
                RF(kx, ky, 2.6f, 2.6f, 0.5f, 0x6A6A6A); RF(kx + 0.2f, ky + 0.1f, 2.2f, 2, 0.4f, 0xFCFCFC);
            }
        RF(9, 21, 14, 1.8f, 0.5f, 0x6A6A6A); RF(9.2f, 21.1f, 13.6f, 1.3f, 0.4f, 0xFCFCFC);
        break;
    case IC_USER:
        GV(0, 0, 32, 20, 0x6EC4F4, 0xC8ECFF);
        GV(0, 20, 32, 12, 0x48A848, 0x2A7A2A);
        TL(16, 31, 16, 18, 1.1f, 0x2A7A20);
        E(12, 24, 4, 1.6f, 0x3A9A30); E(20.5f, 21.5f, 4, 1.6f, 0x3A9A30);
        for (int a = 0; a < 12; a++) { float an = a * 0.5236f; E(16 + 6.6f * sinf(an), 12 - 6.6f * cosf(an), 2.6f, 2.6f, mix(0xFFE040, 0xF0A010, (a * 40) & 255)); }
        C(16, 12, 4.8f, 0x5A2E08); EG(16, 12, 4, 4, 0x8A5A2A, 0x4A2604); C(14.8f, 10.8f, 1.2f, 0xA87A48);
        R(0, 0, 32, 32, 0xFFFFFF);
        break;
    case IC_SOUND:
        RF(3.5f, 11.5f, 7, 9, 1, 0x3A4054); RG(4.2f, 12.2f, 5.6f, 7.6f, 0.6f, 0x8A94B0, 0x4E5670);
        TRI(9.5f, 12, 18, 4.5f, 18, 27.5f, 0x3A4054); TRI(10.5f, 12.6f, 17.2f, 6.4f, 17.2f, 25.6f, 0x6E7894);
        ARC(18, 16, 5.5f, 45, 135, 1, 0x2E6ED0);
        ARC(18, 16, 10, 40, 140, 1, 0x2E6ED0);
        break;
    case IC_SOLITAIRE:
        card(3, 7, 16, 22, 1);
        card(13, 3, 17, 23, 0);
        heart(21.5f, 13.5f, 4.2f, 0xD42020);
        C(19.6f, 11.6f, 0.9f, 0xFF9A9A);
        TL(14.6f, 9, 16.1f, 4.6f, 0.35f, 0xD42020); TL(16.1f, 4.6f, 17.6f, 9, 0.35f, 0xD42020); TL(15.2f, 7.4f, 17, 7.4f, 0.3f, 0xD42020);
        break;
    case IC_BLOCKS:
        TILE(1.5f, 1.5f, 29, 29, 3.2f, 0x06060E, 0x24244A, 0x0E0E1E, 0);
        { static const uint32_t cc[] = { 0x34C8F4, 0xF4C834, 0xEC4858, 0x54D864, 0xA868EC };
          static const float bx[] = { 5, 10, 15, 5, 10, 20, 20, 15, 10, 20 }, by[] = { 23, 23, 23, 18, 18, 23, 18, 18, 7, 12 };
          for (int i = 0; i < 10; i++) {
              uint32_t c = cc[i % 5];
              RF(bx[i], by[i], 4.8f, 4.8f, 0.8f, shade(c, 60));
              RG(bx[i] + 0.4f, by[i] + 0.4f, 4, 4, 0.6f, mix(c, 0xFFFFFF, 80), c);
              F(bx[i] + 0.8f, by[i] + 0.8f, 3.2f, 0.7f, 0xFFFFFF);
          } }
        break;
    case IC_VIEWER:
        TILE(1.5f, 3.5f, 29, 25, 2, 0x6A4E2A, 0xF8E2B4, 0xC8A468, 0);
        RF(4.5f, 6.5f, 23, 19, 0.5f, 0x8A6A3A);
        GV(5, 7, 22, 18, 0x5AA8FF, 0xC8E6FF);
        TRI(5, 25, 13, 13, 21, 25, 0x3E9A3A); TRI(13, 25, 19.5f, 16.5f, 27, 25, 0x267A22);
        C(21.5f, 11.5f, 2.3f, 0xFFF080); C(21.5f, 11.5f, 1.4f, 0xFFFCD0);
        break;
    case IC_IMAGEFILE:
        page(0);
        RF(8.5f, 11, 16, 14, 0.6f, 0x6A6A6A);
        GV(9, 11.5f, 15, 13, 0x5AA8FF, 0xC8E6FF);
        TRI(9, 24.5f, 15, 16, 21, 24.5f, 0x3E9A3A); TRI(15, 24.5f, 19.5f, 19, 24, 24.5f, 0x267A22); C(20, 14.5f, 1.6f, 0xFFF070);
        break;
    case IC_MUSIC:
        sphere(0xA84A08, 0xFFB050, 0xE06A10);
        { uint32_t nc = 0xFFFFFF;
          E(12, 22.5f, 3.4f, 2.6f, nc); E(21, 20.5f, 3.4f, 2.6f, nc); TL(15, 22, 15, 10, 0.8f, nc); TL(24, 20, 24, 8, 0.8f, nc);
          QUAD(14.2f, 9.5f, 24.8f, 7, 24.8f, 10, 14.2f, 12.5f, nc); }
        break;
    case IC_SONGFILE:
        page(0);
        { uint32_t nc = 0x2A46B8;
          E(12, 23.5f, 3, 2.3f, nc); E(20, 22, 3, 2.3f, nc); TL(14.6f, 23, 14.6f, 12.5f, 0.7f, nc); TL(22.6f, 21.5f, 22.6f, 11, 0.7f, nc);
          QUAD(13.9f, 12, 23.3f, 10, 23.3f, 12.6f, 13.9f, 14.6f, nc); }
        break;
    case IC_STICKY:
        QUAD(4, 4, 28, 4, 28, 23, 22, 29, 0xB8A42A); TRI(4, 4, 22, 29, 4, 29, 0xB8A42A);
        QUAD(4.7f, 4.7f, 27.3f, 4.7f, 27.3f, 22.6f, 21.8f, 28.3f, 0xFFF280); TRI(4.7f, 4.7f, 21.8f, 28.3f, 4.7f, 28.3f, 0xFFF280);
        GV(4.7f, 4.7f, 22.6f, 4, 0xF8DC3C, 0xF8E460);
        TRI(21.8f, 28.3f, 27.3f, 22.6f, 21.8f, 22.6f, 0xE0C840);
        for (int i = 0; i < 4; i++) F(8, 12 + i * 3.6f, i == 3 ? 8 : 15, 0.9f, 0xA8962A);
        break;
    case IC_FILE: page(0); break;
    case IC_APPFILE:
        RF(3, 5.5f, 26, 21, 2, 0x1A3A7A); RG(3.8f, 6.3f, 24.4f, 4.4f, 1.4f, 0x5A9AF8, 0x1A56D8); F(3.8f, 10.7f, 24.4f, 15, 0xF6F6F6);
        C(16, 18, 4.4f, 0x1E8A20); C(16, 17.6f, 3.8f, 0x4CC844);
        break;
    case IC_SCRIPT:
        page(0);
        RF(8.5f, 11.5f, 16, 14.5f, 1.5f, 0x0E0A18); RG(9, 12, 15, 13.5f, 1.2f, 0x3E3266, 0x14101E);
        TL(11, 15, 13.8f, 17.8f, 0.7f, 0x6CF08C); TL(13.8f, 17.8f, 11, 20.6f, 0.7f, 0x6CF08C); F(15, 20, 5.5f, 1.3f, 0xFFFFFF);
        break;
    case IC_UP:
        QUAD(12.5f, 13, 19.5f, 13, 19.5f, 29, 12.5f, 29, 0x1E6A1E); TRI(3.5f, 15, 28.5f, 15, 16, 2, 0x1E6A1E);
        QUAD(13.5f, 14, 18.5f, 14, 18.5f, 28, 13.5f, 28, 0x48B848); TRI(5.6f, 14.2f, 26.4f, 14.2f, 16, 3.6f, 0x48B848);
        TRI(8, 13, 16, 4.8f, 16, 13, 0x7ADA6A);
        break;
    case IC_BACK:
        sphere(0x1E6A1E, 0x7AD86A, 0x2E9A2E);
        TRI(7, 16, 16, 8, 16, 24, 0xFFFFFF); RF(15, 13, 10, 6, 1, 0xFFFFFF);
        break;
    case IC_NEWFOLDER:
        folder(0xFFE89C, 0xEDB040, 0xA87A1C);
        for (int a = 0; a < 8; a++) { float an = a * 0.785f; TL(24, 8, 24 + 7.5f * sinf(an), 8 - 7.5f * cosf(an), 0.6f, 0xF0B820); }
        C(24, 8, 4, 0xFFE860); C(24, 8, 2.4f, 0xFFFFD0);
        break;
    case IC_LINUX:
        RF(1.5f, 3.5f, 24, 20, 2.6f, 0x0E0A18);
        RG(2.3f, 4.3f, 22.4f, 18.4f, 2, 0x3A3A44, 0x14141A);
        TL(5, 8, 8.5f, 10.5f, 0.9f, 0x8AE234); TL(8.5f, 10.5f, 5, 13, 0.9f, 0x8AE234); F(10, 12.5f, 6, 1.4f, 0xE8E8F0);
        /* a little penguin standing in front */
        E(22, 22, 8, 9.5f, 0x101018);
        EG(22, 24, 5.6f, 7, 0xFFFFFF, 0xD8DCE4);
        E(22, 13.2f, 5.6f, 5.4f, 0x101018);
        E(20.2f, 12.6f, 1.6f, 1.9f, 0xFFFFFF); E(23.8f, 12.6f, 1.6f, 1.9f, 0xFFFFFF);
        C(20.6f, 12.9f, 0.7f, 0x101018); C(23.4f, 12.9f, 0.7f, 0x101018);
        TRI(19.4f, 15, 24.6f, 15, 22, 17.6f, 0xF4A81C);
        E(18, 30.2f, 3.4f, 1.4f, 0xF4A81C); E(26, 30.2f, 3.4f, 1.4f, 0xF4A81C);
        E(14.6f, 23, 1.8f, 5, 0x101018); E(29.4f, 23, 1.8f, 5, 0x101018);
        GLOSS(17, 9, 10, 3, 50);
        break;
    case IC_DELETE:
        TL(7, 7, 25, 25, 3.2f, 0x9A1810); TL(25, 7, 7, 25, 3.2f, 0x9A1810);
        TL(7, 7, 25, 25, 2.2f, 0xF04A38); TL(25, 7, 7, 25, 2.2f, 0xF04A38);
        TL(7.4f, 6.8f, 24.6f, 24, 0.8f, 0xFF9A8A); TL(24.6f, 6.8f, 7.4f, 24, 0.8f, 0xFF9A8A);
        break;
    }
}

/* arc from angle a0 to a1 (degrees, 0 = up, clockwise) */
static void ARC(float cx, float cy, float r, float a0, float a1, float t, uint32_t c) {
    float px = cx + r * sinf(a0 * 0.0174533f), py = cy - r * cosf(a0 * 0.0174533f);
    for (float a = a0 + 6; a <= a1 + 0.1f; a += 6) {
        float nx = cx + r * sinf(a * 0.0174533f), ny = cy - r * cosf(a * 0.0174533f);
        TL(px, py, nx, ny, t, c);
        px = nx; py = ny;
    }
}

/* ---- cache ---- */
typedef struct { short id, size; uint32_t *px; } IconEntry;
static IconEntry cache[512];
static int ncache;

static uint32_t *render_icon(int id, int size) {
    int ss = size <= 64 ? 4 : 2;
    int R2 = size * ss;
    uint32_t *b = malloc(sizeof(uint32_t) * R2 * R2), *w = malloc(sizeof(uint32_t) * R2 * R2);
    uint32_t *out = malloc(sizeof(uint32_t) * size * size);
    if (!b || !w || !out) { free(b); free(w); free(out); return NULL; }
    GfxState st;
    gfx_push(&st);
    for (int pass = 0; pass < 2; pass++) {
        uint32_t *buf = pass ? w : b;
        gfx_target(buf, 0, 0, R2, R2);
        fill(0, 0, R2, R2, pass ? 0xFFFFFF : 0x000000);
        int shadowed = id != IC_USER;
        X0 = 0; Y0 = 0; SZ = shadowed ? R2 * 15 / 16 : R2;
        icon_raw(id);
    }
    gfx_pop(&st);
    int n = ss * ss;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            int rb = 0, gb = 0, bb = 0, rw = 0, gw = 0, bw = 0;
            for (int j = 0; j < ss; j++)
                for (int i = 0; i < ss; i++) {
                    uint32_t cb = b[(y * ss + j) * R2 + x * ss + i], cw = w[(y * ss + j) * R2 + x * ss + i];
                    rb += (cb >> 16) & 255; gb += (cb >> 8) & 255; bb += cb & 255;
                    rw += (cw >> 16) & 255; gw += (cw >> 8) & 255; bw += cw & 255;
                }
            rb /= n; gb /= n; bb /= n; rw /= n; gw /= n; bw /= n;
            int a = 255 - ((rw - rb) + (gw - gb) + (bw - bb)) / 3;
            if (a < 0) a = 0;
            if (a > 255) a = 255;
            uint32_t c = 0;
            if (a > 0) {
                int r = rb * 255 / a, g = gb * 255 / a, bl = bb * 255 / a;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (bl > 255) bl = 255;
                c = ((uint32_t)a << 24) | RGB(r, g, bl);
            }
            out[y * size + x] = c;
        }
    free(b); free(w);
    if (id != IC_USER && size >= 16) {   /* soft drop shadow from the alpha mask */
        uint8_t *al = malloc(size * size);
        if (al) {
            for (int i = 0; i < size * size; i++) al[i] = out[i] >> 24;
            int off = size >= 40 ? 2 : 1, rad = size >= 40 ? 2 : 1;
            for (int y = 0; y < size; y++)
                for (int x = 0; x < size; x++) {
                    int sum = 0, n = 0;
                    for (int j = -rad; j <= rad; j++)
                        for (int i = -rad; i <= rad; i++) {
                            int sx = x - off + i, sy = y - off + j;
                            if (sx >= 0 && sy >= 0 && sx < size && sy < size) sum += al[sy * size + sx];
                            n++;
                        }
                    int sa = sum / n * (size >= 32 ? 70 : 55) / 255;   /* shadow alpha 0..~70 */
                    uint32_t c = out[y * size + x];
                    int a = c >> 24;
                    if (a >= 255 || sa <= 0) continue;
                    int na = a + sa * (255 - a) / 255;
                    if (na <= 0) continue;
                    int r = ((c >> 16) & 255) * a / na, g = ((c >> 8) & 255) * a / na, bl = (c & 255) * a / na;
                    out[y * size + x] = ((uint32_t)na << 24) | RGB(r, g, bl);
                }
            free(al);
        }
    }
    return out;
}

void draw_icon_a(int id, int x, int y, int size, int alpha) {
    if (id < 0 || id >= IC_COUNT || size <= 0) return;
    for (int i = 0; i < ncache; i++)
        if (cache[i].id == id && cache[i].size == size) { blit_argb(cache[i].px, size, size, x, y, alpha); return; }
    uint32_t *px = render_icon(id, size);
    if (!px) return;
    if (ncache < 512) { cache[ncache].id = id; cache[ncache].size = size; cache[ncache].px = px; ncache++; }
    blit_argb(px, size, size, x, y, alpha);
    if (ncache >= 512) free(px);
}
void draw_icon(int id, int x, int y, int size) { draw_icon_a(id, x, y, size, 256); }

/* ---- mouse pointer (XP style, with soft shadow) ---- */
static const char *arrow[] = {
    "X", "XX", "X.X", "X..X", "X...X", "X....X", "X.....X", "X......X", "X.......X",
    "X........X", "X.........X", "X......XXXXX", "X...X..X", "X..XX..X", "X.X  X..X",
    "XX   X..X", "X     X..X", "      X..X", "       XX",
};
static const char *hourglass[] = {
    "XXXXXXXXX", "X.......X", " X.....X ", " X.ooo.X ", "  X.o.X  ", "   X.X   ",
    "  X...X  ", " X..o..X ", " X.ooo.X ", "X.ooooo.X", "XXXXXXXXX",
};
void draw_cursor(int x, int y, int busy) {
    for (int j = 0; j < 19; j++)
        for (int i = 0; arrow[j][i]; i++)
            if (arrow[j][i] != ' ') { fill_a(x + i + 2, y + j + 2, 1, 1, 0, 60); }
    for (int j = 0; j < 19; j++)
        for (int i = 0; arrow[j][i]; i++) {
            char c = arrow[j][i];
            if (c == 'X') pset(x + i, y + j, 0x000000);
            else if (c == '.') pset(x + i, y + j, 0xFFFFFF);
        }
    if (busy)
        for (int j = 0; j < 11; j++)
            for (int i = 0; hourglass[j][i]; i++) {
                char c = hourglass[j][i];
                if (c == 'X') pset(x + 11 + i, y + 10 + j, 0);
                else if (c == '.') pset(x + 11 + i, y + 10 + j, 0xFFFFFF);
                else if (c == 'o') pset(x + 11 + i, y + 10 + j, 0xD8B040);
            }
}
