/* Task Manager: Applications + Performance */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define HIST 60
typedef struct { int tab, sel; int cpu[HIST], mem[HIST]; uint64_t last; } TM;
static const char *tabs[] = { "Applications", "Performance" };

static void graph(int x, int y, int w, int h, const int *v, int pct_now, const char *label) {
    fill(x, y, w, h, 0);
    for (int gx = 0; gx < w; gx += 12) vline(x + gx, y, h, 0x004000);
    for (int gy = 0; gy < h; gy += 12) hline(x, y + gy, w, 0x004000);
    for (int i = 1; i < HIST; i++) {
        int x0 = x + (i - 1) * w / (HIST - 1), x1 = x + i * w / (HIST - 1);
        line(x0, y + h - 1 - v[i - 1] * (h - 2) / 100, x1, y + h - 1 - v[i] * (h - 2) / 100, 0x00FF00);
    }
    char b[32];
    snprintf(b, sizeof b, "%s %d%%", label, pct_now);
    text(&font_ui, x + 4, y + 3, b, 0x00FF00);
}

static void tm_draw(Window *w, int x, int y, int cw, int ch) {
    TM *d = w->data;
    fill(x + 8, y + 30, cw - 16, ch - 78, 0xFCFCFE);
    rect(x + 8, y + 30, cw - 16, ch - 78, 0x919B9C);
    ui_tab(x + 10, y + 10, tabs, 2, d->tab);
    char b[96];
    if (d->tab == 0) {
        int lx = x + 16, ly = y + 40, lw = cw - 32, lh = ch - 130;
        ui_panel_sunken(lx, ly, lw, lh, 0xFFFFFF);
        fill(lx + 1, ly + 1, lw - 2, 18, C_FACE);
        text(&font_ui, lx + 6, ly + 3, "Task", 0);
        text(&font_ui, lx + lw - 70, ly + 3, "Status", 0);
        int row = 0;
        for (int k = 0; k < nz; k++) {
            Window *t = &wins[zorder[k]];
            if (t == w) continue;
            int ry = ly + 20 + row * 20;
            if (ry + 20 > ly + lh) break;
            if (zorder[k] == d->sel) fill(lx + 2, ry, lw - 4, 20, C_SEL);
            draw_icon(t->icon, lx + 6, ry + 2, 16);
            gfx_clip(lx, ry, lw - 80, 20);
            text(&font_ui, lx + 28, ry + 4, t->title, zorder[k] == d->sel ? 0xFFFFFF : 0);
            gfx_clip(x, y, cw, ch);
            text(&font_ui, lx + lw - 70, ry + 4, t->minimized ? "Minimized" : "Running", zorder[k] == d->sel ? 0xFFFFFF : 0);
            row++;
        }
        ui_button(x + cw - 270, y + ch - 82, 80, 23, "End Task", w);
        ui_button(x + cw - 184, y + ch - 82, 80, 23, "Switch To", w);
        ui_button(x + cw - 98, y + ch - 82, 80, 23, "New Task...", w);
    } else {
        int gw = cw - 140;
        text(&font_ui, x + 20, y + 40, "CPU Usage", 0);
        fill(x + 20, y + 56, 90, 60, 0);
        char p[16];
        snprintf(p, sizeof p, "%d %%", d->cpu[HIST - 1]);
        ui_progress(x + 40, y + 60, 50, 40, d->cpu[HIST - 1]);
        text_center(&font_ui, x + 20, y + 102, 90, p, 0x00FF00);
        text(&font_ui, x + 124, y + 40, "CPU Usage History", 0);
        graph(x + 124, y + 56, gw, 60, d->cpu, d->cpu[HIST - 1], "CPU");
        text(&font_ui, x + 20, y + 124, "Memory", 0);
        int memp = d->mem[HIST - 1];
        fill(x + 20, y + 140, 90, 60, 0);
        ui_progress(x + 40, y + 144, 50, 40, memp);
        snprintf(p, sizeof p, "%d %%", memp);
        text_center(&font_ui, x + 20, y + 186, 90, p, 0x00FF00);
        text(&font_ui, x + 124, y + 124, "Memory Usage History", 0);
        graph(x + 124, y + 140, gw, 60, d->mem, memp, "MEM");
        ui_groupbox(x + 20, y + 210, cw - 40, 54, "Totals");
        uint64_t up = now_ms / 1000;
        int napps = 0;
        for (int k = 0; k < MAX_WIN; k++) napps += wins[k].used;
        snprintf(b, sizeof b, "Windows: %d     Frame rate: %d fps     Up time: %d:%02d:%02d", napps, fps, (int)(up / 3600), (int)(up / 60 % 60), (int)(up % 60));
        text(&font_ui, x + 32, y + 228, b, 0);
        snprintf(b, sizeof b, "Physical memory: 24 MB     Free: %u KB", (unsigned)(plat_mem_free() / 1024));
        text(&font_ui, x + 32, y + 244, b, 0);
    }
    /* status bar */
    fill(x, y + ch - 22, cw, 22, C_FACE);
    hline(x, y + ch - 22, cw, C_SHADOW);
    int napps = 0;
    for (int k = 0; k < MAX_WIN; k++) napps += wins[k].used;
    snprintf(b, sizeof b, "Processes: %d     CPU Usage: %d%%     Memory: %d%%", napps + 3, d->cpu[HIST - 1], d->mem[HIST - 1]);
    text(&font_ui, x + 6, y + ch - 17, b, 0);
}

static void tm_tick(Window *w) {
    TM *d = w->data;
    if (now_ms - d->last < 500) return;
    d->last = now_ms;
    memmove(d->cpu, d->cpu + 1, sizeof(int) * (HIST - 1));
    memmove(d->mem, d->mem + 1, sizeof(int) * (HIST - 1));
    d->cpu[HIST - 1] = cpu_pct;
    size_t fr = plat_mem_free(), tot = plat_mem_total();
    d->mem[HIST - 1] = fr > tot ? 0 : (int)((tot - fr) * 100 / tot);
}

static void tm_mouse(Window *w, int ev, int mx, int my) {
    TM *d = w->data;
    if (ev != EV_DOWN && ev != EV_DBL) return;
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    int t = ui_tab_hit(mx, my, 10, 10, tabs, 2);
    if (t >= 0) { d->tab = t; return; }
    if (d->tab != 0) return;
    int lw = cw - 32, lh = ch - 130, row = 0;
    if (inside(mx, my, 16, 60, lw, lh - 20)) {
        int want = (my - 60) / 20;
        for (int k = 0; k < nz; k++) {
            if (&wins[zorder[k]] == w) continue;
            if (row == want) { d->sel = zorder[k]; if (ev == EV_DBL) win_focus(&wins[d->sel]); return; }
            row++;
        }
        d->sel = -1;
        return;
    }
    Window *s = d->sel >= 0 && wins[d->sel].used && &wins[d->sel] != w ? &wins[d->sel] : NULL;
    if (inside(mx, my, cw - 270, ch - 82, 80, 23) && s) { win_close(s); d->sel = -1; }
    else if (inside(mx, my, cw - 184, ch - 82, 80, 23) && s) win_focus(s);
    else if (inside(mx, my, cw - 98, ch - 82, 80, 23)) app_launch(APP_RUN);
}

void open_taskmgr(void) {
    Window *w = win_new("CubeXP Task Manager", IC_TASKMGR, 400, 330);
    if (!w) return;
    TM *d = calloc(1, sizeof(TM));
    d->sel = -1;
    w->app = APP_TASKMGR; w->no_max = 1; w->data = d;
    w->draw = tm_draw; w->mouse = tm_mouse; w->tick = tm_tick;
}
