/* Window manager: create/close/focus, Luna-style chrome, mouse routing */
#include "os.h"
#include <string.h>
#include <stdlib.h>

Window wins[MAX_WIN];
int zorder[MAX_WIN], nz;
Window *focus;

static int cascade = 0;

static int widx(Window *w) { return (int)(w - wins); }

static void z_remove(int i) {
    for (int k = 0; k < nz; k++)
        if (zorder[k] == i) { memmove(&zorder[k], &zorder[k + 1], (nz - k - 1) * sizeof(int)); nz--; return; }
}

Window *win_new(const char *title, int icon, int cw, int ch) {
    for (int i = 0; i < MAX_WIN; i++) {
        if (wins[i].used) continue;
        Window *w = &wins[i];
        static int gen_counter;
        memset(w, 0, sizeof *w);
        w->used = 1;
        w->gen = ++gen_counter;
        strncpy(w->title, title, sizeof w->title - 1);
        w->icon = icon;
        w->w = cw + 2 * BORDER;
        w->h = ch + TITLE_H + BORDER;
        w->x = 90 + cascade * 26;
        w->y = 30 + cascade * 24;
        cascade = (cascade + 1) % 7;
        if (w->x + w->w > SW) w->x = SW - w->w;
        if (w->y + w->h > SH - TASKBAR_H) w->y = SH - TASKBAR_H - w->h;
        if (w->x < 0) w->x = 0;
        if (w->y < 0) w->y = 0;
        w->min_w = 160; w->min_h = 90;
        w->opened = now_ms; w->fade_t = now_ms;
        zorder[nz++] = i;
        win_focus(w);
        return w;
    }
    msgbox("CubeXP", "Too many windows are open.\nClose a window and try again.", IC_WARN);
    return NULL;
}

void win_center(Window *w) {
    w->x = (SW - w->w) / 2;
    w->y = (SH - TASKBAR_H - w->h) / 2;
}

static Window *top_window(void) {
    for (int k = nz - 1; k >= 0; k--) {
        Window *w = &wins[zorder[k]];
        if (!w->minimized && !w->nofocus) return w;
    }
    return NULL;
}

void win_close(Window *w) {
    if (!w || !w->used) return;
    if (w->on_close) w->on_close(w);
    if (w->data) free(w->data);
    w->data = NULL;
    w->used = 0;
    z_remove(widx(w));
    if (focus == w) focus = top_window();
}

void win_focus(Window *w) {
    if (!w) { focus = NULL; return; }
    if (w->minimized) w->fade_t = now_ms;
    w->minimized = 0;
    z_remove(widx(w));
    zorder[nz++] = widx(w);
    if (!w->nofocus) focus = w;
}

/* "zoom rectangle" animations (minimize to the taskbar) */
typedef struct { int x0, y0, w0, h0, x1, y1, w1, h1; uint64_t t; } ZoomAnim;
static ZoomAnim zooms[4];
static void zoom_add(int x0, int y0, int w0, int h0, int x1, int y1, int w1, int h1) {
    for (int i = 0; i < 4; i++) if (!zooms[i].t || now_ms - zooms[i].t > 260) {
        zooms[i] = (ZoomAnim){ x0, y0, w0, h0, x1, y1, w1, h1, now_ms };
        return;
    }
}
void wm_draw_zooms(void) {
    for (int i = 0; i < 4; i++) {
        ZoomAnim *z = &zooms[i];
        if (!z->t) continue;
        int el = (int)(now_ms - z->t);
        if (el > 220) { z->t = 0; continue; }
        for (int k = 0; k < 3; k++) {
            int t = el * 256 / 220 - k * 30;
            if (t < 0) continue;
            int x = z->x0 + (z->x1 - z->x0) * t / 256, y = z->y0 + (z->y1 - z->y0) * t / 256;
            int ww = z->w0 + (z->w1 - z->w0) * t / 256, hh = z->h0 + (z->h1 - z->h0) * t / 256;
            fill_a(x, y, ww, 18, TH->t2, 150 - k * 40);
            rect(x, y, ww, hh, TH->frame);
        }
    }
}
void win_minimize(Window *w) {
    int tx, ty, tw, th;
    if (!w->minimized && shell_taskbar_rect(w, &tx, &ty, &tw, &th)) zoom_add(w->x, w->y, w->w, w->h, tx, ty, tw, th);
    w->minimized = 1;
    if (focus == w) focus = top_window();
}

void win_toggle_max(Window *w) {
    if (w->no_max) return;
    if (w->maximized) {
        w->maximized = 0;
        w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh;
    } else {
        w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
        w->maximized = 1;
        w->x = -BORDER; w->y = 0; w->w = SW + 2 * BORDER; w->h = SH - TASKBAR_H + BORDER;
    }
}

void win_client(Window *w, int *x, int *y, int *cw, int *ch) {
    *x = w->x + BORDER; *y = w->y + TITLE_H;
    *cw = w->w - 2 * BORDER; *ch = w->h - TITLE_H - BORDER;
}

int win_mouse(Window *w, int *mx, int *my) {
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    *mx = mouse_x - x; *my = mouse_y - y;
    return inside(*mx, *my, 0, 0, cw, ch);
}

Window *win_find(int app) {
    for (int i = 0; i < MAX_WIN; i++) if (wins[i].used && wins[i].app == app) return &wins[i];
    return NULL;
}

/* display order: normal windows, then topmost */
static int display_order(int *out) {
    int n = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < nz; k++) {
            Window *w = &wins[zorder[k]];
            if (w->minimized || w->topmost != pass) continue;
            out[n++] = zorder[k];
        }
    return n;
}

/* ---------- chrome ---------- */
enum { TB_NONE, TB_MIN, TB_MAX, TB_CLOSE };

static void tb_rect(Window *w, int which, int *bx, int *by) {
    int x = w->x + w->w - BORDER - 2 - 21;
    if (which == TB_MAX) x -= 23;
    if (which == TB_MIN) x -= w->no_max ? 23 : 46;
    *bx = x; *by = w->y + 2;
}

static void draw_tbutton(int x, int y, int kind, int active, int hot, int pressed, int maxed) {
    uint32_t c1, c2;
    if (kind == TB_CLOSE) { c1 = 0xEA8E6E; c2 = 0xC53A14; }
    else if (cfg.theme == 2) { c1 = 0xFFFFFF; c2 = 0xC4C6D5; }
    else if (cfg.theme == 1) { c1 = 0xB8CB91; c2 = 0x7D9152; }
    else { c1 = 0x6FA1FA; c2 = 0x1E5ED8; }
    if (!active) { c1 = mix(c1, 0xC0D0F0, 110); c2 = mix(c2, 0xC0D0F0, 110); }
    if (hot) { c1 = shade(c1, 115); c2 = shade(c2, 115); }
    if (pressed) { uint32_t t = c1; c1 = c2; c2 = t; }
    round_fill(x, y, 21, 21, 3, 0xFFFFFF);
    round_grad(x + 1, y + 1, 19, 19, 2, c1, c2, 0);
    uint32_t g = (cfg.theme == 2 && kind != TB_CLOSE) ? 0x40405A : 0xFFFFFF;
    if (kind == TB_CLOSE) {
        thick_line(x + 6, y + 6, x + 14, y + 14, 1, g);
        thick_line(x + 14, y + 6, x + 6, y + 14, 1, g);
    } else if (kind == TB_MIN) {
        fill(x + 5, y + 13, 7, 3, g);
    } else if (!maxed) {
        rect(x + 5, y + 5, 11, 11, g); fill(x + 5, y + 5, 11, 3, g);
    } else {
        rect(x + 8, y + 4, 8, 8, g); fill(x + 8, y + 4, 8, 2, g);
        fill(x + 4, y + 8, 8, 8, c2); rect(x + 4, y + 8, 8, 8, g); fill(x + 4, y + 8, 8, 2, g);
    }
}

static int press_win = -1, press_btn = TB_NONE;

static void draw_frame(Window *w, int active) {
    const Theme *t = TH;
    int x = w->x, y = w->y, ww = w->w, hh = w->h;
    uint32_t frame = active ? t->frame : t->frame_in;
    /* sides + bottom */
    fill(x, y + TITLE_H, BORDER, hh - TITLE_H, frame);
    fill(x + ww - BORDER, y + TITLE_H, BORDER, hh - TITLE_H, frame);
    fill(x, y + hh - BORDER, ww, BORDER, frame);
    vline(x, y + TITLE_H, hh - TITLE_H, shade(frame, 70));
    vline(x + ww - 1, y + TITLE_H, hh - TITLE_H, shade(frame, 60));
    hline(x, y + hh - 1, ww, shade(frame, 60));
    /* title bar: 4-stop gradient with rounded top corners */
    int r = w->maximized ? 0 : 7;
    for (int j = 0; j < TITLE_H; j++) {
        uint32_t c;
        if (active) {
            if (j < 3) c = mix(t->t1, t->t2, j * 256 / 3);
            else if (j < TITLE_H - 6) c = mix(t->t2, t->t3, (j - 3) * 256 / (TITLE_H - 9));
            else c = mix(t->t3, t->tbot, (j - (TITLE_H - 6)) * 256 / 6);
        } else c = mix(t->title_in1, t->title_in2, j * 256 / TITLE_H);
        int in = 0;
        if (j < r) { float dy = r - j - 0.5f; int s = 0; while ((s + 1) * (s + 1) <= r * r - dy * dy) s++; in = r - s; }
        hline(x + in, y + j, ww - 2 * in, c);
    }
    draw_icon(w->icon, x + 6, y + 5, 16);
    int maxw = ww - (w->no_max ? 80 : 100);
    char buf[48];
    strncpy(buf, w->title, sizeof buf - 1); buf[47] = 0;
    while (text_w(&font_title, buf) > maxw && strlen(buf) > 3) { int n = strlen(buf); buf[n - 1] = 0; buf[n - 2] = '.'; buf[n - 3] = '.'; }
    if (cfg.theme == 2) text(&font_title, x + 26, y + 6, buf, active ? 0x000000 : 0x6A6A7A);
    else if (active) text_shadow(&font_title, x + 26, y + 6, buf, 0xFFFFFF);
    else text(&font_title, x + 26, y + 6, buf, t->title_txt);
    for (int b = TB_MIN; b <= TB_CLOSE; b++) {
        if (b == TB_MAX && w->no_max) continue;
        int bx, by;
        tb_rect(w, b, &bx, &by);
        int hot = inside(mouse_x, mouse_y, bx, by, 21, 21);
        draw_tbutton(bx, by, b, active, hot, press_win == widx(w) && press_btn == b && hot, w->maximized);
    }
}

void wm_draw(void) {
    int order[MAX_WIN];
    int n = display_order(order);
    for (int k = 0; k < n; k++) {
        Window *w = &wins[order[k]];
        int cx, cy, cw, ch;
        win_client(w, &cx, &cy, &cw, &ch);
        int fade = (int)(now_ms - w->fade_t);
        int fading = fade < 170;
        if (!w->maximized && !fading) shadow_rect(w->x, w->y, w->w, w->h, 5, 110);
        if (fading) layer_begin(w->x - 6, w->y - 6, w->w + 14, w->h + 14);
        if (fading && !w->maximized) shadow_rect(w->x, w->y, w->w, w->h, 5, 110);
        draw_frame(w, w == focus || (w->nofocus));
        gfx_clip(cx, cy, cw, ch);
        fill(cx, cy, cw, ch, C_FACE);
        if (w->draw) w->draw(w, cx, cy, cw, ch);
        gfx_noclip();
        if (w->resizable && !w->maximized) {   /* grip */
            for (int i = 0; i < 3; i++)
                for (int j = 0; j <= i; j++) {
                    fill(cx + cw - 4 - (i - j) * 4 - 1, cy + ch - 4 - j * 4 - 1, 2, 2, 0xFFFFFF);
                    fill(cx + cw - 4 - (i - j) * 4 - 2, cy + ch - 4 - j * 4 - 2, 2, 2, C_SHADOW);
                }
        }
        if (fading) layer_end(40 + fade * 216 / 170);
    }
}

/* ---------- mouse routing ---------- */
static int drag_win = -1, drag_mode, dox, doy;
enum { DM_MOVE = 1, DM_RESIZE, DM_CLIENT };

Window *wm_hit(int mx, int my) {
    int order[MAX_WIN];
    int n = display_order(order);
    for (int k = n - 1; k >= 0; k--) {
        Window *w = &wins[order[k]];
        if (inside(mx, my, w->x, w->y, w->w, w->h)) return w;
    }
    return NULL;
}

static void to_client(Window *w, int mx, int my, int *x, int *y) {
    int cx, cy, cw, ch;
    win_client(w, &cx, &cy, &cw, &ch);
    *x = mx - cx; *y = my - cy;
}

/* returns 1 if a window consumed the event */
int wm_mouse(int ev, int mx, int my) {
    if (ev == EV_DRAG) {
        if (drag_win < 0) return 0;
        Window *w = &wins[drag_win];
        if (!w->used) { drag_win = -1; return 0; }
        if (drag_mode == DM_MOVE) {
            if (w->maximized) {   /* drag-to-restore */
                win_toggle_max(w);
                dox = w->w / 2; doy = 12;
            }
            w->x = mx - dox; w->y = my - doy;
            if (w->y < 0) w->y = 0;
            if (w->y > SH - TASKBAR_H - 20) w->y = SH - TASKBAR_H - 20;
            if (w->x > SW - 40) w->x = SW - 40;
            if (w->x + w->w < 40) w->x = 40 - w->w;
        } else if (drag_mode == DM_RESIZE) {
            int nw = mx - w->x + dox, nh = my - w->y + doy;
            if (nw < w->min_w) nw = w->min_w;
            if (nh < w->min_h) nh = w->min_h;
            if (w->x + nw > SW) nw = SW - w->x;
            if (w->y + nh > SH - TASKBAR_H) nh = SH - TASKBAR_H - w->y;
            w->w = nw; w->h = nh;
        } else if (drag_mode == DM_CLIENT && w->mouse) {
            int x, y; to_client(w, mx, my, &x, &y);
            w->mouse(w, EV_DRAG, x, y);
        }
        return 1;
    }
    if (ev == EV_UP) {
        int consumed = drag_win >= 0 || press_win >= 0;
        if (press_win >= 0) {
            Window *w = &wins[press_win];
            int bx, by;
            if (w->used) {
                tb_rect(w, press_btn, &bx, &by);
                if (inside(mx, my, bx, by, 21, 21)) {
                    sound_play(SND_CLICK);
                    if (press_btn == TB_CLOSE) win_close(w);
                    else if (press_btn == TB_MIN) win_minimize(w);
                    else win_toggle_max(w);
                }
            }
            press_win = -1; press_btn = TB_NONE;
        }
        if (drag_win >= 0 && drag_mode == DM_CLIENT) {
            Window *w = &wins[drag_win];
            if (w->used && w->mouse) { int x, y; to_client(w, mx, my, &x, &y); w->mouse(w, EV_UP, x, y); }
        }
        drag_win = -1;
        return consumed;
    }
    Window *w = wm_hit(mx, my);
    if (!w) return 0;
    if (ev == EV_SCROLL) {      /* my carries delta; route to window under cursor */
        return 1;
    }
    if (!w->nofocus && w != focus) win_focus(w);
    else if (w->nofocus) win_focus(w);   /* raise only */
    int cx, cy, cw, ch;
    win_client(w, &cx, &cy, &cw, &ch);
    if (my < w->y + TITLE_H) {
        if (ev == EV_RDOWN) return 1;
        for (int b = TB_MIN; b <= TB_CLOSE; b++) {
            if (b == TB_MAX && w->no_max) continue;
            int bx, by; tb_rect(w, b, &bx, &by);
            if (inside(mx, my, bx, by, 21, 21)) {
                if (ev == EV_DOWN) { press_win = widx(w); press_btn = b; }
                return 1;
            }
        }
        if (ev == EV_DBL) { drag_win = -1; win_toggle_max(w); return 1; }
        drag_win = widx(w); drag_mode = DM_MOVE; dox = mx - w->x; doy = my - w->y;
        return 1;
    }
    if (w->resizable && !w->maximized && mx >= w->x + w->w - 14 && my >= w->y + w->h - 14) {
        drag_win = widx(w); drag_mode = DM_RESIZE;
        dox = w->x + w->w - mx; doy = w->y + w->h - my;
        return 1;
    }
    if (inside(mx, my, cx, cy, cw, ch) && w->mouse) {
        int g = w->gen;
        w->mouse(w, ev, mx - cx, my - cy);
        if ((ev == EV_DOWN || ev == EV_DBL) && w->used && w->gen == g) { drag_win = widx(w); drag_mode = DM_CLIENT; }
    }
    return 1;
}

void wm_scroll(int dy) {
    Window *w = wm_hit(mouse_x, mouse_y);
    if (!w) w = focus;
    if (w && w->mouse) w->mouse(w, EV_SCROLL, 0, dy);
}

void wm_tick(void) {
    for (int i = 0; i < MAX_WIN; i++)
        if (wins[i].used && wins[i].tick) wins[i].tick(&wins[i]);
}

void wm_close_all(void) {
    for (int i = 0; i < MAX_WIN; i++) if (wins[i].used) win_close(&wins[i]);
    nz = 0; focus = NULL; cascade = 0;
}

int wm_dragging(void) { return drag_win >= 0; }
