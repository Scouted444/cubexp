/* My Computer: drives, SD card browser, system properties */
#include "../os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

typedef struct { int view, sel; char files[48][40]; int nfiles; } MC;

static void task_pane(int x, int y, int ch, const char *title, const char **items, int n) {
    grad_v(x, y, 160, ch, 0x7BA2E7, 0x6375D6);
    round_fill(x + 8, y + 10, 144, 24 + n * 20, 4, 0xD6DFF7);
    grad_h(x + 8, y + 10, 144, 22, 0xFFFFFF, 0xC6D3F7);
    text(&font_bold, x + 16, y + 15, title, 0x215DC6);
    for (int i = 0; i < n; i++) {
        int hot = inside(mouse_x, mouse_y, x + 14, y + 38 + i * 20, 136, 18);
        draw_icon(i == 0 ? IC_INFO : i == 1 ? IC_CONTROL : IC_FOLDER, x + 14, y + 38 + i * 20, 16);
        text(&font_ui, x + 34, y + 40 + i * 20, items[i], hot ? 0x428EFF : 0x215DC6);
    }
}

static void bar(int x, int y, int w, int pct) {
    fill(x, y, w, 8, 0xFFFFFF); rect(x, y, w, 8, 0x808080);
    fill(x + 1, y + 1, (w - 2) * pct / 100, 6, 0x2A6AD8);
}

__attribute__((unused)) static void load_files(MC *d) {
    d->nfiles = 0;
    DIR *dir = opendir(plat_storage_path());
    if (!dir) return;
    struct dirent *e;
    while ((e = readdir(dir)) && d->nfiles < 48) {
        if (e->d_name[0] == '.') continue;
        strncpy(d->files[d->nfiles++], e->d_name, 39);
    }
    closedir(dir);
}

static void mc_draw(Window *w, int x, int y, int cw, int ch) {
    MC *d = w->data;
    static const char *tasks[] = { "View system info", "Change a setting", "Browse Local Disk" };
    task_pane(x, y, ch, "System Tasks", tasks, 3);
    int cx = x + 160, cwid = cw - 160;
    fill(cx, y, cwid, ch, 0xFFFFFF);
    char b[96];
    if (d->view == 0) {
        text(&font_bold, cx + 12, y + 10, "Storage", 0);
        grad_h(cx + 12, y + 26, cwid - 24, 1, 0x7090D0, 0xFFFFFF);
        /* drives */
        struct { int icon; const char *name; int pct; const char *sub; } dr[3];
        size_t fr = plat_mem_free(), tot = plat_mem_total();
        int used = fr > tot ? 0 : (int)((tot - fr) * 100 / tot);
        (void)used;
        static char sub0[48];
        snprintf(sub0, sizeof sub0, "%ld KB used of 8 MB", vfs_total_size(vfs_root) / 1024);
        dr[0].icon = IC_DRIVE; dr[0].name = DRIVE_NAME; dr[0].pct = (int)(vfs_total_size(vfs_root) * 100 / (8 * 1024 * 1024)) + 1; dr[0].sub = sub0;
        dr[1].icon = IC_SDCARD; dr[1].name = plat_storage_ok() ? "SD Card (D:)" : "SD Card (D:) - none"; dr[1].pct = -1; dr[1].sub = plat_storage_ok() ? "Saving C: to /cubexp" : "Nothing is saved at power off";
        dr[2].icon = IC_DISC; dr[2].name = "DVD Drive (E:)"; dr[2].pct = -1; dr[2].sub = "Mini disc drive";
        for (int i = 0; i < 3; i++) {
            int ix = cx + 14 + (i % 2) * 200, iy = y + 36 + (i / 2) * 56;
            if (d->sel == i) fill(ix - 2, iy - 2, 190, 52, 0xC6D3F7);
            draw_icon(dr[i].icon, ix, iy + 4, 32);
            text(&font_ui, ix + 40, iy + 2, dr[i].name, 0);
            if (dr[i].pct >= 0) bar(ix + 40, iy + 18, 120, dr[i].pct);
            text(&font_ui, ix + 40, iy + 30, dr[i].sub, 0x808080);
        }
        text(&font_bold, cx + 12, y + 154, "Devices", 0);
        grad_h(cx + 12, y + 170, cwid - 24, 1, 0x7090D0, 0xFFFFFF);
        for (int i = 0; i < 4; i++) {
            int ix = cx + 14 + i * 90, iy = y + 180;
            draw_icon(IC_PAD, ix + 20, iy, 32);
            snprintf(b, sizeof b, "Controller %d", i + 1);
            text_center(&font_ui, ix, iy + 36, 80, b, 0);
            text_center(&font_ui, ix, iy + 50, 80, pads[i].connected ? "Connected" : "Not connected", pads[i].connected ? 0x208020 : 0x909090);
        }
    } else if (d->view == 1) {
        snprintf(b, sizeof b, "%s", plat_storage_ok() ? plat_storage_path() : "No SD card");
        text(&font_bold, cx + 12, y + 10, b, 0);
        grad_h(cx + 12, y + 26, cwid - 24, 1, 0x7090D0, 0xFFFFFF);
        if (!d->nfiles) text(&font_ui, cx + 16, y + 40, plat_storage_ok() ? "This folder is empty. Save something from Notepad or Paint!" : "Insert an SD card adapter to use this folder.", 0x808080);
        for (int i = 0; i < d->nfiles; i++) {
            int ix = cx + 10 + (i % 4) * 100, iy = y + 34 + (i / 4) * 64;
            if (iy > y + ch - 60) break;
            const char *dot = strrchr(d->files[i], '.');
            int ic = dot && !strcasecmp(dot, ".bmp") ? IC_PAINT : dot && !strcasecmp(dot, ".txt") ? IC_NOTEPAD : IC_FOLDER;
            if (d->sel == i) fill(ix, iy, 96, 60, 0xC6D3F7);
            draw_icon(ic, ix + 32, iy + 2, 32);
            char nm[16];
            strncpy(nm, d->files[i], 15); nm[15] = 0;
            text_center(&font_ui, ix, iy + 40, 96, nm, 0);
        }
    } else {
        draw_icon(IC_LOGO, cx + 20, y + 20, 64);
        text(&font_big, cx + 100, y + 24, "CubeXP", 0x0A246A);
        text(&font_ui, cx + 100, y + 52, OS_EDITION " - " OS_VERSION, 0);
        int ty = y + 100;
        const char *rows[][2] = {
            { "Registered to:", cfg.user },
            { "Processor:", "IBM PowerPC 750CXe \"Gekko\" @ 485 MHz" },
            { "Graphics:", "ATI \"Flipper\" @ 162 MHz, 3 MB eDRAM" },
            { "Memory:", "24 MB 1T-SRAM + 16 MB ARAM" },
            { "Video mode:", plat_video_mode() },
            { "Storage:", plat_storage_ok() ? plat_storage_name() : "None" },
        };
        for (int i = 0; i < 6; i++) {
            text(&font_bold, cx + 20, ty, rows[i][0], 0);
            text(&font_ui, cx + 120, ty, rows[i][1], 0);
            ty += 18;
        }
        uint64_t up = now_ms / 1000;
        snprintf(b, sizeof b, "%d h %02d m %02d s  -  %d fps", (int)(up / 3600), (int)(up / 60 % 60), (int)(up % 60), fps);
        text(&font_bold, cx + 20, ty, "Up time:", 0);
        text(&font_ui, cx + 120, ty, b, 0);
    }
}

static void mc_mouse(Window *w, int ev, int mx, int my) {
    MC *d = w->data;
    int x, y, cw, ch;
    win_client(w, &x, &y, &cw, &ch);
    if (ev != EV_DOWN && ev != EV_DBL) return;
    for (int i = 0; i < 3; i++)
        if (inside(mx, my, 14, 38 + i * 20, 136, 18) && ev == EV_DOWN) {
            if (i == 0) { d->view = 2; snprintf(w->title, sizeof w->title, "System Properties"); }
            else if (i == 1) app_launch(APP_CONTROL);
            else explorer_open_path("/");
            d->sel = -1;
            return;
        }
    if (mx < 160) return;
    int cx = 160;
    if (d->view == 0) {
        for (int i = 0; i < 3; i++) {
            int ix = cx + 14 + (i % 2) * 200, iy = 36 + (i / 2) * 56;
            if (inside(mx, my, ix - 2, iy - 2, 190, 52)) {
                d->sel = i;
                if (ev == EV_DBL) {
                    if (i == 0) explorer_open_path("/");
                    else if (i == 1) msgbox("SD Card (D:)", plat_storage_ok() ? "Your SD card holds CubeXP's disk image\n(/cubexp/disk.cfs) and settings." : "No SD card adapter was found.\nIn Dolphin: Config > GameCube > Slot A = SD Card.", IC_SDCARD);
                    else msgbox("DVD Drive (E:)", "Please insert a disc into drive E:.\n\n(CubeXP is already running from it... probably.)", IC_DISC);
                }
                return;
            }
        }
        d->sel = -1;
    } else if (d->view == 1) {
        for (int i = 0; i < d->nfiles; i++) {
            int ix = cx + 10 + (i % 4) * 100, iy = 34 + (i / 4) * 64;
            if (inside(mx, my, ix, iy, 96, 60)) {
                d->sel = i;
                if (ev == EV_DBL) {
                    const char *dot = strrchr(d->files[i], '.');
                    if (dot && !strcasecmp(dot, ".txt")) notepad_open_file(d->files[i]);
                    else msgbox(d->files[i], "Open .bmp files with Paint (File > Open).", IC_INFO);
                }
                return;
            }
        }
        d->sel = -1;
    }
}

void open_computer(void) {
    Window *w = win_new("My Computer", IC_COMPUTER, 560, 290);
    if (!w) return;
    MC *d = calloc(1, sizeof(MC));
    d->sel = -1;
    w->app = APP_COMPUTER; w->resizable = 1; w->data = d;
    w->min_w = 420; w->min_h = 260;
    w->draw = mc_draw; w->mouse = mc_mouse;
    w->x = 40; w->y = 30;
}
