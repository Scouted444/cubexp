/* On-Screen Keyboard (never takes focus; types into the active window) */
#include "../os.h"
#include <string.h>
#include <stdlib.h>

#define U 26
typedef struct { const char *lo, *hi; int w4; int code; } Key;  /* w4 = width in quarter units */
enum { KC_CHAR = 0, KC_BKSP = -1, KC_TAB = -2, KC_CAPS = -3, KC_ENTER = -4, KC_SHIFT = -5, KC_SPACE = -6,
       KC_LEFT = -7, KC_RIGHT = -8, KC_UP = -9, KC_DOWN = -10, KC_DEL = -11, KC_HOME = -12, KC_END = -13 };

static const Key row0[] = { {"`","~",4,0},{"1","!",4,0},{"2","@",4,0},{"3","#",4,0},{"4","$",4,0},{"5","%",4,0},{"6","^",4,0},{"7","&",4,0},{"8","*",4,0},{"9","(",4,0},{"0",")",4,0},{"-","_",4,0},{"=","+",4,0},{"Bksp","Bksp",8,KC_BKSP} };
static const Key row1[] = { {"Tab","Tab",6,KC_TAB},{"q","Q",4,0},{"w","W",4,0},{"e","E",4,0},{"r","R",4,0},{"t","T",4,0},{"y","Y",4,0},{"u","U",4,0},{"i","I",4,0},{"o","O",4,0},{"p","P",4,0},{"[","{",4,0},{"]","}",4,0},{"\\","|",6,0} };
static const Key row2[] = { {"Caps","Caps",7,KC_CAPS},{"a","A",4,0},{"s","S",4,0},{"d","D",4,0},{"f","F",4,0},{"g","G",4,0},{"h","H",4,0},{"j","J",4,0},{"k","K",4,0},{"l","L",4,0},{";",":",4,0},{"'","\"",4,0},{"Enter","Enter",9,KC_ENTER} };
static const Key row3[] = { {"Shift","Shift",9,KC_SHIFT},{"z","Z",4,0},{"x","X",4,0},{"c","C",4,0},{"v","V",4,0},{"b","B",4,0},{"n","N",4,0},{"m","M",4,0},{",","<",4,0},{".",">",4,0},{"/","?",4,0},{"Shift","Shift",11,KC_SHIFT} };
static const Key row4[] = { {"Home","Home",6,KC_HOME},{"End","End",6,KC_END},{"","",26,KC_SPACE},{"Del","Del",5,KC_DEL},{"<","<",4,KC_LEFT},{"^","^",4,KC_UP},{"v","v",4,KC_DOWN},{">",">",5,KC_RIGHT} };
static const Key *rows[5] = { row0, row1, row2, row3, row4 };
static const int rown[5] = { 14, 14, 13, 12, 8 };

typedef struct { int shift, caps; int flash_r, flash_k; uint64_t flash_t; } OSK;

static void key_rect(int r, int k, int *x, int *y, int *w) {
    int px = 0;
    for (int i = 0; i < k; i++) px += rows[r][i].w4 * U / 4;
    *x = 6 + px; *y = 6 + r * (U + 2); *w = rows[r][k].w4 * U / 4 - 2;
}

static void osk_draw(Window *win, int x, int y, int cw, int ch) {
    OSK *d = win->data;
    fill(x, y, cw, ch, 0xD4D0C8);
    for (int r = 0; r < 5; r++)
        for (int k = 0; k < rown[r]; k++) {
            int kx, ky, kw;
            key_rect(r, k, &kx, &ky, &kw);
            const Key *K = &rows[r][k];
            int hot = inside(mouse_x, mouse_y, x + kx, y + ky, kw, U);
            int lit = (K->code == KC_SHIFT && d->shift) || (K->code == KC_CAPS && d->caps) ||
                      (d->flash_r == r && d->flash_k == k && now_ms - d->flash_t < 120);
            uint32_t c1 = lit ? 0xB0C8F8 : hot ? 0xFFFFFF : 0xF4F4F0, c2 = lit ? 0x7CA0E8 : hot ? 0xE8E8F8 : 0xDCDAD0;
            round_fill(x + kx, y + ky, kw, U, 3, 0x808080);
            round_grad(x + kx + 1, y + ky + 1, kw - 2, U - 2, 3, c1, c2, 0);
            int up = d->shift ^ (d->caps && K->lo[0] >= 'a' && K->lo[0] <= 'z' && !K->lo[1]);
            const char *lab = up ? K->hi : K->lo;
            const Font *f = strlen(lab) > 1 ? &font_ui : &font_bold;
            text_center(f, x + kx, y + ky + 7, kw, lab, 0x000000);
        }
}

static void osk_mouse(Window *win, int ev, int mx, int my) {
    OSK *d = win->data;
    if (ev != EV_DOWN && ev != EV_DBL) return;
    for (int r = 0; r < 5; r++)
        for (int k = 0; k < rown[r]; k++) {
            int kx, ky, kw;
            key_rect(r, k, &kx, &ky, &kw);
            if (!inside(mx, my, kx, ky, kw, U)) continue;
            const Key *K = &rows[r][k];
            d->flash_r = r; d->flash_k = k; d->flash_t = now_ms;
            sound_play(SND_CLICK);
            switch (K->code) {
            case KC_SHIFT: d->shift = !d->shift; return;
            case KC_CAPS: d->caps = !d->caps; return;
            case KC_BKSP: send_key(K_BKSP); return;
            case KC_TAB: send_key(K_TAB); return;
            case KC_ENTER: send_key(K_ENTER); return;
            case KC_SPACE: send_key(' '); return;
            case KC_LEFT: send_key(K_LEFT); return;
            case KC_RIGHT: send_key(K_RIGHT); return;
            case KC_UP: send_key(K_UP); return;
            case KC_DOWN: send_key(K_DOWN); return;
            case KC_DEL: send_key(K_DEL); return;
            case KC_HOME: send_key(K_HOME); return;
            case KC_END: send_key(K_END); return;
            default: {
                int up = d->shift ^ (d->caps && K->lo[0] >= 'a' && K->lo[0] <= 'z');
                send_key((unsigned char)(up ? K->hi[0] : K->lo[0]));
                d->shift = 0;
                return; }
            }
        }
}

void open_osk(void) {
    Window *prev = focus;
    Window *w = win_new("On-Screen Keyboard", IC_KEYBOARD, 15 * U + 12 - 2 + 4, 5 * (U + 2) + 10);
    if (!w) return;
    w->app = APP_OSK; w->no_max = 1; w->nofocus = 1; w->topmost = 1;
    w->data = calloc(1, sizeof(OSK));
    ((OSK *)w->data)->flash_r = -1;
    w->draw = osk_draw; w->mouse = osk_mouse;
    w->x = (SW - w->w) / 2; w->y = SH - TASKBAR_H - w->h - 2;
    focus = prev;
}

int osk_visible(void) { Window *w = win_find(APP_OSK); return w && !w->minimized; }
void osk_toggle(void) {
    Window *w = win_find(APP_OSK);
    if (w) win_close(w);
    else open_osk();
}
