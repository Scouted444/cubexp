/* Small XP-style widget painters shared by the apps */
#include "os.h"
#include <string.h>

void ui_panel_sunken(int x, int y, int w, int h, uint32_t bg) {
    fill(x, y, w, h, bg);
    rect(x, y, w, h, 0x7F9DB9);
}

void ui_menubar(int x, int y, int w, const char **items, int n) {
    fill(x, y, w, 19, C_FACE);
    hline(x, y + 19, w, 0xFFFFFF);
    int px = x + 4;
    for (int i = 0; i < n; i++) {
        int tw = text_w(&font_ui, items[i]);
        if (inside(mouse_x, mouse_y, px - 3, y + 1, tw + 8, 17)) fill(px - 3, y + 1, tw + 8, 17, C_SEL);
        text(&font_ui, px + 1, y + 3, items[i], inside(mouse_x, mouse_y, px - 3, y + 1, tw + 8, 17) ? 0xFFFFFF : 0);
        px += tw + 12;
    }
}
int ui_menubar_hit(int mx, int my, const char **items, int n) {
    if (my < 0 || my >= 19) return -1;
    int px = 4;
    for (int i = 0; i < n; i++) {
        int tw = text_w(&font_ui, items[i]);
        if (mx >= px - 3 && mx < px + tw + 5) return i;
        px += tw + 12;
    }
    return -1;
}

void ui_check(int x, int y, const char *label, int on) {
    fill(x, y, 13, 13, 0xFFFFFF);
    rect(x, y, 13, 13, 0x1C5180);
    if (on) for (int i = 0; i < 3; i++) { line(x + 3, y + 5 + i, x + 5, y + 7 + i, 0x21A121); line(x + 5, y + 7 + i, x + 9, y + 3 + i, 0x21A121); }
    text(&font_ui, x + 19, y, label, 0);
}
void ui_radio(int x, int y, const char *label, int on) {
    fill_circle(x + 6, y + 6, 6, 0x1C5180);
    fill_circle(x + 6, y + 6, 5, 0xFFFFFF);
    if (on) fill_circle(x + 6, y + 6, 3, 0x21A121);
    text(&font_ui, x + 19, y, label, 0);
}
void ui_groupbox(int x, int y, int w, int h, const char *label) {
    rect(x, y + 6, w, h - 6, 0xD0D0BF);
    int tw = text_w(&font_ui, label);
    fill(x + 6, y, tw + 6, 13, C_FACE);
    text(&font_ui, x + 9, y, label, 0x0046D5);
}
void ui_scrollbar(int x, int y, int h, int total, int visible, int pos) {
    fill(x, y, 16, h, 0xF4F4F0);
    xp_button(x, y, 16, 16, NULL, 0, 0);
    xp_button(x, y + h - 16, 16, 16, NULL, 0, 0);
    for (int j = 0; j < 4; j++) { hline(x + 8 - j, y + 6 + j, 1 + 2 * j, 0x4D6185); hline(x + 8 - j, y + h - 6 - j, 1 + 2 * j, 0x4D6185); }
    if (total > visible && total > 0) {
        int track = h - 32;
        int th = track * visible / total;
        if (th < 12) th = 12;
        int ty = y + 16 + (track - th) * pos / (total - visible > 0 ? total - visible : 1);
        round_fill(x + 1, ty, 14, th, 2, 0xB9CCF8);
        rect(x + 1, ty, 14, th, 0x7C9FE8);
    }
}
void ui_progress(int x, int y, int w, int h, int pct) {
    round_fill(x, y, w, h, 3, 0x686868);
    fill(x + 1, y + 1, w - 2, h - 2, 0xFFFFFF);
    int n = (w - 6) * pct / 100;
    for (int i = 0; i + 6 <= n; i += 8) grad_v(x + 3 + i, y + 3, 6, h - 6, 0x8CE08C, 0x2AA02A);
}
void ui_tab(int x, int y, const char **tabs, int n, int sel) {
    int px = x;
    for (int i = 0; i < n; i++) {
        int tw = text_w(&font_ui, tabs[i]) + 16;
        int up = i == sel ? 2 : 0;
        fill(px, y - up, tw, 20 + up, i == sel ? 0xFCFCFE : 0xF0F0EA);
        rect(px, y - up, tw, 21 + up, 0x919B9C);
        if (i == sel) { hline(px + 1, y - up, tw - 2, 0xFFC83C); hline(px + 1, y - up + 1, tw - 2, 0xF8B330); hline(px + 1, y + 20, tw - 2, 0xFCFCFE); }
        text(&font_ui, px + 8, y + 4 - up, tabs[i], 0);
        px += tw;
    }
}
int ui_tab_hit(int mx, int my, int x, int y, const char **tabs, int n) {
    int px = x;
    for (int i = 0; i < n; i++) {
        int tw = text_w(&font_ui, tabs[i]) + 16;
        if (inside(mx, my, px, y - 2, tw, 22)) return i;
        px += tw;
    }
    return -1;
}
void ui_button(int x, int y, int w, int h, const char *label, Window *win) {
    int mx, my;
    int hot = 0;
    if (win == focus || !win) {
        (void)mx; (void)my;
        hot = inside(mouse_x, mouse_y, x, y, w, h);
    }
    xp_button(x, y, w, h, label, hot && mouse_btn, hot);
}
