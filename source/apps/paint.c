/* Paint: pencil, brush, eraser, fill, line, rectangle, ellipse, spray; save BMP; set as wallpaper */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define CW 400
#define CH 260
enum { T_PENCIL, T_BRUSH, T_ERASER, T_FILL, T_LINE, T_RECT, T_ELLIPSE, T_SPRAY, T_COUNT };
static const char *tool_names[] = { "Pencil", "Brush", "Eraser", "Fill", "Line", "Rectangle", "Ellipse", "Airbrush" };
static const uint32_t palette[28] = {
    0x000000, 0x808080, 0x800000, 0x808000, 0x008000, 0x008080, 0x000080, 0x800080, 0x808040, 0x004040, 0x0080FF, 0x004080, 0x8000FF, 0x804000,
    0xFFFFFF, 0xC0C0C0, 0xFF0000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0x0000FF, 0xFF00FF, 0xFFFF80, 0x00FF80, 0x80FFFF, 0x8080FF, 0xFF0080, 0xFF8040,
};

typedef struct {
    uint32_t px[CW * CH];
    int tool, size, drawing, sx, sy, lx, ly, count;
    uint32_t fg, bg, cur_col;
    char path[160];
} Paint;

static const char *pt_menu[] = { "File", "Image", "Help" };
#define TBX 0
#define CVX 56
#define CVY 22

static void cv_set(Paint *d, int x, int y, uint32_t c) { if (x >= 0 && y >= 0 && x < CW && y < CH) d->px[y * CW + x] = c; }
static void cv_dot(Paint *d, int x, int y, int r, uint32_t c) {
    for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i * i + j * j <= r * r + r / 2) cv_set(d, x + i, y + j, c);
}
static void cv_line(Paint *d, int x0, int y0, int x1, int y1, int r, uint32_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        cv_dot(d, x0, y0, r, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
static void cv_rect(Paint *d, int x0, int y0, int x1, int y1, int r, uint32_t c) {
    cv_line(d, x0, y0, x1, y0, r, c); cv_line(d, x1, y0, x1, y1, r, c);
    cv_line(d, x1, y1, x0, y1, r, c); cv_line(d, x0, y1, x0, y0, r, c);
}
static void cv_ellipse(Paint *d, int x0, int y0, int x1, int y1, int r, uint32_t c) {
    float cx = (x0 + x1) / 2.f, cy = (y0 + y1) / 2.f, rx = abs(x1 - x0) / 2.f, ry = abs(y1 - y0) / 2.f;
    int n = (int)(rx + ry) * 2 + 8;
    int px = -1, py = -1;
    for (int i = 0; i <= n; i++) {
        float a = i * 6.2831853f / n;
        int x = (int)(cx + cosf(a) * rx + 0.5f), y = (int)(cy + sinf(a) * ry + 0.5f);
        if (px >= 0) cv_line(d, px, py, x, y, r, c);
        px = x; py = y;
    }
}
static void cv_fill(Paint *d, int x, int y, uint32_t c) {
    uint32_t target = d->px[y * CW + x];
    if (target == c) return;
    int *stack = malloc(sizeof(int) * CW * CH);
    if (!stack) return;
    int sp = 0;
    stack[sp++] = y * CW + x;
    while (sp) {
        int p = stack[--sp];
        int px = p % CW, py = p / CW;
        int l = px, r = px;
        while (l > 0 && d->px[py * CW + l - 1] == target) l--;
        while (r < CW - 1 && d->px[py * CW + r + 1] == target) r++;
        for (int i = l; i <= r; i++) {
            d->px[py * CW + i] = c;
            if (py > 0 && d->px[(py - 1) * CW + i] == target && sp < CW * CH) stack[sp++] = (py - 1) * CW + i;
            if (py < CH - 1 && d->px[(py + 1) * CW + i] == target && sp < CW * CH) stack[sp++] = (py + 1) * CW + i;
        }
    }
    free(stack);
}

static void tool_icon(int t, int x, int y) {
    switch (t) {
    case T_PENCIL: thick_line(x + 4, y + 16, x + 15, y + 5, 1, 0xE0A020); pset(x + 3, y + 17, 0); break;
    case T_BRUSH: thick_line(x + 6, y + 14, x + 15, y + 4, 1, 0x8A5A20); fill_circle(x + 5, y + 15, 3, 0x2050D0); break;
    case T_ERASER: fill(x + 4, y + 7, 13, 8, 0xF0A0C0); rect(x + 4, y + 7, 13, 8, 0x804060); break;
    case T_FILL: fill(x + 5, y + 7, 9, 10, 0xC0C0C0); rect(x + 5, y + 7, 9, 10, 0x404040); fill(x + 14, y + 9, 3, 6, 0x2050D0); break;
    case T_LINE: thick_line(x + 3, y + 16, x + 16, y + 3, 0, 0); break;
    case T_RECT: rect(x + 3, y + 5, 14, 11, 0); break;
    case T_ELLIPSE: circle(x + 10, y + 10, 7, 0); break;
    case T_SPRAY: fill(x + 9, y + 9, 6, 9, 0x606060); for (int i = 0; i < 8; i++) pset(x + 3 + (i * 5) % 7, y + 3 + (i * 3) % 6, 0); break;
    }
}

static void pt_draw(Window *w, int x, int y, int cw, int ch) {
    Paint *d = w->data;
    ui_menubar(x, y, cw, pt_menu, 3);
    /* toolbox */
    for (int t = 0; t < T_COUNT; t++) {
        int bx = x + 4 + (t % 2) * 24, by = y + CVY + 4 + (t / 2) * 24;
        if (t == d->tool) { fill(bx, by, 22, 22, 0xFFFFFF); bevel(bx, by, 22, 22, 1); }
        else bevel(bx, by, 22, 22, 0);
        tool_icon(t, bx + 1, by + 1);
    }
    for (int s = 0; s < 3; s++) {   /* size picker */
        int by = y + CVY + 110 + s * 18;
        if (d->size == s) fill(x + 6, by, 44, 16, C_SEL);
        fill_circle(x + 28, by + 8, s + 1, d->size == s ? 0xFFFFFF : 0);
    }
    /* canvas area */
    int ax = x + CVX, ay = y + CVY, aw = cw - CVX, ah = ch - CVY - 44;
    fill(ax, ay, aw, ah, 0x808080);
    gfx_clip(ax, ay, aw, ah);
    {
        int x0, y0, w0, h0;
        gfx_getclip(&x0, &y0, &w0, &h0);
        int sx0 = ax + 4 > x0 ? ax + 4 : x0, sx1 = ax + 4 + CW < x0 + w0 ? ax + 4 + CW : x0 + w0;
        for (int j = 0; j < CH && sx1 > sx0; j++) {
            int py = ay + 4 + j;
            if (py < y0 || py >= y0 + h0) continue;
            memcpy(&fb[py * SW + sx0], &d->px[j * CW + (sx0 - ax - 4)], (sx1 - sx0) * 4);
        }
    }
    /* shape preview */
    if (d->drawing && (d->tool == T_LINE || d->tool == T_RECT || d->tool == T_ELLIPSE)) {
        int r = d->size, X0 = ax + 4 + d->sx, Y0 = ay + 4 + d->sy, X1 = ax + 4 + d->lx, Y1 = ay + 4 + d->ly;
        if (d->tool == T_LINE) thick_line(X0, Y0, X1, Y1, r, d->cur_col);
        else if (d->tool == T_RECT) { thick_line(X0, Y0, X1, Y0, r, d->cur_col); thick_line(X1, Y0, X1, Y1, r, d->cur_col); thick_line(X1, Y1, X0, Y1, r, d->cur_col); thick_line(X0, Y1, X0, Y0, r, d->cur_col); }
        else { rect(X0 < X1 ? X0 : X1, Y0 < Y1 ? Y0 : Y1, abs(X1 - X0) + 1, abs(Y1 - Y0) + 1, 0x808080); }
    }
    gfx_noclip();
    /* palette */
    int py = y + ch - 40;
    fill(x + 4, py, 30, 30, 0xFFFFFF); bevel(x + 4, py, 30, 30, 1);
    fill(x + 16, py + 12, 14, 14, d->bg); rect(x + 16, py + 12, 14, 14, 0x808080);
    fill(x + 8, py + 4, 14, 14, d->fg); rect(x + 8, py + 4, 14, 14, 0x808080);
    for (int i = 0; i < 28; i++) {
        int px = x + 40 + (i % 14) * 16, ppy = py + (i / 14) * 16;
        fill(px, ppy, 15, 15, palette[i]); bevel(px, ppy, 15, 15, 1);
    }
    text(&font_ui, x + 280, py + 2, tool_names[d->tool], 0x404040);
    text(&font_ui, x + 280, py + 16, "A: colour   B: 2nd colour", 0x808080);
}

static void canvas_xy(int mx, int my, int *cx, int *cy) { *cx = mx - CVX - 4; *cy = my - CVY - 4; }

static void stroke(Paint *d, int x0, int y0, int x1, int y1) {
    uint32_t c = d->tool == T_ERASER ? d->bg : d->cur_col;
    int r = d->tool == T_PENCIL ? 0 : d->tool == T_BRUSH ? d->size + 1 : d->tool == T_ERASER ? d->size * 3 + 3 : 0;
    if (d->tool == T_SPRAY) {
        for (int i = 0; i < 12; i++) {
            int rr = 6 + d->size * 4;
            int ox = rand() % (2 * rr + 1) - rr, oy = rand() % (2 * rr + 1) - rr;
            if (ox * ox + oy * oy <= rr * rr) cv_set(d, x1 + ox, y1 + oy, c);
        }
        return;
    }
    cv_line(d, x0, y0, x1, y1, r, c);
}

static int ctx_of(Window *w) { return (int)(w - wins) | (w->gen << 8); }
static Window *win_of(int ctx) {
    Window *w = &wins[ctx & 255];
    return (w->used && w->app == APP_PAINT && w->gen == (ctx >> 8)) ? w : NULL;
}

static void pt_title(Window *w) {
    Paint *d = w->data;
    const char *nm = d->path[0] ? strrchr(d->path, '/') + 1 : "untitled";
    snprintf(w->title, sizeof w->title, "%.30s - Paint", nm);
}
static void save_bmp(const char *path, int ctx) {
    Window *w = win_of(ctx);
    if (!w) return;
    Paint *d = w->data;
    int sz;
    uint8_t *bmp = bmp_encode(d->px, CW, CH, &sz);
    VNode *f = bmp ? vfs_create(NULL, path) : NULL;
    if (!f || !vfs_write(f, bmp, sz)) { free(bmp); msgbox("Paint", "The picture could not be saved.", IC_WARN); return; }
    free(bmp);
    vfs_path(f, d->path, sizeof d->path);
    pt_title(w);
    char m[200];
    snprintf(m, sizeof m, "Saved to %s", d->path);
    notify("Paint", m);
}
static int load_into(Paint *d, const char *path) {
    VNode *f = vfs_lookup(NULL, path);
    int bw, bh;
    uint32_t *px = f && !f->dir ? bmp_decode(f->data, f->size, &bw, &bh) : NULL;
    if (!px) return 0;
    for (int i = 0; i < CW * CH; i++) d->px[i] = 0xFFFFFF;
    for (int y = 0; y < bh && y < CH; y++)
        for (int x = 0; x < bw && x < CW; x++) d->px[y * CW + x] = px[y * bw + x];
    free(px);
    vfs_path(f, d->path, sizeof d->path);
    return 1;
}
static void load_bmp(const char *path, int ctx) {
    Window *w = win_of(ctx);
    if (!w) return;
    if (!load_into(w->data, path)) msgbox("Paint", "Only 24-bit BMP pictures can be opened.", IC_WARN);
    pt_title(w);
}

static void pt_menu_cb(int id, int ctx) {
    Window *w = win_of(ctx);
    if (!w) return;
    Paint *d = w->data;
    switch (id) {
    case 1: for (int i = 0; i < CW * CH; i++) d->px[i] = 0xFFFFFF; d->path[0] = 0; pt_title(w); break;
    case 2: file_dialog(FD_OPEN, "Open", ".bmp", "/Pictures", NULL, load_bmp, ctx); break;
    case 3: if (d->path[0]) { save_bmp(d->path, ctx); break; } /* fall through */
    case 10: file_dialog(FD_SAVE, "Save As", ".bmp", "/Pictures", d->path[0] ? strrchr(d->path, '/') + 1 : "Painting.bmp", save_bmp, ctx); break;
    case 4: set_custom_wallpaper(d->px, CW, CH); settings_save(); notify("Paint", "Your painting is now the desktop background."); break;
    case 5: win_close(w); break;
    case 6: for (int i = 0; i < CW * CH; i++) d->px[i] ^= 0xFFFFFF; break;
    case 7: for (int y = 0; y < CH; y++) for (int x = 0; x < CW / 2; x++) { uint32_t t = d->px[y * CW + x]; d->px[y * CW + x] = d->px[y * CW + CW - 1 - x]; d->px[y * CW + CW - 1 - x] = t; } break;
    case 8: for (int y = 0; y < CH / 2; y++) for (int x = 0; x < CW; x++) { uint32_t t = d->px[y * CW + x]; d->px[y * CW + x] = d->px[(CH - 1 - y) * CW + x]; d->px[(CH - 1 - y) * CW + x] = t; } break;
    case 9: msgbox("About Paint", "CubeXP Paint\n\nCanvas: 400 x 260. Saves 24-bit BMP pictures\nto Local Disk (C:). Try Image > Set as Background!", IC_PAINT); break;
    }
}

static void pt_mouse(Window *w, int ev, int mx, int my) {
    Paint *d = w->data;
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    int cx, cy;
    canvas_xy(mx, my, &cx, &cy);
    if (ev == EV_DOWN || ev == EV_RDOWN) {
        int m = ui_menubar_hit(mx, my, pt_menu, 3);
        if (m >= 0 && ev == EV_DOWN) {
            int px = x + 4;
            for (int i = 0; i < m; i++) px += text_w(&font_ui, pt_menu[i]) + 12;
            if (m == 0) { static const MItem it[] = { { "New", 1 }, { "Open...", 2 }, { "Save", 3 }, { "Save As...", 10 }, { NULL, 0 }, { "Exit", 5 } }; shell_popup(px - 3, y + 19, it, 6, pt_menu_cb, ctx_of(w)); }
            else if (m == 1) { static const MItem it[] = { { "Set as Background", 4 }, { NULL, 0 }, { "Invert Colors", 6 }, { "Flip Horizontal", 7 }, { "Flip Vertical", 8 }, { NULL, 0 }, { "Clear Image", 1 } }; shell_popup(px - 3, y + 19, it, 7, pt_menu_cb, ctx_of(w)); }
            else { static const MItem it[] = { { "About Paint", 9 } }; shell_popup(px - 3, y + 19, it, 1, pt_menu_cb, ctx_of(w)); }
            return;
        }
        for (int t = 0; t < T_COUNT; t++)
            if (inside(mx, my, 4 + (t % 2) * 24, CVY + 4 + (t / 2) * 24, 22, 22)) { d->tool = t; return; }
        for (int s = 0; s < 3; s++) if (inside(mx, my, 6, CVY + 110 + s * 18, 44, 16)) { d->size = s; return; }
        int py = ch - 40;
        for (int i = 0; i < 28; i++)
            if (inside(mx, my, 40 + (i % 14) * 16, py + (i / 14) * 16, 15, 15)) {
                if (ev == EV_DOWN) d->fg = palette[i]; else d->bg = palette[i];
                return;
            }
        if (cx >= 0 && cy >= 0 && cx < CW && cy < CH && mx >= CVX && my < ch - 44) {
            d->cur_col = ev == EV_DOWN ? d->fg : d->bg;
            if (d->tool == T_FILL) { cv_fill(d, cx, cy, d->cur_col); return; }
            d->drawing = 1; d->sx = d->lx = cx; d->sy = d->ly = cy;
            if (d->tool <= T_ERASER || d->tool == T_SPRAY) stroke(d, cx, cy, cx, cy);
            if (ev == EV_RDOWN) {   /* right button has no drag events; just a dot */
                d->drawing = 0;
            }
        }
        return;
    }
    if (ev == EV_DRAG && d->drawing) {
        if (cx < -50) cx = -50;
        if (cy < -50) cy = -50;
        if (d->tool <= T_ERASER || d->tool == T_SPRAY) stroke(d, d->lx, d->ly, cx, cy);
        d->lx = cx; d->ly = cy;
        return;
    }
    if (ev == EV_UP && d->drawing) {
        int r = d->size;
        if (d->tool == T_LINE) cv_line(d, d->sx, d->sy, cx, cy, r, d->cur_col);
        else if (d->tool == T_RECT) cv_rect(d, d->sx, d->sy, cx, cy, r, d->cur_col);
        else if (d->tool == T_ELLIPSE) cv_ellipse(d, d->sx, d->sy, cx, cy, r, d->cur_col);
        d->drawing = 0;
    }
}
static void pt_tick(Window *w) {   /* airbrush keeps spraying while held */
    Paint *d = w->data;
    if (d->drawing && d->tool == T_SPRAY && mouse_btn) stroke(d, d->lx, d->ly, d->lx, d->ly);
}

void open_paint(void) {
    Window *w = win_new("untitled - Paint", IC_PAINT, CVX + CW + 8 + 4, CVY + CH + 8 + 44);
    if (!w) return;
    Paint *d = calloc(1, sizeof(Paint));
    if (!d) { win_close(w); msgbox("Paint", "Not enough memory.", IC_WARN); return; }
    for (int i = 0; i < CW * CH; i++) d->px[i] = 0xFFFFFF;
    d->fg = 0; d->bg = 0xFFFFFF; d->tool = T_BRUSH; d->size = 1;
    w->app = APP_PAINT; w->data = d; w->no_max = 1;
    w->draw = pt_draw; w->mouse = pt_mouse; w->tick = pt_tick;
    w->x = 70; w->y = 20;
    pt_title(w);
}
void paint_open_file(const char *path) {
    open_paint();
    Window *w = focus;
    if (!w || w->app != APP_PAINT) return;
    if (!load_into(w->data, path)) msgbox("Paint", "Only 24-bit BMP pictures can be opened.", IC_WARN);
    pt_title(w);
}
