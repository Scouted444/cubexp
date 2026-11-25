/* PC test harness: runs CubeXP headless from a script and dumps screenshots */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../../source/platform.h"
#include "../../source/gfx.h"
#include "../../source/os.h"

void shell_debug_warp(int x, int y);
static FILE *script;
static long frame;
static uint32_t held, prev;
static long wait_until;

void plat_init(void) {
    script = fopen(getenv("SCRIPT") ? getenv("SCRIPT") : "script.txt", "r");
    if (!script) { fprintf(stderr, "no script\n"); exit(1); }
    mkdir("sd", 0777); mkdir("sd/cubexp", 0777);
}
static uint32_t btn(const char *s) {
    static const char *n[] = { "A", "B", "X", "Y", "Z", "START", "L", "R", "UP", "DOWN", "LEFT", "RIGHT" };
    for (int i = 0; i < 12; i++) if (!strcmp(s, n[i])) return 1u << i;
    return 0;
}
static void shot(const char *name) {
    FILE *f = fopen(name, "wb");
    fprintf(f, "P6 %d %d 255\n", SW, SH);
    for (int i = 0; i < SW * SH; i++) { fputc(fb[i] >> 16, f); fputc(fb[i] >> 8, f); fputc(fb[i], f); }
    fclose(f);
}
void plat_present(void) {
    frame++;
    prev = held;
    static uint32_t tap;
    held &= ~tap; tap = 0;
    if (frame < wait_until) return;
    char line[256];
    while (fgets(line, sizeof line, script)) {
        char cmd[32], a[64] = "", b[64] = "";
        if (sscanf(line, "%31s %63s %63s", cmd, a, b) < 1 || cmd[0] == '#') continue;
        if (!strcmp(cmd, "wait")) { wait_until = frame + atoi(a); return; }
        if (!strcmp(cmd, "warp")) shell_debug_warp(atoi(a), atoi(b));
        if (!strcmp(cmd, "tap")) { held |= btn(a); tap = btn(a); wait_until = frame + 2; return; }
        if (!strcmp(cmd, "hold")) held |= btn(a);
        if (!strcmp(cmd, "release")) held &= ~btn(a);
        if (!strcmp(cmd, "type")) { for (char *p = a; *p; p++) send_key(*p == '_' ? ' ' : *p); }
        if (!strcmp(cmd, "enter")) send_key(K_ENTER);
        if (!strcmp(cmd, "shot")) shot(a);
        if (!strcmp(cmd, "quit")) exit(0);
        if (!strcmp(cmd, "launch")) app_launch(atoi(a));
        if (!strcmp(cmd, "vtdemo")) {
            extern void linux_debug_feed(const char *s);
            linux_debug_feed(
                "[    0.000000] Linux version 6.8.0-rc1 (cnlohr@cnlohr-1520) (riscv32-buildroot-linux-uclibc-gcc) #1\r\n"
                "[    0.000000] Machine model: riscv-minimal-nommu,qemu\r\n"
                "[    0.400000] Run /init as init process\r\n\r\nWelcome to Buildroot\r\n"
                "\x1b[1;32m~ #\x1b[0m ls --color /\r\n"
                "\x1b[1;34mbin\x1b[0m   \x1b[1;34mdev\x1b[0m   \x1b[1;34metc\x1b[0m   \x1b[1;36minit\x1b[0m  \x1b[1;34mlib\x1b[0m   \x1b[1;34mproc\x1b[0m  \x1b[1;34mroot\x1b[0m  \x1b[1;34msbin\x1b[0m  \x1b[1;34msys\x1b[0m   \x1b[1;34mtmp\x1b[0m   \x1b[1;34musr\x1b[0m\r\n"
                "\x1b[1;32m~ #\x1b[0m uname -a\r\nLinux buildroot 6.8.0-rc1 #1 Mon Jan 22 riscv32 GNU/Linux\r\n"
                "\x1b[1;32m~ #\x1b[0m cat /proc/cpuinfo | head -3\r\nprocessor\t: 0\r\nhart\t\t: 0\r\nisa\t\t: rv32ima\r\n"
                "\x1b[7m reverse \x1b[0m \x1b[41;37m red bg \x1b[0m \x1b[4munderline\x1b[0m \x1b[93mbright yellow\x1b[0m\r\n"
                "\x1b[1;32m~ #\x1b[0m top\x1b[s\x1b[20;50H\x1b[44;97m positioned box \x1b[0m\x1b[u\r\n"
                "\x1b[1;32m~ #\x1b[0m echo hi\b\b\x1b[K\x1b[1;35mHI\x1b[0m");
        }
        if (!strcmp(cmd, "closeall")) { extern void wm_close_all(void); wm_close_all(); }
    }
    exit(0);
}
void plat_poll(PadState p[4]) {
    memset(p, 0, sizeof(PadState) * 4);
    p[0].connected = 1; p[1].connected = 1;
    p[0].held = held; p[0].down = held & ~prev; p[0].up = prev & ~held;
    prev = held;
}
uint64_t plat_ms(void) { return (uint64_t)frame * 17; }
uint64_t plat_us(void) { static uint64_t t; return (uint64_t)frame * 17000 + (t += 50); }
int16_t *plat_audio_alloc(int samples) { return calloc((samples + 15) & ~15, 2); }
void plat_audio_ready(int16_t *pcm, int samples) { (void)pcm; (void)samples; }
void plat_play(const int16_t *pcm, int n, int r, int v) { (void)pcm; (void)n; (void)r; (void)v; }
void plat_stop_all(void) {}
int plat_storage_ok(void) { return 1; }
const char *plat_storage_path(void) { return "sd/cubexp"; }
const char *plat_storage_name(void) { return "SD Gecko"; }
const char *plat_video_mode(void) { return "NTSC 480i"; }
size_t plat_mem_free(void) { return 17u * 1024 * 1024; }
size_t plat_mem_total(void) { return 24u * 1024 * 1024; }
void plat_reboot(void) { exit(0); }
void plat_exit(void) { exit(0); }
