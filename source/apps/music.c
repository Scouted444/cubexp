/* Music Maker: piano + 16-step sequencer, songs saved as .song text files */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define MRATE 22050
#define NNOTES 25          /* C4 .. C6 */
#define MAXSTEP 64
#define NINST 3
static const char *inst_names[NINST] = { "bell", "chip", "organ" };
static int16_t *samp[NINST][NNOTES];
static int samp_len;

static void synth(int inst, int note) {
    if (samp[inst][note]) return;
    int len = (int)(MRATE * 0.55f);
    samp_len = len;
    int16_t *b = plat_audio_alloc(len);
    if (!b) return;
    float f = 261.63f * powf(2.0f, note / 12.0f);
    for (int i = 0; i < len; i++) {
        float t = (float)i / MRATE, v;
        float att = i < 60 ? i / 60.f : 1.f;
        if (inst == 0) v = (sinf(6.2831853f * f * t) * 0.7f + sinf(6.2831853f * f * 2 * t) * 0.2f * expf(-t * 8) + sinf(6.2831853f * f * 3.01f * t) * 0.08f) * expf(-t * 5.5f);
        else if (inst == 1) v = (fmodf(f * t, 1.f) < 0.5f ? 0.35f : -0.35f) * (t < 0.3f ? 1.f : expf(-(t - 0.3f) * 18)) * (1 - t * 0.6f);
        else v = (sinf(6.2831853f * f * t) * 0.5f + sinf(6.2831853f * f * 2 * t) * 0.25f + sinf(6.2831853f * f * 4 * t) * 0.12f) * (t < 0.42f ? 1.f : expf(-(t - 0.42f) * 30));
        b[i] = (int16_t)(v * att * 20000);
    }
    plat_audio_ready(b, len);
    samp[inst][note] = b;
}
static void play_note(int inst, int note) {
    if (note < 0 || note >= NNOTES || !cfg.sound) return;
    synth(inst, note);
    if (samp[inst][note]) plat_play(samp[inst][note], samp_len, MRATE, 180);
}

static const char *nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static int parse_note(const char *s) {   /* "C4", "F#5" -> index from C4 */
    int b = -1;
    char u = (char)(s[0] & ~32);
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   /* A B C D E F G */
    if (u < 'A' || u > 'G') return -1;
    b = base[u - 'A'];
    s++;
    if (*s == '#') { b++; s++; } else if (*s == 'b') { b--; s++; }
    int oct = *s ? atoi(s) : 4;
    return (oct - 4) * 12 + b;
}

typedef struct { uint32_t steps[MAXSTEP]; int nsteps, tempo, inst; } Song;
static void song_parse(Song *s, const char *txt) {
    memset(s, 0, sizeof *s);
    s->tempo = 150;
    char buf[2048];
    snprintf(buf, sizeof buf, "%s", txt);
    char *save = NULL, *tok = strtok_r(buf, " \t\r\n", &save);
    while (tok) {
        if (!strcmp(tok, "tempo")) { tok = strtok_r(NULL, " \t\r\n", &save); if (tok) s->tempo = atoi(tok); }
        else if (!strcmp(tok, "inst")) { tok = strtok_r(NULL, " \t\r\n", &save); for (int i = 0; tok && i < NINST; i++) if (!strcmp(tok, inst_names[i])) s->inst = i; }
        else if (s->nsteps < MAXSTEP) {
            uint32_t m = 0;
            char *save2 = NULL, *part = strtok_r(tok, "+", &save2);
            while (part) { int n = parse_note(part); if (n >= 0 && n < NNOTES) m |= 1u << n; part = strtok_r(NULL, "+", &save2); }
            s->steps[s->nsteps++] = m;
        }
        tok = strtok_r(NULL, " \t\r\n", &save);
    }
    if (s->tempo < 40) s->tempo = 40;
    if (s->tempo > 400) s->tempo = 400;
}

/* background player for the terminal's "play" command */
static Song bg;
static int bg_pos = -1;
static uint64_t bg_t;
void music_play_notes(const char *notes, int tempo) {
    char t[600];
    snprintf(t, sizeof t, "tempo %d\ninst bell\n%s", tempo, notes);
    song_parse(&bg, t);
    bg_pos = 0; bg_t = now_ms;
}
void music_tick(void) {
    if (bg_pos < 0 || now_ms < bg_t) return;
    if (bg_pos >= bg.nsteps) { bg_pos = -1; return; }
    for (int n = 0; n < NNOTES; n++) if (bg.steps[bg_pos] & (1u << n)) play_note(bg.inst, n);
    bg_pos++;
    bg_t = now_ms + 60000 / bg.tempo;
}

/* ---- window ---- */
typedef struct { Song s; int playing, pos, page; uint64_t next; char path[160]; int lit_note; uint64_t lit_t; } MM;
#define GX 40
#define GY 58
#define CELL 22
#define ROWS 13      /* C4..C5 shown in the grid */

static int ctx_of(Window *w) { return (int)(w - wins) | (w->gen << 8); }
static Window *win_of(int ctx) { Window *w = &wins[ctx & 255]; return (w->used && w->app == APP_MUSIC && w->gen == (ctx >> 8)) ? w : NULL; }

static void mm_title(Window *w) {
    MM *d = w->data;
    snprintf(w->title, sizeof w->title, "%.28s - Music Maker", d->path[0] ? strrchr(d->path, '/') + 1 : "Untitled");
}
static const char *tbtn[] = { "Play", "Stop", "Tempo -", "Tempo +", "Sound", "Clear", "Open", "Save" };
static int tb_x(int i) { int x = 6; for (int k = 0; k < i; k++) x += text_w(&font_ui, tbtn[k]) + 20; return x; }

static void mm_draw(Window *w, int x, int y, int cw, int ch) {
    MM *d = w->data;
    grad_v(x, y, cw, ch, 0x2A2A3A, 0x16161E);
    for (int i = 0; i < 8; i++) {
        int bx = x + tb_x(i), bw = text_w(&font_ui, tbtn[i]) + 14;
        int hot = w == focus && inside(mouse_x, mouse_y, bx, y + 6, bw, 22);
        int on = (i == 0 && d->playing);
        round_fill(bx, y + 6, bw, 22, 4, on ? 0x4CAF50 : hot ? 0x56567A : 0x3E3E58);
        text_center(&font_ui, bx, y + 10, bw, tbtn[i], 0xFFFFFF);
    }
    char b[64];
    snprintf(b, sizeof b, "Tempo %d   Sound: %s   Page %d/4", d->s.tempo, inst_names[d->s.inst], d->page + 1);
    text(&font_ui, x + 8, y + 36, b, 0xB0B0D0);
    /* grid */
    for (int r = 0; r < ROWS; r++) {
        int note = ROWS - 1 - r;
        int ry = y + GY + r * (CELL - 6);
        int black = strchr("CDFGA", nn[note % 12][0]) && nn[note % 12][1] == '#';
        text(&font_small, x + 6, ry + 2, nn[note % 12], black ? 0x8080A0 : 0xD0D0F0);
        for (int c = 0; c < 16; c++) {
            int step = d->page * 16 + c;
            int cx = x + GX + c * CELL, on = (d->s.steps[step] & (1u << note)) != 0;
            uint32_t bgc = (c / 4) % 2 ? 0x30304A : 0x262638;
            if (black) bgc = shade(bgc, 85);
            if (d->playing && step == d->pos) bgc = 0x50507A;
            fill(cx, ry, CELL - 2, CELL - 8, bgc);
            if (on) round_fill(cx + 1, ry + 1, CELL - 4, CELL - 10, 3, mix(0xFF7A30, 0xFFD040, note * 20));
        }
    }
    /* step markers */
    int my = y + GY + ROWS * (CELL - 6) + 2;
    for (int c = 0; c < 16; c++) {
        int step = d->page * 16 + c;
        fill(x + GX + c * CELL, my, CELL - 2, 3, step < d->s.nsteps ? 0x6A9AFF : 0x404050);
    }
    round_fill(x + GX + 16 * CELL + 2, y + GY + 20, 18, 18, 3, 0x3E3E58); text_center(&font_bold, x + GX + 16 * CELL + 2, y + GY + 22, 18, "<", 0xFFFFFF);
    round_fill(x + GX + 16 * CELL + 2, y + GY + 42, 18, 18, 3, 0x3E3E58); text_center(&font_bold, x + GX + 16 * CELL + 2, y + GY + 44, 18, ">", 0xFFFFFF);
    /* piano */
    int py = my + 10, kw = (cw - 16) / 15, ph = ch - (py - y) - 6;
    for (int k = 0; k < 15; k++) {
        static const int wn[15] = { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24 };
        int lit = d->lit_note == wn[k] && now_ms - d->lit_t < 200;
        fill(x + 8 + k * kw, py, kw - 1, ph, lit ? 0xFFD880 : 0xF4F4F4);
        fill(x + 8 + k * kw, py + ph - 4, kw - 1, 4, 0xC8C8C8);
        if (wn[k] % 12 == 0) { snprintf(b, sizeof b, "C%d", 4 + wn[k] / 12); text_center(&font_small, x + 8 + k * kw, py + ph - 18, kw, b, 0x808080); }
    }
    for (int k = 0; k < 14; k++) {
        static const int has[7] = { 1, 1, 0, 1, 1, 1, 0 };
        if (!has[k % 7]) continue;
        static const int bn[7] = { 1, 3, -1, 6, 8, 10, -1 };
        int note = bn[k % 7] + (k / 7) * 12;
        int lit = d->lit_note == note && now_ms - d->lit_t < 200;
        fill(x + 8 + k * kw + kw * 2 / 3, py, kw * 2 / 3, ph * 3 / 5, lit ? 0xC09030 : 0x1A1A1A);
    }
}

static int piano_hit(int mx, int my, int cw, int ch, int py) {
    int kw = (cw - 16) / 15, ph = ch - py - 6;
    if (my < py || my > py + ph || mx < 8) return -1;
    int k = (mx - 8) / kw;
    if (my < py + ph * 3 / 5) {
        static const int has[7] = { 1, 1, 0, 1, 1, 1, 0 }, bn[7] = { 1, 3, -1, 6, 8, 10, -1 };
        for (int j = k - 1; j <= k; j++) {
            if (j < 0 || j >= 14 || !has[j % 7]) continue;
            int bx = 8 + j * kw + kw * 2 / 3;
            if (mx >= bx && mx < bx + kw * 2 / 3) return bn[j % 7] + (j / 7) * 12;
        }
    }
    static const int wn[15] = { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24 };
    return k < 15 ? wn[k] : -1;
}

static void song_text(MM *d, char *out, int n) {
    int L = snprintf(out, n, "tempo %d\ninst %s\n", d->s.tempo, inst_names[d->s.inst]);
    for (int i = 0; i < d->s.nsteps && L < n - 40; i++) {
        uint32_t m = d->s.steps[i];
        if (!m) L += snprintf(out + L, n - L, "-");
        int first = 1;
        for (int k = 0; k < NNOTES; k++) if (m & (1u << k)) { L += snprintf(out + L, n - L, "%s%s%d", first ? "" : "+", nn[k % 12], 4 + k / 12); first = 0; }
        L += snprintf(out + L, n - L, (i % 16 == 15) ? "\n" : " ");
    }
    if (L < n - 1) { out[L++] = '\n'; out[L] = 0; }
}
static void save_cb(const char *path, int ctx) {
    Window *w = win_of(ctx);
    if (!w) return;
    MM *d = w->data;
    char t[2048];
    song_text(d, t, sizeof t);
    VNode *f = vfs_create(NULL, path);
    if (!f || !vfs_write(f, t, strlen(t))) { msgbox("Music Maker", "The song couldn't be saved.", IC_WARN); return; }
    vfs_path(f, d->path, sizeof d->path);
    mm_title(w);
    notify("Music Maker", "Song saved.");
}
static int load(MM *d, const char *path) {
    VNode *f = vfs_lookup(NULL, path);
    if (!f || f->dir) return 0;
    char t[2048];
    int n = f->size < 2047 ? f->size : 2047;
    memcpy(t, f->data, n); t[n] = 0;
    song_parse(&d->s, t);
    vfs_path(f, d->path, sizeof d->path);
    d->pos = 0; d->page = 0;
    return 1;
}
static void open_cb(const char *path, int ctx) {
    Window *w = win_of(ctx);
    if (w && load(w->data, path)) mm_title(w);
}

static void mm_mouse(Window *w, int ev, int mx, int my) {
    MM *d = w->data;
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    if (ev != EV_DOWN && ev != EV_DRAG) return;
    if (ev == EV_DOWN)
        for (int i = 0; i < 8; i++) {
            int bx = tb_x(i), bw = text_w(&font_ui, tbtn[i]) + 14;
            if (!inside(mx, my, bx, 6, bw, 22)) continue;
            switch (i) {
            case 0: if (!d->s.nsteps) d->s.nsteps = 16; d->playing = 1; d->pos = 0; d->next = now_ms; break;
            case 1: d->playing = 0; break;
            case 2: if (d->s.tempo > 60) d->s.tempo -= 10; break;
            case 3: if (d->s.tempo < 400) d->s.tempo += 10; break;
            case 4: d->s.inst = (d->s.inst + 1) % NINST; play_note(d->s.inst, 7); break;
            case 5: memset(d->s.steps, 0, sizeof d->s.steps); d->s.nsteps = 16; d->playing = 0; break;
            case 6: file_dialog(FD_OPEN, "Open Song", ".song", "/Music", NULL, open_cb, ctx_of(w)); break;
            case 7: file_dialog(FD_SAVE, "Save Song", ".song", "/Music", d->path[0] ? strrchr(d->path, '/') + 1 : "My Song.song", save_cb, ctx_of(w)); break;
            }
            return;
        }
    if (ev == EV_DOWN && inside(mx, my, GX + 16 * CELL + 2, GY + 20, 18, 18)) { if (d->page) d->page--; return; }
    if (ev == EV_DOWN && inside(mx, my, GX + 16 * CELL + 2, GY + 42, 18, 18)) { if (d->page < 3) d->page++; return; }
    int gy1 = GY + ROWS * (CELL - 6);
    if (ev == EV_DOWN && mx >= GX && mx < GX + 16 * CELL && my >= GY && my < gy1) {
        int c = (mx - GX) / CELL, r = (my - GY) / (CELL - 6);
        int note = ROWS - 1 - r, step = d->page * 16 + c;
        d->s.steps[step] ^= 1u << note;
        if (step + 1 > d->s.nsteps) d->s.nsteps = ((step / 16) + 1) * 16;
        if (d->s.steps[step] & (1u << note)) play_note(d->s.inst, note);
        return;
    }
    int py = gy1 + 12;
    int note = piano_hit(mx, my, cw, ch, py);
    if (note >= 0 && (ev == EV_DOWN || note != d->lit_note || now_ms - d->lit_t > 250)) {
        play_note(d->s.inst, note);
        d->lit_note = note; d->lit_t = now_ms;
    }
}
static void mm_tick(Window *w) {
    MM *d = w->data;
    if (!d->playing || now_ms < d->next) return;
    if (d->pos >= d->s.nsteps) d->pos = 0;
    uint32_t m = d->s.steps[d->pos];
    for (int n = 0; n < NNOTES; n++) if (m & (1u << n)) play_note(d->s.inst, n);
    d->page = d->pos / 16;
    d->pos++;
    d->next = now_ms + 60000 / d->s.tempo;
}
static void mm_pad(Window *w, uint32_t down, uint32_t held) {
    (void)held;
    MM *d = w->data;
    static const struct { uint32_t b; int n; } map[] = { { B_LEFT, 0 }, { B_DOWN, 4 }, { B_RIGHT, 7 }, { B_UP, 12 } };
    for (int i = 0; i < 4; i++) if (down & map[i].b) { play_note(d->s.inst, map[i].n); d->lit_note = map[i].n; d->lit_t = now_ms; }
    (void)w;
}

static Window *mm_new(void) {
    Window *w = win_new("Music Maker", IC_MUSIC, GX + 16 * CELL + 30, 340);
    if (!w) return NULL;
    MM *d = calloc(1, sizeof(MM));
    d->s.tempo = 160; d->s.nsteps = 16; d->lit_note = -1;
    w->app = APP_MUSIC; w->no_max = 1; w->wants_pad = 1; w->data = d;
    w->draw = mm_draw; w->mouse = mm_mouse; w->tick = mm_tick; w->pad = mm_pad;
    mm_title(w);
    return w;
}
void open_music(void) { mm_new(); }
void music_open_file(const char *path) {
    Window *w = mm_new();
    if (!w) return;
    MM *d = w->data;
    if (load(d, path)) { mm_title(w); d->playing = 1; d->pos = 0; d->next = now_ms + 300; }
}
