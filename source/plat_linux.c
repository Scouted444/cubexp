/* CubeLinux platform layer: the CubeXP desktop running as a normal Linux program.
   Video: /dev/fb0 (cubefb), input: /dev/input/event* (cubepad), time: CLOCK_MONOTONIC. */
#if defined(CUBELINUX) && !defined(HOST_BUILD)
#define _GNU_SOURCE
#include "platform.h"
#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/reboot.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>

static int fbfd = -1, ttyfd = -1;
static uint8_t *fbmem;
static size_t fblen;
static int fb_w, fb_h, fb_ll, fb_bpp;
static char storage[256];

#define MAXEV 8
static int evfd[MAXEV], evport[MAXEV], nev;
static uint32_t held[4], prevheld[4];
static int axis[4][6];   /* X Y RX RY Z RZ, 0..255 */
static int present[4];

static void restore_console(void) {
    if (ttyfd >= 0) { ioctl(ttyfd, KDSETMODE, KD_TEXT); }
}
static void on_signal(int sig) {
    restore_console();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void open_inputs(void) {
    DIR *d = opendir("/dev/input");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && nev < MAXEV) {
        if (strncmp(e->d_name, "event", 5)) continue;
        char p[300];
        snprintf(p, sizeof p, "/dev/input/%s", e->d_name);
        int fd = open(p, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        char name[128] = "";
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        int port = -1;
        if (!strncmp(name, "GameCube Controller ", 20)) port = name[20] - '1';
        if (port < 0 || port > 3) { close(fd); continue; }
        evfd[nev] = fd; evport[nev] = port; nev++;
        present[port] = 1;
        for (int a = 0; a < 6; a++) axis[port][a] = a >= 4 ? 0 : 128;
    }
    closedir(d);
}

void plat_init(void) {
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGSEGV, on_signal);
    signal(SIGPIPE, SIG_IGN);
    atexit(restore_console);

    ttyfd = open("/dev/tty1", O_RDWR | O_NOCTTY);
    if (ttyfd < 0) ttyfd = open("/dev/tty0", O_RDWR | O_NOCTTY);
    if (ttyfd >= 0) ioctl(ttyfd, KDSETMODE, KD_GRAPHICS);   /* stop the text console drawing over us */

    fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd >= 0) {
        struct fb_var_screeninfo v;
        struct fb_fix_screeninfo f;
        if (!ioctl(fbfd, FBIOGET_VSCREENINFO, &v) && !ioctl(fbfd, FBIOGET_FSCREENINFO, &f)) {
            fb_w = v.xres; fb_h = v.yres; fb_bpp = v.bits_per_pixel; fb_ll = f.line_length;
            fblen = f.smem_len;
            fbmem = mmap(NULL, fblen, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
            if (fbmem == MAP_FAILED) fbmem = NULL;
        }
    }
    if (!fbmem) fprintf(stderr, "cubede: no usable framebuffer (/dev/fb0)\n");
    open_inputs();

    const char *home = getenv("HOME");
    if (!home || !*home) { home = "/home/player"; setenv("HOME", home, 1); }
    snprintf(storage, sizeof storage, "%s/.config/cubexp", home);
    char p[300];
    snprintf(p, sizeof p, "%s/.config", home); mkdir(p, 0755);
    mkdir(storage, 0755);
}

static uint64_t last_present;
void plat_present(void) {
    if (fbmem) {
        int w = fb_w < SW ? fb_w : SW, h = fb_h < SH ? fb_h : SH;
        if (fb_bpp == 32) {
            /* copy only rows that changed: untouched pages stay clean, so the TV driver skips them */
            static uint32_t *shadow;
            if (!shadow) shadow = calloc(SW * SH, 4), memset(fbmem, 0, fblen);
            for (int y = 0; y < h; y++) {
                const uint32_t *row = &fb[y * SW];
                if (shadow && !memcmp(shadow + y * SW, row, w * 4)) continue;
                memcpy(fbmem + y * fb_ll, row, w * 4);
                if (shadow) memcpy(shadow + y * SW, row, w * 4);
            }
        } else if (fb_bpp == 16) {
            for (int y = 0; y < h; y++) {
                uint16_t *d = (uint16_t *)(fbmem + y * fb_ll);
                const uint32_t *s = &fb[y * SW];
                for (int x = 0; x < w; x++) { uint32_t c = s[x]; d[x] = ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x1F); }
            }
        }
    }
    /* ~30 fps: the TV driver converts dirty rows at the same rate, and Linux programs need CPU too */
    uint64_t now = plat_us();
    if (last_present && now - last_present < 33000) usleep((useconds_t)(33000 - (now - last_present)));
    last_present = plat_us();
}

void plat_poll(PadState p[4]) {
    struct input_event ev[32];
    for (int i = 0; i < nev; i++) {
        int port = evport[i];
        for (;;) {
            ssize_t n = read(evfd[i], ev, sizeof ev);
            if (n <= 0) break;
            for (int k = 0; k < (int)(n / sizeof ev[0]); k++) {
                struct input_event *e = &ev[k];
                if (e->type == EV_KEY) {
                    uint32_t b = 0;
                    switch (e->code) {
                    case BTN_A: b = B_A; break;
                    case BTN_B: b = B_B; break;
                    case BTN_X: b = B_X; break;
                    case BTN_Y: b = B_Y; break;
                    case BTN_Z: b = B_Z; break;
                    case BTN_TL: b = B_L; break;
                    case BTN_TR: b = B_R; break;
                    case BTN_START: b = B_START; break;
                    case BTN_DPAD_UP: b = B_UP; break;
                    case BTN_DPAD_DOWN: b = B_DOWN; break;
                    case BTN_DPAD_LEFT: b = B_LEFT; break;
                    case BTN_DPAD_RIGHT: b = B_RIGHT; break;
                    }
                    if (e->value) held[port] |= b; else held[port] &= ~b;
                } else if (e->type == EV_ABS) {
                    static const int map[] = { ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ };
                    for (int a = 0; a < 6; a++) if (e->code == map[a]) axis[port][a] = e->value;
                }
            }
        }
    }
    for (int i = 0; i < 4; i++) {
        p[i].connected = present[i];
        p[i].held = held[i];
        p[i].down = held[i] & ~prevheld[i];
        p[i].up = prevheld[i] & ~held[i];
        prevheld[i] = held[i];
        p[i].sx = axis[i][0] - 128;
        p[i].sy = 128 - axis[i][1];     /* the driver reports down as positive */
        p[i].cx = axis[i][2] - 128;
        p[i].cy = 128 - axis[i][3];
        p[i].l = axis[i][4];
        p[i].r = axis[i][5];
    }
}

uint64_t plat_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000;
}
uint64_t plat_ms(void) { return plat_us() / 1000; }

/* no sound driver yet: keep the buffers so the desktop's sound code is happy */
int16_t *plat_audio_alloc(int samples) { return calloc((samples + 15) & ~15, 2); }
void plat_audio_ready(int16_t *pcm, int samples) { (void)pcm; (void)samples; }
void plat_play(const int16_t *pcm, int samples, int rate, int vol) { (void)pcm; (void)samples; (void)rate; (void)vol; }
void plat_stop_all(void) {}

int plat_storage_ok(void) { return 1; }
const char *plat_storage_path(void) { return storage; }
const char *plat_storage_name(void) { return "Linux home folder"; }
const char *plat_video_mode(void) { static char b[48]; snprintf(b, sizeof b, "%dx%d Linux framebuffer", fb_w, fb_h); return b; }

size_t plat_mem_free(void) {
    struct sysinfo si;
    if (sysinfo(&si)) return 0;
    return (size_t)(si.freeram + si.bufferram) * si.mem_unit;
}
size_t plat_mem_total(void) {
    struct sysinfo si;
    if (sysinfo(&si)) return 24u << 20;
    return (size_t)si.totalram * si.mem_unit;
}

void plat_reboot(void) {
    restore_console();
    sync();
    reboot(RB_AUTOBOOT);
    exit(0);
}
void plat_exit(void) {
    sync();
    reboot(RB_POWER_OFF);   /* the GameCube kernel halts here and keeps the last picture */
    exit(0);
}
#endif
