/* Procedurally drawn icons (designed on a 32x32 grid, scaled to any size) */
#include "gfx.h"

static int X0, Y0, SZ;
#define S(v) ((v) * SZ / 32)
#define F(x, y, w, h, c) fill(X0 + S(x), Y0 + S(y), S(w) ? S(w) : 1, S(h) ? S(h) : 1, c)
#define R(x, y, w, h, c) rect(X0 + S(x), Y0 + S(y), S(w), S(h), c)
#define GV(x, y, w, h, a, b) grad_v(X0 + S(x), Y0 + S(y), S(w), S(h), a, b)
#define GH(x, y, w, h, a, b) grad_h(X0 + S(x), Y0 + S(y), S(w), S(h), a, b)
#define C(x, y, r, c) fill_circle(X0 + S(x), Y0 + S(y), S(r), c)
#define CO(x, y, r, c) circle(X0 + S(x), Y0 + S(y), S(r), c)
#define L(a, b, c, d, col) line(X0 + S(a), Y0 + S(b), X0 + S(c), Y0 + S(d), col)
#define TL(a, b, c, d, r, col) thick_line(X0 + S(a), Y0 + S(b), X0 + S(c), Y0 + S(d), (r) * SZ / 32, col)
#define RF(x, y, w, h, r, c) round_fill(X0 + S(x), Y0 + S(y), S(w), S(h), S(r), c)
#define RG(x, y, w, h, r, a, b) round_grad(X0 + S(x), Y0 + S(y), S(w), S(h), S(r), a, b, 0)

static void glyph(const char *s, int cx, int cy, uint32_t c) {
    const Font *f = SZ >= 32 ? &font_big : &font_bold;
    int w = text_w(f, s);
    text(f, X0 + S(cx) - w / 2, Y0 + S(cy) - f->ascent * 3 / 5 - (SZ >= 32 ? 0 : 1), s, c);
}

static void monitor(uint32_t scr1, uint32_t scr2) {
    RF(3, 2, 26, 20, 2, 0x5A5A50);
    GV(4, 3, 24, 18, 0xF2F0E6, 0xC9C5B2);
    GV(6, 5, 20, 13, scr1, scr2);
    F(12, 22, 8, 3, 0x9C9886);
    GV(6, 25, 20, 4, 0xE8E5D8, 0xA9A592);
    R(6, 25, 20, 4, 0x6A6858);
}

void draw_icon(int id, int x, int y, int size) {
    X0 = x; Y0 = y; SZ = size;
    switch (id) {
    case IC_COMPUTER:
        monitor(0x6FB0FF, 0x1050C0);
        F(8, 13, 16, 4, 0x48A040);
        break;
    case IC_NOTEPAD:
        F(8, 3, 19, 27, 0x7A7A7A);
        F(7, 2, 19, 27, 0xFFFFFF); R(7, 2, 19, 27, 0x8A8A8A);
        GV(7, 2, 19, 5, 0x9CC8F0, 0x4F8CD0);
        for (int i = 0; i < 5; i++) F(10, 11 + i * 4, 13, 1, 0x9AA8C0);
        break;
    case IC_CALC:
        RF(7, 2, 19, 28, 3, 0x505860);
        GV(8, 3, 17, 26, 0xD8DCE4, 0x9DA4B0);
        GV(10, 5, 13, 6, 0xD6F2C8, 0xA8D098); R(10, 5, 13, 6, 0x506050);
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 3; c++)
                F(10 + c * 5, 13 + r * 4, 3, 3, (r == 3 && c == 2) ? 0xE06030 : 0x404850);
        break;
    case IC_PAINT:
        C(15, 17, 12, 0x8A6A3A); C(15, 17, 11, 0xF0D8A8);
        C(20, 22, 3, 0x8A6A3A); C(20, 22, 2, 0xFFFFFF);
        C(9, 13, 3, 0xE03030); C(15, 10, 3, 0x2860E0); C(22, 13, 3, 0x30B040); C(9, 21, 3, 0xF0C020);
        TL(18, 30, 29, 3, 2, 0x8A5A20); F(26, 2, 4, 5, 0xB0B0B0);
        break;
    case IC_MINES:
        GV(2, 2, 28, 28, 0xE0E0E0, 0xB0B0B0); R(2, 2, 28, 28, 0x707070);
        TL(16, 5, 16, 27, 1, 0x101010); TL(5, 16, 27, 16, 1, 0x101010);
        TL(8, 8, 24, 24, 1, 0x101010); TL(24, 8, 8, 24, 1, 0x101010);
        C(16, 16, 8, 0x101010); C(13, 13, 2, 0xFFFFFF);
        break;
    case IC_CMD:
        F(2, 4, 28, 24, 0x303030);
        GH(3, 5, 26, 5, 0x0A50D8, 0x4C8CF0);
        F(3, 10, 26, 17, 0x000000);
        F(5, 13, 2, 2, 0xC0C0C0); F(8, 13, 5, 2, 0xC0C0C0); F(14, 13, 3, 2, 0xC0C0C0);
        F(5, 18, 7, 2, 0xC0C0C0); F(13, 18, 3, 2, 0xFFFFFF);
        break;
    case IC_CONTROL:
        GV(2, 3, 28, 26, 0xE8F0FF, 0x9AB8E8); R(2, 3, 28, 26, 0x4A6AA8);
        for (int a = 0; a < 8; a++) {
            static const int dx[8] = { 0, 7, 10, 7, 0, -7, -10, -7 }, dy[8] = { -10, -7, 0, 7, 10, 7, 0, -7 };
            C(16 + dx[a], 16 + dy[a], 3, 0x6A7A90);
        }
        C(16, 16, 8, 0x8A9AB0); C(16, 16, 3, 0xE8F0FF);
        break;
    case IC_TASKMGR:
        monitor(0x101810, 0x002000);
        L(7, 15, 11, 12, 0x40FF40); L(11, 12, 14, 14, 0x40FF40); L(14, 14, 18, 7, 0x40FF40);
        L(18, 7, 21, 11, 0x40FF40); L(21, 11, 25, 9, 0x40FF40);
        break;
    case IC_SNAKE:
        F(2, 2, 28, 28, 0x204020);
        F(5, 22, 18, 4, 0x50D050); F(19, 10, 4, 14, 0x50D050); F(9, 10, 12, 4, 0x50D050);
        F(9, 6, 4, 6, 0x70F070); F(10, 7, 1, 1, 0); F(12, 7, 1, 1, 0);
        C(25, 6, 3, 0xE02020); F(25, 2, 1, 2, 0x6A4A20);
        break;
    case IC_CLOCK:
        C(16, 16, 14, 0x1A4AA0); C(16, 16, 12, 0xFFFFFF);
        for (int i = 0; i < 12; i++) {
            static const int px[12] = { 16, 21, 25, 26, 25, 21, 16, 11, 7, 6, 7, 11 };
            static const int py[12] = { 6, 7, 11, 16, 21, 25, 26, 25, 21, 16, 11, 7 };
            F(px[i], py[i], 1, 1, 0x404040);
        }
        TL(16, 16, 16, 8, 1, 0x101010); TL(16, 16, 22, 18, 1, 0x101010); C(16, 16, 1, 0xC02020);
        break;
    case IC_RECYCLE:
        GH(8, 7, 17, 22, 0xC8D4C0, 0x7C8C78);
        R(8, 7, 17, 22, 0x4A5A48);
        for (int i = 0; i < 3; i++) F(11 + i * 4, 10, 1, 17, 0x5A6A58);
        GV(6, 4, 21, 4, 0xE0E8DC, 0x8A9A88); R(6, 4, 21, 4, 0x4A5A48);
        F(13, 2, 7, 2, 0x6A7A68);
        break;
    case IC_HELP:
        C(16, 16, 14, 0x1048B8); C(16, 15, 12, 0x3A80E8);
        glyph("?", 16, 16, 0xFFFFFF);
        break;
    case IC_INFO:
        C(16, 16, 14, 0x1048B8); C(16, 15, 12, 0x3A80E8);
        glyph("i", 16, 16, 0xFFFFFF);
        break;
    case IC_WARN:
        for (int j = 0; j < 26; j++) F(16 - j / 2, 3 + j, j + 1, 1, j < 2 ? 0x806000 : 0xF8D030);
        F(3, 28, 27, 1, 0x806000);
        glyph("!", 16, 19, 0x000000);
        break;
    case IC_FOLDER:
        RF(3, 6, 12, 6, 2, 0xD8A830);
        GV(3, 9, 26, 19, 0xFFE490, 0xE8B840); R(3, 9, 26, 19, 0xB08020);
        break;
    case IC_LOGO: {
        /* isometric cube: top orange, left green, right blue */
        for (int j = 0; j <= 8; j++) {          /* top diamond */
            F(16 - j * 3 / 2 * 1, 3 + j, j * 3, 1, 0xF59A23);
            F(16 - (8 - j) * 3 / 2, 11 + j, (8 - j) * 3, 1, 0xF59A23);
        }
        for (int i = 0; i < 12; i++) {           /* left / right faces */
            F(4 + i, 11 + i / 2, 1, 14, mix(0x3DB54A, 0x2A8A35, i * 20));
            F(16 + i, 17 - i / 2, 1, 14, mix(0x2F7BE6, 0x1C55B0, i * 20));
        }
        break; }
    case IC_DRIVE:
        GV(3, 10, 26, 13, 0xE4E4E4, 0x9A9A9A); R(3, 10, 26, 13, 0x505050);
        F(6, 18, 12, 1, 0x606060); F(23, 17, 3, 2, 0x30E030);
        break;
    case IC_DISC:
        C(16, 16, 14, 0x8A8A9A); C(16, 16, 13, 0xD8DCE8);
        C(16, 16, 9, 0xB8C8F0); C(16, 16, 6, 0xE8D0F0); C(16, 16, 3, 0x8A8A9A); C(16, 16, 2, 0xFFFFFF);
        break;
    case IC_PAD:
        RF(3, 9, 26, 15, 6, 0x3A2E80); RF(4, 10, 24, 12, 5, 0x5848B0);
        F(8, 14, 6, 2, 0xD0D0E0); F(10, 12, 2, 6, 0xD0D0E0);
        C(22, 15, 3, 0x30C060); C(26, 12, 1, 0xE04040); C(18, 18, 1, 0xF0C020);
        break;
    case IC_POWER:
        RG(2, 2, 28, 28, 5, 0xF08060, 0xC03010);
        CO(16, 17, 8, 0xFFFFFF); CO(16, 17, 7, 0xFFFFFF);
        F(14, 6, 5, 6, 0xD04A28); F(15, 5, 3, 11, 0xFFFFFF);
        break;
    case IC_RESTART:
        RG(2, 2, 28, 28, 5, 0x80D070, 0x2E8A20);
        CO(16, 16, 8, 0xFFFFFF); CO(16, 16, 7, 0xFFFFFF);
        F(17, 6, 8, 8, 0x58B048);
        for (int j = 0; j < 5; j++) F(19, 6 + j, 6 - j, 1, 0xFFFFFF);
        break;
    case IC_LOGOFF:
        RG(2, 2, 28, 28, 5, 0xFFD060, 0xD88A10);
        C(12, 16, 5, 0xFFFFFF); C(12, 16, 2, 0xE0A020);
        TL(16, 16, 26, 16, 1, 0xFFFFFF); F(22, 16, 2, 5, 0xFFFFFF);
        break;
    case IC_RUN:
        F(3, 5, 26, 22, 0x1A4AA0); F(4, 9, 24, 17, 0xFFFFFF);
        GH(4, 6, 24, 3, 0x2A6AE0, 0x8AB8F8);
        TL(9, 22, 20, 13, 2, 0x30A030); F(15, 12, 7, 2, 0x30A030); F(20, 12, 2, 7, 0x30A030);
        break;
    case IC_KEYBOARD:
        RF(2, 9, 28, 15, 2, 0x606060); GV(3, 10, 26, 13, 0xF0F0F0, 0xC0C0C0);
        for (int r = 0; r < 3; r++) for (int c = 0; c < 7; c++) F(5 + c * 3 + r, 12 + r * 3, 2, 2, 0x505050);
        F(9, 21, 13, 1, 0x505050);
        break;
    case IC_USER:
        GV(0, 0, 32, 32, 0x7EC8F0, 0x2E8A30);
        F(0, 20, 32, 12, 0x3EA040);
        TL(16, 30, 16, 16, 1, 0x2A7A20);
        for (int a = 0; a < 8; a++) {
            static const int dx[8] = { 0, 5, 7, 5, 0, -5, -7, -5 }, dy[8] = { -7, -5, 0, 5, 7, 5, 0, -5 };
            C(16 + dx[a], 12 + dy[a], 3, 0xFFD020);
        }
        C(16, 12, 4, 0x6A3A10);
        R(0, 0, 32, 32, 0xFFFFFF);
        break;
    case IC_SOUND:
        F(5, 12, 6, 8, 0x505870);
        for (int j = 0; j < 10; j++) F(11, 11 - j / 2 + 0, 1 + j, 1, 0);
        for (int j = 0; j < 14; j++) F(11, 9 + j, 8 - (j < 7 ? 7 - j : j - 6) / 1, 1, 0x505870);
        CO(18, 16, 6, 0x3070D0); CO(18, 16, 10, 0x3070D0);
        F(5, 2, 1, 1, C_FACE);
        break;
    }
}

/* ---- mouse pointer ---- */
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
