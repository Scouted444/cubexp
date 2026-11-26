/* CubeLinux Terminal: a real shell (/bin/sh) on a pseudo-terminal, shown with the VT100 module */
#ifdef CUBELINUX
#define _GNU_SOURCE
#include "../os.h"
#include "../vt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

typedef struct { VT *vt; int master; pid_t pid; int ctrl, exited; } PT;
static const char *chips[] = { "Ctrl", "Ctrl+C", "Ctrl+D", "Tab", "Esc", "Up", "Down", "Enter", "Paste", "clear" };
#define NCH 10
#define BAR 26

static void pt_send(PT *d, const char *s, int n) {
    if (d->master < 0 || d->exited) return;
    while (n > 0) {
        ssize_t k = write(d->master, s, n);
        if (k <= 0) { if (errno == EAGAIN || errno == EINTR) continue; return; }
        s += k; n -= (int)k;
    }
}
static void pt_reply(void *ctx, const char *s) { pt_send(ctx, s, (int)strlen(s)); }

static int pt_spawn(PT *d) {
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) || unlockpt(m)) { if (m >= 0) close(m); return -1; }
    const char *sn = ptsname(m);
    struct winsize ws = { VR, VC, 0, 0 };
    ioctl(m, TIOCSWINSZ, &ws);
    pid_t pid = fork();
    if (pid < 0) { close(m); return -1; }
    if (pid == 0) {
        setsid();
        int s = open(sn, O_RDWR);
        if (s < 0) _exit(1);
        ioctl(s, TIOCSCTTY, 0);
        dup2(s, 0); dup2(s, 1); dup2(s, 2);
        if (s > 2) close(s);
        close(m);
        signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL); signal(SIGCHLD, SIG_DFL); signal(SIGPIPE, SIG_DFL);
        const char *home = getenv("HOME");
        if (!home) home = "/home/player";
        setenv("TERM", "vt100", 1);
        setenv("HOME", home, 1);
        setenv("USER", cfg.user, 1);
        if (chdir(home)) {}
        execl("/bin/sh", "-sh", (char *)NULL);
        _exit(127);
    }
    fcntl(m, F_SETFL, fcntl(m, F_GETFL) | O_NONBLOCK);
    d->master = m; d->pid = pid; d->exited = 0;
    return 0;
}

static void pt_tick(Window *w) {
    PT *d = w->data;
    if (d->master < 0) return;
    char buf[4096];
    for (int loops = 0; loops < 8; loops++) {
        ssize_t n = read(d->master, buf, sizeof buf);
        if (n > 0) { vt_write(d->vt, buf, (int)n); continue; }
        break;
    }
    if (!d->exited && d->pid > 0) {
        int st;
        if (waitpid(d->pid, &st, WNOHANG) == d->pid) {
            d->exited = 1;
            const char *m = "\r\n\x1b[33m[ The shell has exited. Press Enter for a new one. ]\x1b[0m\r\n";
            vt_write(d->vt, m, (int)strlen(m));
        }
    }
}

static void pt_draw(Window *w, int x, int y, int cw, int ch) {
    PT *d = w->data;
    int th = ch - BAR;
    fill(x, y, cw, th, vt_bg_color());
    if (d->master < 0) {
        text_center(&font_bold, x, y + th / 2 - 8, cw, "Couldn't start a shell (no pseudo-terminals?)", 0xFFFFFF);
    } else vt_draw(d->vt, x + 6, y + 4, w == focus && !d->exited && (now_ms / 500) % 2 == 0);
    if (vt_viewing_history(d->vt)) {
        char b[48];
        snprintf(b, sizeof b, " scrollback -%d ", vt_viewing_history(d->vt));
        int tw = text_w(&font_ui, b);
        round_fill(x + cw - tw - 14, y + 4, tw + 8, 16, 4, 0x3465A4);
        text(&font_ui, x + cw - tw - 10, y + 5, b, 0xFFFFFF);
    }
    int by = y + th;
    grad_v(x, by, cw, BAR, 0x2C2C38, 0x1C1C24);
    hline(x, by, cw, 0x40404E);
    int px = x + 4;
    for (int i = 0; i < NCH; i++) {
        int bw = text_w(&font_ui, chips[i]) + 14;
        int lit = i == 0 && d->ctrl;
        int hot = w == focus && inside(mouse_x, mouse_y, px, by + 4, bw, 18);
        round_fill(px, by + 4, bw, 18, 4, 0x55556A);
        round_fill(px + 1, by + 5, bw - 2, 16, 4, lit ? 0x4E9A06 : hot ? 0x484860 : 0x34344A);
        text_center(&font_ui, px, by + 6, bw, chips[i], 0xE8E8F0);
        px += bw + 4;
    }
}

static void pt_byte(PT *d, int c) {
    if (d->ctrl && c >= 0x40 && c < 0x80) { c &= 0x1f; d->ctrl = 0; }
    char b = (char)c;
    pt_send(d, &b, 1);
}
static void pt_key(Window *w, int k) {
    PT *d = w->data;
    vt_scroll_view(d->vt, -100000);
    if (d->exited) { if (k == K_ENTER) { close(d->master); d->master = -1; vt_reset(d->vt); pt_spawn(d); } return; }
    switch (k) {
    case K_ENTER: pt_send(d, "\r", 1); return;
    case K_BKSP: pt_send(d, "\x7f", 1); return;
    case K_TAB: pt_send(d, "\t", 1); return;
    case K_ESC: pt_send(d, "\x1b", 1); return;
    case K_UP: pt_send(d, "\x1b[A", 3); return;
    case K_DOWN: pt_send(d, "\x1b[B", 3); return;
    case K_RIGHT: pt_send(d, "\x1b[C", 3); return;
    case K_LEFT: pt_send(d, "\x1b[D", 3); return;
    case K_HOME: pt_send(d, "\x1b[H", 3); return;
    case K_END: pt_send(d, "\x1b[F", 3); return;
    case K_DEL: pt_send(d, "\x1b[3~", 4); return;
    }
    if (k >= 32 && k < 127) pt_byte(d, k);
}
static void pt_mouse(Window *w, int ev, int mx, int my) {
    PT *d = w->data;
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    if (ev == EV_SCROLL) { vt_scroll_view(d->vt, -my * 3); return; }
    if (ev != EV_DOWN) return;
    if (my >= ch - BAR) {
        int px = 4;
        for (int i = 0; i < NCH; i++) {
            int bw = text_w(&font_ui, chips[i]) + 14;
            if (inside(mx, my, px, ch - BAR + 4, bw, 18)) {
                sound_play(SND_CLICK);
                vt_scroll_view(d->vt, -100000);
                switch (i) {
                case 0: d->ctrl = !d->ctrl; break;
                case 1: pt_send(d, "\x03", 1); break;
                case 2: pt_send(d, "\x04", 1); break;
                case 3: pt_send(d, "\t", 1); break;
                case 4: pt_send(d, "\x1b", 1); break;
                case 5: pt_send(d, "\x1b[A", 3); break;
                case 6: pt_send(d, "\x1b[B", 3); break;
                case 7: pt_key(w, K_ENTER); break;
                case 8: for (const char *p = clipboard; *p; p++) { char c = *p == '\n' ? '\r' : *p; pt_send(d, &c, 1); } break;
                case 9: pt_send(d, "clear\r", 6); break;
                }
                return;
            }
            px += bw + 4;
        }
        return;
    }
    if (!osk_visible()) osk_toggle();
}
static void pt_close(Window *w) {
    PT *d = w->data;
    if (d->pid > 0 && !d->exited) { kill(-d->pid, SIGHUP); kill(d->pid, SIGHUP); waitpid(d->pid, NULL, WNOHANG); }
    if (d->master >= 0) close(d->master);
    vt_destroy(d->vt);
}

static Window *pt_open(const char *first_cmd) {
    int adv = font_mono.g['m' - 32].adv;
    Window *w = win_new("Terminal", IC_TERMINAL, VC * adv + 12, VR * 13 + 8 + BAR);
    if (!w) return NULL;
    PT *d = calloc(1, sizeof(PT));
    if (!d) { win_close(w); return NULL; }
    d->master = -1;
    d->vt = vt_create(pt_reply, d);
    w->app = APP_TERMINAL; w->no_max = 1; w->wants_keys = 1; w->data = d;
    w->draw = pt_draw; w->key = pt_key; w->mouse = pt_mouse; w->tick = pt_tick; w->on_close = pt_close;
    if (w->x + w->w > SW) w->x = SW - w->w;
    if (pt_spawn(d) == 0 && first_cmd) { pt_send(d, first_cmd, (int)strlen(first_cmd)); pt_send(d, "\r", 1); }
    return w;
}
void open_terminal(void) { pt_open(NULL); }
void terminal_run(const char *cmdline) {
    /* desktop paths ("/Documents/x.sh") live under the user's home directory on the real disk */
    const char *home = getenv("HOME");
    if (!home) home = "/home/player";
    char c[600];
    int n = snprintf(c, sizeof c, "cd \"%s\" && ", home);
    for (const char *p = cmdline; *p && n < (int)sizeof c - 80; p++) {
        int start = p == cmdline || p[-1] == ' ';
        if ((start && *p == '/') || (start && *p == '"' && p[1] == '/')) {
            if (*p == '"') c[n++] = *p++;
            n += snprintf(c + n, sizeof c - n, "%s", home);
        }
        c[n++] = *p;
    }
    c[n] = 0;
    pt_open(c);
}
void open_linux(void) { pt_open(NULL); }
#endif
