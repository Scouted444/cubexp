/* Control Panel (Display / Appearance / Screen Saver / Mouse & Clock / Sounds & User) */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct { int tab; Settings saved; uint32_t *prev; int prev_idx; } CP;
static const char *tabs[] = { "Desktop", "Appearance", "Screen Saver", "Mouse & Clock", "Sounds & User" };
static const int saver_opts[] = { 0, 1, 3, 5, 10 };

static void draw_monitor(CP *d, int x, int y) {
    round_fill(x, y, 180, 140, 6, 0x2A2A2A);
    round_fill(x + 2, y + 2, 176, 136, 5, 0x4A4A4A);
    if (d->prev && d->prev_idx != cfg.wallpaper) { wallpaper_render(cfg.wallpaper, d->prev); d->prev_idx = cfg.wallpaper; }
    if (d->prev)
        for (int j = 0; j < 120; j++)
            for (int i = 0; i < 160; i++) pset(x + 10 + i, y + 10 + j, d->prev[(j * 4) * SW + i * 4]);
    fill(x + 10, y + 124, 160, 6, TH->bar1);
    fill(x + 10, y + 124, 22, 6, TH->start1);
    fill(x + 70, y + 140, 40, 8, 0x8A8A8A);
    fill(x + 50, y + 148, 80, 6, 0xA0A0A0);
}

static void cp_draw(Window *w, int x, int y, int cw, int ch) {
    CP *d = w->data;
    int ty = y + 10;
    fill(x + 8, ty + 20, cw - 16, ch - 70, 0xFCFCFE);
    rect(x + 8, ty + 20, cw - 16, ch - 70, 0x919B9C);
    ui_tab(x + 10, ty, tabs, 5, d->tab);
    int px = x + 20, py = ty + 34;
    char b[64];
    switch (d->tab) {
    case 0:
        draw_monitor(d, x + (cw - 180) / 2, py);
        text(&font_ui, px, py + 166, "Background:", 0);
        ui_panel_sunken(px, py + 182, cw - 40, 4 * 18 + 4, 0xFFFFFF);
        for (int i = 0; i < wallpaper_count; i++) {
            int col = i / 4, row = i % 4;
            int ix = px + 2 + col * ((cw - 44) / 2), iy = py + 184 + row * 18;
            if (i == cfg.wallpaper) fill(ix, iy, (cw - 44) / 2, 18, C_SEL);
            draw_icon(IC_PAINT, ix + 2, iy + 1, 16);
            text(&font_ui, ix + 22, iy + 3, wallpaper_names[i], i == cfg.wallpaper ? 0xFFFFFF : 0);
        }
        break;
    case 1:
        ui_groupbox(px - 4, py, cw - 32, 110, "Windows and buttons");
        for (int i = 0; i < 3; i++) ui_radio(px + 10, py + 22 + i * 26, themes[i].name, cfg.theme == i);
        /* preview title bar */
        {
            int bx = px + 180, by = py + 26;
            const Theme *t = TH;
            round_grad(bx, by, 150, 22, 6, t->t1, t->t3, 1);
            text(&font_title, bx + 8, by + 5, "Active Window", cfg.theme == 2 ? 0 : 0xFFFFFF);
            fill(bx, by + 22, 150, 50, C_FACE);
            rect(bx, by + 22, 150, 50, t->frame);
            xp_button(bx + 45, by + 40, 60, 22, "OK", 0, 0);
        }
        text(&font_ui, px, py + 126, "Tip: Silver goes great with the Starfield background.", 0x606060);
        break;
    case 2:
        round_fill(x + (cw - 180) / 2, py, 180, 110, 6, 0x2A2A2A);
        fill(x + (cw - 180) / 2 + 10, py + 10, 160, 90, 0);
        draw_icon(IC_LOGO, x + cw / 2 - 30 + (int)(now_ms / 40 % 40), py + 30 + (int)(now_ms / 60 % 20), 24);
        ui_groupbox(px - 4, py + 124, cw - 32, 96, "Start the starfield screen saver after");
        for (int i = 0; i < 5; i++) {
            if (saver_opts[i]) snprintf(b, sizeof b, "%d minute%s", saver_opts[i], saver_opts[i] > 1 ? "s" : "");
            else strcpy(b, "(None)");
            ui_radio(px + 10 + (i % 3) * 110, py + 146 + (i / 3) * 26, b, cfg.saver_min == saver_opts[i]);
        }
        break;
    case 3:
        ui_groupbox(px - 4, py, cw - 32, 80, "Pointer speed");
        text(&font_ui, px + 10, py + 30, "Slow", 0);
        text(&font_ui, px + 272, py + 30, "Fast", 0);
        hline(px + 50, py + 36, 210, 0x808080);
        for (int i = 0; i < 5; i++) vline(px + 50 + i * 52, py + 42, 5, 0x404040);
        round_fill(px + 45 + (cfg.cursor_speed - 1) * 52, py + 26, 11, 20, 2, 0x2A6AD8);
        text(&font_ui, px + 10, py + 56, "Click a tick mark to choose a speed.", 0x606060);
        ui_groupbox(px - 4, py + 96, cw - 32, 80, "Clock");
        ui_check(px + 10, py + 120, "Use 24-hour clock", cfg.clock24);
        ui_check(px + 10, py + 144, "Show seconds on the taskbar", cfg.show_secs);
        break;
    case 4:
        ui_groupbox(px - 4, py, cw - 32, 80, "Sounds");
        ui_check(px + 10, py + 24, "Play system sounds", cfg.sound);
        ui_button(px + 10, py + 46, 110, 23, "Play startup", w);
        ui_button(px + 130, py + 46, 110, 23, "Play ding", w);
        ui_groupbox(px - 4, py + 96, cw - 32, 90, "User account");
        fill(px + 8, py + 118, 52, 52, 0xFFFFFF);
        draw_icon(IC_USER, px + 10, py + 120, 48);
        text(&font_big, px + 72, py + 120, cfg.user, 0x0A246A);
        snprintf(b, sizeof b, "Saved to: %s", plat_storage_ok() ? plat_storage_name() : "nowhere (no SD card)");
        text(&font_ui, px + 72, py + 146, b, 0x606060);
        ui_button(px + 220, py + 146, 100, 23, "Change name...", w);
        break;
    }
    ui_button(x + cw - 252, y + ch - 34, 75, 23, "OK", w);
    ui_button(x + cw - 170, y + ch - 34, 75, 23, "Cancel", w);
    ui_button(x + cw - 88, y + ch - 34, 75, 23, "Apply", w);
}

static void rename_cb(const char *name, int ctx) {
    (void)ctx;
    if (!*name) return;
    if (!strcasecmp(name, "guest")) { msgbox("User account", "That name is reserved.", IC_WARN); return; }
    strncpy(cfg.user, name, sizeof cfg.user - 1);
    cfg.user[sizeof cfg.user - 1] = 0;
    settings_save();
}

static void cp_mouse(Window *w, int ev, int mx, int my) {
    CP *d = w->data;
    if (ev != EV_DOWN) return;
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    int t = ui_tab_hit(mx, my, 10, 10, tabs, 5);
    if (t >= 0) { d->tab = t; return; }
    if (inside(mx, my, cw - 252, ch - 34, 75, 23)) { settings_save(); win_close(w); return; }
    if (inside(mx, my, cw - 170, ch - 34, 75, 23)) {
        int wp = cfg.wallpaper;
        cfg = d->saved;
        if (wp != cfg.wallpaper) shell_wallpaper_dirty();
        win_close(w);
        return;
    }
    if (inside(mx, my, cw - 88, ch - 34, 75, 23)) { settings_save(); d->saved = cfg; sound_play(SND_DING); return; }
    int px = 20, py = 44;
    switch (d->tab) {
    case 0:
        for (int i = 0; i < wallpaper_count; i++) {
            int col = i / 4, row = i % 4;
            int ix = px + 2 + col * ((cw - 44) / 2), iy = py + 184 + row * 18;
            if (inside(mx, my, ix, iy, (cw - 44) / 2, 18)) { cfg.wallpaper = i; shell_wallpaper_dirty(); }
        }
        break;
    case 1:
        for (int i = 0; i < 3; i++) if (inside(mx, my, px + 10, py + 20 + i * 26, 150, 18)) cfg.theme = i;
        break;
    case 2:
        for (int i = 0; i < 5; i++) if (inside(mx, my, px + 10 + (i % 3) * 110, py + 144 + (i / 3) * 26, 100, 18)) cfg.saver_min = saver_opts[i];
        break;
    case 3:
        for (int i = 0; i < 5; i++) if (inside(mx, my, px + 40 + i * 52, py + 22, 22, 30)) cfg.cursor_speed = i + 1;
        if (inside(mx, my, px + 10, py + 118, 200, 18)) cfg.clock24 = !cfg.clock24;
        if (inside(mx, my, px + 10, py + 142, 220, 18)) cfg.show_secs = !cfg.show_secs;
        break;
    case 4:
        if (inside(mx, my, px + 10, py + 22, 200, 18)) cfg.sound = !cfg.sound;
        if (inside(mx, my, px + 10, py + 46, 110, 23)) sound_play(SND_STARTUP);
        if (inside(mx, my, px + 130, py + 46, 110, 23)) sound_play(SND_DING);
        if (inside(mx, my, px + 220, py + 146, 100, 23)) prompt_box("Change name", "Type a new user name:", cfg.user, rename_cb, 0);
        break;
    }
}
static void cp_close(Window *w) { CP *d = w->data; free(d->prev); d->prev = NULL; }

void open_control(void) {
    Window *w = win_new("Control Panel - Display Properties", IC_CONTROL, 440, 350);
    if (!w) return;
    CP *d = calloc(1, sizeof(CP));
    if (!d) { win_close(w); return; }
    d->saved = cfg;
    d->prev = malloc(sizeof(uint32_t) * SW * SH);
    d->prev_idx = -1;
    w->app = APP_CONTROL; w->no_max = 1; w->data = d;
    w->draw = cp_draw; w->mouse = cp_mouse; w->on_close = cp_close;
}
