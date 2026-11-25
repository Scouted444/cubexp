/* renders every icon at 16/32/48 onto a sheet (PPM) for review */
#include <stdio.h>
#include "../../source/gfx.h"
int main(void) {
    gfx_target_screen();
    fill(0, 0, SW, SH, 0xECE9D8);
    fill(0, 240, SW, 240, 0x3A6EA5);
    for (int i = 0; i < IC_COUNT; i++) {
        int x = 8 + (i % 13) * 48, y = 8 + (i / 13) * 58;
        draw_icon(i, x, y, 32);
        draw_icon(i, x + 34, y, 16);
        draw_icon(i, x, y + 240, 48);
    }
    FILE *f = fopen(getenv("OUT") ? getenv("OUT") : "icons.ppm", "wb");
    fprintf(f, "P6 %d %d 255\n", SW, SH);
    for (int i = 0; i < SW * SH; i++) { fputc(fb[i] >> 16, f); fputc(fb[i] >> 8, f); fputc(fb[i], f); }
    fclose(f);
    return 0;
}
