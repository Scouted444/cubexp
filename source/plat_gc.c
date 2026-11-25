/* GameCube platform layer: libogc video/pad/audio/SD. */
#ifndef HOST_BUILD
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <asndlib.h>
#include <fat.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "platform.h"
#include "gfx.h"

static GXRModeObj *rmode;
static void *xfb[2];
static int cur;
static int storage;
static char storage_path[32] = "sd:/cubexp";
static const char *storage_name = "None";


void plat_init(void) {
    VIDEO_Init();
    PAD_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    VIDEO_Configure(rmode);
    for (int i = 0; i < 2; i++) {
        xfb[i] = SYS_AllocateFramebuffer(rmode);   /* cached address; we flush */
        VIDEO_ClearFrameBuffer(rmode, MEM_K0_TO_K1(xfb[i]), COLOR_BLACK);
    }
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    ASND_Init();
    ASND_Pause(0);

    /* SD Gecko / SD2SP2 if present; harmless if not */
    static const char *devs[] = { "sd", "carda", "cardb", "sp2" };
    if (fatInitDefault()) {
        for (int i = 0; i < 4; i++) {
            char p[32];
            snprintf(p, sizeof p, "%s:/", devs[i]);
            struct stat st;
            if (stat(p, &st) == 0) {
                snprintf(storage_path, sizeof storage_path, "%s:/cubexp", devs[i]);
                mkdir(storage_path, 0777);
                storage = 1;
                storage_name = i == 0 ? "SD Card" : i == 3 ? "SD2SP2" : "SD Gecko";
                break;
            }
        }
    }
}

/* RGB -> YUY2 (two pixels per u32: Y0 Cb Y1 Cr) */
void plat_present(void) {
    uint32_t *dst = (uint32_t *)xfb[cur];
    int stride = rmode->fbWidth / 2;
    int lines = rmode->xfbHeight;
    int yoff = (lines - SH) / 2;
    if (yoff < 0) yoff = 0;
    for (int y = 0; y < lines; y++) {
        uint32_t *d = dst + y * stride;
        int sy = y - yoff;
        if (sy < 0 || sy >= SH) {
            for (int x = 0; x < stride; x++) d[x] = 0x10801080;
            continue;
        }
        const uint32_t *s = &fb[sy * SW];
        int n = stride < SW / 2 ? stride : SW / 2;
        for (int x = 0; x < n; x++) {
            uint32_t p0 = s[2 * x], p1 = s[2 * x + 1];
            int r0 = (p0 >> 16) & 255, g0 = (p0 >> 8) & 255, b0 = p0 & 255;
            int r1 = (p1 >> 16) & 255, g1 = (p1 >> 8) & 255, b1 = p1 & 255;
            int y0 = ((66 * r0 + 129 * g0 + 25 * b0 + 128) >> 8) + 16;
            int y1 = ((66 * r1 + 129 * g1 + 25 * b1 + 128) >> 8) + 16;
            int r = (r0 + r1) >> 1, g = (g0 + g1) >> 1, b = (b0 + b1) >> 1;
            int cb = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
            int cr = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
            d[x] = ((uint32_t)y0 << 24) | ((uint32_t)cb << 16) | ((uint32_t)y1 << 8) | (uint32_t)cr;
        }
    }
    DCFlushRange(xfb[cur], stride * 4 * lines);
    VIDEO_SetNextFramebuffer(xfb[cur]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    cur ^= 1;
}

void plat_poll(PadState p[4]) {
    u32 conn = PAD_ScanPads();
    for (int i = 0; i < 4; i++) {
        static const struct { u16 pad; uint32_t ours; } map[] = {
            { PAD_BUTTON_A, B_A }, { PAD_BUTTON_B, B_B }, { PAD_BUTTON_X, B_X }, { PAD_BUTTON_Y, B_Y },
            { PAD_TRIGGER_Z, B_Z }, { PAD_BUTTON_START, B_START }, { PAD_TRIGGER_L, B_L }, { PAD_TRIGGER_R, B_R },
            { PAD_BUTTON_UP, B_UP }, { PAD_BUTTON_DOWN, B_DOWN }, { PAD_BUTTON_LEFT, B_LEFT }, { PAD_BUTTON_RIGHT, B_RIGHT },
        };
        u16 h = PAD_ButtonsHeld(i), d = PAD_ButtonsDown(i), u = PAD_ButtonsUp(i);
        uint32_t H = 0, D = 0, U = 0;
        for (unsigned k = 0; k < sizeof map / sizeof map[0]; k++) {
            if (h & map[k].pad) H |= map[k].ours;
            if (d & map[k].pad) D |= map[k].ours;
            if (u & map[k].pad) U |= map[k].ours;
        }
        p[i].connected = (conn >> i) & 1;
        p[i].held = H; p[i].down = D; p[i].up = U;
        p[i].sx = PAD_StickX(i); p[i].sy = PAD_StickY(i);
        p[i].cx = PAD_SubStickX(i); p[i].cy = PAD_SubStickY(i);
        p[i].l = PAD_TriggerL(i); p[i].r = PAD_TriggerR(i);
    }
}

uint64_t plat_ms(void) { return ticks_to_millisecs(gettime()); }

int16_t *plat_audio_alloc(int samples) {
    int n = (samples + 15) & ~15;
    int16_t *p = memalign(32, n * 2);
    if (p) memset(p, 0, n * 2);
    return p;
}
void plat_audio_ready(int16_t *pcm, int samples) { DCFlushRange(pcm, ((samples + 15) & ~15) * 2); }

void plat_play(const int16_t *pcm, int samples, int rate, int vol) {
    s32 v = ASND_GetFirstUnusedVoice();
    if (v < 0 || !pcm) return;
    int bytes = ((samples + 15) & ~15) * 2;
    ASND_SetVoice(v, VOICE_MONO_16BIT, rate, 0, (void *)pcm, bytes, vol, vol, NULL);
}
void plat_stop_all(void) { for (int i = 0; i < 16; i++) ASND_StopVoice(i); }

int plat_storage_ok(void) { return storage; }
const char *plat_storage_path(void) { return storage_path; }
const char *plat_storage_name(void) { return storage_name; }

const char *plat_video_mode(void) {
    switch (rmode->viTVMode >> 2) {
        case VI_PAL: return "PAL 576i";
        case VI_MPAL: return "MPAL 480i";
        case VI_EURGB60: return "PAL60 480i";
        default: return (rmode->viTVMode & VI_PROGRESSIVE) ? "NTSC 480p" : "NTSC 480i";
    }
}

size_t plat_mem_free(void) {
    struct mallinfo mi = mallinfo();
    size_t arena = (size_t)((char *)SYS_GetArena1Hi() - (char *)SYS_GetArena1Lo());
    return arena + (mi.fordblks);
}
size_t plat_mem_total(void) { return 24u * 1024 * 1024; }

void plat_reboot(void) { SYS_ResetSystem(SYS_HOTRESET, 0, 0); }
void plat_exit(void) { exit(0); }
#endif
