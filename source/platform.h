#ifndef PLATFORM_H
#define PLATFORM_H
#include <stdint.h>
#include <stddef.h>

enum {
    B_A = 1, B_B = 2, B_X = 4, B_Y = 8, B_Z = 16, B_START = 32, B_L = 64, B_R = 128,
    B_UP = 256, B_DOWN = 512, B_LEFT = 1024, B_RIGHT = 2048
};

typedef struct {
    int connected;
    uint32_t held, down, up;
    int sx, sy;     /* main stick, -128..127, +y = up */
    int cx, cy;     /* C stick */
    int l, r;       /* analog triggers 0..255 */
} PadState;

void plat_init(void);
void plat_present(void);           /* push fb[] to the TV and wait for vblank */
void plat_poll(PadState pads[4]);
uint64_t plat_ms(void);
/* audio buffers must come from plat_audio_alloc and be finished with plat_audio_ready;
   they are played in place, so keep them alive */
int16_t *plat_audio_alloc(int samples);
void plat_audio_ready(int16_t *pcm, int samples);
void plat_play(const int16_t *pcm, int samples, int rate, int vol);
void plat_stop_all(void);
int  plat_storage_ok(void);
const char *plat_storage_path(void);  /* folder for saves, no trailing slash */
const char *plat_video_mode(void);
const char *plat_storage_name(void);
size_t plat_mem_free(void);
size_t plat_mem_total(void);
void plat_reboot(void);
void plat_exit(void);

#endif
