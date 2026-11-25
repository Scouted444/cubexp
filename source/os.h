#ifndef OS_H
#define OS_H
#include <stdint.h>
#include "gfx.h"
#include <strings.h>
#include "platform.h"

#define OS_NAME    "CubeXP"
#define OS_EDITION "Professional"
#define OS_VERSION "Version 1.0 (Build 2600)"

/* ---------- input events ---------- */
enum { EV_DOWN, EV_UP, EV_DRAG, EV_RDOWN, EV_SCROLL, EV_DBL };
enum {
    K_BKSP = 8, K_TAB = 9, K_ENTER = 10, K_ESC = 27, K_DEL = 127,
    K_LEFT = 200, K_RIGHT, K_UP, K_DOWN, K_HOME, K_END
};

/* ---------- windows ---------- */
typedef struct Window Window;
struct Window {
    int used, app, icon, gen;
    char title[48];
    int x, y, w, h;                 /* outer frame */
    int min_w, min_h, resizable, no_max;
    int maximized, minimized, rx, ry, rw, rh;
    int nofocus, topmost;           /* OSK style */
    int wants_keys, wants_pad;
    uint64_t opened;
    void *data;
    void (*draw)(Window *, int x, int y, int w, int h);
    void (*mouse)(Window *, int ev, int x, int y);   /* client coords */
    void (*key)(Window *, int k);
    void (*pad)(Window *, uint32_t down, uint32_t held);
    void (*tick)(Window *);
    void (*on_close)(Window *);
};

#define MAX_WIN 16
#define TITLE_H 25
#define BORDER  4
#define TASKBAR_H 30

extern Window wins[MAX_WIN];
extern int zorder[MAX_WIN], nz;     /* indices into wins, bottom->top */
extern Window *focus;

Window *win_new(const char *title, int icon, int cw, int ch);  /* client size */
void win_close(Window *w);
void win_focus(Window *w);
void win_client(Window *w, int *x, int *y, int *cw, int *ch);
int  win_mouse(Window *w, int *mx, int *my);   /* client-relative cursor; returns 1 if inside */
Window *win_find(int app);
void win_center(Window *w);
void win_minimize(Window *w);
void win_toggle_max(Window *w);

/* ---------- settings / theme ---------- */
typedef struct {
    int wallpaper, theme, cursor_speed, clock24, sound, saver_min, show_secs;
    char user[24];
} Settings;
extern Settings cfg;
void settings_load(void);
void settings_save(void);

typedef struct {
    const char *name;
    uint32_t t1, t2, t3, tbot, frame, frame_in, title_txt, title_in1, title_in2;
    uint32_t bar1, bar2, bar_hi, tray1, tray2, btn, btn_act, start1, start2, menu_side;
} Theme;
extern const Theme themes[3];
#define TH (&themes[cfg.theme])

extern const char *wallpaper_names[];
extern int wallpaper_count;
void wallpaper_render(int idx, uint32_t *dst);

/* ---------- shell services ---------- */
extern int mouse_x, mouse_y, mouse_btn;
extern uint64_t now_ms;
extern int fps;
extern PadState pads[4];

enum { SND_STARTUP, SND_SHUTDOWN, SND_DING, SND_ERROR, SND_CLICK, SND_NOTIFY, SND_LOGON, SND_EXPLODE, SND_EAT, SND_COUNT };
void sound_init(void);
void sound_play(int id);

void notify(const char *title, const char *msg);
void msgbox(const char *title, const char *msg, int icon);
void osk_toggle(void);
int  osk_visible(void);
void send_key(int k);
void shell_shutdown_dialog(void);
void shell_logoff_dialog(void);
void shell_run_dialog(void);
void shell_open_start(void);
void shell_restart(void);
void shell_turn_off(void);
void shell_log_off(void);

static inline int inside(int px, int py, int x, int y, int w, int h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

/* ---------- apps ---------- */
enum {
    APP_COMPUTER, APP_NOTEPAD, APP_CALC, APP_PAINT, APP_MINES, APP_CMD, APP_CONTROL,
    APP_TASKMGR, APP_SNAKE, APP_CLOCK, APP_RECYCLE, APP_ABOUT, APP_HELP, APP_OSK,
    APP_RUN, APP_MSGBOX, APP_PONG, APP_COUNT
};
typedef struct { const char *name, *cmd; int icon; void (*open)(void); const char *desc; } AppInfo;
extern const AppInfo apps[APP_COUNT];
void app_launch(int app);
int  app_by_cmd(const char *cmd);

void open_computer(void); void open_notepad(void); void open_calc(void); void open_paint(void);
void open_mines(void); void open_cmd(void); void open_control(void); void open_taskmgr(void);
void open_snake(void); void open_clock(void); void open_recycle(void); void open_about(void);
void open_help(void); void open_osk(void); void open_run(void); void open_pong(void);

/* shell popup menus (NULL label = separator) */
typedef struct { const char *label; int id; } MItem;
void shell_popup(int x, int y, const MItem *items, int n, void (*cb)(int id, int ctx), int ctx);
/* single-line input dialog; cb receives the text (not called on Cancel) */
void prompt_box(const char *title, const char *label, const char *initial, void (*cb)(const char *text, int ctx), int ctx);
/* file chooser listing files with extension ext in the save folder */
void file_picker(const char *title, const char *ext, void (*cb)(const char *name, int ctx), int ctx);
extern int cpu_pct;
void shell_wallpaper_dirty(void);
void set_custom_wallpaper(const uint32_t *img, int w, int h);

/* notepad helper for "New Text Document" + cmd "notepad file" */
void notepad_open_file(const char *name);

/* shared widget helpers (widgets.c) */
void ui_panel_sunken(int x, int y, int w, int h, uint32_t bg);
void ui_menubar(int x, int y, int w, const char **items, int n);
int  ui_menubar_hit(int mx, int my, const char **items, int n);  /* client-relative to menubar origin */
void ui_check(int x, int y, const char *label, int on);
void ui_radio(int x, int y, const char *label, int on);
void ui_groupbox(int x, int y, int w, int h, const char *label);
void ui_scrollbar(int x, int y, int h, int total, int visible, int pos);
void ui_progress(int x, int y, int w, int h, int pct);
void ui_tab(int x, int y, const char **tabs, int n, int sel);
int  ui_tab_hit(int mx, int my, int x, int y, const char **tabs, int n);
void ui_button(int x, int y, int w, int h, const char *label, Window *win);  /* hover-aware */

#endif
