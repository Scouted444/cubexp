/* Shell: boot, login, desktop, taskbar, Start menu, menus, dialogs, input */
#include "os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

/* from wm.c */
void wm_draw(void);
int  wm_mouse(int ev, int mx, int my);
void wm_scroll(int dy);
void wm_tick(void);
void wm_close_all(void);
int  wm_dragging(void);
Window *wm_hit(int mx, int my);

int mouse_x = 320, mouse_y = 240, mouse_btn;
uint64_t now_ms;
int fps;
PadState pads[4];

static uint32_t wall[SW * SH];
static int wall_idx = -1, wall_theme = -1;

enum { ST_BOOT, ST_LOGIN, ST_WELCOME, ST_DESKTOP, ST_TRANSITION, ST_OFF };
static int state = ST_BOOT;
static uint64_t state_t;
static int trans_next;              /* state after ST_TRANSITION */
static const char *trans_msg = "";
static int saver_on;
static uint64_t last_input;

/* ---------------- apps table ---------------- */
static void open_msgbox_dummy(void) {}
const AppInfo apps[APP_COUNT] = {
    { "My Computer", "computer", IC_COMPUTER, open_computer, "System information" },
    { "Notepad", "notepad", IC_NOTEPAD, open_notepad, "Write and save notes" },
    { "Calculator", "calc", IC_CALC, open_calc, "Do quick maths" },
    { "Paint", "mspaint", IC_PAINT, open_paint, "Draw pictures" },
    { "Minesweeper", "winmine", IC_MINES, open_mines, "Classic mine hunt" },
    { "Command Prompt", "cmd", IC_CMD, open_cmd, "Type commands" },
    { "Control Panel", "control", IC_CONTROL, open_control, "Change your settings" },
    { "Task Manager", "taskmgr", IC_TASKMGR, open_taskmgr, "See running programs" },
    { "Snake", "snake", IC_SNAKE, open_snake, "Eat apples, grow long" },
    { "Clock and Calendar", "timedate", IC_CLOCK, open_clock, "Date and time" },
    { "Recycle Bin", "recycle", IC_RECYCLE, open_recycle, "" },
    { "About CubeXP", "winver", IC_LOGO, open_about, "" },
    { "Help and Support", "help", IC_HELP, open_help, "" },
    { "On-Screen Keyboard", "osk", IC_KEYBOARD, open_osk, "Type with the cursor" },
    { "Run...", "run", IC_RUN, open_run, "" },
    { "Message", "msgbox", IC_INFO, open_msgbox_dummy, "" },
    { "Pong", "pong", IC_PAD, open_pong, "Bat and ball vs CPU" },
};

void app_launch(int app) {
    static const int single[] = { APP_CONTROL, APP_TASKMGR, APP_COMPUTER, APP_RECYCLE, APP_CLOCK,
                                  APP_ABOUT, APP_HELP, APP_OSK, APP_RUN, APP_CALC };
    for (unsigned i = 0; i < sizeof single / sizeof single[0]; i++)
        if (single[i] == app) {
            Window *w = win_find(app);
            if (w) { win_focus(w); return; }
        }
    if (app >= 0 && app < APP_COUNT && apps[app].open) apps[app].open();
}

int app_by_cmd(const char *cmd) {
    for (int i = 0; i < APP_COUNT; i++) {
        if (i == APP_MSGBOX) continue;
        if (!strcasecmp(cmd, apps[i].cmd) || !strcasecmp(cmd, apps[i].name)) return i;
    }
    if (!strcasecmp(cmd, "calculator")) return APP_CALC;
    if (!strcasecmp(cmd, "paint")) return APP_PAINT;
    if (!strcasecmp(cmd, "minesweeper") || !strcasecmp(cmd, "mines")) return APP_MINES;
    if (!strcasecmp(cmd, "explorer") || !strcasecmp(cmd, "mycomputer")) return APP_COMPUTER;
    if (!strcasecmp(cmd, "clock") || !strcasecmp(cmd, "time")) return APP_CLOCK;
    if (!strcasecmp(cmd, "settings") || !strcasecmp(cmd, "desk.cpl")) return APP_CONTROL;
    return -1;
}

/* ---------------- desktop icons ---------------- */
typedef struct { int app; const char *label; } DIcon;
static const DIcon dicons[] = {
    { APP_COMPUTER, "My Computer" }, { APP_RECYCLE, "Recycle Bin" }, { APP_NOTEPAD, "Notepad" },
    { APP_PAINT, "Paint" }, { APP_CMD, "Command Prompt" }, { APP_CONTROL, "Control Panel" },
    { APP_MINES, "Minesweeper" }, { APP_SNAKE, "Snake" }, { APP_PONG, "Pong" }, { APP_CALC, "Calculator" },
};
#define NDI ((int)(sizeof dicons / sizeof dicons[0]))
static int dsel = -1;
static void dicon_pos(int i, int *x, int *y) { *x = 6 + (i / 6) * 78; *y = 8 + (i % 6) * 70; }
static int dicon_hit(int mx, int my) {
    for (int i = 0; i < NDI; i++) {
        int x, y; dicon_pos(i, &x, &y);
        if (inside(mx, my, x, y, 74, 62)) return i;
    }
    return -1;
}
static void draw_desktop_icons(void) {
    for (int i = 0; i < NDI; i++) {
        int x, y; dicon_pos(i, &x, &y);
        draw_icon(apps[dicons[i].app].icon, x + 21, y + 2, 32);
        if (i == dsel) fill_a(x + 21, y + 2, 32, 32, C_SEL, 110);
        /* label, wrapped to two lines */
        const char *s = dicons[i].label;
        char l1[24], l2[24] = "";
        strncpy(l1, s, 23); l1[23] = 0;
        if (text_w(&font_ui, s) > 72) {
            const char *sp = strchr(s, ' ');
            if (sp) { int n = (int)(sp - s); memcpy(l1, s, n); l1[n] = 0; strncpy(l2, sp + 1, 23); }
        }
        int ty = y + 37;
        for (int k = 0; k < 2; k++) {
            const char *t = k ? l2 : l1;
            if (!*t) continue;
            int tw = text_w(&font_ui, t);
            int tx = x + 37 - tw / 2;
            if (i == dsel) { fill(tx - 1, ty + k * 13, tw + 2, 13, C_SEL); text(&font_ui, tx, ty + k * 13, t, 0xFFFFFF); }
            else text_shadow(&font_ui, tx, ty + k * 13, t, 0xFFFFFF);
        }
    }
}

/* ---------------- balloon ---------------- */
static char bal_title[48], bal_msg[160];
static uint64_t bal_until;
void notify(const char *t, const char *m) {
    strncpy(bal_title, t, sizeof bal_title - 1);
    strncpy(bal_msg, m, sizeof bal_msg - 1);
    bal_until = now_ms + 7000;
    sound_play(SND_NOTIFY);
}
static int tray_x;
static void draw_balloon(void) {
    if (now_ms > bal_until) return;
    int lines = 1;
    for (const char *p = bal_msg; *p; p++) if (*p == '\n') lines++;
    int w = 250, h = 34 + lines * 14, x = SW - w - 10, y = SH - TASKBAR_H - h - 14;
    round_fill(x + 3, y + 3, w, h, 6, 0x808080);
    round_fill(x, y, w, h, 6, 0x000000);
    round_fill(x + 1, y + 1, w - 2, h - 2, 6, 0xFFFFE1);
    for (int j = 0; j < 12; j++) { hline(x + w - 60 + j, y + h - 1 + j, 12 - j, 0x000000); hline(x + w - 59 + j, y + h - 2 + j, 10 - j, 0xFFFFE1); }
    draw_icon(IC_INFO, x + 8, y + 7, 16);
    text(&font_bold, x + 30, y + 8, bal_title, 0);
    rect(x + w - 20, y + 6, 13, 13, 0xA0A0A0); text(&font_ui, x + w - 17, y + 6, "x", 0x404040);
    int ly = y + 26;
    const char *p = bal_msg;
    while (*p) {
        const char *e = strchr(p, '\n');
        int n = e ? (int)(e - p) : (int)strlen(p);
        text_n(&font_ui, x + 10, ly, p, n, 0);
        ly += 14;
        p += n + (e ? 1 : 0);
    }
}

/* ---------------- popup menus ---------------- */
static MItem pm_items[12];
static int pm_n, pm_x, pm_y, pm_open, pm_ctx;
static void (*pm_cb)(int id, int ctx);
static void popup(int x, int y, const MItem *items, int n, void (*cb)(int, int), int ctx) {
    memcpy(pm_items, items, n * sizeof(MItem));
    pm_n = n; pm_cb = cb; pm_ctx = ctx; pm_open = 1;
    int h = 4;
    for (int i = 0; i < n; i++) h += items[i].label ? 20 : 7;
    if (x + 170 > SW) x = SW - 170;
    if (y + h > SH - 2) y = y - h;
    if (y < 0) y = 0;
    pm_x = x; pm_y = y;
}
static int popup_item_at(int mx, int my) {
    int y = pm_y + 2;
    for (int i = 0; i < pm_n; i++) {
        int h = pm_items[i].label ? 20 : 7;
        if (pm_items[i].label && inside(mx, my, pm_x + 2, y, 166, h)) return i;
        y += h;
    }
    return -1;
}
static void draw_popup(void) {
    if (!pm_open) return;
    int h = 4;
    for (int i = 0; i < pm_n; i++) h += pm_items[i].label ? 20 : 7;
    fill_a(pm_x + 3, pm_y + 3, 170, h, 0, 70);
    fill(pm_x, pm_y, 170, h, 0xFFFFFF);
    rect(pm_x, pm_y, 170, h, C_SHADOW);
    fill(pm_x + 1, pm_y + 1, 24, h - 2, 0xF4F2EA);
    int hv = popup_item_at(mouse_x, mouse_y);
    int y = pm_y + 2;
    for (int i = 0; i < pm_n; i++) {
        if (!pm_items[i].label) { hline(pm_x + 28, y + 3, 138, C_SHADOW); y += 7; continue; }
        if (i == hv) fill(pm_x + 2, y, 166, 20, C_SEL);
        text(&font_ui, pm_x + 30, y + 4, pm_items[i].label, i == hv ? 0xFFFFFF : 0x000000);
        y += 20;
    }
}

void shell_popup(int x, int y, const MItem *items, int n, void (*cb)(int, int), int ctx) { popup(x, y, items, n, cb, ctx); }
void shell_wallpaper_dirty(void) { wall_idx = -1; }

/* ---------------- start menu ---------------- */
static int start_open, allprog_open;
enum { MK_APP, MK_ALLPROG, MK_LOGOFF, MK_TURNOFF };
typedef struct { int x, y, w, h, kind, app, isz, bold; const char *label, *desc; } SItem;
static SItem sitems[40];
static int nsi;
#define SM_W 390
#define SM_H 430
static int sm_x(void) { return 0; }
static int sm_y(void) { return SH - TASKBAR_H - SM_H; }

static void build_start(void) {
    nsi = 0;
    int X = sm_x(), Y = sm_y();
    int y = Y + 62;
    static const int big[] = { APP_NOTEPAD, APP_CMD };
    for (int i = 0; i < 2; i++, y += 40)
        sitems[nsi++] = (SItem){ X + 4, y, 188, 38, MK_APP, big[i], 32, 1, apps[big[i]].name, apps[big[i]].desc };
    y += 6;
    static const int mid[] = { APP_PAINT, APP_MINES, APP_SNAKE, APP_CALC, APP_PONG };
    for (int i = 0; i < 5; i++, y += 36)
        sitems[nsi++] = (SItem){ X + 4, y, 188, 34, MK_APP, mid[i], 32, 0, apps[mid[i]].name, NULL };
    sitems[nsi++] = (SItem){ X + 4, Y + 352, 188, 30, MK_ALLPROG, 0, 0, 1, "All Programs", NULL };
    y = Y + 64;
    static const int right[] = { APP_COMPUTER, APP_RECYCLE, -1, APP_CONTROL, APP_TASKMGR, APP_CLOCK, APP_OSK, -1, APP_HELP, APP_ABOUT, APP_RUN };
    for (int i = 0; i < 11; i++) {
        if (right[i] < 0) { y += 9; continue; }
        sitems[nsi++] = (SItem){ X + 198, y, 188, 26, MK_APP, right[i], 24, i < 2, apps[right[i]].name, NULL };
        y += 27;
    }
    sitems[nsi++] = (SItem){ X + 150, Y + SM_H - 34, 88, 28, MK_LOGOFF, 0, 24, 0, "Log Off", NULL };
    sitems[nsi++] = (SItem){ X + 242, Y + SM_H - 34, 144, 28, MK_TURNOFF, 0, 24, 0, "Turn Off Computer", NULL };
    if (allprog_open) {
        int n = 0;
        for (int a = 0; a < APP_COUNT; a++) if (a != APP_MSGBOX) n++;
        int py = Y + 382 - n * 22;
        if (py < 2) py = 2;
        for (int a = 0; a < APP_COUNT; a++) {
            if (a == APP_MSGBOX) continue;
            sitems[nsi++] = (SItem){ X + 194, py, 198, 22, MK_APP, a, 16, 0, apps[a].name, NULL };
            py += 22;
        }
    }
}

static int sitem_at(int mx, int my) {
    for (int i = nsi - 1; i >= 0; i--)
        if (inside(mx, my, sitems[i].x, sitems[i].y, sitems[i].w, sitems[i].h)) return i;
    return -1;
}

static void draw_start(void) {
    if (!start_open) return;
    const Theme *t = TH;
    int X = sm_x(), Y = sm_y();
    build_start();
    fill_a(X + 4, Y + 4, SM_W, SM_H, 0, 80);
    round_grad(X, Y, SM_W, 58, 7, t->bar_hi, t->bar1, 1);
    fill(X, Y + 58, SM_W, SM_H - 58, t->bar2);
    hline(X, Y + 56, SM_W, 0xE39B3C); hline(X, Y + 57, SM_W, 0xF8C46C);
    fill(X + 4, Y + 58, 190, SM_H - 58 - 40, 0xFFFFFF);
    fill(X + 194, Y + 58, SM_W - 198, SM_H - 58 - 40, t->menu_side);
    vline(X + 194, Y + 58, SM_H - 98, 0x95BDEE);
    grad_v(X, Y + SM_H - 40, SM_W, 40, t->bar1, t->bar2);
    /* header */
    fill(X + 7, Y + 7, 46, 46, 0xFFFFFF);
    draw_icon(IC_USER, X + 9, Y + 9, 42);
    if (cfg.theme == 2) text(&font_big, X + 62, Y + 18, cfg.user, 0x202030);
    else text_shadow(&font_big, X + 62, Y + 18, cfg.user, 0xFFFFFF);
    hline(X + 12, Y + 145, 170, 0xD6D3CE);
    hline(X + 12, Y + 344, 170, 0xD6D3CE);
    hline(X + 204, Y + 64 + 27 * 2 + 4, 176, 0xB0C8E8);
    hline(X + 204, Y + 64 + 27 * 6 + 13, 176, 0xB0C8E8);
    int hv = sitem_at(mouse_x, mouse_y);
    int nmain = nsi;
    if (allprog_open) nmain = nsi - (APP_COUNT - 1);
    for (int pass = 0; pass < 2; pass++)
    for (int i = pass ? nmain : 0; i < (pass ? nsi : nmain); i++) {
        SItem *s = &sitems[i];
        if (pass == 1 && i == nmain) {   /* submenu frame goes over the main menu */
            int n = APP_COUNT - 1, py = s->y;
            fill_a(X + 196, py + 4, 202, n * 22 + 4, 0, 70);
            fill(X + 190, py - 2, 204, n * 22 + 4, 0xFFFFFF);
            rect(X + 190, py - 2, 204, n * 22 + 4, C_SHADOW);
            fill(X + 191, py - 1, 3, n * 22 + 2, t->bar1);
        }
        int hot = i == hv;
        uint32_t fg = 0x000000;
        if (s->kind == MK_LOGOFF || s->kind == MK_TURNOFF) {
            if (hot) fill_a(s->x, s->y, s->w, s->h, 0xFFFFFF, 60);
            draw_icon(s->kind == MK_LOGOFF ? IC_LOGOFF : IC_POWER, s->x + 2, s->y + 2, 24);
            text(&font_ui, s->x + 30, s->y + 8, s->label, cfg.theme == 2 ? 0 : 0xFFFFFF);
            continue;
        }
        if (hot) { fill(s->x, s->y, s->w, s->h, C_SEL); fg = 0xFFFFFF; }
        if (s->kind == MK_ALLPROG) {
            int tw = text_w(&font_bold, s->label);
            int tx = s->x + (s->w - tw - 24) / 2;
            text(&font_bold, tx, s->y + 8, s->label, fg);
            fill_circle(tx + tw + 14, s->y + 14, 8, 0x3C9A3C);
            for (int j = 0; j < 9; j++) vline(tx + tw + 11 + j / 2, s->y + 10 + j / 2, 9 - j / 2 * 2, 0xFFFFFF);
            continue;
        }
        draw_icon(apps[s->app].icon, s->x + 4, s->y + (s->h - s->isz) / 2, s->isz);
        const Font *f = s->bold ? &font_bold : &font_ui;
        int tx = s->x + s->isz + 10;
        if (s->desc) {
            text(f, tx, s->y + 5, s->label, fg);
            text(&font_ui, tx, s->y + 20, s->desc, hot ? 0xDDE8FF : 0x7A7A7A);
        } else text(f, tx, s->y + (s->h - 13) / 2, s->label, s->isz == 24 && !hot && !s->bold ? 0x0A246A : fg);
    }
}

static void start_click(int mx, int my) {
    build_start();
    int i = sitem_at(mx, my);
    if (i < 0) {
        if (!inside(mx, my, sm_x(), sm_y(), SM_W, SM_H)) { start_open = 0; allprog_open = 0; }
        return;
    }
    SItem *s = &sitems[i];
    if (s->kind == MK_ALLPROG) { allprog_open = !allprog_open; return; }
    start_open = 0; allprog_open = 0;
    sound_play(SND_CLICK);
    if (s->kind == MK_APP) app_launch(s->app);
    else if (s->kind == MK_LOGOFF) shell_logoff_dialog();
    else shell_shutdown_dialog();
}
void shell_open_start(void) { start_open = !start_open; allprog_open = 0; }

/* ---------------- taskbar ---------------- */
static int tb_buttons(int *idx) {
    int n = 0;
    for (int k = 0; k < nz; k++) {
        Window *w = &wins[zorder[k]];
        if (w->app == APP_OSK || w->app == APP_MSGBOX) continue;
        idx[n++] = zorder[k];
    }
    /* stable order: by window slot */
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (idx[b] < idx[a]) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
    return n;
}
static int tb_btn_w(int n) {
    int avail = tray_x - 112 - 6;
    int w = n ? avail / n : 160;
    return w > 160 ? 160 : w;
}

static void clock_str(char *buf, int n) {
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    if (cfg.clock24) {
        if (cfg.show_secs) snprintf(buf, n, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
        else snprintf(buf, n, "%02d:%02d", tm->tm_hour, tm->tm_min);
    } else {
        int h = tm->tm_hour % 12; if (!h) h = 12;
        if (cfg.show_secs) snprintf(buf, n, "%d:%02d:%02d %s", h, tm->tm_min, tm->tm_sec, tm->tm_hour < 12 ? "AM" : "PM");
        else snprintf(buf, n, "%d:%02d %s", h, tm->tm_min, tm->tm_hour < 12 ? "AM" : "PM");
    }
}

static void draw_taskbar(void) {
    const Theme *t = TH;
    int y = SH - TASKBAR_H;
    grad_v(0, y, SW, 4, t->bar_hi, t->bar1);
    grad_v(0, y + 4, SW, TASKBAR_H - 4, t->bar1, t->bar2);
    /* start button */
    int hot = inside(mouse_x, mouse_y, 0, y, 100, TASKBAR_H);
    uint32_t s1 = t->start1, s2 = t->start2;
    if (hot) { s1 = shade(s1, 115); s2 = shade(s2, 115); }
    if (start_open) { s1 = shade(s1, 80); s2 = shade(s2, 80); }
    for (int j = 0; j < TASKBAR_H; j++) {
        int in = 0;
        if (j < 12) { float dy = 12 - j - 0.5f; in = 12 - (int)sqrtf(144 - dy * dy); }
        if (j >= TASKBAR_H - 12) { float dy = j - (TASKBAR_H - 12) + 0.5f; in = 12 - (int)sqrtf(144 - dy * dy); }
        uint32_t c = j < 3 ? mix(0xA8E0A0, s1, j * 85) : mix(s1, s2, (j - 3) * 256 / (TASKBAR_H - 3));
        hline(0, y + j, 100 - in, c);
    }
    draw_icon(IC_LOGO, 8, y + 5, 20);
    text_shadow(&font_start, 33, y + 4, "start", 0xFFFFFF);
    /* tray */
    char clk[24];
    clock_str(clk, sizeof clk);
    int cw = text_w(&font_ui, clk);
    int tw = cw + 24 + 3 * 20;
    tray_x = SW - tw;
    grad_v(tray_x, y, tw, TASKBAR_H, t->tray1, t->tray2);
    vline(tray_x, y, TASKBAR_H, shade(t->tray2, 80));
    vline(tray_x + 1, y + 1, TASKBAR_H - 1, t->bar_hi);
    draw_icon(IC_SOUND, tray_x + 8, y + 7, 16);
    if (!cfg.sound) { line(tray_x + 8, y + 22, tray_x + 23, y + 7, 0xE02020); line(tray_x + 9, y + 22, tray_x + 24, y + 7, 0xE02020); }
    draw_icon(IC_KEYBOARD, tray_x + 28, y + 7, 16);
    draw_icon(IC_PAD, tray_x + 48, y + 7, 16);
    text(&font_ui, tray_x + 70, y + 8, clk, cfg.theme == 2 ? 0x000000 : 0xFFFFFF);
    /* window buttons */
    int idx[MAX_WIN];
    int n = tb_buttons(idx), bw = tb_btn_w(n);
    for (int i = 0; i < n; i++) {
        Window *w = &wins[idx[i]];
        int bx = 112 + i * (bw + 3), by = y + 3;
        int act = w == focus && !w->minimized;
        int h2 = inside(mouse_x, mouse_y, bx, by, bw, 24);
        uint32_t c = act ? t->btn_act : t->btn;
        if (h2 && !act) c = shade(c, 112);
        round_fill(bx, by, bw, 24, 3, shade(c, 70));
        round_fill(bx + 1, by + 1, bw - 2, 22, 3, c);
        if (!act) hline(bx + 3, by + 1, bw - 6, shade(c, 125));
        draw_icon(w->icon, bx + 5, by + 4, 16);
        char tbuf[48];
        strncpy(tbuf, w->title, 47); tbuf[47] = 0;
        int maxw = bw - 30;
        while (text_w(&font_ui, tbuf) > maxw && strlen(tbuf) > 3) { int L = strlen(tbuf); tbuf[L - 1] = 0; tbuf[L - 2] = '.'; tbuf[L - 3] = '.'; }
        text(&font_ui, bx + 25, by + 6, tbuf, cfg.theme == 2 ? 0x000000 : 0xFFFFFF);
    }
}

static void tb_menu_cb(int id, int ctx) {
    Window *w = ctx >= 0 ? &wins[ctx] : NULL;
    switch (id) {
    case 1: if (w) win_focus(w); break;
    case 2: if (w) win_minimize(w); break;
    case 3: if (w) { win_focus(w); if (!w->maximized) win_toggle_max(w); } break;
    case 4: if (w) win_close(w); break;
    case 5: app_launch(APP_TASKMGR); break;
    case 6: for (int i = 0; i < MAX_WIN; i++) if (wins[i].used && !wins[i].topmost) win_minimize(&wins[i]); break;
    case 7: app_launch(APP_CONTROL); break;
    }
}

static int taskbar_click(int mx, int my, int right) {
    int y = SH - TASKBAR_H;
    if (my < y) return 0;
    if (mx < 100) { if (!right) { start_open = !start_open; allprog_open = 0; sound_play(SND_CLICK); } return 1; }
    start_open = 0;
    if (mx >= tray_x) {
        if (right) return 1;
        if (mx < tray_x + 26) { cfg.sound = !cfg.sound; settings_save(); if (cfg.sound) sound_play(SND_DING); }
        else if (mx < tray_x + 46) osk_toggle();
        else if (mx < tray_x + 66) {
            int c = 0;
            for (int i = 0; i < 4; i++) c += pads[i].connected;
            char m[96];
            snprintf(m, sizeof m, "%d controller%s connected.\nPort 1 drives the cursor.", c, c == 1 ? "" : "s");
            notify("Controllers", m);
        } else app_launch(APP_CLOCK);
        return 1;
    }
    int idx[MAX_WIN];
    int n = tb_buttons(idx), bw = tb_btn_w(n);
    for (int i = 0; i < n; i++) {
        int bx = 112 + i * (bw + 3);
        if (inside(mx, my, bx, y + 3, bw, 24)) {
            Window *w = &wins[idx[i]];
            if (right) {
                static const MItem m[] = { { "Restore", 1 }, { "Minimize", 2 }, { "Maximize", 3 }, { NULL, 0 }, { "Close", 4 } };
                popup(mx, my, m, 5, tb_menu_cb, idx[i]);
            } else if (w == focus && !w->minimized) win_minimize(w);
            else win_focus(w);
            return 1;
        }
    }
    if (right) {
        static const MItem m[] = { { "Task Manager", 5 }, { "Show the Desktop", 6 }, { NULL, 0 }, { "Properties", 7 } };
        popup(mx, my, m, 4, tb_menu_cb, -1);
    }
    return 1;
}

/* ---------------- desktop context menus ---------------- */
static void desk_menu_cb(int id, int ctx) {
    switch (id) {
    case 1: dsel = -1; wall_idx = -1; break;
    case 2: notepad_open_file(NULL); break;
    case 3: app_launch(APP_CONTROL); break;
    case 4: app_launch(APP_CMD); break;
    case 10: app_launch(dicons[ctx].app); break;
    case 11: {
        char m[200];
        snprintf(m, sizeof m, "%s\n\n%s", apps[dicons[ctx].app].name,
                 apps[dicons[ctx].app].desc[0] ? apps[dicons[ctx].app].desc : "System folder");
        msgbox("Properties", m, apps[dicons[ctx].app].icon);
        break; }
    }
}

/* ---------------- shutdown / log off dialog ---------------- */
static int dlg;            /* 0 none, 1 turn off, 2 log off */
static uint64_t dlg_t;
void shell_shutdown_dialog(void) { dlg = 1; dlg_t = now_ms; start_open = 0; }
void shell_logoff_dialog(void) { dlg = 2; dlg_t = now_ms; start_open = 0; }

#define DLG_W 312
#define DLG_H 200
static void dlg_btn_rect(int i, int *x, int *y) {
    int bx = (SW - DLG_W) / 2, by = (SH - DLG_H) / 2;
    if (dlg == 1) { *x = bx + 40 + i * 90; *y = by + 70; }
    else { *x = bx + 85 + i * 100; *y = by + 70; }
}
static void draw_dialog(void) {
    if (!dlg) return;
    int amt = (int)((now_ms - dlg_t) * 256 / 1200);
    if (amt > 256) amt = 256;
    grayscale_screen(amt);
    int x = (SW - DLG_W) / 2, y = (SH - DLG_H) / 2;
    fill(x, y, DLG_W, 44, 0x0A2A8C);
    text(&font_big, x + 12, y + 12, dlg == 1 ? "Turn off computer" : "Log Off CubeXP", 0xFFFFFF);
    draw_icon(IC_LOGO, x + DLG_W - 44, y + 6, 32);
    grad_h(x, y + 44, DLG_W, 2, 0x4A6AD0, 0xF0F0FF);
    grad_v(x, y + 46, DLG_W, DLG_H - 90, 0x5A7EDC, 0x3A60D0);
    fill(x, y + DLG_H - 44, DLG_W, 44, 0x0A2A8C);
    int nb = dlg == 1 ? 3 : 2;
    static const char *l1[] = { "Stand By", "Turn Off", "Restart" };
    static const int i1[] = { IC_CLOCK, IC_POWER, IC_RESTART };
    static const char *l2[] = { "Switch User", "Log Off" };
    static const int i2[] = { IC_USER, IC_LOGOFF };
    for (int i = 0; i < nb; i++) {
        int bx, by; dlg_btn_rect(i, &bx, &by);
        int hot = inside(mouse_x, mouse_y, bx, by, 52, 70);
        if (hot) round_fill(bx - 4, by - 4, 60, 76, 5, 0x7A9AEA);
        draw_icon(dlg == 1 ? i1[i] : i2[i], bx + 6, by, 40);
        text_center(&font_ui, bx - 20, by + 50, 92, dlg == 1 ? l1[i] : l2[i], 0xFFFFFF);
    }
    int cx = x + DLG_W - 86, cy = y + DLG_H - 33;
    xp_button(cx, cy, 72, 22, "Cancel", 0, inside(mouse_x, mouse_y, cx, cy, 72, 22));
}
static void begin_transition(const char *msg, int next, int snd) {
    trans_msg = msg; trans_next = next;
    state = ST_TRANSITION; state_t = now_ms;
    if (snd >= 0) sound_play(snd);
}
void shell_restart(void) { settings_save(); wm_close_all(); begin_transition("Restarting...", ST_BOOT, SND_SHUTDOWN); }
void shell_turn_off(void) { settings_save(); wm_close_all(); begin_transition("CubeXP is shutting down...", ST_OFF, SND_SHUTDOWN); }
void shell_log_off(void) { settings_save(); wm_close_all(); begin_transition("Logging off...", ST_LOGIN, SND_SHUTDOWN); }

static void dialog_click(int mx, int my) {
    int x = (SW - DLG_W) / 2, y = (SH - DLG_H) / 2;
    int nb = dlg == 1 ? 3 : 2;
    for (int i = 0; i < nb; i++) {
        int bx, by; dlg_btn_rect(i, &bx, &by);
        if (inside(mx, my, bx - 4, by - 4, 60, 76)) {
            int d = dlg; dlg = 0;
            if (d == 1) { if (i == 0) saver_on = 1; else if (i == 1) shell_turn_off(); else shell_restart(); }
            else shell_log_off();
            return;
        }
    }
    if (inside(mx, my, x + DLG_W - 86, y + DLG_H - 33, 72, 22) || !inside(mx, my, x, y, DLG_W, DLG_H)) dlg = 0;
}

/* ---------------- boot / login / transition screens ---------------- */
static void draw_boot(void) {
    fill(0, 0, SW, SH, 0);
    draw_icon(IC_LOGO, 196, 150, 64);
    text(&font_huge, 272, 160, "CubeXP", 0xFFFFFF);
    text(&font_bold, 400, 152, OS_EDITION, 0xF59A23);
    text(&font_ui, 274, 204, "GameCube Edition", 0xA0A0A0);
    int bx = 250, by = 300;
    round_fill(bx, by, 140, 16, 4, 0xB0B0B0);
    round_fill(bx + 2, by + 2, 136, 12, 3, 0x000000);
    int ph = (int)((now_ms - state_t) / 12 % 170) - 30;
    gfx_clip(bx + 3, by + 3, 134, 10);
    for (int i = 0; i < 3; i++) {
        int px = bx + 3 + ph + i * 10;
        grad_v(px, by + 4, 8, 8, 0x7AA8FF, 0x1A4AD0);
    }
    gfx_noclip();
    text(&font_ui, 20, SH - 30, "Copyright (c) 2026 CubeXP Project", 0xFFFFFF);
    text(&font_ui, SW - 20 - text_w(&font_ui, "Homebrew software"), SH - 30, "Homebrew software", 0xFFFFFF);
}

static const char *login_users[2] = { NULL, "Guest" };
static void login_bg(void) {
    fill(0, 0, SW, 70, 0x00309C);
    hline(0, 70, SW, 0x5A7EDC);
    grad_h(0, 71, SW, 2, 0x00309C, 0xFFFFFF);
    for (int y = 73; y < SH - 70; y++) {
        for (int x = 0; x < SW; x++) {
            float dx = (x - 120) / 260.f, dy = (y - 140) / 220.f;
            float d = dx * dx + dy * dy;
            int t = d > 1 ? 256 : (int)(d * 256);
            fb[y * SW + x] = mix(0x91B1F2, 0x5A7EDC, t);
        }
    }
    grad_h(0, SH - 70, SW, 2, 0xFFFFFF, 0xF8A050);
    fill(0, SH - 68, SW, 68, 0x00309C);
    vline(SW / 2, 110, SH - 220, 0xC0D4FA);
}
static void user_tile_rect(int i, int *x, int *y) { *x = SW / 2 + 30; *y = 150 + i * 76; }

static void draw_login(void) {
    login_bg();
    int lw = text_w(&font_huge, "CubeXP");
    draw_icon(IC_LOGO, 296 - lw / 2 - 32, 160, 64);
    text_shadow(&font_huge, 296 - lw, 232, "CubeXP", 0xFFFFFF);
    text(&font_ui, 296 - text_w(&font_ui, "To begin, click your user name"), 290, "To begin, click your user name", 0xFFFFFF);
    login_users[0] = cfg.user;
    for (int i = 0; i < 2; i++) {
        int x, y; user_tile_rect(i, &x, &y);
        int hot = inside(mouse_x, mouse_y, x, y, 260, 64);
        if (hot) { round_fill(x, y, 260, 64, 8, 0x1A3EA8); round_fill(x + 2, y + 2, 256, 60, 7, 0x2A54C4); }
        fill(x + 8, y + 8, 48, 48, hot ? 0xF8C048 : 0xFFFFFF);
        if (i == 0) draw_icon(IC_USER, x + 10, y + 10, 44);
        else { fill(x + 10, y + 10, 44, 44, 0x8090A8); draw_icon(IC_PAD, x + 12, y + 14, 40); }
        text(&font_big, x + 68, y + 20, login_users[i], 0xFFFFFF);
    }
    int px = 20, py = SH - 50;
    int hot = inside(mouse_x, mouse_y, px, py, 190, 30);
    draw_icon(IC_POWER, px, py, 28);
    text(&font_bold, px + 36, py + 8, "Turn off CubeXP", hot ? 0xFFF080 : 0xFFFFFF);
    text(&font_ui, SW - 300, SH - 52, "After you log on, press START for the menu", 0xFFFFFF);
    text(&font_ui, SW - 300, SH - 36, "and X for the on-screen keyboard.", 0xFFFFFF);
}

static void login_click(int mx, int my) {
    for (int i = 0; i < 2; i++) {
        int x, y; user_tile_rect(i, &x, &y);
        if (inside(mx, my, x, y, 260, 64)) {
            if (i == 1) strcpy(cfg.user, "Guest");
            else settings_load();
            state = ST_WELCOME; state_t = now_ms;
            sound_play(SND_LOGON);
            return;
        }
    }
    if (inside(mx, my, 20, SH - 50, 190, 30)) begin_transition("CubeXP is shutting down...", ST_OFF, SND_SHUTDOWN);
}

static void draw_welcome(void) {
    login_bg();
    int a = (int)((now_ms - state_t) / 3);
    if (a > 256) a = 256;
    text(&font_huge, 140 + 2, 200 + 2, "welcome", mix(0x5A7EDC, 0x2A4A9C, a));
    text(&font_huge, 140, 200, "welcome", mix(0x5A7EDC, 0xFFFFFF, a));
    text(&font_ui, 380, 210, "Loading your personal settings...", 0xFFFFFF);
}

static void draw_transition(void) {
    login_bg();
    text_shadow(&font_big, SW / 2 - text_w(&font_big, trans_msg) / 2, 220, trans_msg, 0xFFFFFF);
    draw_icon(IC_LOGO, SW / 2 - 16, 260, 32);
}

static void draw_off(void) {
    fill(0, 0, SW, SH, 0);
    text_center(&font_big, 0, 200, SW, "It's now safe to turn off", 0xF59A23);
    text_center(&font_big, 0, 230, SW, "your GameCube.", 0xF59A23);
    text_center(&font_ui, 0, 330, SW, "Press START to restart CubeXP    -    Press Z to exit to the loader", 0x808080);
}

/* ---------------- screensaver ---------------- */
static float sv_x = 100, sv_y = 100, sv_dx = 1.6f, sv_dy = 1.1f;
static void draw_saver(void) {
    fill(0, 0, SW, SH, 0);
    static uint32_t seed = 1;
    seed = 1;
    for (int i = 0; i < 120; i++) {           /* drifting starfield */
        seed = seed * 1103515245 + 12345;
        float sx = (float)((seed >> 8) % 2000) - 1000, sy = (float)((seed >> 4) % 1500) - 750;
        float z = (float)(((seed >> 2) + now_ms / 6) % 1000) + 1;
        int px = SW / 2 + (int)(sx * 100 / (1000 - z + 60)), py = SH / 2 + (int)(sy * 100 / (1000 - z + 60));
        int b = (int)(z / 4);
        if (b > 255) b = 255;
        pset(px, py, RGB(b, b, b));
        if (z > 800) pset(px + 1, py, RGB(b, b, b));
    }
    sv_x += sv_dx; sv_y += sv_dy;
    if (sv_x < 0 || sv_x > SW - 200) sv_dx = -sv_dx;
    if (sv_y < 0 || sv_y > SH - 80) sv_dy = -sv_dy;
    draw_icon(IC_LOGO, (int)sv_x, (int)sv_y, 64);
    text(&font_huge, (int)sv_x + 70, (int)sv_y + 12, "CubeXP", 0xFFFFFF);
}

/* ---------------- input ---------------- */
static uint64_t last_click_t;
static int last_click_x, last_click_y;
static int rep_key;
static uint64_t rep_t;
static float fx = 320, fy = 240;

static void merged_pad(PadState *m) {
    memset(m, 0, sizeof *m);
    int best = 0;
    for (int i = 0; i < 4; i++) {
        if (!pads[i].connected) continue;
        m->held |= pads[i].held; m->down |= pads[i].down; m->up |= pads[i].up;
        int mag = abs(pads[i].sx) + abs(pads[i].sy);
        if (mag > best) { best = mag; m->sx = pads[i].sx; m->sy = pads[i].sy; }
        if (abs(pads[i].cy) > abs(m->cy)) m->cy = pads[i].cy;
        m->connected = 1;
    }
}

static int any_input(PadState *p) {
    return p->down || abs(p->sx) > 20 || abs(p->sy) > 20 || abs(p->cy) > 30;
}

static void move_cursor(PadState *p) {
    float maxs = 3.0f + cfg.cursor_speed * 2.2f;
    float ax = p->sx / 100.f, ay = p->sy / 100.f;
    if (fabsf(ax) < 0.12f) ax = 0;
    if (fabsf(ay) < 0.12f) ay = 0;
    if (ax > 1) ax = 1;
    if (ax < -1) ax = -1;
    if (ay > 1) ay = 1;
    if (ay < -1) ay = -1;
    fx += ax * fabsf(ax) * maxs;
    fy -= ay * fabsf(ay) * maxs;
    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    if (fx > SW - 1) fx = SW - 1;
    if (fy > SH - 1) fy = SH - 1;
    mouse_x = (int)fx; mouse_y = (int)fy;
}

void send_key(int k) {
    if (focus && focus->wants_keys && focus->key) focus->key(focus, k);
}

static void cycle_windows(void) {
    if (nz == 0) return;
    for (int k = 0; k < nz; k++) {   /* bottom-most focusable window comes to front */
        Window *w = &wins[zorder[k]];
        if (!w->nofocus && w != focus) { win_focus(w); return; }
    }
}

static void desktop_mouse(int ev, int right) {
    int mx = mouse_x, my = mouse_y;
    if (dlg) { if (ev == EV_DOWN) dialog_click(mx, my); return; }
    if (pm_open && (ev == EV_DOWN || ev == EV_RDOWN)) {
        int i = popup_item_at(mx, my);
        pm_open = 0;
        if (i >= 0) { sound_play(SND_CLICK); pm_cb(pm_items[i].id, pm_ctx); }
        return;
    }
    if (start_open && (ev == EV_DOWN || ev == EV_RDOWN) && my < SH - TASKBAR_H) {
        if (ev == EV_DOWN) start_click(mx, my);
        else start_open = 0;
        if (!start_open) { /* click outside closes only */ }
        return;
    }
    if (ev == EV_DOWN || ev == EV_RDOWN) {
        if (taskbar_click(mx, my, right)) return;
    }
    if (ev == EV_DBL) {
        if (wm_mouse(EV_DBL, mx, my)) return;
        int i = dicon_hit(mx, my);
        if (i >= 0) { sound_play(SND_CLICK); app_launch(dicons[i].app); }
        return;
    }
    if (wm_mouse(ev, mx, my)) { if (ev == EV_DOWN) dsel = -1; return; }
    if (ev == EV_DOWN) { dsel = dicon_hit(mx, my); start_open = 0; }
    if (ev == EV_RDOWN) {
        int i = dicon_hit(mx, my);
        if (i >= 0) {
            dsel = i;
            static const MItem m[] = { { "Open", 10 }, { NULL, 0 }, { "Properties", 11 } };
            popup(mx, my, m, 3, desk_menu_cb, i);
        } else {
            static const MItem m[] = { { "Refresh", 1 }, { NULL, 0 }, { "New Text Document", 2 },
                                       { "Open Command Prompt", 4 }, { NULL, 0 }, { "Properties", 3 } };
            popup(mx, my, m, 6, desk_menu_cb, -1);
        }
    }
}

static void desktop_input(PadState *p) {
    move_cursor(p);
    uint32_t d = p->down;
    if (d & B_A) {
        int dbl = now_ms - last_click_t < 450 && abs(mouse_x - last_click_x) < 6 && abs(mouse_y - last_click_y) < 6;
        last_click_t = dbl ? 0 : now_ms; last_click_x = mouse_x; last_click_y = mouse_y;
        mouse_btn = 1;
        desktop_mouse(EV_DOWN, 0);
        if (dbl && !dlg) desktop_mouse(EV_DBL, 0);
    } else if ((p->held & B_A) && mouse_btn) {
        wm_mouse(EV_DRAG, mouse_x, mouse_y);
    }
    if (p->up & B_A) { mouse_btn = 0; wm_mouse(EV_UP, mouse_x, mouse_y); }
    if (d & B_B) desktop_mouse(EV_RDOWN, 1);
    if (dlg) {
        if (d & B_START) dlg = 0;
        return;
    }
    if (d & B_START) { start_open = !start_open; allprog_open = 0; pm_open = 0; sound_play(SND_CLICK); }
    if (d & B_X) osk_toggle();
    if (d & B_Z) cycle_windows();
    /* C-stick scroll */
    static int scroll_acc;
    if (abs(p->cy) > 30) {
        scroll_acc += p->cy;
        while (scroll_acc > 300) { wm_scroll(-1); scroll_acc -= 300; }
        while (scroll_acc < -300) { wm_scroll(1); scroll_acc += 300; }
    } else scroll_acc = 0;
    /* keys */
    int text_focus = focus && focus->wants_keys && !focus->minimized;
    if (focus && focus->wants_pad && focus->pad && !focus->minimized) focus->pad(focus, d, p->held);
    else if (text_focus) {
        int k = 0;
        if (d & B_UP) k = K_UP;
        else if (d & B_DOWN) k = K_DOWN;
        else if (d & B_LEFT) k = K_LEFT;
        else if (d & B_RIGHT) k = K_RIGHT;
        else if (d & B_L) k = K_BKSP;
        else if (d & B_R) k = ' ';
        else if (d & B_Y) k = K_ENTER;
        if (k) { send_key(k); rep_key = k; rep_t = now_ms + 400; }
        uint32_t mask = rep_key == K_UP ? B_UP : rep_key == K_DOWN ? B_DOWN : rep_key == K_LEFT ? B_LEFT :
                        rep_key == K_RIGHT ? B_RIGHT : rep_key == K_BKSP ? B_L : rep_key == ' ' ? B_R : 0;
        if (rep_key && (p->held & mask)) {
            if (now_ms >= rep_t) { send_key(rep_key); rep_t = now_ms + 55; }
        } else rep_key = 0;
    } else {
        if (p->held & B_LEFT) fx -= 1;
        if (p->held & B_RIGHT) fx += 1;
        if (p->held & B_UP) fy -= 1;
        if (p->held & B_DOWN) fy += 1;
        if (d & B_Y) for (int i = 0; i < MAX_WIN; i++) if (wins[i].used && !wins[i].topmost) win_minimize(&wins[i]);
    }
}

/* ---------------- frame ---------------- */
static void enter_desktop(void) {
    state = ST_DESKTOP; state_t = now_ms;
    start_open = 0; dsel = -1; dlg = 0; pm_open = 0;
    sound_play(SND_STARTUP);
    char m[160];
    snprintf(m, sizeof m, "Hi %s! Press START for the menu,\nX for the keyboard, Z to switch windows.", cfg.user);
    bal_until = 0;
    notify("Welcome to CubeXP", m);
    if (!plat_storage_ok()) {}
}

static int shell_busy(void) {
    for (int i = 0; i < MAX_WIN; i++) if (wins[i].used && now_ms - wins[i].opened < 400) return 1;
    return 0;
}

void shell_frame(void) {
    plat_poll(pads);
    PadState p;
    merged_pad(&p);
    if (any_input(&p)) last_input = now_ms;

    switch (state) {
    case ST_BOOT:
        draw_boot();
        if (now_ms - state_t > 3800 || ((p.down & (B_A | B_START)) && now_ms - state_t > 500)) {
            state = ST_LOGIN; state_t = now_ms;
        }
        return;
    case ST_LOGIN:
        move_cursor(&p);
        if (p.down & B_A) login_click(mouse_x, mouse_y);
        if (state == ST_LOGIN) { draw_login(); draw_cursor(mouse_x, mouse_y, 0); }
        return;
    case ST_WELCOME:
        draw_welcome();
        if (now_ms - state_t > 1800) enter_desktop();
        return;
    case ST_TRANSITION:
        draw_transition();
        if (now_ms - state_t > 2600) {
            state = trans_next; state_t = now_ms;
            if (state == ST_LOGIN) strcpy(cfg.user, "Player"), settings_load();
        }
        return;
    case ST_OFF:
        draw_off();
        if (p.down & B_START) { state = ST_BOOT; state_t = now_ms; }
        if (p.down & B_Z) plat_exit();
        return;
    }

    /* desktop */
    if (saver_on) {
        if (p.down || abs(p.sx) > 40 || abs(p.sy) > 40) { saver_on = 0; last_input = now_ms; return; }
        draw_saver();
        return;
    }
    if (cfg.saver_min > 0 && now_ms - last_input > (uint64_t)cfg.saver_min * 60000 && !dlg) { saver_on = 1; return; }

    desktop_input(&p);
    if (state != ST_DESKTOP) return;
    wm_tick();

    if (wall_idx != cfg.wallpaper) { wallpaper_render(cfg.wallpaper, wall); wall_idx = cfg.wallpaper; }
    (void)wall_theme;
    memcpy(fb, wall, sizeof wall);
    draw_desktop_icons();
    wm_draw();
    draw_taskbar();
    draw_start();
    draw_popup();
    draw_balloon();
    if (now_ms < bal_until && p.down & B_A) {
        int x = SW - 260, y = SH - TASKBAR_H - 90;
        if (inside(mouse_x, mouse_y, x, y, 250, 80)) bal_until = 0;
    }
    draw_dialog();
    draw_cursor(mouse_x, mouse_y, shell_busy());
}

#ifdef HOST_BUILD
void shell_debug_warp(int x, int y) { fx = x; fy = y; mouse_x = x; mouse_y = y; }
#endif

void shell_init(void) {
    state = ST_BOOT; state_t = plat_ms();
    last_input = state_t;
}
