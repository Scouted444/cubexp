/* Linux Terminal: boots a real Linux kernel (RISC-V rv32ima, nommu, with BusyBox)
   inside cnlohr's mini-rv32ima emulator, with a VT100 terminal on top. */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* kernel image linked in by the build (data/linux.bin -> linux_bin); weak so builds without it still link */
/* Use the start/end labels (incomplete arrays) rather than the 4-byte linux_bin_size
   variable: PowerPC EABI treats small extern variables as "small data" and the
   generated access can't reach a symbol that really lives in .rodata. */
extern const uint8_t linux_bin[] __attribute__((weak));
extern const uint8_t linux_bin_end[] __attribute__((weak));
static uint32_t kernel_size(void) {
    if (!linux_bin || !linux_bin_end || linux_bin_end <= linux_bin) return 0;
    return (uint32_t)(linux_bin_end - linux_bin);
}

/* ---------------- emulator glue ---------------- */
static uint32_t lx_ram_amt;
static struct MiniRV32IMAState *lx_core;
static uint8_t inq[512];
static int inq_r, inq_w;
static int kb_hit(void) { return inq_r != inq_w; }
static int kb_read(void) { int c = inq[inq_r]; inq_r = (inq_r + 1) & 511; return c; }
static void kb_push(int c) { int n = (inq_w + 1) & 511; if (n != inq_r) { inq[inq_w] = (uint8_t)c; inq_w = n; } }
static void kb_str(const char *s) { while (*s) kb_push((unsigned char)*s++); }
static void vt_put(int c);
static long out_bytes;

static uint32_t HandleControlStore(uint32_t addy, uint32_t val);
static uint32_t HandleControlLoad(uint32_t addy);
static void HandleOtherCSRWrite(uint8_t *image, uint16_t csrno, uint32_t value);
static int32_t HandleOtherCSRRead(uint8_t *image, uint16_t csrno);

/* RISC-V is little-endian; the GameCube is big-endian, so all guest memory access goes through these */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
static inline uint32_t ld4(const uint8_t *p) { if (!((uintptr_t)p & 3)) return __builtin_bswap32(*(const uint32_t *)p); return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static inline uint32_t ld2(const uint8_t *p) { if (!((uintptr_t)p & 1)) return __builtin_bswap16(*(const uint16_t *)p); return p[0] | p[1] << 8; }
static inline void st4(uint8_t *p, uint32_t v) { if (!((uintptr_t)p & 3)) { *(uint32_t *)p = __builtin_bswap32(v); return; } p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static inline void st2(uint8_t *p, uint32_t v) { if (!((uintptr_t)p & 1)) { *(uint16_t *)p = __builtin_bswap16((uint16_t)v); return; } p[0] = v; p[1] = v >> 8; }
#else
static inline uint32_t ld4(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint32_t ld2(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static inline void st4(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static inline void st2(uint8_t *p, uint32_t v) { uint16_t h = (uint16_t)v; memcpy(p, &h, 2); }
#endif

#define MINIRV32WARN(...)
#define MINIRV32_DECORATE static
#define MINI_RV32_RAM_SIZE lx_ram_amt
#define MINIRV32_IMPLEMENTATION
#define MINIRV32_HANDLE_MEM_STORE_CONTROL(addy, val) if (HandleControlStore(addy, val)) return val;
#define MINIRV32_HANDLE_MEM_LOAD_CONTROL(addy, rval) rval = HandleControlLoad(addy);
#define MINIRV32_OTHERCSR_WRITE(csrno, value) HandleOtherCSRWrite(image, csrno, value);
#define MINIRV32_OTHERCSR_READ(csrno, value) value = HandleOtherCSRRead(image, csrno);
#define MINIRV32_CUSTOM_MEMORY_BUS
#define MINIRV32_STORE4(ofs, val) st4(image + (ofs), val)
#define MINIRV32_STORE2(ofs, val) st2(image + (ofs), val)
#define MINIRV32_STORE1(ofs, val) (image[ofs] = (uint8_t)(val))
#define MINIRV32_LOAD4(ofs) ld4(image + (ofs))
#define MINIRV32_LOAD2(ofs) ld2(image + (ofs))
#define MINIRV32_LOAD1(ofs) image[ofs]
#define MINIRV32_LOAD2_SIGNED(ofs) ((int16_t)ld2(image + (ofs)))
#define MINIRV32_LOAD1_SIGNED(ofs) ((int8_t)image[ofs])
#include "../linux/mini-rv32ima.h"
#include "../linux/default64mbdtc.h"

static uint32_t HandleControlStore(uint32_t addy, uint32_t val) {
    if (addy == 0x10000000) { vt_put(val & 255); out_bytes++; }
    else if (addy == 0x11004004) lx_core->timermatchh = val;
    else if (addy == 0x11004000) lx_core->timermatchl = val;
    else if (addy == 0x11100000) { lx_core->pc = lx_core->pc + 4; return val; }
    return 0;
}
static uint32_t HandleControlLoad(uint32_t addy) {
    if (addy == 0x10000005) return 0x60 | kb_hit();
    if (addy == 0x10000000 && kb_hit()) return kb_read();
    if (addy == 0x1100bffc) return lx_core->timerh;
    if (addy == 0x1100bff8) return lx_core->timerl;
    return 0;
}
static void HandleOtherCSRWrite(uint8_t *image, uint16_t csrno, uint32_t value) {
    (void)image;
    if (csrno == 0x139) vt_put(value & 255);
}
static int32_t HandleOtherCSRRead(uint8_t *image, uint16_t csrno) {
    (void)image;
    if (csrno == 0x140) { if (!kb_hit()) return -1; return kb_read(); }
    return 0;
}


/* ---------------- VT100 terminal ---------------- */
#define VC 80
#define VR 24
#define SBL 300
typedef struct { char c; uint8_t fg, bg, fl; } Cell;   /* fl: 1 bold, 2 reverse, 4 underline */
typedef struct {
    Cell s[VR][VC];
    Cell sb[SBL][VC];
    int sbn, sbhead, view;
    int cx, cy, wrap, top, bot, scx, scy, cursor_on;
    uint8_t fg, bg, fl;
    int st, np, p[16], priv;
    int utf8_skip;
} VT;
static VT *vt;

static void vt_clear_cells(Cell *c, int n) { for (int i = 0; i < n; i++) { c[i].c = ' '; c[i].fg = vt->fg; c[i].bg = vt->bg; c[i].fl = 0; } }
static void vt_reset(void) {
    memset(vt, 0, sizeof *vt);
    vt->fg = 7; vt->bg = 0; vt->bot = VR - 1; vt->cursor_on = 1;
    for (int r = 0; r < VR; r++) vt_clear_cells(vt->s[r], VC);
}
static void vt_scroll_up(int top, int bot, int n) {
    for (int k = 0; k < n; k++) {
        if (top == 0 && bot == VR - 1) {   /* keep scrollback */
            memcpy(vt->sb[vt->sbhead], vt->s[0], sizeof vt->s[0]);
            vt->sbhead = (vt->sbhead + 1) % SBL;
            if (vt->sbn < SBL) vt->sbn++;
        }
        memmove(vt->s[top], vt->s[top + 1], sizeof vt->s[0] * (bot - top));
        vt_clear_cells(vt->s[bot], VC);
    }
}
static void vt_scroll_down(int top, int bot, int n) {
    for (int k = 0; k < n; k++) {
        memmove(vt->s[top + 1], vt->s[top], sizeof vt->s[0] * (bot - top));
        vt_clear_cells(vt->s[top], VC);
    }
}
static void vt_lf(void) {
    if (vt->cy == vt->bot) vt_scroll_up(vt->top, vt->bot, 1);
    else if (vt->cy < VR - 1) vt->cy++;
}
static int P(int i, int def) { return i < vt->np && vt->p[i] > 0 ? vt->p[i] : def; }
static void clampc(void) {
    if (vt->cx < 0) vt->cx = 0;
    if (vt->cx >= VC) vt->cx = VC - 1;
    if (vt->cy < 0) vt->cy = 0;
    if (vt->cy >= VR) vt->cy = VR - 1;
}
static void vt_sgr(void) {
    if (!vt->np) { vt->fg = 7; vt->bg = 0; vt->fl = 0; return; }
    for (int i = 0; i < vt->np; i++) {
        int v = vt->p[i];
        if (v == 0) { vt->fg = 7; vt->bg = 0; vt->fl = 0; }
        else if (v == 1) vt->fl |= 1;
        else if (v == 4) vt->fl |= 4;
        else if (v == 7) vt->fl |= 2;
        else if (v == 22) vt->fl &= ~1;
        else if (v == 24) vt->fl &= ~4;
        else if (v == 27) vt->fl &= ~2;
        else if (v >= 30 && v <= 37) vt->fg = v - 30;
        else if (v == 39) vt->fg = 7;
        else if (v >= 40 && v <= 47) vt->bg = v - 40;
        else if (v == 49) vt->bg = 0;
        else if (v >= 90 && v <= 97) vt->fg = v - 90 + 8;
        else if (v >= 100 && v <= 107) vt->bg = v - 100 + 8;
        else if ((v == 38 || v == 48) && i + 2 < vt->np && vt->p[i + 1] == 5) {   /* 256-colour: map to 16 */
            int c = vt->p[i + 2] & 15;
            if (v == 38) vt->fg = c; else vt->bg = c;
            i += 2;
        }
    }
}
static void vt_csi(char f) {
    int n = P(0, 1);
    switch (f) {
    case 'A': vt->cy -= n; if (vt->cy < vt->top) vt->cy = vt->top; break;
    case 'B': case 'e': vt->cy += n; if (vt->cy > vt->bot) vt->cy = vt->bot; break;
    case 'C': case 'a': vt->cx += n; break;
    case 'D': vt->cx -= n; break;
    case 'E': vt->cy += n; vt->cx = 0; break;
    case 'F': vt->cy -= n; vt->cx = 0; break;
    case 'G': case '`': vt->cx = n - 1; break;
    case 'd': vt->cy = n - 1; break;
    case 'H': case 'f': vt->cy = P(0, 1) - 1; vt->cx = P(1, 1) - 1; break;
    case 'J': {
        int m = vt->np ? vt->p[0] : 0;
        if (m == 0) { vt_clear_cells(&vt->s[vt->cy][vt->cx], VC - vt->cx); for (int r = vt->cy + 1; r < VR; r++) vt_clear_cells(vt->s[r], VC); }
        else if (m == 1) { for (int r = 0; r < vt->cy; r++) vt_clear_cells(vt->s[r], VC); vt_clear_cells(vt->s[vt->cy], vt->cx + 1); }
        else for (int r = 0; r < VR; r++) vt_clear_cells(vt->s[r], VC);
        break; }
    case 'K': {
        int m = vt->np ? vt->p[0] : 0;
        if (m == 0) vt_clear_cells(&vt->s[vt->cy][vt->cx], VC - vt->cx);
        else if (m == 1) vt_clear_cells(vt->s[vt->cy], vt->cx + 1);
        else vt_clear_cells(vt->s[vt->cy], VC);
        break; }
    case 'X': { int k = n; if (vt->cx + k > VC) k = VC - vt->cx; vt_clear_cells(&vt->s[vt->cy][vt->cx], k); break; }
    case 'P': { int k = n; if (vt->cx + k > VC) k = VC - vt->cx;
        memmove(&vt->s[vt->cy][vt->cx], &vt->s[vt->cy][vt->cx + k], sizeof(Cell) * (VC - vt->cx - k));
        vt_clear_cells(&vt->s[vt->cy][VC - k], k); break; }
    case '@': { int k = n; if (vt->cx + k > VC) k = VC - vt->cx;
        memmove(&vt->s[vt->cy][vt->cx + k], &vt->s[vt->cy][vt->cx], sizeof(Cell) * (VC - vt->cx - k));
        vt_clear_cells(&vt->s[vt->cy][vt->cx], k); break; }
    case 'L': if (vt->cy >= vt->top && vt->cy <= vt->bot) vt_scroll_down(vt->cy, vt->bot, n); break;
    case 'M': if (vt->cy >= vt->top && vt->cy <= vt->bot) vt_scroll_up(vt->cy, vt->bot, n); break;
    case 'S': vt_scroll_up(vt->top, vt->bot, n); break;
    case 'T': vt_scroll_down(vt->top, vt->bot, n); break;
    case 'r': vt->top = P(0, 1) - 1; vt->bot = P(1, VR) - 1;
        if (vt->top < 0) vt->top = 0;
        if (vt->bot >= VR) vt->bot = VR - 1;
        if (vt->top >= vt->bot) { vt->top = 0; vt->bot = VR - 1; }
        vt->cx = 0; vt->cy = 0; break;
    case 'm': vt_sgr(); break;
    case 's': vt->scx = vt->cx; vt->scy = vt->cy; break;
    case 'u': vt->cx = vt->scx; vt->cy = vt->scy; break;
    case 'h': case 'l':
        if (vt->priv && vt->np && vt->p[0] == 25) vt->cursor_on = f == 'h';
        if (vt->priv && vt->np && (vt->p[0] == 1049 || vt->p[0] == 47 || vt->p[0] == 1047)) {
            for (int r = 0; r < VR; r++) vt_clear_cells(vt->s[r], VC);
            vt->cx = vt->cy = 0;
        }
        break;
    case 'n':
        if (vt->np && vt->p[0] == 6) { char b[24]; snprintf(b, sizeof b, "\x1b[%d;%dR", vt->cy + 1, vt->cx + 1); kb_str(b); }
        else if (vt->np && vt->p[0] == 5) kb_str("\x1b[0n");
        break;
    case 'c': if (!vt->priv) kb_str("\x1b[?1;2c"); break;
    }
    vt->wrap = 0;
    clampc();
}

static void vt_put(int ch) {
    if (!vt) return;
    unsigned char c = (unsigned char)ch;
    if (vt->st == 1) {   /* after ESC */
        vt->st = 0;
        switch (c) {
        case '[': vt->st = 2; vt->np = 0; vt->priv = 0; memset(vt->p, 0, sizeof vt->p); return;
        case ']': vt->st = 4; return;   /* OSC: skip to BEL / ST */
        case '(': case ')': case '#': vt->st = 5; return;
        case '7': vt->scx = vt->cx; vt->scy = vt->cy; return;
        case '8': vt->cx = vt->scx; vt->cy = vt->scy; return;
        case 'D': vt_lf(); return;
        case 'E': vt->cx = 0; vt_lf(); return;
        case 'M': if (vt->cy == vt->top) vt_scroll_down(vt->top, vt->bot, 1); else if (vt->cy) vt->cy--; return;
        case 'c': vt_reset(); return;
        default: return;
        }
    }
    if (vt->st == 2) {   /* CSI parameters */
        if (c == '?' || c == '>' || c == '=') { vt->priv = 1; return; }
        if (c >= '0' && c <= '9') { if (!vt->np) vt->np = 1; vt->p[vt->np - 1] = vt->p[vt->np - 1] * 10 + (c - '0'); return; }
        if (c == ';') { if (!vt->np) vt->np = 1; if (vt->np < 16) vt->np++; return; }
        if (c >= 0x40 && c <= 0x7e) { vt->st = 0; vt_csi((char)c); return; }
        if (c < 0x20) { /* control inside CSI: execute */ } else return;
    }
    if (vt->st == 4) { if (c == 7) vt->st = 0; else if (c == 0x1b) vt->st = 6; return; }
    if (vt->st == 6) { vt->st = 0; return; }
    if (vt->st == 5) { vt->st = 0; return; }
    if (vt->utf8_skip > 0) { if ((c & 0xC0) == 0x80) { vt->utf8_skip--; return; } vt->utf8_skip = 0; }
    switch (c) {
    case 0x1b: vt->st = 1; return;
    case '\r': vt->cx = 0; vt->wrap = 0; return;
    case '\n': case 0x0b: case 0x0c: vt_lf(); vt->wrap = 0; return;
    case '\b': if (vt->cx > 0) vt->cx--; vt->wrap = 0; return;
    case '\t': vt->cx = (vt->cx / 8 + 1) * 8; if (vt->cx >= VC) vt->cx = VC - 1; return;
    case 7: sound_play(SND_CLICK); return;
    }
    if (c < 0x20 || c == 0x7f) return;
    if (c >= 0x80) {   /* UTF-8: show one placeholder per character */
        if ((c & 0xE0) == 0xC0) vt->utf8_skip = 1;
        else if ((c & 0xF0) == 0xE0) vt->utf8_skip = 2;
        else if ((c & 0xF8) == 0xF0) vt->utf8_skip = 3;
        else return;
        c = (vt->utf8_skip == 2) ? '-' : '?';   /* box-drawing (3-byte) chars look best as dashes */
    }
    if (vt->wrap) { vt->cx = 0; vt_lf(); vt->wrap = 0; }
    Cell *cl = &vt->s[vt->cy][vt->cx];
    cl->c = (char)c; cl->fg = vt->fg; cl->bg = vt->bg; cl->fl = vt->fl;
    if (vt->cx == VC - 1) vt->wrap = 1; else vt->cx++;
    vt->view = 0;
}

/* ---------------- the window ---------------- */
enum { LX_NOIMAGE, LX_NOMEM, LX_BOOT, LX_RUN, LX_OFF };
typedef struct {
    uint8_t *ram;
    int state, ctrl, dtb_ptr;
    uint64_t last_us, boot_t;
    uint64_t instr_window, instr_total;
    uint64_t mips_t;
    int mips10;
} LX;
static Window *lx_win;

static const uint32_t pal[16] = {
    0x101014, 0xCC3333, 0x4E9A06, 0xC4A000, 0x3465A4, 0x75507B, 0x06989A, 0xD3D7CF,
    0x555753, 0xEF2929, 0x8AE234, 0xFCE94F, 0x729FCF, 0xAD7FA8, 0x34E2E2, 0xFFFFFF,
};
static const char *chips[] = { "Ctrl", "Ctrl+C", "Ctrl+D", "Tab", "Esc", "Up", "Down", "Enter", "Paste", "Reboot" };
#define NCH 10
#define BAR 26

static void lx_boot(LX *d) {
    memset(d->ram, 0, lx_ram_amt);
    memcpy(d->ram, linux_bin, kernel_size());
    d->dtb_ptr = lx_ram_amt - (int)sizeof(default64mbdtb) - (int)sizeof(struct MiniRV32IMAState);
    memcpy(d->ram + d->dtb_ptr, default64mbdtb, sizeof default64mbdtb);
    uint8_t *db = d->ram + d->dtb_ptr;
    if (db[0x13c] == 0x03 && db[0x13d] == 0xff && db[0x13e] == 0xc0 && db[0x13f] == 0x00) {   /* RAM size, big-endian in the DTB */
        uint32_t v = (uint32_t)d->dtb_ptr;
        db[0x13c] = v >> 24; db[0x13d] = v >> 16; db[0x13e] = v >> 8; db[0x13f] = v;
    }
    lx_core = (struct MiniRV32IMAState *)(d->ram + lx_ram_amt - sizeof(struct MiniRV32IMAState));
    lx_core->pc = MINIRV32_RAM_IMAGE_OFFSET;
    lx_core->regs[10] = 0;
    lx_core->regs[11] = d->dtb_ptr + MINIRV32_RAM_IMAGE_OFFSET;
    lx_core->extraflags |= 3;
    d->state = LX_BOOT;
    d->boot_t = now_ms;
    d->last_us = plat_us();
    out_bytes = 0;
    inq_r = inq_w = 0;
    vt_reset();
}

static void lx_tick(Window *w) {
    LX *d = w->data;
    if (d->state != LX_BOOT && d->state != LX_RUN) return;
    if (d->state == LX_BOOT && out_bytes > 0) d->state = LX_RUN;
    /* time slice: generous while the window is active (and while booting), small in the background */
    uint64_t budget = (w == focus) ? 11000 : (d->state == LX_BOOT ? 7000 : 2500);
    uint64_t start = plat_us();
    while (plat_us() - start < budget) {
        uint64_t nowu = plat_us();
        uint32_t el = (uint32_t)(nowu - d->last_us);
        d->last_us = nowu;
        int ret = MiniRV32IMAStep(lx_core, d->ram, 0, el, 1024);
        d->instr_window += 1024;
        if (ret == 0) continue;
        if (ret == 1) { uint64_t *cc = (uint64_t *)&lx_core->cyclel; *cc += 1024; break; }   /* WFI: idle */
        if (ret == 0x7777) { lx_boot(d); break; }
        if (ret == 0x5555) {
            d->state = LX_OFF;
            const char *m = "\r\n\x1b[33m[ Linux has powered off. Press Enter to boot it again. ]\x1b[0m\r\n";
            while (*m) vt_put(*m++);
            break;
        }
        break;
    }
    if (now_ms - d->mips_t >= 1000) {
        d->mips10 = (int)(d->instr_window * 10 / 1000000 * 1000 / (now_ms - d->mips_t + 1));
        d->instr_total += d->instr_window;
        d->instr_window = 0;
        d->mips_t = now_ms;
    }
}

static void lx_draw(Window *w, int x, int y, int cw, int ch) {
    LX *d = w->data;
    int adv = font_mono.g['m' - 32].adv, lh = 13;
    int th = ch - BAR;
    fill(x, y, cw, th, pal[0]);
    if (d->state == LX_NOIMAGE || d->state == LX_NOMEM) {
        draw_icon(IC_LINUX, x + cw / 2 - 24, y + 50, 48);
        text_center(&font_bold, x, y + 112, cw, d->state == LX_NOIMAGE ? "This build doesn't include the Linux kernel image." : "Not enough memory to start Linux.", 0xFFFFFF);
        text_center(&font_ui, x, y + 132, cw, d->state == LX_NOIMAGE ? "Builds from GitHub Actions include it automatically." : "Close some programs (Paint, Control Panel) and try again.", 0xA0A0B0);
    } else {
        int ox = x + 6, oy = y + 4;
        for (int r = 0; r < VR; r++) {
            const Cell *row;
            int srow = r - vt->view;   /* rows above 0 come from scrollback */
            if (srow >= 0) row = vt->s[srow];
            else {
                int k = vt->sbn + srow;
                if (k < 0) continue;
                row = vt->sb[(vt->sbhead - vt->sbn + k + SBL) % SBL];
            }
            for (int c = 0; c < VC; c++) {   /* backgrounds */
                const Cell *cl = &row[c];
                int bgc = cl->fl & 2 ? cl->fg : cl->bg;
                if (bgc) fill(ox + c * adv, oy + r * lh, adv, lh, pal[bgc & 15]);
            }
            int c = 0;
            while (c < VC) {   /* text in same-colour runs */
                const Cell *cl = &row[c];
                int fgc = cl->fl & 2 ? cl->bg : cl->fg;
                if (cl->fl & 1 && fgc < 8) fgc += 8;
                int e = c;
                char buf[VC + 1];
                while (e < VC) {
                    const Cell *k = &row[e];
                    int f2 = k->fl & 2 ? k->bg : k->fg;
                    if (k->fl & 1 && f2 < 8) f2 += 8;
                    if (f2 != fgc) break;
                    buf[e - c] = k->c ? k->c : ' ';
                    e++;
                }
                int n = e - c;
                while (n > 0 && buf[n - 1] == ' ') n--;
                if (n) text_n(&font_mono, ox + c * adv, oy + r * lh, buf, n, pal[fgc & 15]);
                for (int u = c; u < e; u++) if (row[u].fl & 4) hline(ox + u * adv, oy + r * lh + 11, adv, pal[fgc & 15]);
                c = e;
            }
        }
        if (vt->view == 0 && vt->cursor_on && d->state != LX_OFF && (now_ms / 500) % 2 == 0)
            fill_a(ox + vt->cx * adv, oy + vt->cy * lh, adv, lh, 0xE0E0E0, 200);
        if (vt->view) {
            char b[48];
            snprintf(b, sizeof b, " scrollback -%d ", vt->view);
            int tw = text_w(&font_ui, b);
            round_fill(x + cw - tw - 14, y + 4, tw + 8, 16, 4, 0x3465A4);
            text(&font_ui, x + cw - tw - 10, y + 5, b, 0xFFFFFF);
        }
        if (d->state == LX_BOOT) {
            int el = (int)(now_ms - d->boot_t) / 1000;
            char b[96];
            snprintf(b, sizeof b, "Starting Linux (RISC-V emulated)... %ds", el);
            int bw = 300, bx = x + (cw - bw) / 2, by = y + th / 2 - 30;
            shadow_rect(bx, by, bw, 60, 4, 100);
            round_fill(bx, by, bw, 60, 6, 0x24242E);
            text_center(&font_bold, bx, by + 12, bw, b, 0xFFFFFF);
            int bar = (int)((now_ms / 12) % 260);
            round_fill(bx + 20, by + 36, bw - 40, 10, 4, 0x0E0E14);
            gfx_clip(bx + 21, by + 37, bw - 42, 8);
            grad_h(bx + 21 + bar - 60, by + 37, 60, 8, 0x24242E, 0x4CC85A);
            gfx_noclip();
        }
    }
    /* toolbar */
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
        text_center(&font_ui, px, by + 6, bw, chips[i], i == 9 ? 0xFF9A8A : 0xE8E8F0);
        px += bw + 4;
    }
    char st[40];
    if (d->state == LX_RUN || d->state == LX_BOOT) snprintf(st, sizeof st, "%d.%d MIPS", d->mips10 / 10, d->mips10 % 10);
    else snprintf(st, sizeof st, "%s", d->state == LX_OFF ? "powered off" : "");
    text(&font_small, x + cw - text_w(&font_small, st) - 8, by + 8, st, 0x8AE234);
}

static void send_key_byte(LX *d, int c) {
    if (d->ctrl && c >= 0x40 && c < 0x80) { c &= 0x1f; d->ctrl = 0; }
    kb_push(c);
}
static void lx_key(Window *w, int k) {
    LX *d = w->data;
    vt->view = 0;
    if (d->state == LX_OFF) { if (k == K_ENTER) lx_boot(d); return; }
    if (d->state != LX_RUN && d->state != LX_BOOT) return;
    switch (k) {
    case K_ENTER: kb_push('\r'); return;
    case K_BKSP: kb_push(0x7f); return;
    case K_TAB: kb_push(9); return;
    case K_ESC: kb_push(27); return;
    case K_UP: kb_str("\x1b[A"); return;
    case K_DOWN: kb_str("\x1b[B"); return;
    case K_RIGHT: kb_str("\x1b[C"); return;
    case K_LEFT: kb_str("\x1b[D"); return;
    case K_HOME: kb_str("\x1b[H"); return;
    case K_END: kb_str("\x1b[F"); return;
    case K_DEL: kb_str("\x1b[3~"); return;
    }
    if (k >= 32 && k < 127) send_key_byte(d, k);
}
static void lx_mouse(Window *w, int ev, int mx, int my) {
    LX *d = w->data;
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    if (ev == EV_SCROLL) {
        vt->view -= my * 3;
        if (vt->view < 0) vt->view = 0;
        if (vt->view > vt->sbn) vt->view = vt->sbn;
        return;
    }
    if (ev != EV_DOWN) return;
    if (my >= ch - BAR) {
        int px = 4;
        for (int i = 0; i < NCH; i++) {
            int bw = text_w(&font_ui, chips[i]) + 14;
            if (inside(mx, my, px, ch - BAR + 4, bw, 18)) {
                sound_play(SND_CLICK);
                vt->view = 0;
                switch (i) {
                case 0: d->ctrl = !d->ctrl; break;
                case 1: kb_push(3); break;
                case 2: kb_push(4); break;
                case 3: kb_push(9); break;
                case 4: kb_push(27); break;
                case 5: kb_str("\x1b[A"); break;
                case 6: kb_str("\x1b[B"); break;
                case 7: lx_key(w, K_ENTER); break;
                case 8: for (const char *p = clipboard; *p; p++) kb_push(*p == '\n' ? '\r' : (unsigned char)*p); break;
                case 9: if (d->state >= LX_BOOT) lx_boot(d); break;
                }
                return;
            }
            px += bw + 4;
        }
        return;
    }
    if (!osk_visible()) osk_toggle();
}
static void lx_close(Window *w) {
    LX *d = w->data;
    free(d->ram); d->ram = NULL;
    free(vt); vt = NULL;
    lx_core = NULL;
    lx_win = NULL;
}

void open_linux(void) {
    if (lx_win && lx_win->used && lx_win->app == APP_LINUX) { win_focus(lx_win); return; }
    int adv = font_mono.g['m' - 32].adv;
    Window *w = win_new("Linux Terminal", IC_LINUX, VC * adv + 12, VR * 13 + 8 + BAR);
    if (!w) return;
    LX *d = calloc(1, sizeof(LX));
    vt = calloc(1, sizeof(VT));
    if (!d || !vt) { free(d); free(vt); vt = NULL; win_close(w); msgbox("Linux", "Not enough memory.", IC_WARN); return; }
    w->app = APP_LINUX; w->no_max = 1; w->wants_keys = 1; w->data = d;
    w->draw = lx_draw; w->key = lx_key; w->mouse = lx_mouse; w->tick = lx_tick; w->on_close = lx_close;
    w->x = (SW - w->w) / 2; w->y = 4;
    lx_win = w;
    vt_reset();
    if (kernel_size() < 65536) { d->state = LX_NOIMAGE; return; }
    static const uint32_t sizes[] = { 12u << 20, 10u << 20, 8u << 20 };
    for (unsigned i = 0; i < 3 && !d->ram; i++) {
        if (sizes[i] < kernel_size() + (3u << 20)) continue;
        d->ram = malloc(sizes[i]);
        if (d->ram) lx_ram_amt = sizes[i];
    }
    if (!d->ram) { d->state = LX_NOMEM; return; }
    d->mips_t = now_ms;
    lx_boot(d);
    notify("Linux Terminal", "Booting a real Linux kernel inside an emulated\nRISC-V CPU. The first boot takes a little while.");
}

#ifdef HOST_BUILD
/* test hook: pretend Linux printed this text */
void linux_debug_feed(const char *s) {
    if (!lx_win || !vt) return;
    ((LX *)lx_win->data)->state = LX_OFF;   /* display only; nothing to execute */
    while (*s) vt_put((unsigned char)*s++);
}
#endif
