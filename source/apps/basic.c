/* Message boxes, input prompt, file picker, Run, About, Help, Recycle Bin, Clock */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#ifdef CUBELINUX
#include <sys/utsname.h>
#endif

/* ---------- single line text field ---------- */
void tf_set(TField *t, const char *s) { strncpy(t->s, s, 63); t->s[63] = 0; t->len = t->cur = strlen(t->s); }
void tf_key(TField *t, int k) {
    if (k == K_BKSP) { if (t->cur > 0) { memmove(t->s + t->cur - 1, t->s + t->cur, t->len - t->cur + 1); t->cur--; t->len--; } }
    else if (k == K_DEL) { if (t->cur < t->len) { memmove(t->s + t->cur, t->s + t->cur + 1, t->len - t->cur); t->len--; } }
    else if (k == K_LEFT) { if (t->cur > 0) t->cur--; }
    else if (k == K_RIGHT) { if (t->cur < t->len) t->cur++; }
    else if (k == K_HOME) t->cur = 0;
    else if (k == K_END) t->cur = t->len;
    else if (k >= 32 && k < 127 && t->len < 62) {
        memmove(t->s + t->cur + 1, t->s + t->cur, t->len - t->cur + 1);
        t->s[t->cur++] = (char)k; t->len++;
    }
}
void tf_draw(TField *t, int x, int y, int w, int active) {
    fill(x, y, w, 21, 0xFFFFFF);
    rect(x, y, w, 21, 0x7F9DB9);
    gfx_clip(x + 2, y, w - 4, 21);
    text(&font_ui, x + 4, y + 4, t->s, 0);
    if (active && (now_ms / 500) % 2 == 0) vline(x + 4 + text_wn(&font_ui, t->s, t->cur), y + 3, 15, 0);
    gfx_noclip();
}

/* ---------- message box ---------- */
typedef struct { char msg[320]; int icon, lines, okx, oky; } MsgData;
static void msg_draw(Window *w, int x, int y, int cw, int ch) {
    MsgData *d = w->data;
    (void)ch;
    draw_icon(d->icon, x + 14, y + 14, 32);
    int ly = y + 18;
    const char *p = d->msg;
    while (*p) {
        const char *e = strchr(p, '\n');
        int n = e ? (int)(e - p) : (int)strlen(p);
        text_n(&font_ui, x + 60, ly, p, n, 0);
        ly += 14;
        p += n + (e ? 1 : 0);
    }
    d->okx = (cw - 75) / 2; d->oky = ch - 32;
    ui_button(x + d->okx, y + d->oky, 75, 23, "OK", w);
}
static void msg_mouse(Window *w, int ev, int mx, int my) {
    MsgData *d = w->data;
    if (ev == EV_DOWN && inside(mx, my, d->okx, d->oky, 75, 23)) win_close(w);
}
static void msg_key(Window *w, int k) { if (k == K_ENTER || k == ' ') win_close(w); }

void msgbox(const char *title, const char *msg, int icon) {
    MsgData *d = calloc(1, sizeof *d);
    if (!d) return;
    strncpy(d->msg, msg, sizeof d->msg - 1);
    d->icon = icon;
    int lines = 1, maxw = 0;
    const char *p = d->msg;
    while (*p) {
        const char *e = strchr(p, '\n');
        int n = e ? (int)(e - p) : (int)strlen(p);
        int tw = text_wn(&font_ui, p, n);
        if (tw > maxw) maxw = tw;
        p += n + (e ? 1 : 0);
        if (e) lines++;
    }
    int cw = maxw + 84, chh = lines * 14 + 72;
    if (cw < 240) cw = 240;
    if (chh < 100) chh = 100;
    Window *w = win_new(title, icon == IC_WARN ? IC_WARN : IC_INFO, cw, chh);
    if (!w) { free(d); return; }
    w->app = APP_MSGBOX; w->no_max = 1; w->data = d; w->wants_keys = 1;
    w->draw = msg_draw; w->mouse = msg_mouse; w->key = msg_key;
    win_center(w);
    sound_play(icon == IC_WARN ? SND_ERROR : SND_DING);
}

/* ---------- prompt box ---------- */
typedef struct { TField f; char label[80]; void (*cb)(const char *, int); int ctx; } PromptData;
static void pr_draw(Window *w, int x, int y, int cw, int ch) {
    PromptData *d = w->data;
    text(&font_ui, x + 12, y + 12, d->label, 0);
    tf_draw(&d->f, x + 12, y + 32, cw - 24, w == focus);
    ui_button(x + cw - 170, y + ch - 32, 75, 23, "OK", w);
    ui_button(x + cw - 87, y + ch - 32, 75, 23, "Cancel", w);
}
static void pr_ok(Window *w) {
    PromptData *d = w->data;
    void (*cb)(const char *, int) = d->cb;
    char buf[64];
    strcpy(buf, d->f.s);
    int ctx = d->ctx;
    win_close(w);
    if (cb) cb(buf, ctx);
}
static void pr_mouse(Window *w, int ev, int mx, int my) {
    if (ev != EV_DOWN) return;
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    if (inside(mx, my, cw - 170, ch - 32, 75, 23)) pr_ok(w);
    else if (inside(mx, my, cw - 87, ch - 32, 75, 23)) win_close(w);
    else if (inside(mx, my, 12, 32, cw - 24, 21)) { if (!osk_visible()) osk_toggle(); }
}
static void pr_key(Window *w, int k) {
    PromptData *d = w->data;
    if (k == K_ENTER) pr_ok(w);
    else if (k == K_ESC) win_close(w);
    else tf_key(&d->f, k);
}
void prompt_box(const char *title, const char *label, const char *initial, void (*cb)(const char *, int), int ctx) {
    PromptData *d = calloc(1, sizeof *d);
    if (!d) return;
    tf_set(&d->f, initial ? initial : "");
    strncpy(d->label, label, sizeof d->label - 1);
    d->cb = cb; d->ctx = ctx;
    Window *w = win_new(title, IC_NOTEPAD, 300, 100);
    if (!w) { free(d); return; }
    w->app = APP_MSGBOX; w->no_max = 1; w->data = d; w->wants_keys = 1;
    w->draw = pr_draw; w->mouse = pr_mouse; w->key = pr_key;
    win_center(w);
    w->y = 70;
    if (!osk_visible()) osk_toggle();
}

/* ---------- Run ---------- */
typedef struct { TField f; } RunData;
static void run_draw(Window *w, int x, int y, int cw, int ch) {
    RunData *d = w->data;
    draw_icon(IC_RUN, x + 12, y + 12, 32);
    text(&font_ui, x + 56, y + 12, "Type the name of a program and " OS_NAME, 0);
    text(&font_ui, x + 56, y + 26, "will open it for you. (Try: notepad, cmd, snake)", 0);
    text(&font_ui, x + 12, y + 60, "Open:", 0);
    tf_draw(&d->f, x + 56, y + 56, cw - 68, w == focus);
    ui_button(x + cw - 252, y + ch - 32, 75, 23, "OK", w);
    ui_button(x + cw - 170, y + ch - 32, 75, 23, "Cancel", w);
    ui_button(x + cw - 88, y + ch - 32, 75, 23, "Help", w);
}
static void run_go(Window *w) {
    RunData *d = w->data;
    char cmd[64];
    strcpy(cmd, d->f.s);
    char *s = cmd;
    while (*s == ' ') s++;
    char *e = s + strlen(s);
    while (e > s && e[-1] == ' ') *--e = 0;
    if (!*s) return;
    int a = app_by_cmd(s);
    win_close(w);
    if (a >= 0) app_launch(a);
    else {
        char m[160];
        snprintf(m, sizeof m, OS_NAME " cannot find '%s'. Make sure you typed\nthe name correctly, and then try again.", s);
        msgbox(s, m, IC_WARN);
    }
}
static void run_mouse(Window *w, int ev, int mx, int my) {
    if (ev != EV_DOWN) return;
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    if (inside(mx, my, cw - 252, ch - 32, 75, 23)) run_go(w);
    else if (inside(mx, my, cw - 170, ch - 32, 75, 23)) win_close(w);
    else if (inside(mx, my, cw - 88, ch - 32, 75, 23)) app_launch(APP_HELP);
    else if (inside(mx, my, 56, 56, cw - 68, 21) && !osk_visible()) osk_toggle();
}
static void run_key(Window *w, int k) {
    if (k == K_ENTER) run_go(w);
    else tf_key(&((RunData *)w->data)->f, k);
}
void open_run(void) {
    Window *w = win_new("Run", IC_RUN, 340, 130);
    if (!w) return;
    w->app = APP_RUN; w->no_max = 1; w->wants_keys = 1;
    w->data = calloc(1, sizeof(RunData));
    w->draw = run_draw; w->mouse = run_mouse; w->key = run_key;
    w->x = 8; w->y = 60;
    if (!osk_visible()) osk_toggle();
}

/* ---------- About ---------- */
static void about_draw(Window *w, int x, int y, int cw, int ch) {
    grad_h(x, y, cw, 70, 0x0A2A8C, 0x5A7EDC);
    draw_icon(IC_LOGO, x + 16, y + 11, 48);
    text(&font_huge, x + 76, y + 14, OS_NAME, 0xFFFFFF);
    text(&font_bold, x + 210, y + 18, OS_EDITION, 0xF59A23);
    hline(x, y + 70, cw, 0xF59A23);
    int ty = y + 86;
    text(&font_ui, x + 20, ty, OS_NAME " " OS_EDITION, 0); ty += 15;
#ifdef CUBELINUX
    { struct utsname u; char kb[120]; uname(&u); snprintf(kb, sizeof kb, "Linux %s (%s) - GameCube Edition", u.release, u.machine);
      text(&font_ui, x + 20, ty, kb, 0); ty += 15; }
    text(&font_ui, x + 20, ty, "Real Linux for the Nintendo GameCube, with the CubeXP desktop.", 0); ty += 15;
#else
    text(&font_ui, x + 20, ty, OS_VERSION " - GameCube Edition", 0); ty += 15;
    text(&font_ui, x + 20, ty, "A Windows XP-style shell for the Nintendo GameCube.", 0); ty += 15;
#endif
    text(&font_ui, x + 20, ty, "Built with devkitPPC and libogc. Fonts: DejaVu.", 0); ty += 24;
    hline(x + 20, ty, cw - 40, C_SHADOW); ty += 10;
    char b[96];
    snprintf(b, sizeof b, "This product is licensed to:  %s", cfg.user);
    text(&font_ui, x + 20, ty, b, 0); ty += 18;
    snprintf(b, sizeof b, "Physical memory available to CubeXP: %u KB", (unsigned)(plat_mem_free() / 1024));
    text(&font_ui, x + 20, ty, b, 0);
    ui_button(x + cw - 90, y + ch - 32, 75, 23, "OK", w);
}
static void about_mouse(Window *w, int ev, int mx, int my) {
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    if (ev == EV_DOWN && inside(mx, my, cw - 90, ch - 32, 75, 23)) win_close(w);
}
void open_about(void) {
    Window *w = win_new("About " OS_NAME, IC_LOGO, 400, 270);
    if (!w) return;
    w->app = APP_ABOUT; w->no_max = 1;
    w->draw = about_draw; w->mouse = about_mouse;
    win_center(w);
}

/* ---------- Help ---------- */
static const char *help_lines[] = {
    "#Welcome to CubeXP Help and Support",
    "",
    "#Controller",
    "Control Stick ........ move the mouse pointer",
    "A .................... click (double-click to open)",
    "B .................... right-click (menus)",
    "START ................ Start menu",
    "X .................... On-Screen Keyboard",
    "Z .................... window switcher (press again to cycle)",
    "Y .................... show the desktop (Enter when typing)",
    "C-Stick .............. scroll",
    "D-Pad ................ arrow keys / games / nudge pointer",
    "L / R ................ Backspace / Space when typing",
    "",
    "#Files",
    "Everything lives on Local Disk (C:), which CubeXP saves",
    "to your SD card. Open Windows Explorer from the taskbar",
    "or My Documents. Right-click files to rename, delete,",
    "copy and paste. Deleted files go to the Recycle Bin.",
    "Files you put in /Desktop show up on the desktop.",
    "",
    "#Terminal",
    "A real shell: ls, cd, mkdir, rm, cp, mv, cat, grep,",
    "find, wc, sort, head, tail, echo > file, pipes (|),",
    "$VARIABLES, $(commands), *.wildcards, for loops and",
    ".sh scripts. Tab completes names; Up/Down = history.",
    "Try: neofetch, cowsay hi, tree /, play C4 E4 G4",
    "",
    "#Games",
    "Solitaire: drag cards; double-click sends a card home.",
    "Minesweeper: A reveals, B flags.",
    "Blocks: D-Pad moves, Up rotates, B drops.",
    "Snake, Pong: D-Pad.",
    "",
    "#Saving",
    "Real GameCube: SD Gecko or SD2SP2 adapter.",
    "Dolphin: Config > GameCube > Slot A = SD Card adapter.",
};
#define NHELP ((int)(sizeof help_lines / sizeof help_lines[0]))
typedef struct { int scroll; } HelpData;
static void help_draw(Window *w, int x, int y, int cw, int ch) {
    HelpData *d = w->data;
    grad_h(x, y, cw, 46, 0x0A2A8C, 0x3A6AD8);
    draw_icon(IC_HELP, x + 10, y + 7, 32);
    text(&font_big, x + 52, y + 12, "Help and Support Center", 0xFFFFFF);
    int vis = (ch - 56) / 15;
    gfx_clip(x, y + 50, cw - 18, ch - 50);
    for (int i = 0; i < vis && d->scroll + i < NHELP; i++) {
        const char *s = help_lines[d->scroll + i];
        if (s[0] == '#') text(&font_bold, x + 12, y + 54 + i * 15, s + 1, 0x0A246A);
        else text(&font_ui, x + 20, y + 54 + i * 15, s, 0);
    }
    gfx_noclip();
    ui_scrollbar(x + cw - 17, y + 47, ch - 47, NHELP, vis, d->scroll);
}
static void help_mouse(Window *w, int ev, int mx, int my) {
    HelpData *d = w->data;
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    int vis = (ch - 56) / 15, mxs = NHELP - vis > 0 ? NHELP - vis : 0;
    if (ev == EV_SCROLL) d->scroll += my * 2;
    else if (ev == EV_DOWN && mx > cw - 18) d->scroll += my < ch / 2 + 23 ? -5 : 5;
    if (d->scroll < 0) d->scroll = 0;
    if (d->scroll > mxs) d->scroll = mxs;
}
void open_help(void) {
    Window *w = win_new("Help and Support Center", IC_HELP, 420, 330);
    if (!w) return;
    w->app = APP_HELP; w->resizable = 1;
    w->data = calloc(1, sizeof(HelpData));
    w->draw = help_draw; w->mouse = help_mouse;
}

/* ---------- Recycle Bin ---------- */
typedef struct { int sel, scroll; } RB;
static VNode *rb_item(int idx) { VNode *c = vfs_recycle->child; while (c && idx--) c = c->next; return c; }
static void rb_draw(Window *w, int x, int y, int cw, int ch) {
    RB *d = w->data;
    grad_v(x, y, 170, ch, 0x7BA2E7, 0x6375D6);
    round_fill(x + 10, y + 10, 150, 86, 4, 0xD6DFF7);
    grad_h(x + 10, y + 10, 150, 22, 0xFFFFFF, 0xC6D3F7);
    text(&font_bold, x + 18, y + 15, "Recycle Bin Tasks", 0x215DC6);
    static const char *t[] = { "Empty Recycle Bin", "Restore this item", "Restore all items" };
    static const int ic[] = { IC_RECYCLE, IC_BACK, IC_FOLDER };
    for (int i = 0; i < 3; i++) {
        int hot = inside(mouse_x, mouse_y, x + 16, y + 38 + i * 19, 140, 17);
        draw_icon(ic[i], x + 16, y + 38 + i * 19, 16);
        text(&font_ui, x + 36, y + 40 + i * 19, t[i], hot ? 0x428EFF : 0x215DC6);
        if (hot) hline(x + 36, y + 53 + i * 19, text_w(&font_ui, t[i]), 0x428EFF);
    }
    fill(x + 170, y, cw - 170, ch, 0xFFFFFF);
    fill(x + 170, y, cw - 170, 20, C_FACE);
    hline(x + 170, y + 20, cw - 170, C_SHADOW);
    text(&font_ui, x + 178, y + 4, "Name", 0);
    text(&font_ui, x + 330, y + 4, "Original Location", 0);
    if (!vfs_recycle->child) { text(&font_ui, x + 186, y + 34, "The Recycle Bin is empty.", 0x808080); return; }
    int i = 0;
    gfx_clip(x + 170, y + 21, cw - 170, ch - 21);
    for (VNode *n = vfs_recycle->child; n; n = n->next, i++) {
        int iy = y + 24 + (i - d->scroll) * 22;
        if (i < d->scroll) continue;
        if (i == d->sel) fill(x + 172, iy, cw - 174, 22, C_SEL);
        draw_icon(vfs_icon(n), x + 176, iy + 3, 16);
        uint32_t fg = i == d->sel ? 0xFFFFFF : 0;
        gfx_clip(x + 170, y + 21, 150, ch - 21);
        text(&font_ui, x + 198, iy + 5, n->name, fg);
        gfx_clip(x + 170, y + 21, cw - 170, ch - 21);
        char loc[96];
        snprintf(loc, sizeof loc, "%s", n->orig[0] ? n->orig : "?");
        char *sl = strrchr(loc, '/');
        if (sl && sl != loc) *sl = 0; else if (sl) strcpy(loc, "/");
        text(&font_ui, x + 330, iy + 5, loc, fg);
    }
    gfx_noclip();
}
static void rb_mouse(Window *w, int ev, int mx, int my) {
    RB *d = w->data;
    if (ev == EV_SCROLL) { d->scroll += my; if (d->scroll < 0) d->scroll = 0; return; }
    if (ev != EV_DOWN && ev != EV_DBL && ev != EV_RDOWN) return;
    if (mx < 170) {
        if (ev != EV_DOWN) return;
        int t = (my - 38) / 19;
        if (my < 38 || t > 2) return;
        if (t == 0 && vfs_recycle->child) {
            int n = vfs_count(vfs_recycle);
            while (vfs_recycle->child) vfs_remove(vfs_recycle->child, 0);
            sound_play(SND_EXPLODE);
            char m[64]; snprintf(m, sizeof m, "%d item%s permanently deleted.", n, n == 1 ? " was" : "s were");
            notify("Recycle Bin", m);
            d->sel = -1;
        } else if (t == 1) { VNode *n = rb_item(d->sel); if (n) { vfs_restore(n); d->sel = -1; } }
        else if (t == 2) { while (vfs_recycle->child) vfs_restore(vfs_recycle->child); d->sel = -1; }
        return;
    }
    if (my < 22) return;
    int i = d->scroll + (my - 24) / 22;
    d->sel = rb_item(i) ? i : -1;
    if (ev == EV_DBL && d->sel >= 0) {
        VNode *n = rb_item(d->sel);
        char m[200];
        snprintf(m, sizeof m, "%s\n\nDeleted from: %s\nSize: %ld bytes\n\nUse 'Restore this item' to put it back.", n->name, n->orig, vfs_total_size(n));
        msgbox("Properties", m, vfs_icon(n));
    }
}
void open_recycle(void) {
    Window *w = win_new("Recycle Bin", IC_RECYCLE, 470, 280);
    if (!w) return;
    RB *d = calloc(1, sizeof(RB));
    d->sel = -1;
    w->app = APP_RECYCLE; w->resizable = 1; w->data = d;
    w->draw = rb_draw; w->mouse = rb_mouse;
}

/* ---------- Clock & Calendar ---------- */
typedef struct { int moff; } ClockData;
static void clk_draw(Window *w, int x, int y, int cw, int ch) {
    ClockData *d = w->data;
    (void)ch;
    time_t tt = time(NULL);
    struct tm tm = *localtime(&tt);
    /* analog */
    int ccx = x + 100, ccy = y + 110, r = 80;
    fill_circle(ccx, ccy, r + 4, 0x1A4AA0);
    fill_circle(ccx, ccy, r, 0xFFFFFF);
    for (int i = 0; i < 60; i++) {
        float a = i * 6.2831853f / 60;
        int len = i % 5 ? 3 : 8;
        int x0 = ccx + (int)(sinf(a) * (r - 2)), y0 = ccy - (int)(cosf(a) * (r - 2));
        int x1 = ccx + (int)(sinf(a) * (r - 2 - len)), y1 = ccy - (int)(cosf(a) * (r - 2 - len));
        if (i % 5) line(x0, y0, x1, y1, 0x808080); else thick_line(x0, y0, x1, y1, 1, 0x202020);
    }
    float ha = ((tm.tm_hour % 12) + tm.tm_min / 60.f) * 6.2831853f / 12, ma = (tm.tm_min + tm.tm_sec / 60.f) * 6.2831853f / 60;
    float sa = tm.tm_sec * 6.2831853f / 60;
    thick_line(ccx, ccy, ccx + (int)(sinf(ha) * 42), ccy - (int)(cosf(ha) * 42), 2, 0x101010);
    thick_line(ccx, ccy, ccx + (int)(sinf(ma) * 64), ccy - (int)(cosf(ma) * 64), 1, 0x101010);
    line(ccx, ccy, ccx + (int)(sinf(sa) * 70), ccy - (int)(cosf(sa) * 70), 0xD02020);
    fill_circle(ccx, ccy, 3, 0xD02020);
    char b[48];
    strftime(b, sizeof b, cfg.clock24 ? "%H:%M:%S" : "%I:%M:%S %p", &tm);
    text_center(&font_big, x, y + 200, 200, b, 0x0A246A);
    /* calendar */
    int month = tm.tm_mon + d->moff, year = tm.tm_year + 1900;
    while (month < 0) { month += 12; year--; }
    while (month > 11) { month -= 12; year++; }
    static const char *mn[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    int gx = x + 220, gy = y + 14;
    ui_groupbox(gx - 8, gy - 6, cw - 222, 232, "Date");
    snprintf(b, sizeof b, "%s %d", mn[month], year);
    text_center(&font_bold, gx, gy + 14, 196, b, 0);
    ui_button(gx, gy + 10, 22, 20, "<", w);
    ui_button(gx + 174, gy + 10, 22, 20, ">", w);
    static const char *dn[] = { "S", "M", "T", "W", "T", "F", "S" };
    fill(gx, gy + 38, 196, 18, 0x316AC5);
    for (int i = 0; i < 7; i++) text_center(&font_bold, gx + i * 28, gy + 41, 28, dn[i], 0xFFFFFF);
    struct tm first = { 0 };
    first.tm_year = year - 1900; first.tm_mon = month; first.tm_mday = 1; first.tm_hour = 12;
    mktime(&first);
    static const int mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int nd = mdays[month] + (month == 1 && ((year % 4 == 0 && year % 100) || year % 400 == 0));
    fill(gx, gy + 56, 196, 6 * 24, 0xFFFFFF);
    for (int dd = 1; dd <= nd; dd++) {
        int cell = first.tm_wday + dd - 1;
        int cx = gx + (cell % 7) * 28, cy = gy + 58 + (cell / 7) * 24;
        int today = d->moff == 0 && dd == tm.tm_mday;
        if (today) { fill(cx + 2, cy, 24, 22, 0x316AC5); }
        snprintf(b, sizeof b, "%d", dd);
        text_center(&font_ui, cx, cy + 4, 28, b, today ? 0xFFFFFF : (cell % 7 == 0 ? 0xC03020 : 0));
    }
}
static void clk_mouse(Window *w, int ev, int mx, int my) {
    ClockData *d = w->data;
    if (ev != EV_DOWN) return;
    if (inside(mx, my, 220, 24, 22, 20)) d->moff--;
    if (inside(mx, my, 394, 24, 22, 20)) d->moff++;
}
void open_clock(void) {
    Window *w = win_new("Date and Time Properties", IC_CLOCK, 440, 240);
    if (!w) return;
    w->app = APP_CLOCK; w->no_max = 1;
    w->data = calloc(1, sizeof(ClockData));
    w->draw = clk_draw; w->mouse = clk_mouse;
}
