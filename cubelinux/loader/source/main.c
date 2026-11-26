/* CubeLinux boot loader.
 *
 * Runs as an ordinary GameCube DOL. It sets the TV up the way the CubeLinux kernel
 * expects (640x480 YUY2 picture at physical 0x01700000), starts controller polling
 * in the Serial Interface, shows a GRUB-style menu, then copies the embedded
 * kernel ELF (dtbImage.gamecube, which carries the initramfs) into place and jumps.
 */
#include <gccore.h>
#include <ogc/machine/processor.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"

#define XFB_PHYS   0x01700000u
#define XFB_UNC    ((uint32_t *)(0xC0000000u | XFB_PHYS))

extern const uint8_t kernel_bin[], kernel_bin_end[];

static GXRModeObj *rmode;

/* ---------- video ---------- */
static void video_init(void) {
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    /* the kernel's TV driver draws 480 lines; PAL 576-line consoles run in PAL60 */
    if (rmode->xfbHeight != 480 || rmode->fbWidth != 640) {
        if (rmode->viTVMode >> 2 == VI_PAL || rmode->viTVMode >> 2 == VI_EURGB60) rmode = &TVEurgb60Hz480IntDf;
        else rmode = &TVNtsc480IntDf;
    }
    VIDEO_Configure(rmode);
    for (int i = 0; i < 640 * 480 / 2; i++) XFB_UNC[i] = 0x10801080;
    VIDEO_SetNextFramebuffer(XFB_UNC);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
}

static void present(void) {
    uint32_t *d = XFB_UNC;
    for (int i = 0; i < SW * SH; i += 2) {
        uint32_t p0 = fb[i], p1 = fb[i + 1];
        int r0 = (p0 >> 16) & 255, g0 = (p0 >> 8) & 255, b0 = p0 & 255;
        int r1 = (p1 >> 16) & 255, g1 = (p1 >> 8) & 255, b1 = p1 & 255;
        int y0 = ((66 * r0 + 129 * g0 + 25 * b0 + 128) >> 8) + 16;
        int y1 = ((66 * r1 + 129 * g1 + 25 * b1 + 128) >> 8) + 16;
        int r = (r0 + r1) >> 1, g = (g0 + g1) >> 1, b = (b0 + b1) >> 1;
        int cb = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
        int cr = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
        *d++ = ((uint32_t)y0 << 24) | ((uint32_t)cb << 16) | ((uint32_t)y1 << 8) | (uint32_t)cr;
    }
    VIDEO_WaitVSync();
}

/* ---------- ELF ---------- */
typedef struct { uint8_t ident[16]; uint16_t type, machine; uint32_t version, entry, phoff, shoff, flags;
                 uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } Ehdr;
typedef struct { uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align; } Phdr;
typedef struct { uint32_t name, type, flags, addr, offset, size, link, info, addralign, entsize; } Shdr;

static const char *elf_error;

static int elf_check(const uint8_t *img, size_t len) {
    const Ehdr *e = (const Ehdr *)img;
    if (len < sizeof *e || memcmp(e->ident, "\x7f" "ELF", 4)) { elf_error = "No kernel is built into this loader."; return 0; }
    if (e->ident[4] != 1 || e->ident[5] != 2 || e->machine != 20) { elf_error = "The kernel is not a 32-bit PowerPC ELF."; return 0; }
    return 1;
}

/* the boot wrapper uses __builtin_cmdline when it is not empty */
static void set_cmdline(uint8_t *img, const char *cmd) {
    Ehdr *e = (Ehdr *)img;
    if (!e->shoff || e->shstrndx >= e->shnum) return;
    Shdr *sh = (Shdr *)(img + e->shoff);
    const char *names = (const char *)img + sh[e->shstrndx].offset;
    for (int i = 0; i < e->shnum; i++)
        if (!strcmp(names + sh[i].name, "__builtin_cmdline") && sh[i].size > 1) {
            memset(img + sh[i].offset, 0, sh[i].size);
            strncpy((char *)img + sh[i].offset, cmd, sh[i].size - 1);
        }
}

/* ---------- drawing ---------- */
static const char *items[] = {
    "CubeLinux",
    "CubeLinux (text console)",
    "CubeLinux (verbose boot messages)",
    "Return to the homebrew loader",
};
static const char *cmdlines[] = {
    "console=tty0 rdinit=/sbin/init loglevel=4 quiet",
    "console=tty0 rdinit=/sbin/init loglevel=5 cubelinux.text",
    "console=tty0 rdinit=/sbin/init loglevel=8 ignore_loglevel",
};
#define NITEMS 4

static void draw_menu(int sel, int secs, uint32_t ksize) {
    grad_v(0, 0, SW, SH, 0x0B1A3A, 0x02040C);
    draw_icon(IC_LOGO, 48, 34, 48);
    text(&font_huge, 108, 36, "CubeLinux", 0xFFFFFF);
    text(&font_ui, 110, 72, "GNU/Linux for the Nintendo GameCube", 0x9FB4E0);

    int bx = 48, by = 120, bw = SW - 96, bh = 30 * NITEMS + 20;
    round_fill_a(bx, by, bw, bh, 6, 0x000000, 120);
    rect(bx, by, bw, bh, 0x3A5A9A);
    for (int i = 0; i < NITEMS; i++) {
        int y = by + 10 + i * 30;
        if (i == sel) { round_grad(bx + 6, y, bw - 12, 26, 4, 0x3D7BE0, 0x2456B0, 0); }
        text(&font_bold, bx + 20, y + 6, items[i], i == sel ? 0xFFFFFF : 0xC8D2E8);
    }
    char b[96];
    text(&font_ui, 48, by + bh + 18, "Use the D-Pad or stick to choose, A to start.", 0x9FB4E0);
    if (secs >= 0) {
        snprintf(b, sizeof b, "The highlighted entry starts automatically in %d second%s.", secs, secs == 1 ? "" : "s");
        text(&font_ui, 48, by + bh + 36, b, 0x9FB4E0);
    }
    snprintf(b, sizeof b, "Kernel image: %lu KB   -   Video: %s", (unsigned long)(ksize >> 10),
             rmode == &TVEurgb60Hz480IntDf ? "PAL 60 Hz 480i" : rmode->viTVMode & VI_NON_INTERLACE ? "480p" : "NTSC 480i");
    text(&font_small, 48, SH - 40, b, 0x6A7EA8);
    text(&font_small, 48, SH - 54, "Dolphin users: Config > Advanced > Enable MMU must be ON, or Linux stops with \"Invalid write\".", 0xE0C070);
    text(&font_small, 48, SH - 26, "Linux is licensed under the GPL v2. BusyBox is GPL v2. The desktop is CubeXP.", 0x6A7EA8);
}

static void draw_status(const char *msg, int pct, int err) {
    grad_v(0, 0, SW, SH, 0x0B1A3A, 0x02040C);
    draw_icon(IC_LOGO, SW / 2 - 24, 150, 48);
    text_center(&font_huge, 0, 210, SW, "CubeLinux", 0xFFFFFF);
    text_center(&font_ui, 0, 262, SW, msg, err ? 0xFF8080 : 0x9FB4E0);
    if (pct >= 0) {
        round_fill(SW / 2 - 120, 290, 240, 10, 5, 0x1A2A50);
        if (pct) round_fill(SW / 2 - 120, 290, 240 * pct / 100, 10, 5, 0x5A9AF0);
    }
    present();
}

/* ---------- main ---------- */
int main(void) {
    SYS_SetArenaHi((void *)(0x80000000u | XFB_PHYS));
    video_init();
    PAD_Init();   /* puts the Serial Interface into continuous polling; the kernel reads the results */

    size_t klen = kernel_bin_end - kernel_bin;
    int sel = 0, last = 0;
    uint64_t start = gettime();
    int autoboot = 1;
    uint8_t *img = (uint8_t *)kernel_bin;

    for (;;) {
        PAD_ScanPads();
        uint32_t d = PAD_ButtonsDown(0);
        int sy = PAD_StickY(0);
        int stick = sy > 50 ? -1 : sy < -50 ? 1 : 0;
        if (stick && stick != last) d |= stick < 0 ? PAD_BUTTON_UP : PAD_BUTTON_DOWN;
        last = stick;
        if (d) autoboot = 0;
        if (d & PAD_BUTTON_UP) sel = (sel + NITEMS - 1) % NITEMS;
        if (d & PAD_BUTTON_DOWN) sel = (sel + 1) % NITEMS;
        int secs = autoboot ? 5 - (int)(ticks_to_millisecs(gettime() - start) / 1000) : -1;
        if (d & (PAD_BUTTON_A | PAD_BUTTON_START)) break;
        if (autoboot && secs <= 0) break;
        draw_menu(sel, secs, klen);
        present();
    }
    if (sel == 3) {
        draw_status("Returning to the loader...", -1, 0);
        void (*reload)(void) = (void (*)(void))0x80001800;
        if (*(volatile uint32_t *)0x80001804 == 0x53545542) reload();  /* "STUB" */
        SYS_ResetSystem(SYS_HOTRESET, 0, 0);
    }

    if (!elf_check(img, klen)) {
        for (;;) { draw_status(elf_error, -1, 1); }
    }
    set_cmdline(img, cmdlines[sel]);

    Ehdr *e = (Ehdr *)img;
    Phdr *ph = (Phdr *)(img + e->phoff);
    /* the kernel goes to its link address (0x00600000), which the loader itself may be
       sitting on. Build the final image in free memory just below the TV picture, then a
       register-only routine copies it into place and jumps. */
    uint32_t lo = 0xFFFFFFFF, hi = 0;
    for (int i = 0; i < e->phnum; i++) {
        if (ph[i].type != 1 || !ph[i].memsz) continue;
        uint32_t a = 0x80000000u | ph[i].paddr;
        if (a < lo) lo = a;
        if (a + ph[i].memsz > hi) hi = a + ph[i].memsz;
    }
    if (hi <= lo) for (;;) draw_status("The kernel has nothing to load.", -1, 1);
    uint32_t len = (hi - lo + 31) & ~31u;
    uint32_t stage = ((0x80000000u | XFB_PHYS) - len) & ~31u;
    uint32_t sp;
    __asm__ volatile("mr %0, 1" : "=r"(sp));
    uint32_t used = (uint32_t)(uintptr_t)SYS_GetArenaLo();
    if (sp + 0x1000 > used) used = sp + 0x1000;
    if ((uint32_t)(uintptr_t)kernel_bin_end > used) used = (uint32_t)(uintptr_t)kernel_bin_end;
    if (stage < used || stage < hi || lo < 0x80004000u) {
        char m[140];
        snprintf(m, sizeof m, "Not enough memory: kernel %08lx-%08lx, loader up to %08lx.", (unsigned long)lo, (unsigned long)hi, (unsigned long)used);
        for (;;) draw_status(m, -1, 1);
    }

    draw_status("Starting Linux...", 30, 0);
    uint8_t *st = (uint8_t *)stage;
    memset(st, 0, len);
    for (int i = 0; i < e->phnum; i++) {
        if (ph[i].type != 1 || !ph[i].memsz) continue;
        memcpy(st + ((0x80000000u | ph[i].paddr) - lo), img + ph[i].offset, ph[i].filesz);
    }
    DCFlushRange(st, len);
    draw_status("Starting Linux...", 100, 0);

    /* hand over: no more interrupts, VI keeps scanning our picture, SI keeps polling pads */
    extern void cl_copy_and_jump(uint32_t dst, uint32_t src, uint32_t n, uint32_t entry) __attribute__((noreturn));
    u32 level;
    _CPU_ISR_Disable(level);
    (void)level;
    *(volatile uint32_t *)0xCC003004 = 0;   /* PI interrupt mask: nothing */
    cl_copy_and_jump(lo, stage, len, 0x80000000u | e->entry);
    for (;;) ;
}
