/* Themes, wallpapers, settings persistence, synthesized sounds */
#include "os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

Settings cfg = { 0, 0, 3, 0, 1, 3, 0, "Player" };

const Theme themes[3] = {
    /* name, t1, t2, t3, tbot, frame, frame_in, title_txt, title_in1, title_in2,
       bar1, bar2, bar_hi, tray1, tray2, btn, btn_act, start1, start2, menu_side */
    { "Default (blue)", 0x3D95FF, 0x0058EE, 0x0062FF, 0x0042C8, 0x0055EA, 0x7B9CE6, 0xD8E4F8, 0x9DB9EB, 0x7B9CE6,
      0x245EDB, 0x1941A5, 0x3F8CF3, 0x0F8BEE, 0x0C59B9, 0x3C81F3, 0x1E52B7, 0x3C9A3C, 0x2B7A2B, 0xD3E5FA },
    { "Olive Green", 0xC4D3A0, 0x8BA169, 0x99AF77, 0x6C8248, 0x8BA169, 0xC6CEAE, 0xF0F4E0, 0xD8DFC4, 0xC4CDA8,
      0x9DAE7C, 0x6C7D4D, 0xC1D09E, 0xA9BB86, 0x7C8E59, 0xB3C392, 0x6F8150, 0x4D8F3E, 0x356C29, 0xE3E9D2 },
    { "Silver", 0xFFFFFF, 0xD6D7E3, 0xC6C7D7, 0x9FA0B5, 0xC0C1D2, 0xE0E1EA, 0x9A9AAA, 0xF0F0F6, 0xE0E1EA,
      0xC2C4D6, 0x8E90A8, 0xF4F4FA, 0xE3E4EE, 0xA6A8BF, 0xE4E5EE, 0xA9ABC2, 0x3C9A3C, 0x2B7A2B, 0xEDEDF3 },
};

const char *wallpaper_names[] = { "Rolling Hills", "Azure", "Sunset", "Starfield", "Classic Teal", "CubeXP Logo", "My Painting" };
int wallpaper_count = 6;
static uint32_t *custom;
static int cust_w, cust_h;

void set_custom_wallpaper(const uint32_t *img, int w, int h) {
    free(custom);
    custom = malloc(sizeof(uint32_t) * w * h);
    if (!custom) return;
    memcpy(custom, img, sizeof(uint32_t) * w * h);
    cust_w = w; cust_h = h;
    wallpaper_count = 7;
    cfg.wallpaper = 6;
    shell_wallpaper_dirty();
    if (vfs_system) {   /* keep it across restarts */
        int sz;
        uint8_t *b = bmp_encode(img, w, h, &sz);
        VNode *f = b ? vfs_create(vfs_system, "wallpaper.bmp") : NULL;
        if (f) vfs_write(f, b, sz);
        free(b);
    }
}
void wallpaper_restore(void) {
    VNode *f = vfs_lookup(NULL, "/System/wallpaper.bmp");
    int w, h;
    uint32_t *px = f ? bmp_decode(f->data, f->size, &w, &h) : NULL;
    if (!px) { if (cfg.wallpaper == 6) cfg.wallpaper = 0; return; }
    int keep = cfg.wallpaper;
    free(custom);
    custom = px; cust_w = w; cust_h = h; wallpaper_count = 7;
    cfg.wallpaper = keep;
}

static uint32_t rng = 12345;
static int rnd(int n) { rng = rng * 1103515245 + 12345; return (int)((rng >> 16) % (unsigned)n); }

static void blob(uint32_t *d, int cx, int cy, int r, uint32_t c, int a) {
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++) {
            if (x < 0 || y < 0 || x >= SW || y >= SH) continue;
            int dd = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            if (dd > r * r) continue;
            int k = a * (r * r - dd) / (r * r);
            d[y * SW + x] = mix(d[y * SW + x], c, k);
        }
}

void wallpaper_render(int idx, uint32_t *d) {
    rng = 777;
    if (idx == 6) {
        if (!custom) idx = 0;
        else {
            for (int y = 0; y < SH; y++)
                for (int x = 0; x < SW; x++) d[y * SW + x] = custom[(y * cust_h / SH) * cust_w + x * cust_w / SW];
            return;
        }
    }
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) {
            uint32_t c = 0;
            switch (idx) {
            case 0: {
                float h1 = 300 + 28 * sinf(x * 0.006f + 0.6f) + 18 * sinf(x * 0.017f);
                float h2 = 350 + 35 * sinf(x * 0.004f + 2.2f);
                if (y < h1 && y < h2) c = mix(0x1F5FCF, 0xA8D2F5, y * 256 / 330);
                else if (y < h2) c = mix(0x7BC24A, 0x3E8E22, (int)((y - h1) * 256 / (SH - h1 + 1)));
                else c = mix(0x5DAE2B, 0x22621A, (int)((y - h2) * 256 / (SH - h2 + 1)));
                break; }
            case 1: {
                float dx = x - 420.f, dy = y - 120.f;
                int t = (int)(sqrtf(dx * dx + dy * dy) * 0.45f);
                if (t > 256) t = 256;
                c = mix(0x5AA0F0, 0x0A2A80, t);
                float wv = y - (330 + 25 * sinf(x * 0.012f));
                if (wv > 0 && wv < 3) c = mix(c, 0xFFFFFF, 60);
                float wv2 = y - (360 + 30 * sinf(x * 0.009f + 1.0f));
                if (wv2 > 0 && wv2 < 2) c = mix(c, 0xFFFFFF, 40);
                break; }
            case 2: {
                c = y < 240 ? mix(0x2A1A60, 0xF07030, y * 256 / 240) : mix(0xF07030, 0xFFD080, (y - 240) * 256 / 120);
                float dx = x - 320.f, dy = y - 300.f;
                float r = sqrtf(dx * dx + dy * dy);
                if (r < 60) c = mix(c, 0xFFF0C0, 220);
                else if (r < 110) c = mix(c, 0xFFE0A0, (int)((110 - r) * 2));
                float hill = 330 + 30 * sinf(x * 0.01f) + 12 * sinf(x * 0.05f);
                if (y > hill) c = mix(0x301838, 0x100818, (int)((y - hill) * 256 / (SH - hill + 1)));
                break; }
            case 3:
                c = mix(0x000010, 0x101840, y * 256 / SH);
                break;
            case 4:
                c = 0x3A6EA5;
                break;
            case 5:
                c = mix(0x5A8FE8, 0x1440A0, (x + y) * 256 / (SW + SH));
                break;
            }
            d[y * SW + x] = c;
        }
    if (idx == 0)
        for (int i = 0; i < 14; i++) {
            int cx = rnd(SW), cy = 40 + rnd(160);
            for (int k = 0; k < 6; k++) blob(d, cx + rnd(90) - 45, cy + rnd(20) - 10, 18 + rnd(22), 0xFFFFFF, 150);
        }
    if (idx == 3) {
        blob(d, 470, 160, 140, 0x8040C0, 70);
        blob(d, 400, 210, 100, 0x2060E0, 60);
        for (int i = 0; i < 500; i++) {
            int x = rnd(SW), y = rnd(SH), b = 100 + rnd(156);
            d[y * SW + x] = RGB(b, b, b);
            if (rnd(20) == 0 && x > 0 && y > 0 && x < SW - 1 && y < SH - 1) {
                d[y * SW + x + 1] = d[y * SW + x - 1] = d[(y + 1) * SW + x] = d[(y - 1) * SW + x] = RGB(b / 2, b / 2, b);
            }
        }
    }
    if (idx == 5) {   /* draw the logo big into the buffer via fb swap trick */
        uint32_t *save = malloc(sizeof(uint32_t) * SW * SH);
        if (save) {
            memcpy(save, fb, sizeof(uint32_t) * SW * SH);
            memcpy(fb, d, sizeof(uint32_t) * SW * SH);
            draw_icon(IC_LOGO, 224, 110, 192);
            int tw = text_w(&font_huge, "CubeXP");
            text(&font_huge, 320 - tw / 2 + 2, 322, "CubeXP", 0x0A2050);
            text(&font_huge, 320 - tw / 2, 320, "CubeXP", 0xFFFFFF);
            memcpy(d, fb, sizeof(uint32_t) * SW * SH);
            memcpy(fb, save, sizeof(uint32_t) * SW * SH);
            free(save);
        }
    }
}

/* ---------- settings ---------- */
void settings_load(void) {
    if (!plat_storage_ok()) return;
    char p[64];
    snprintf(p, sizeof p, "%s/settings.cfg", plat_storage_path());
    FILE *f = fopen(p, "r");
    if (!f) return;
    char line[96];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = eq + 1;
        v[strcspn(v, "\r\n")] = 0;
        int n = atoi(v);
        if (!strcmp(line, "wallpaper") && n >= 0 && n < 7) cfg.wallpaper = n;
        else if (!strcmp(line, "theme") && n >= 0 && n < 3) cfg.theme = n;
        else if (!strcmp(line, "cursor") && n >= 1 && n <= 5) cfg.cursor_speed = n;
        else if (!strcmp(line, "clock24")) cfg.clock24 = !!n;
        else if (!strcmp(line, "sound")) cfg.sound = !!n;
        else if (!strcmp(line, "saver")) cfg.saver_min = n;
        else if (!strcmp(line, "secs")) cfg.show_secs = !!n;
        else if (!strcmp(line, "user") && *v) { strncpy(cfg.user, v, sizeof cfg.user - 1); }
    }
    fclose(f);
}
void settings_save(void) {
    if (!plat_storage_ok()) return;
    char p[64];
    snprintf(p, sizeof p, "%s/settings.cfg", plat_storage_path());
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "wallpaper=%d\ntheme=%d\ncursor=%d\nclock24=%d\nsound=%d\nsaver=%d\nsecs=%d\nuser=%s\n",
            cfg.wallpaper, cfg.theme, cfg.cursor_speed, cfg.clock24, cfg.sound, cfg.saver_min, cfg.show_secs, cfg.user);
    fclose(f);
}

/* ---------- sound synthesis ---------- */
#define RATE 32000
static int16_t *snd[SND_COUNT];
static int snd_len[SND_COUNT];

typedef struct { float t, freq, dur, vol; int wave; } Note; /* wave 0 sine-bell, 1 square, 2 noise */

static void render(int id, const Note *n, int count, float total) {
    int len = (int)(total * RATE);
    float *acc = calloc(len, sizeof(float));
    int16_t *out = plat_audio_alloc(len);
    if (!acc || !out) { free(acc); free(out); return; }
    uint32_t nz = 99;
    for (int k = 0; k < count; k++) {
        int s0 = (int)(n[k].t * RATE), sl = (int)(n[k].dur * RATE);
        for (int i = 0; i < sl && s0 + i < len; i++) {
            float t = (float)i / RATE;
            float env = expf(-t * 4.0f / n[k].dur) * (i < 80 ? i / 80.f : 1.f);
            float v;
            if (n[k].wave == 0) v = sinf(6.2831853f * n[k].freq * t) * 0.8f + sinf(6.2831853f * n[k].freq * 2.01f * t) * 0.2f;
            else if (n[k].wave == 1) v = fmodf(n[k].freq * t, 1.f) < 0.5f ? 0.5f : -0.5f;
            else { nz = nz * 1664525 + 1013904223; v = ((int)(nz >> 16) % 2000 - 1000) / 1000.f; }
            acc[s0 + i] += v * env * n[k].vol;
        }
    }
    /* small echo for a roomier sound */
    int dl = RATE / 7;
    for (int i = dl; i < len; i++) acc[i] += acc[i - dl] * 0.28f;
    for (int i = 0; i < len; i++) {
        float v = acc[i];
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        out[i] = (int16_t)(v * 26000);
    }
    free(acc);
    plat_audio_ready(out, len);
    snd[id] = out; snd_len[id] = len;
}

void sound_init(void) {
    /* original "cube" chime: rising pentatonic arpeggio with a soft pad */
    static const Note startup[] = {
        { 0.00f, 311.1f, 2.6f, 0.22f, 0 }, { 0.00f, 466.2f, 2.6f, 0.16f, 0 },
        { 0.10f, 622.3f, 1.2f, 0.30f, 0 }, { 0.32f, 698.5f, 1.2f, 0.28f, 0 },
        { 0.54f, 932.3f, 1.4f, 0.28f, 0 }, { 0.82f, 1244.5f, 2.2f, 0.26f, 0 },
        { 0.82f, 622.3f, 2.4f, 0.20f, 0 }, { 0.82f, 415.3f, 2.4f, 0.18f, 0 },
    };
    static const Note shutdown[] = {
        { 0.00f, 1046.5f, 1.0f, 0.25f, 0 }, { 0.22f, 784.0f, 1.0f, 0.25f, 0 },
        { 0.44f, 659.3f, 1.0f, 0.25f, 0 }, { 0.66f, 523.3f, 2.0f, 0.28f, 0 }, { 0.66f, 261.6f, 2.0f, 0.2f, 0 },
    };
    static const Note logon[] = {
        { 0.00f, 523.3f, 0.9f, 0.25f, 0 }, { 0.15f, 784.0f, 0.9f, 0.25f, 0 }, { 0.30f, 1046.5f, 1.2f, 0.25f, 0 },
    };
    static const Note ding[] = { { 0, 880.0f, 0.9f, 0.35f, 0 }, { 0, 1318.5f, 0.6f, 0.12f, 0 } };
    static const Note error[] = { { 0, 220.0f, 0.35f, 0.25f, 1 }, { 0, 233.1f, 0.35f, 0.2f, 1 } };
    static const Note click[] = { { 0, 2400.0f, 0.03f, 0.18f, 0 } };
    static const Note notify[] = { { 0, 987.8f, 0.5f, 0.3f, 0 }, { 0.12f, 1318.5f, 0.7f, 0.3f, 0 } };
    static const Note boom[] = { { 0, 0, 0.7f, 0.6f, 2 }, { 0, 60, 0.6f, 0.4f, 1 } };
    static const Note eat[] = { { 0, 1200, 0.08f, 0.25f, 1 }, { 0.05f, 1600, 0.08f, 0.25f, 1 } };
    render(SND_STARTUP, startup, 8, 3.4f);
    render(SND_SHUTDOWN, shutdown, 5, 2.8f);
    render(SND_LOGON, logon, 3, 1.6f);
    render(SND_DING, ding, 2, 1.0f);
    render(SND_ERROR, error, 2, 0.45f);
    render(SND_CLICK, click, 1, 0.05f);
    render(SND_NOTIFY, notify, 2, 0.9f);
    render(SND_EXPLODE, boom, 2, 0.8f);
    render(SND_EAT, eat, 2, 0.15f);
}

void sound_play(int id) {
    if (!cfg.sound || id < 0 || id >= SND_COUNT || !snd[id]) return;
    plat_play(snd[id], snd_len[id], RATE, 200);
}
