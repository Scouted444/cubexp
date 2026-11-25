/* Command Prompt */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <ctype.h>
#include <dirent.h>

#define CL 200
#define CC 80
typedef struct {
    char lines[CL][CC + 1];
    int n, scroll;
    char in[CC + 1];
    int len, cur;
    char hist[16][CC + 1];
    int nh, hpos;
    uint32_t fg, bg;
    int matrix;
    int mcol[80], mtick;
} Cmd;

static const uint32_t dos_col[16] = {
    0x000000, 0x000080, 0x008000, 0x008080, 0x800000, 0x800080, 0x808000, 0xC0C0C0,
    0x808080, 0x0000FF, 0x00FF00, 0x00FFFF, 0xFF0000, 0xFF00FF, 0xFFFF00, 0xFFFFFF,
};

static void out(Cmd *d, const char *s) {
    while (1) {   /* split on newlines and wrap */
        const char *e = strchr(s, '\n');
        int n = e ? (int)(e - s) : (int)strlen(s);
        do {
            int k = n > CC ? CC : n;
            if (d->n == CL) { memmove(d->lines[0], d->lines[1], sizeof d->lines[0] * (CL - 1)); d->n--; }
            memcpy(d->lines[d->n], s, k); d->lines[d->n][k] = 0; d->n++;
            s += k; n -= k;
        } while (n > 0);
        if (!e) break;
        s = e + 1;
    }
    d->scroll = 0;
}
static void outf(Cmd *d, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void outf(Cmd *d, const char *fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    out(d, b);
}

/* ---- tiny expression evaluator ---- */
static const char *ep;
static int eerr;
static double expr(void);
static void skip(void) { while (*ep == ' ') ep++; }
static double atom(void) {
    skip();
    if (*ep == '(') { ep++; double v = expr(); skip(); if (*ep == ')') ep++; else eerr = 1; return v; }
    if (*ep == '-') { ep++; return -atom(); }
    if (*ep == '+') { ep++; return atom(); }
    if (!strncmp(ep, "pi", 2)) { ep += 2; return 3.14159265358979; }
    if (!strncmp(ep, "sqrt", 4)) { ep += 4; return sqrt(atom()); }
    char *end;
    double v = strtod(ep, &end);
    if (end == ep) { eerr = 1; return 0; }
    ep = end;
    return v;
}
static double power(void) {
    double b = atom();
    skip();
    if (*ep == '^') { ep++; return pow(b, power()); }
    return b;
}
static double term(void) {
    double v = power();
    for (;;) {
        skip();
        if (*ep == '*') { ep++; v *= power(); }
        else if (*ep == '/') { ep++; double r = power(); if (r == 0) eerr = 2; else v /= r; }
        else if (*ep == '%') { ep++; double r = power(); if (r == 0) eerr = 2; else v = fmod(v, r); }
        else return v;
    }
}
static double expr(void) {
    double v = term();
    for (;;) {
        skip();
        if (*ep == '+') { ep++; v += term(); }
        else if (*ep == '-') { ep++; v -= term(); }
        else return v;
    }
}

static void prompt_line(Cmd *d) { outf(d, "C:\\Users\\%s>%s", cfg.user, d->in); }

static void run(Window *w, Cmd *d, char *line) {
    while (*line == ' ') line++;
    char *e = line + strlen(line);
    while (e > line && e[-1] == ' ') *--e = 0;
    if (!*line) return;
    char cmd[32] = "";
    int i = 0;
    while (line[i] && line[i] != ' ' && i < 31) { cmd[i] = (char)tolower((unsigned char)line[i]); i++; }
    cmd[i] = 0;
    char *arg = line + i;
    while (*arg == ' ') arg++;

    if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
        out(d, "For more information on a command, just try it.\n"
               "APPS       Lists programs you can start by name.\n"
               "CALC expr  Evaluates maths, e.g. calc (2+3)*4^2\n"
               "CLS        Clears the screen.\n"
               "COLOR xy   Sets colours (x=background, y=text, 0-F).\n"
               "COWSAY txt Has a cow say something.\n"
               "DATE/TIME  Shows the date or time.\n"
               "DEL file   Deletes a file on the SD card.\n"
               "DIR        Lists files on the SD card.\n"
               "ECHO txt   Prints a message.\n"
               "EXIT       Closes the Command Prompt.\n"
               "MATRIX     Wake up, Neo. (any key to stop)\n"
               "MEM        Shows memory usage.\n"
               "PING host  Sends pretend packets.\n"
               "SHUTDOWN   /s turn off, /r restart, /l log off.\n"
               "START app  Starts a program.\n"
               "SYSTEMINFO Shows system information.\n"
               "TASKLIST   Lists open windows. TASKKILL n closes one.\n"
               "TITLE txt  Sets the window title.\n"
               "TYPE file  Shows a text file from the SD card.\n"
               "VER        Shows the CubeXP version.\n"
               "WHOAMI     Shows the current user.");
    } else if (!strcmp(cmd, "cls")) { d->n = 0; }
    else if (!strcmp(cmd, "ver")) outf(d, "\nCubeXP [%s]\n", OS_VERSION);
    else if (!strcmp(cmd, "echo")) out(d, *arg ? arg : "ECHO is on.");
    else if (!strcmp(cmd, "time") || !strcmp(cmd, "date")) {
        time_t t = time(NULL);
        char b[64];
        strftime(b, sizeof b, cmd[0] == 't' ? "The current time is: %H:%M:%S" : "The current date is: %a %m/%d/%Y", localtime(&t));
        out(d, b);
    } else if (!strcmp(cmd, "whoami")) outf(d, "cubexp\\%s", cfg.user);
    else if (!strcmp(cmd, "hostname")) out(d, "GAMECUBE");
    else if (!strcmp(cmd, "cd") || !strcmp(cmd, "pwd")) outf(d, "C:\\Users\\%s", cfg.user);
    else if (!strcmp(cmd, "mem")) {
        outf(d, "\n%10u KB total memory\n%10u KB available\n%10u KB used by CubeXP\n",
             (unsigned)(plat_mem_total() / 1024), (unsigned)(plat_mem_free() / 1024),
             (unsigned)((plat_mem_total() - plat_mem_free()) / 1024));
    } else if (!strcmp(cmd, "systeminfo")) {
        uint64_t up = now_ms / 1000;
        outf(d, "\nHost Name:          GAMECUBE\nOS Name:            CubeXP %s\nOS Version:         %s\n"
                "System Type:        PowerPC (Gekko) 485 MHz\nGraphics:           ATI Flipper 162 MHz\n"
                "Total Memory:       24 MB 1T-SRAM (+16 MB ARAM)\nVideo Mode:         %s\nStorage:            %s\n"
                "System Up Time:     %d h %d min %d s\nFrame Rate:         %d fps\n",
             OS_EDITION, OS_VERSION, plat_video_mode(), plat_storage_ok() ? plat_storage_name() : "No SD card",
             (int)(up / 3600), (int)(up / 60 % 60), (int)(up % 60), fps);
    } else if (!strcmp(cmd, "color")) {
        int bg = 0, fg = 7;
        if (strlen(arg) >= 2 && isxdigit((unsigned char)arg[0]) && isxdigit((unsigned char)arg[1])) {
            char a[2] = { arg[0], 0 }, b[2] = { arg[1], 0 };
            bg = (int)strtol(a, NULL, 16); fg = (int)strtol(b, NULL, 16);
        } else if (strlen(arg) == 1 && isxdigit((unsigned char)arg[0])) { char b[2] = { arg[0], 0 }; fg = (int)strtol(b, NULL, 16); }
        else if (*arg) { out(d, "Usage: COLOR xy  (e.g. COLOR 0A for green on black)"); return; }
        if (bg == fg) { out(d, "Text and background can't be the same colour."); return; }
        d->bg = dos_col[bg]; d->fg = dos_col[fg];
    } else if (!strcmp(cmd, "title")) { snprintf(w->title, sizeof w->title, "%s", *arg ? arg : "Command Prompt"); }
    else if (!strcmp(cmd, "exit")) { win_close(w); }
    else if (!strcmp(cmd, "apps")) {
        for (int a = 0; a < APP_COUNT; a++) if (a != APP_MSGBOX) outf(d, "  %-10s %s", apps[a].cmd, apps[a].name);
    } else if (!strcmp(cmd, "start")) {
        int a = app_by_cmd(arg);
        if (a < 0) outf(d, "The system cannot find the program '%s'.", arg); else app_launch(a);
    } else if (!strcmp(cmd, "calc") && *arg) {
        ep = arg; eerr = 0;
        double v = expr();
        skip();
        if (*ep) eerr = 1;
        if (eerr == 2) out(d, "Divide by zero error.");
        else if (eerr) out(d, "Invalid expression.");
        else outf(d, "%.10g", v);
    } else if (!strcmp(cmd, "cowsay")) {
        const char *s = *arg ? arg : "Moo! Welcome to CubeXP.";
        int n = (int)strlen(s);
        if (n > 60) n = 60;
        char bar[64];
        memset(bar, '-', n + 2); bar[n + 2] = 0;
        outf(d, " %s\n< %.*s >\n %s", bar, n, s, bar);
        out(d, "        \\   ^__^\n         \\  (oo)\\_______\n            (__)\\       )\\/\\\n                ||----w |\n                ||     ||");
    } else if (!strcmp(cmd, "dir") || !strcmp(cmd, "ls")) {
        if (!plat_storage_ok()) { out(d, " Volume in drive C has no SD card.\n File Not Found"); return; }
        outf(d, " Directory of %s\n", plat_storage_path());
        DIR *dir = opendir(plat_storage_path());
        int files = 0;
        if (dir) {
            struct dirent *de;
            while ((de = readdir(dir))) {
                if (de->d_name[0] == '.') continue;
                char p[160];
                snprintf(p, sizeof p, "%s/%s", plat_storage_path(), de->d_name);
                FILE *f = fopen(p, "rb");
                long sz = 0;
                if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f); }
                outf(d, "  %10ld  %s", sz, de->d_name);
                files++;
            }
            closedir(dir);
        }
        outf(d, "        %d File(s)", files);
    } else if (!strcmp(cmd, "type") || !strcmp(cmd, "cat")) {
        char p[160];
        snprintf(p, sizeof p, "%s/%s", plat_storage_path(), arg);
        FILE *f = plat_storage_ok() && *arg ? fopen(p, "rb") : NULL;
        if (!f) { out(d, "The system cannot find the file specified."); return; }
        char b[4096];
        int n = (int)fread(b, 1, sizeof b - 1, f);
        fclose(f);
        b[n] = 0;
        for (int k = 0; k < n; k++) if (b[k] == '\r') b[k] = ' ';
        out(d, b);
    } else if (!strcmp(cmd, "del") || !strcmp(cmd, "rm")) {
        char p[160];
        snprintf(p, sizeof p, "%s/%s", plat_storage_path(), arg);
        if (!plat_storage_ok() || !*arg || remove(p) != 0) out(d, "Could Not Find that file.");
        else outf(d, "Deleted %s.", arg);
    } else if (!strcmp(cmd, "ping")) {
        const char *h = *arg ? arg : "localhost";
        outf(d, "\nPinging %s with 32 bytes of data:", h);
        for (int k = 0; k < 4; k++) outf(d, "Reply from %s: bytes=32 time<1ms TTL=128", !strcmp(h, "localhost") ? "127.0.0.1" : h);
        out(d, "\n(The GameCube has no network here - these replies are imaginary.)");
    } else if (!strcmp(cmd, "tasklist")) {
        out(d, "\n#  Image Name           Window Title\n== ==================== ==============================");
        for (int k = 0; k < MAX_WIN; k++)
            if (wins[k].used) outf(d, "%-2d %-20s %.30s", k, apps[wins[k].app].cmd, wins[k].title);
    } else if (!strcmp(cmd, "taskkill")) {
        int k = atoi(arg);
        if (*arg && k >= 0 && k < MAX_WIN && wins[k].used && &wins[k] != w) { outf(d, "SUCCESS: closed \"%s\".", wins[k].title); win_close(&wins[k]); }
        else out(d, "ERROR: no such task. Use TASKLIST to see numbers.");
    } else if (!strcmp(cmd, "shutdown")) {
        if (strstr(arg, "/r")) shell_restart();
        else if (strstr(arg, "/l")) shell_log_off();
        else if (strstr(arg, "/s")) shell_turn_off();
        else out(d, "Usage: SHUTDOWN /s (turn off)  /r (restart)  /l (log off)");
    } else if (!strcmp(cmd, "beep")) sound_play(SND_DING);
    else if (!strcmp(cmd, "matrix")) { d->matrix = 1; for (int k = 0; k < 80; k++) d->mcol[k] = -(rand() % 40); }
    else if (!strcmp(cmd, "tree")) out(d, "C:.\n+---Desktop\n+---Documents\n|   \\---Homework\n+---Games\n|   +---Minesweeper\n|   \\---Snake\n\\---Pictures");
    else {
        int a = app_by_cmd(cmd);
        if (a >= 0) { app_launch(a); return; }
        outf(d, "'%s' is not recognized as an internal or external command,\noperable program or batch file.", cmd);
    }
}

static void cmd_key(Window *w, int k) {
    Cmd *d = w->data;
    if (d->matrix) { d->matrix = 0; return; }
    if (k == K_ENTER) {
        prompt_line(d);
        char line[CC + 1];
        strcpy(line, d->in);
        if (d->len) {
            if (d->nh == 16) { memmove(d->hist[0], d->hist[1], sizeof d->hist[0] * 15); d->nh--; }
            strcpy(d->hist[d->nh++], d->in);
        }
        d->hpos = d->nh;
        d->in[0] = 0; d->len = d->cur = 0;
        run(w, d, line);
        if (w->used) out(d, "");
    } else if (k == K_BKSP) {
        if (d->cur > 0) { memmove(d->in + d->cur - 1, d->in + d->cur, d->len - d->cur + 1); d->cur--; d->len--; }
    } else if (k == K_LEFT) { if (d->cur > 0) d->cur--; }
    else if (k == K_RIGHT) { if (d->cur < d->len) d->cur++; }
    else if (k == K_UP || k == K_DOWN) {
        if (!d->nh) return;
        d->hpos += k == K_UP ? -1 : 1;
        if (d->hpos < 0) d->hpos = 0;
        if (d->hpos >= d->nh) { d->hpos = d->nh; d->in[0] = 0; }
        else strcpy(d->in, d->hist[d->hpos]);
        d->len = d->cur = strlen(d->in);
    } else if (k >= 32 && k < 127 && d->len < CC - 30) {
        memmove(d->in + d->cur + 1, d->in + d->cur, d->len - d->cur + 1);
        d->in[d->cur++] = (char)k; d->len++;
    }
}

static void cmd_draw(Window *w, int x, int y, int cw, int ch) {
    Cmd *d = w->data;
    fill(x, y, cw, ch, d->bg);
    int lh = 13, adv = font_mono.g['m' - 32].adv;
    if (d->matrix) {
        int cols = cw / adv, rows = ch / lh;
        for (int c = 0; c < cols && c < 80; c++) {
            int head = d->mcol[c];
            for (int r = 0; r < rows; r++) {
                int dist = head - r;
                if (dist < 0 || dist > 14) continue;
                char s[2] = { (char)(33 + ((c * 7 + r * 13 + (int)(now_ms / 90)) % 90)), 0 };
                uint32_t col = dist == 0 ? 0xD0FFD0 : mix(0x00FF40, 0x002000, dist * 18);
                text(&font_mono, x + c * adv, y + r * lh, s, col);
            }
        }
        return;
    }
    int vis = ch / lh - 1;
    if (vis < 1) vis = 1;
    int total = d->n + 1;
    int first = total - vis - d->scroll;
    if (first < 0) first = 0;
    int row = 0;
    for (int i = first; i < d->n && row < vis + 1; i++, row++) text(&font_mono, x + 3, y + 2 + row * lh, d->lines[i], d->fg);
    if (row <= vis && d->scroll == 0) {
        char p[CC + 40];
        snprintf(p, sizeof p, "C:\\Users\\%s>", cfg.user);
        int px = text(&font_mono, x + 3, y + 2 + row * lh, p, d->fg);
        text(&font_mono, x + 3 + px, y + 2 + row * lh, d->in, d->fg);
        if (w == focus && (now_ms / 400) % 2 == 0) fill(x + 3 + px + d->cur * adv, y + 2 + row * lh + 10, adv, 2, d->fg);
    }
}
static void cmd_mouse(Window *w, int ev, int mx, int my) {
    Cmd *d = w->data;
    (void)mx;
    if (ev == EV_SCROLL) {
        d->scroll -= my * 2;
        if (d->scroll < 0) d->scroll = 0;
        if (d->scroll > d->n) d->scroll = d->n;
    } else if (ev == EV_DOWN && !osk_visible()) osk_toggle();
}
static void cmd_tick(Window *w) {
    Cmd *d = w->data;
    if (!d->matrix || d->mtick++ % 3) return;
    for (int c = 0; c < 80; c++) { d->mcol[c]++; if (d->mcol[c] > 50) d->mcol[c] = -(rand() % 20); }
}

void open_cmd(void) {
    Window *w = win_new("Command Prompt", IC_CMD, 520, 300);
    if (!w) return;
    Cmd *d = calloc(1, sizeof(Cmd));
    if (!d) { win_close(w); return; }
    d->fg = 0xC0C0C0; d->bg = 0x000000;
    w->app = APP_CMD; w->resizable = 1; w->wants_keys = 1; w->data = d;
    w->draw = cmd_draw; w->key = cmd_key; w->mouse = cmd_mouse; w->tick = cmd_tick;
    outf(d, "CubeXP [%s]\n(C) Copyright 2026 CubeXP Project.\n\nType HELP for a list of commands.\n", OS_VERSION);
}
