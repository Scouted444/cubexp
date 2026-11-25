#include "os.h"

void shell_init(void);
void shell_frame(void);
int cpu_pct;

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    plat_init();
    settings_load();
    shell_init();
    now_ms = plat_ms();
    shell_frame();          /* show the boot screen while sounds are synthesized */
    plat_present();
    sound_init();
    uint64_t fps_t = plat_ms();
    int frames = 0;
    for (;;) {
        now_ms = plat_ms();
        shell_frame();
        {   /* rough "CPU" load: time spent building this frame vs a 60 Hz budget */
            int pct = (int)((plat_ms() - now_ms) * 100 / 17);
            cpu_pct = (cpu_pct * 7 + (pct > 100 ? 100 : pct)) / 8;
        }
        plat_present();
        frames++;
        if (now_ms - fps_t >= 1000) { fps = frames; frames = 0; fps_t = now_ms; }
    }
    return 0;
}
