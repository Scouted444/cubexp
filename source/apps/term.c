/* Terminal + Command Prompt: front-ends for the CubeXP shell */
#include "../sh.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TL 500
#define IN_MAX 190
typedef struct {
    Sh sh;
    char *lines[TL];
    int n, scroll, cols, rows;
    char in[IN_MAX + 1];
    int len, cur, hpos;
    int style;
    int mcol[100], mtick;
    uint64_t last_key;
} Term;

static const uint32_t pal_dos[16] = {
    0x000000, 0x000080, 0x008000, 0x008080, 0x800000, 0x800080, 0x808000, 0xC0C0C0,
    0x808080, 0x0000FF, 0x00FF00, 0x00FFFF, 0xFF0000, 0xFF00FF, 0xFFFF00, 0xFFFFFF,
};
static const uint32_t pal_term[16] = {
    0x16131F, 0x3465A4, 0x4E9A06, 0x06989A, 0xCC0000, 0x75507B, 0xC4A000, 0xD3D7CF,
    0x6E6A80, 0x729FCF, 0x8AE234, 0x34E2E2, 0xEF2929, 0xAD7FA8, 0xFCE94F, 0xFFFFFF,
};
static const uint32_t *pal(Term *d) { return d->style == 1 ? pal_dos : pal_term; }

static const char *chips[] = { "Tab", "Enter", "ls", "cd ..", "clear", "help", "|", ">", "History" };
#define NCHIP 9
#define BAR_H 26

static int vis_len(const char *s) { int n = 0; for (; *s; s++) { if (*s == '\x1b') { if (s[1]) s++; continue; } n++; } return n; }

static void push_line(Term *d, const char *s, int n) {
    if (d->n == TL) { free(d->lines[0]); memmove(d->lines, d->lines + 1, sizeof(char *) * (TL - 1)); d->n--; }
    char *l = malloc(n + 1);
    if (!l) return;
    memcpy(l, s, n); l[n] = 0;
    d->lines[d->n++] = l;
}
/* append text, splitting on newlines and wrapping at the current width */
static void term_out(Term *d, const char *s) {
    int cols = d->cols > 10 ? d->cols : 70;
    char buf[1024];
    int n = 0, vis = 0;
    char cur_col = 0;
    for (;; s++) {
        if (!*s || *s == '\n') {
            if (*s || n) push_line(d, buf, n);
            n = 0; vis = 0;
            if (cur_col && *s) { buf[n++] = '\x1b'; buf[n++] = cur_col; }
            if (!*s) break;
            continue;
        }
        if (*s == '\x1b' && s[1]) { if (n < 1020) { buf[n++] = *s; buf[n++] = s[1]; } cur_col = s[1] == 'r' ? 0 : s[1]; s++; continue; }
        if (*s == '\t') { do { if (n < 1020) buf[n++] = ' '; vis++; } while (vis % 4); continue; }
        if (*s == '\r') continue;
        if (vis >= cols) {
            push_line(d, buf, n);
            n = 0; vis = 0;
            if (cur_col) { buf[n++] = '\x1b'; buf[n++] = cur_col; }
        }
        if (n < 1020) buf[n++] = *s;
        vis++;
    }
    d->scroll = 0;
}

static void draw_colored(Term *d, int x, int y, const char *s, uint32_t def, int adv) {
    uint32_t c = def;
    const uint32_t *P = pal(d);
    while (*s) {
        if (*s == '\x1b' && s[1]) {
            char k = s[1];
            c = k == 'r' ? def : P[(k >= 'a' ? k - 'a' + 10 : k - '0') & 15];
            s += 2;
            continue;
        }
        const char *e = s;
        while (*e && *e != '\x1b') e++;
        text_n(&font_mono, x, y, s, (int)(e - s), c);
        x += (int)(e - s) * adv;
        s = e;
    }
}

static void run_input(Window *w, Term *d) {
    char prompt[200];
    sh_prompt(&d->sh, prompt, sizeof prompt);
    char echo[400];
    snprintf(echo, sizeof echo, "%s%s", prompt, d->in);
    term_out(d, echo);
    char line[IN_MAX + 1];
    strcpy(line, d->in);
    sh_add_history(&d->sh, line);
    d->hpos = d->sh.nh;
    d->in[0] = 0; d->len = d->cur = 0;
    SBuf out = { 0 };
    sh_run_line(&d->sh, line, &out);
    if (d->sh.req_clear) {
        for (int i = 0; i < d->n; i++) free(d->lines[i]);
        d->n = 0; d->sh.req_clear = 0;
        /* keep anything printed after clear */
    }
    if (out.n) term_out(d, out.s);
    sb_free(&out);
    if (d->sh.req_matrix) { d->sh.req_matrix = 0; for (int k = 0; k < 100; k++) d->mcol[k] = -(rand() % 40); d->mtick = 1; }
    if (d->style == 1 && line[0]) term_out(d, "");
    if (d->sh.req_exit) win_close(w);
}

static void hist_nav(Term *d, int dir) {
    if (!d->sh.nh) return;
    d->hpos += dir;
    if (d->hpos < 0) d->hpos = 0;
    if (d->hpos >= d->sh.nh) { d->hpos = d->sh.nh; d->in[0] = 0; }
    else snprintf(d->in, sizeof d->in, "%s", d->sh.hist[d->hpos]);
    d->len = d->cur = strlen(d->in);
}

static void insert_text(Term *d, const char *s) {
    for (; *s && d->len < IN_MAX; s++) {
        memmove(d->in + d->cur + 1, d->in + d->cur, d->len - d->cur + 1);
        d->in[d->cur++] = *s; d->len++;
    }
}

static void term_key(Window *w, int k) {
    Term *d = w->data;
    d->last_key = now_ms;
    if (d->mtick) { d->mtick = 0; return; }
    d->scroll = 0;
    switch (k) {
    case K_ENTER: run_input(w, d); return;
    case K_BKSP: if (d->cur > 0) { memmove(d->in + d->cur - 1, d->in + d->cur, d->len - d->cur + 1); d->cur--; d->len--; } return;
    case K_DEL: if (d->cur < d->len) { memmove(d->in + d->cur, d->in + d->cur + 1, d->len - d->cur); d->len--; } return;
    case K_LEFT: if (d->cur > 0) d->cur--; return;
    case K_RIGHT: if (d->cur < d->len) d->cur++; return;
    case K_HOME: d->cur = 0; return;
    case K_END: d->cur = d->len; return;
    case K_UP: hist_nav(d, -1); return;
    case K_DOWN: hist_nav(d, 1); return;
    case K_ESC: d->in[0] = 0; d->len = d->cur = 0; return;
    case K_TAB: {
        SBuf o = { 0 };
        sh_complete(&d->sh, d->in, &d->cur, IN_MAX, &o);
        d->len = strlen(d->in);
        if (o.n) {
            char prompt[200], echo[400];
            sh_prompt(&d->sh, prompt, sizeof prompt);
            snprintf(echo, sizeof echo, "%s%s", prompt, d->in);
            term_out(d, echo);
            term_out(d, o.s);
        }
        sb_free(&o);
        return; }
    }
    if (k >= 32 && k < 127) { char s[2] = { (char)k, 0 }; insert_text(d, s); }
}

static void chip_rect(int i, int cw, int ch, int *x, int *y, int *w) {
    int px = 4;
    for (int k = 0; k < i; k++) px += text_w(&font_ui, chips[k]) + 18;
    *x = px; *y = ch - BAR_H + 4; *w = text_w(&font_ui, chips[i]) + 14;
    (void)cw;
}

static void term_draw(Window *w, int x, int y, int cw, int ch) {
    Term *d = w->data;
    const uint32_t *P = pal(d);
    uint32_t bg = P[d->sh.bg & 15], fg = P[d->sh.fg & 15];
    int th = ch - BAR_H;
    fill(x, y, cw, th, bg);
    int adv = font_mono.g['m' - 32].adv, lh = 13;
    d->cols = (cw - 12) / adv;
    d->rows = (th - 6) / lh;
    if (d->mtick) {
        int cols = cw / adv;
        for (int c = 0; c < cols && c < 100; c++)
            for (int r = 0; r < d->rows + 1; r++) {
                int dist = d->mcol[c] - r;
                if (dist < 0 || dist > 16) continue;
                char s[2] = { (char)(33 + ((c * 7 + r * 13 + (int)(now_ms / 90)) % 90)), 0 };
                text(&font_mono, x + c * adv, y + r * lh, s, dist == 0 ? 0xD8FFD8 : mix(0x00FF50, bg, dist * 15));
            }
    } else {
        /* prompt + input as one wrapped "virtual" line */
        char prompt[200], full[420];
        sh_prompt(&d->sh, prompt, sizeof prompt);
        int plen = vis_len(prompt);
        snprintf(full, sizeof full, "%s" COL_RESET "%s", prompt, d->in);
        int cols = d->cols > 0 ? d->cols : 1;
        int in_rows = (plen + d->len) / cols + 1;
        int total = d->n + in_rows;
        int first = total - d->rows - d->scroll;
        if (first < 0) first = 0;
        int row = 0;
        for (int i = first; i < d->n && row < d->rows; i++, row++) draw_colored(d, x + 6, y + 4 + row * lh, d->lines[i], fg, adv);
        if (d->scroll == 0) {
            /* draw the input line in wrapped chunks */
            int vis = 0, r = 0;
            char chunk[420];
            int cn = 0;
            const char *s = full;
            char curc = 0;
            for (;; s++) {
                if (!*s || vis == cols) {
                    chunk[cn] = 0;
                    if (row + r < d->rows + 1) draw_colored(d, x + 6, y + 4 + (row + r) * lh, chunk, fg, adv);
                    r++; cn = 0; vis = 0;
                    if (curc) { chunk[cn++] = '\x1b'; chunk[cn++] = curc; }
                    if (!*s) break;
                }
                if (*s == '\x1b' && s[1]) { chunk[cn++] = *s; chunk[cn++] = s[1]; curc = s[1] == 'r' ? 0 : s[1]; s++; continue; }
                chunk[cn++] = *s; vis++;
            }
            int cpos = plen + d->cur;
            int cr = row + cpos / cols, cc = cpos % cols;
            int on = w == focus && ((now_ms - d->last_key) < 500 || (now_ms / 500) % 2 == 0);
            if (on && cr <= d->rows) {
                if (d->style == 1) fill(x + 6 + cc * adv, y + 4 + cr * lh + 10, adv, 2, fg);
                else {
                    fill(x + 6 + cc * adv, y + 4 + cr * lh, adv, lh, fg);
                    if (d->cur < d->len) { char s2[2] = { d->in[d->cur], 0 }; text(&font_mono, x + 6 + cc * adv, y + 4 + cr * lh, s2, bg); }
                }
            }
        } else {
            char b[40];
            snprintf(b, sizeof b, " scrolled up %d - press a key ", d->scroll);
            int tw = text_w(&font_ui, b);
            round_fill(x + cw - tw - 14, y + 4, tw + 8, 16, 4, P[8]);
            text(&font_ui, x + cw - tw - 10, y + 5, b, 0xFFFFFF);
        }
        /* scrollbar thumb */
        if (total > d->rows) {
            int track = th - 8, hgt = track * d->rows / total;
            if (hgt < 12) hgt = 12;
            int pos = track - hgt - (track - hgt) * d->scroll / (total - d->rows > 0 ? total - d->rows : 1);
            round_fill(x + cw - 6, y + 4 + pos, 4, hgt, 2, mix(bg, fg, 90));
        }
    }
    /* toolbar */
    int by = y + th;
    if (d->style == 1) grad_v(x, by, cw, BAR_H, 0xF4F3EE, 0xDDDACB);
    else grad_v(x, by, cw, BAR_H, 0x2C2640, 0x1E1A2E);
    hline(x, by, cw, d->style == 1 ? C_SHADOW : 0x3E3858);
    for (int i = 0; i < NCHIP; i++) {
        int cx, cy, cwid;
        chip_rect(i, cw, ch, &cx, &cy, &cwid);
        if (cx + cwid > cw - 4) break;
        int hot = w == focus && inside(mouse_x, mouse_y, x + cx, y + cy, cwid, 18);
        uint32_t c = d->style == 1 ? (hot ? 0xFFFFFF : 0xF8F7F2) : (hot ? 0x4A4270 : 0x37304E);
        round_fill(x + cx, y + cy, cwid, 18, 4, d->style == 1 ? 0x9A9888 : 0x58507A);
        round_fill(x + cx + 1, y + cy + 1, cwid - 2, 16, 4, c);
        text_center(&font_ui, x + cx, y + cy + 2, cwid, chips[i], d->style == 1 ? 0x202020 : (i < 2 ? 0x8AE234 : 0xE0E0F0));
    }
}

static void term_mouse(Window *w, int ev, int mx, int my) {
    Term *d = w->data;
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    if (ev == EV_SCROLL) {
        d->scroll -= my * 3;
        int total = d->n + 1;
        if (d->scroll > total - d->rows) d->scroll = total - d->rows;
        if (d->scroll < 0) d->scroll = 0;
        return;
    }
    if (ev != EV_DOWN) return;
    if (my >= ch - BAR_H) {
        for (int i = 0; i < NCHIP; i++) {
            int cx, cy, cwid;
            chip_rect(i, cw, ch, &cx, &cy, &cwid);
            if (!inside(mx, my, cx, cy, cwid, 18)) continue;
            sound_play(SND_CLICK);
            d->scroll = 0;
            const char *c = chips[i];
            if (!strcmp(c, "Tab")) term_key(w, K_TAB);
            else if (!strcmp(c, "Enter")) term_key(w, K_ENTER);
            else if (!strcmp(c, "History")) term_key(w, K_UP);
            else if (!strcmp(c, "|") || !strcmp(c, ">")) { char s[4]; snprintf(s, sizeof s, " %s ", c); insert_text(d, s); }
            else if (d->len == 0) { insert_text(d, c); run_input(w, d); }
            else { insert_text(d, c); insert_text(d, " "); }
            return;
        }
        return;
    }
    if (!osk_visible()) osk_toggle();
}

static void term_tick(Window *w) {
    Term *d = w->data;
    if (!d->mtick || d->mtick++ % 3) return;
    for (int c = 0; c < 100; c++) { d->mcol[c]++; if (d->mcol[c] > d->rows + 18) d->mcol[c] = -(rand() % 25); }
}
static void term_close(Window *w) {
    Term *d = w->data;
    for (int i = 0; i < d->n; i++) free(d->lines[i]);
}

static void open_term_style(int style) {
    Window *w = win_new(style ? "Command Prompt" : "Terminal", style ? IC_CMD : IC_TERMINAL, 540, 320);
    if (!w) return;
    Term *d = calloc(1, sizeof(Term));
    if (!d) { win_close(w); return; }
    d->style = style;
    sh_init(&d->sh, style, w);
    d->cols = 82;
    w->app = style ? APP_CMD : APP_TERMINAL; w->resizable = 1; w->wants_keys = 1; w->data = d;
    w->min_w = 300; w->min_h = 160;
    w->draw = term_draw; w->key = term_key; w->mouse = term_mouse; w->tick = term_tick; w->on_close = term_close;
    if (style) {
        char b[160];
        snprintf(b, sizeof b, "CubeXP [%s]\n(C) Copyright 2026 CubeXP Project.\n", OS_VERSION);
        term_out(d, b);
        term_out(d, "");
    } else {
        term_out(d, COL(e) "CubeXP Terminal" COL_RESET " - a real shell for your GameCube.");
        VNode *m = vfs_lookup(NULL, "/System/motd.txt");
        if (m && m->size) { char *t = malloc(m->size + 1); if (t) { memcpy(t, m->data, m->size); t[m->size] = 0; term_out(d, t); free(t); } }
        term_out(d, COL(8) "Tip: tap the chips below, press X for the keyboard, Tab completes names." COL_RESET);
        term_out(d, "");
        VNode *ar = vfs_lookup(NULL, "/System/autorun.sh");
        if (ar && ar->size) { SBuf o = { 0 }; sh_run_line(&d->sh, "sh /System/autorun.sh", &o); if (o.n) term_out(d, o.s); sb_free(&o); }
    }
}
void open_cmd(void) { open_term_style(1); }
#ifndef CUBELINUX
void open_terminal(void) { open_term_style(0); }

/* used by Explorer / desktop: run a script in a new terminal */
void terminal_run(const char *cmdline) {
    open_term_style(0);
    Window *w = focus;
    if (!w || w->app != APP_TERMINAL) return;
    Term *d = w->data;
    snprintf(d->in, sizeof d->in, "%s", cmdline);
    d->len = d->cur = strlen(d->in);
    run_input(w, d);
}
#endif
