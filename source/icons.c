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

/* ---- reusable parts ---- */
static void monitor(uint32_t s1, uint32_t s2) {
    RF(2, 3, 28, 20, 2.5f, 0x4A4A44);
    GV(3, 4, 26, 18, 0xF4F2EA, 0xC8C4B0);
    GV(5.5f, 6, 21, 13, s1, s2);
    F(5.5f, 6, 21, 1, 0xFFFFFF);
    GV(12, 23, 8, 3, 0xB0AC98, 0x8C8874);
    RG(6, 26, 20, 4, 1.5f, 0xF0EDE2, 0xA8A490);
    R(6, 26, 20, 4, 0x7A7866);
}
static void folder(uint32_t a, uint32_t b, uint32_t edge) {
    RF(2, 6, 13, 6, 2, shade(a, 85));
    RG(2, 9, 28, 19, 2, a, b);
    R(2, 9, 28, 19, edge);
    F(3, 10, 26, 1, mix(a, 0xFFFFFF, 140));
}
static void page(uint32_t corner) {
    TRI(7, 2, 21, 2, 7, 30, 0xFFFFFF); F(7, 2, 14, 28, 0xFFFFFF);
    F(7, 9, 19, 21, 0xFFFFFF);
    TRI(21, 2, 26, 7, 21, 7, corner);
    TRI(21, 2, 26, 7, 26, 9, 0xFFFFFF);
    F(21, 7, 5, 2, 0xFFFFFF);
    TL(7, 2, 21, 2, 0.5f, 0x8A8A8A); TL(21, 2, 26, 7, 0.5f, 0x8A8A8A); TL(26, 7, 26, 30, 0.5f, 0x8A8A8A);
    TL(26, 30, 7, 30, 0.5f, 0x8A8A8A); TL(7, 30, 7, 2, 0.5f, 0x8A8A8A);
    TRI(21, 2, 21, 7, 26, 7, corner);
}
/* arc from angle a0 to a1 (degrees, 0 = up, clockwise) */
static void ARC(float cx, float cy, float r, float a0, float a1, float t, uint32_t c) {
    float px = cx + r * sinf(a0 * 0.0174533f), py = cy - r * cosf(a0 * 0.0174533f);
    for (float a = a0 + 8; a <= a1 + 0.1f; a += 8) {
        float nx = cx + r * sinf(a * 0.0174533f), ny = cy - r * cosf(a * 0.0174533f);
        TL(px, py, nx, ny, t, c);
        px = nx; py = ny;
    }
}
static void glyph_i(float cx, float cy, uint32_t c) { C(cx, cy - 7, 2, c); RF(cx - 1.8f, cy - 3, 3.6f, 11, 1, c); }
static void glyph_ex(float cx, float cy, uint32_t c) { RF(cx - 2, cy - 9, 4, 11, 1.5f, c); C(cx, cy + 5.5f, 2.2f, c); }

static void icon_raw(int id) {
    switch (id) {
    case IC_COMPUTER:
        monitor(0x7DB9FF, 0x1A56C8);
        GV(5.5f, 14, 21, 5, 0x5CB848, 0x2E8A22);
        break;
    case IC_NOTEPAD: case IC_TEXTFILE:
        if (id == IC_NOTEPAD) {
            RF(6, 3, 21, 27, 1.5f, 0x5A6A80);
            GV(7, 4, 19, 25, 0xFFFFFF, 0xEEF2F8);
            GV(7, 4, 19, 5, 0x8CC0F0, 0x3A7CCC);
            for (int i = 0; i < 4; i++) C(10 + i * 4.3f, 4, 1.1f, 0x404858);
            for (int i = 0; i < 5; i++) F(10, 12.5f + i * 3.4f, i == 4 ? 8 : 13, 1.1f, 0x8898B8);
        } else {
            page(0xD8D8D8);
            for (int i = 0; i < 6; i++) F(10, 11 + i * 3, i == 5 ? 8 : 13, 1, 0x7A8AA8);
        }
        break;
    case IC_CALC:
        RF(6, 2, 20, 28, 3, 0x3E4650);
        RG(7, 3, 18, 26, 2.5f, 0xE4E8EE, 0xA8B0BC);
        RG(9, 5, 14, 6, 1, 0xDCF5D0, 0xA8D29A); R(9, 5, 14, 6, 0x5A6E58);
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 3; c++)
                RF(9.5f + c * 4.7f, 13.5f + r * 3.8f, 3.6f, 2.8f, 0.8f, (r == 3 && c == 2) ? 0xE86A2A : 0x485260);
        break;
    case IC_PAINT:
        C(15, 17, 13, 0x7A5A2E); C(15, 17, 12, 0xF4DEB0);
        C(21, 23, 3.4f, 0x7A5A2E); C(21, 23, 2.4f, 0xFFFFFF);
        C(9, 13, 3, 0xE23030); C(15, 9.5f, 3, 0x2A62E6); C(22, 13, 3, 0x2EB044); C(9, 21, 3, 0xF4C418);
        TL(17, 29, 28, 5, 1.5f, 0x8A5A20); TL(26.5f, 8, 29, 3, 1.4f, 0xC0C0C8); C(28.6f, 3.4f, 1.5f, 0x303030);
        break;
    case IC_MINES:
        RG(2, 2, 28, 28, 2, 0xE8E8E8, 0xB4B4B4); R(2, 2, 28, 28, 0x6A6A6A);
        TL(16, 5, 16, 27, 1, 0x101010); TL(5, 16, 27, 16, 1, 0x101010);
        TL(8.5f, 8.5f, 23.5f, 23.5f, 1, 0x101010); TL(23.5f, 8.5f, 8.5f, 23.5f, 1, 0x101010);
        C(16, 16, 8, 0x101010); C(13, 13, 2.2f, 0xFFFFFF);
        break;
    case IC_CMD:
        RF(2, 4, 28, 24, 2, 0x2A2A2A);
        GH(3, 5, 26, 5, 0x0A50D8, 0x5A96F4);
        F(3, 10, 26, 17, 0x000000);
        F(5, 13, 6, 1.5f, 0xC0C0C0); F(12, 13, 4, 1.5f, 0xC0C0C0);
        F(5, 17, 9, 1.5f, 0xC0C0C0); F(15, 17, 3, 1.5f, 0xFFFFFF);
        break;
    case IC_TERMINAL:
        RF(2, 4, 28, 24, 3, 0x1E1A2E);
        GV(3, 5, 26, 22, 0x3A2E5A, 0x1A1428);
        TL(7, 11, 12, 15, 1.2f, 0x6CF08C); TL(12, 15, 7, 19, 1.2f, 0x6CF08C);
        F(14, 19, 9, 2, 0xE0E0F0);
        for (int i = 0; i < 3; i++) C(6 + i * 3, 7, 0.9f, i == 0 ? 0xFF6058 : i == 1 ? 0xFFBD2E : 0x28C940);
        break;
    case IC_CONTROL:
        RG(2, 3, 28, 26, 2.5f, 0xEEF4FF, 0x9EBCEC); R(2, 3, 28, 26, 0x4A6AA8);
        for (int a = 0; a < 8; a++) {
            float an = a * 0.785398f;
            C(16 + 9 * sinf(an), 16 - 9 * cosf(an), 3, 0x5E6E86);
        }
        C(16, 16, 8.5f, 0x7E8EA6); C(16, 16, 7, 0x9AAAC0); C(16, 16, 3.2f, 0xEEF4FF);
        break;
    case IC_TASKMGR:
        monitor(0x0C140C, 0x002400);
        TL(7, 15, 11, 11, 0.6f, 0x40FF40); TL(11, 11, 14, 14, 0.6f, 0x40FF40); TL(14, 14, 18, 8, 0.6f, 0x40FF40);
        TL(18, 8, 21, 12, 0.6f, 0x40FF40); TL(21, 12, 25, 9, 0.6f, 0x40FF40);
        break;
    case IC_SNAKE:
        RF(2, 2, 28, 28, 3, 0x1E3A1E);
        TL(6, 24, 20, 24, 2, 0x50D050); TL(20, 24, 20, 12, 2, 0x50D050); TL(20, 12, 10, 12, 2, 0x50D050); TL(10, 12, 10, 7, 2.3f, 0x7CF06C);
        C(9, 6, 0.8f, 0); C(11.5f, 6, 0.8f, 0);
        C(25, 7, 3.2f, 0xE02828); C(24, 6, 1, 0xFF9090); TL(25, 3, 26, 1.5f, 0.5f, 0x6A4A20);
        break;
    case IC_CLOCK:
        C(16, 16, 14, 0x16408E); C(16, 16, 12.5f, 0xFFFFFF);
        for (int i = 0; i < 12; i++) { float a = i * 0.5236f; C(16 + 10.3f * sinf(a), 16 - 10.3f * cosf(a), i % 3 ? 0.6f : 1, 0x404040); }
        TL(16, 16, 16, 8, 1, 0x101010); TL(16, 16, 22, 19, 0.8f, 0x101010); C(16, 16, 1.4f, 0xC02020);
        break;
    case IC_RECYCLE: case IC_RECYCLE_FULL:
        TRI(7, 8, 25, 8, 9, 29, 0xB8C6B2); TRI(25, 8, 23, 29, 9, 29, 0x8A9A86);
        GH(8, 8, 16, 21, 0xD0DCCA, 0x7C8C78);
        for (int i = 0; i < 3; i++) TL(12 + i * 4, 11, 12 + i * 3.6f - 0.6f + 0.6f, 26, 0.5f, 0x5A6A58);
        TL(7, 8, 9, 29, 0.5f, 0x4A5A48); TL(25, 8, 23, 29, 0.5f, 0x4A5A48); TL(9, 29, 23, 29, 0.5f, 0x4A5A48);
        if (id == IC_RECYCLE_FULL) {
            page(0xD8D8D8);
            GH(8, 12, 16, 17, 0xD0DCCA, 0x7C8C78);
            TL(7, 8, 9, 29, 0.5f, 0x4A5A48); TL(25, 8, 23, 29, 0.5f, 0x4A5A48); TL(9, 29, 23, 29, 0.5f, 0x4A5A48);
            TL(7, 12, 25, 12, 0.5f, 0x4A5A48);
        } else {
            RG(5, 5, 22, 3.5f, 1, 0xE6EEE2, 0x8A9A88); R(5, 5, 22, 3.5f, 0x4A5A48);
            F(13, 3, 6, 2, 0x6A7A68);
        }
        break;
    case IC_HELP: case IC_INFO:
        C(16, 16, 14, 0x0E44B0); C(16, 15.4f, 12.8f, 0x3A86EE); C(14, 10, 6, 0x5CA0F4);
        if (id == IC_INFO) glyph_i(16, 16.5f, 0xFFFFFF);
        else {
            ARC(16, 12, 4.8f, -80, 150, 1.4f, 0xFFFFFF);
            TL(18.4f, 16.2f, 16, 18.5f, 1.4f, 0xFFFFFF);
            TL(16, 18.5f, 16, 20, 1.4f, 0xFFFFFF);
            C(16, 24.2f, 2, 0xFFFFFF);
        }
        break;
    case IC_WARN:
        TRI(16, 2, 31, 29, 1, 29, 0x8A6400);
        TRI(16, 4.2f, 29.2f, 27.8f, 2.8f, 27.8f, 0xFFD838);
        glyph_ex(16, 18, 0x101010);
        break;
    case IC_ERROR:
        C(16, 16, 14, 0x8A1010); C(16, 15.5f, 13, 0xE23A2A);
        TL(10.5f, 10.5f, 21.5f, 21.5f, 1.8f, 0xFFFFFF); TL(21.5f, 10.5f, 10.5f, 21.5f, 1.8f, 0xFFFFFF);
        break;
    case IC_FOLDER: folder(0xFFE69A, 0xEDB84A, 0xB88A28); break;
    case IC_DOCS: folder(0xFFE69A, 0xEDB84A, 0xB88A28); F(9, 13, 14, 12, 0xFFFFFF); for (int i = 0; i < 4; i++) F(11, 15 + i * 2.4f, 10, 0.8f, 0x8090B0); break;
    case IC_PICS: folder(0xFFE69A, 0xEDB84A, 0xB88A28); GV(9, 13, 14, 11, 0x7CC0FF, 0x5AB04A); C(19, 16, 1.6f, 0xFFF080); R(9, 13, 14, 11, 0xFFFFFF); break;
    case IC_MUSICF: folder(0xFFE69A, 0xEDB84A, 0xB88A28); C(13, 22, 2.4f, 0x3050C0); C(20, 20.5f, 2.4f, 0x3050C0); TL(15, 22, 15, 13, 0.6f, 0x3050C0); TL(22, 20.5f, 22, 11.5f, 0.6f, 0x3050C0); TL(15, 13, 22, 11.5f, 1, 0x3050C0); break;
    case IC_DESKTOP:
        RF(2, 5, 28, 20, 2, 0x2A4A8A); GV(3.5f, 6.5f, 25, 15, 0x5A9AF0, 0x5CB848); F(3.5f, 19, 25, 2.5f, 0x2A5AD0); F(3.5f, 19, 6, 2.5f, 0x3CA03C);
        F(12, 25, 8, 3, 0x6A6A6A); F(9, 28, 14, 1.5f, 0x5A5A5A);
        break;
    case IC_SHOWDESK:
        RF(4, 6, 24, 18, 2, 0x2A4A8A); GV(5.5f, 7.5f, 21, 15, 0xDDEBFF, 0x8AB0E8);
        F(8, 10, 10, 7, 0xFFFFFF); R(8, 10, 10, 7, 0x3A5AA0); F(8, 10, 10, 2, 0x3A6AD8);
        TL(14, 26, 22, 18, 1, 0x30A030); F(20, 18, 3, 1.5f, 0x30A030);
        break;
    case IC_EXPLORER:
        folder(0xFFE69A, 0xEDB84A, 0xB88A28);
        RING(19, 19, 6, 1.8f, 0x2A5AC8); C(19, 19, 4.2f, 0xC8E4FF); C(17.5f, 17.5f, 1.5f, 0xFFFFFF);
        TL(23.5f, 23.5f, 28.5f, 28.5f, 1.6f, 0x6A4A20);
        break;
    case IC_LOGO: {
        /* isometric cube: top orange, left green, right blue */
        TRI(16, 3, 29, 9.5f, 16, 16, 0xF7A43A); TRI(16, 3, 3, 9.5f, 16, 16, 0xF59A23);
        TRI(3, 9.5f, 16, 16, 3, 23.5f, 0x44BC52); TRI(16, 16, 16, 30, 3, 23.5f, 0x2E9038);
        TRI(29, 9.5f, 16, 16, 29, 23.5f, 0x3C88EE); TRI(16, 16, 16, 30, 29, 23.5f, 0x1F5CC0);
        TL(16, 16, 16, 30, 0.35f, 0xFFFFFF); TL(3, 9.5f, 16, 16, 0.35f, 0xFFFFFF); TL(29, 9.5f, 16, 16, 0.35f, 0xFFFFFF);
        break; }
    case IC_DRIVE:
        RG(2, 10, 28, 14, 2, 0xF0F0F0, 0x9A9A9A); R(2, 10, 28, 14, 0x505050);
        F(5, 18, 13, 1, 0x707070); F(5, 20, 13, 1, 0x707070); C(25, 18.5f, 1.4f, 0x30E030);
        break;
    case IC_SDCARD:
        TRI(8, 2, 13, 2, 8, 7, 0x2A2A2A); F(8, 6, 18, 24, 0x2A3A6A); F(13, 2, 13, 5, 0x2A3A6A); TRI(8, 2, 13, 2, 8, 7, 0);
        for (int i = 0; i < 5; i++) F(14 + i * 2.4f, 3, 1.4f, 4, 0xE0C060);
        RF(10, 12, 14, 12, 1, 0xF4F4F4); F(12, 15, 10, 1.5f, 0x2A3A6A); F(12, 19, 7, 1.5f, 0x2A3A6A);
        break;
    case IC_DISC:
        C(16, 16, 14, 0x8A8A9A); C(16, 16, 13.2f, 0xDDE2EE);
        C(16, 16, 9.5f, 0xBCCCF4); C(16, 16, 6.5f, 0xEED4F4); C(16, 16, 3.5f, 0x8A8A9A); C(16, 16, 2.4f, 0xFFFFFF);
        TL(8, 9, 12, 7, 0.6f, 0xFFFFFF);
        break;
    case IC_PAD:
        RF(2, 9, 28, 16, 7, 0x2E2470); RF(3, 10, 26, 14, 6, 0x5848B8); RF(3, 10, 26, 5, 4, 0x6C5CCC);
        C(9.5f, 16, 4, 0x3A3090); F(7, 15, 5, 2, 0xE0E0F0); F(8.5f, 13.5f, 2, 5, 0xE0E0F0);
        C(22.5f, 16.5f, 3, 0x36C468); C(26.5f, 13, 1.4f, 0xE84848); C(18.5f, 20, 1.3f, 0xF4C820);
        break;
    case IC_POWER:
        RG(2, 2, 28, 28, 5, 0xF49070, 0xC23412);
        RING(16, 17, 8.5f, 2.2f, 0xFFFFFF);
        F(13.4f, 4.5f, 5.2f, 8, 0xD84A28); RF(14.8f, 5, 2.4f, 11.5f, 1, 0xFFFFFF);
        break;
    case IC_RESTART:
        RG(2, 2, 28, 28, 5, 0x8EDC7C, 0x2C8A20);
        RING(16, 16, 8.5f, 2.2f, 0xFFFFFF);
        F(16, 5, 8, 9, 0x56B046);
        TRI(17, 4, 24, 8, 17, 12.5f, 0xFFFFFF);
        break;
    case IC_LOGOFF:
        RG(2, 2, 28, 28, 5, 0xFFD878, 0xD8880E);
        RING(11.5f, 16, 5, 2.2f, 0xFFFFFF);
        TL(16.5f, 16, 27, 16, 1.1f, 0xFFFFFF); TL(23, 16, 23, 21, 1.1f, 0xFFFFFF); TL(26, 16, 26, 20, 1, 0xFFFFFF);
        break;
    case IC_RUN:
        RF(2, 5, 28, 22, 2, 0x16448E); F(3.5f, 10, 25, 15.5f, 0xFFFFFF);
        GH(3.5f, 6.5f, 25, 3.5f, 0x2A6AE0, 0x8ABCF8);
        TL(9, 22, 19, 14, 1.4f, 0x2EA030); TRI(15, 11, 23, 11, 23, 19, 0x2EA030);
        break;
    case IC_KEYBOARD:
        RF(1, 9, 30, 16, 2.5f, 0x5A5A5A); RG(2, 10, 28, 14, 2, 0xF8F8F8, 0xC4C4C4);
        for (int r = 0; r < 3; r++) for (int c = 0; c < 7; c++) RF(4 + c * 3.4f + r * 0.8f, 12 + r * 3.3f, 2.5f, 2.3f, 0.5f, 0x4A4A4A);
        RF(9, 21.5f, 14, 1.8f, 0.5f, 0x4A4A4A);
        break;
    case IC_USER:
        GV(0, 0, 32, 20, 0x7ECCF4, 0xC8ECFF);
        GV(0, 20, 32, 12, 0x48A848, 0x2A7A2A);
        TL(16, 31, 16, 17, 1.1f, 0x2A7A20);
        TRI(16, 25, 10, 21, 16, 22, 0x3A9A30);
        for (int a = 0; a < 10; a++) { float an = a * 0.628f; C(16 + 6.5f * sinf(an), 12 - 6.5f * cosf(an), 3.2f, 0xFFD020); }
        C(16, 12, 4.6f, 0x6A3A10); C(15, 11, 1.5f, 0x8A5A30);
        break;
    case IC_SOUND:
        F(4, 12, 6, 8, 0x50586E);
        TRI(9, 12, 17, 5, 17, 27, 0x50586E); F(9, 12, 2, 8, 0x50586E);
        ARC(17, 16, 5.5f, 45, 135, 0.9f, 0x2E6ED0);
        ARC(17, 16, 10, 40, 140, 0.9f, 0x2E6ED0);
        break;
    case IC_SOLITAIRE:
        RF(4, 6, 16, 22, 2, 0x404040); RF(4.5f, 6.5f, 15, 21, 1.8f, 0x1A4AA8);
        for (int i = 0; i < 4; i++) TL(6, 9 + i * 4, 18, 13 + i * 4, 0.4f, 0x5A8AE8);
        RF(12, 3, 17, 23, 2, 0x606060); RF(12.5f, 3.5f, 16, 22, 1.8f, 0xFFFFFF);
        C(18, 14, 3.2f, 0xD42020); C(23, 14, 3.2f, 0xD42020); TRI(15, 15, 26, 15, 20.5f, 22, 0xD42020);
        break;
    case IC_BLOCKS:
        RF(2, 2, 28, 28, 3, 0x14142A);
        { static const uint32_t cc[] = { 0x30C0F0, 0xF0C030, 0xE04050, 0x50D060, 0xA060E8 };
          static const int bx[] = { 5, 10, 15, 5, 10, 20, 20, 15, 10, 20 }, by[] = { 23, 23, 23, 18, 18, 23, 18, 18, 8, 13 };
          for (int i = 0; i < 10; i++) { RF(bx[i], by[i], 4.6f, 4.6f, 0.8f, cc[i % 5]); F(bx[i] + 0.6f, by[i] + 0.6f, 3.4f, 1, 0xFFFFFF); } }
        break;
    case IC_VIEWER: case IC_IMAGEFILE:
        if (id == IC_IMAGEFILE) { page(0xD8D8D8); GV(9, 12, 15, 14, 0x7CC0FF, 0xC0E4FF); TRI(9, 26, 15, 17, 21, 26, 0x4AA040); TRI(15, 26, 20, 20, 24, 26, 0x2E8A2A); C(20, 15, 1.6f, 0xFFE860); }
        else {
            RF(2, 4, 28, 24, 2, 0x7A6040); RG(3.5f, 5.5f, 25, 21, 1, 0xF0D8A8, 0xC8A870);
            GV(6, 8, 20, 16, 0x6CB4FF, 0xC0E0FF); TRI(6, 24, 14, 13, 22, 24, 0x48A040); TRI(14, 24, 20, 17, 26, 24, 0x2E8A2A);
            C(21, 12, 2.2f, 0xFFF080);
        }
        break;
    case IC_MUSIC: case IC_SONGFILE:
        if (id == IC_SONGFILE) page(0xD8D8D8);
        else { C(16, 16, 14, 0xE87A1A); C(16, 15, 12.6f, 0xFFA040); C(13, 11, 6, 0xFFB868); }
        { uint32_t nc = id == IC_SONGFILE ? 0x3050C0 : 0xFFFFFF;
          C(12, 22.5f, 3, nc); C(21, 20.5f, 3, nc); TL(14.6f, 22.5f, 14.6f, 10, 0.8f, nc); TL(23.6f, 20.5f, 23.6f, 8, 0.8f, nc);
          TL(14.6f, 10.5f, 23.6f, 8.5f, 1.4f, nc); }
        break;
    case IC_STICKY:
        TRI(4, 4, 28, 4, 4, 28, 0xFFF070); F(4, 4, 24, 18, 0xFFF070); TRI(28, 4, 28, 22, 22, 28, 0xFFF070); F(4, 22, 18, 6, 0xFFF070);
        TRI(22, 28, 28, 22, 22, 22, 0xD8C440);
        for (int i = 0; i < 4; i++) F(8, 10 + i * 4, 15 - (i == 3 ? 6 : 0), 1, 0xA89A30);
        F(4, 4, 24, 3, 0xF4D838);
        break;
    case IC_FILE: page(0xD8D8D8); break;
    case IC_APPFILE:
        RF(4, 6, 24, 20, 2, 0x2A4A8A); GV(5, 7, 22, 4, 0x3A7AE8, 0x2A5AC8); F(5, 11, 22, 14, 0xF4F4F4);
        C(16, 18, 4, 0x3CA03C);
        break;
    case IC_SCRIPT:
        page(0xD8D8D8);
        RF(9, 12, 15, 14, 1.5f, 0x1E1A2E); TL(11.5f, 15, 14, 17.5f, 0.6f, 0x6CF08C); TL(14, 17.5f, 11.5f, 20, 0.6f, 0x6CF08C); F(15, 20, 5, 1.2f, 0xFFFFFF);
        break;
    case IC_UP:
        F(13, 12, 6, 16, 0x2E9A2E); TRI(5, 14, 27, 14, 16, 3, 0x2E9A2E);
        break;
    case IC_BACK:
        C(16, 16, 13, 0x2A8A2A); C(16, 15, 12, 0x48B848);
        TRI(7, 16, 16, 8, 16, 24, 0xFFFFFF); F(15, 13, 10, 6, 0xFFFFFF);
        break;
    case IC_NEWFOLDER:
        folder(0xFFE69A, 0xEDB84A, 0xB88A28);
        C(24, 8, 5.5f, 0xFFF4A0); for (int a = 0; a < 8; a++) { float an = a * 0.785f; TL(24, 8, 24 + 7 * sinf(an), 8 - 7 * cosf(an), 0.5f, 0xF0C020); } C(24, 8, 3, 0xFFFFC0);
        break;
    case IC_DELETE:
        TL(7, 7, 25, 25, 3, 0xC82018); TL(25, 7, 7, 25, 3, 0xC82018);
        TL(7, 7, 25, 25, 1.8f, 0xF04A38); TL(25, 7, 7, 25, 1.8f, 0xF04A38);
        break;
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
        X0 = 0; Y0 = 0; SZ = R2;
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
