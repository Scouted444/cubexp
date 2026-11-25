#ifndef GFX_H
#define GFX_H
#include <stdint.h>

#define SW 640
#define SH 480

typedef struct { uint8_t w, h; int8_t xo, yo; uint8_t adv; uint32_t off; } Glyph;
typedef struct { int height, ascent; const Glyph *g; const unsigned char *px; } Font;

extern const Font font_ui, font_bold, font_mono, font_title, font_big, font_huge, font_start, font_small;

#define RGB(r,g,b) ((uint32_t)(((r)<<16)|((g)<<8)|(b)))
#define C_WHITE  0xFFFFFF
#define C_BLACK  0x000000
#define C_FACE   0xECE9D8   /* XP "button face" beige */
#define C_SHADOW 0xACA899
#define C_DKSHAD 0x716F64
#define C_SEL    0x316AC5

extern uint32_t fb[SW * SH];

void gfx_target(uint32_t *buf, int x, int y, int w, int h);
void gfx_target_screen(void);
typedef struct { uint32_t *b; int x, y, w, h, c[4]; } GfxState;
void gfx_push(GfxState *s);
void gfx_pop(const GfxState *s);
void layer_begin(int x, int y, int w, int h);
void layer_end(int alpha);
void blit_argb(const uint32_t *src, int w, int h, int x, int y, int alpha);
void shadow_rect(int x, int y, int w, int h, int r, int strength);
void round_fill_a(int x, int y, int w, int h, int r, uint32_t c, int a);
void gfx_clip(int x, int y, int w, int h);
void gfx_noclip(void);
void gfx_getclip(int *x, int *y, int *w, int *h);

void pset(int x, int y, uint32_t c);
uint32_t pget(int x, int y);
void fill(int x, int y, int w, int h, uint32_t c);
void fill_a(int x, int y, int w, int h, uint32_t c, int alpha);
void rect(int x, int y, int w, int h, uint32_t c);
void hline(int x, int y, int w, uint32_t c);
void vline(int x, int y, int h, uint32_t c);
void line(int x0, int y0, int x1, int y1, uint32_t c);
void thick_line(int x0, int y0, int x1, int y1, int r, uint32_t c);
void grad_v(int x, int y, int w, int h, uint32_t c1, uint32_t c2);
void grad_h(int x, int y, int w, int h, uint32_t c1, uint32_t c2);
void fill_circle(int cx, int cy, int r, uint32_t c);
void circle(int cx, int cy, int r, uint32_t c);
void round_fill(int x, int y, int w, int h, int r, uint32_t c);
void round_grad(int x, int y, int w, int h, int r, uint32_t c1, uint32_t c2, int top_only);
void bevel(int x, int y, int w, int h, int sunken);   /* classic 3D edge */
void xp_button(int x, int y, int w, int h, const char *label, int pressed, int hot);
void grayscale_screen(int amount);  /* 0..256 */
void darken_screen(int alpha);

uint32_t mix(uint32_t a, uint32_t b, int t); /* t 0..256 */
uint32_t shade(uint32_t c, int pct);         /* pct 100 = same */

int  text(const Font *f, int x, int y, const char *s, uint32_t c);
int  text_n(const Font *f, int x, int y, const char *s, int n, uint32_t c);
int  text_w(const Font *f, const char *s);
int  text_wn(const Font *f, const char *s, int n);
void text_center(const Font *f, int x, int y, int w, const char *s, uint32_t c);
void text_shadow(const Font *f, int x, int y, const char *s, uint32_t c);

/* icons.c */
enum {
    IC_COMPUTER, IC_NOTEPAD, IC_CALC, IC_PAINT, IC_MINES, IC_CMD, IC_CONTROL,
    IC_TASKMGR, IC_SNAKE, IC_CLOCK, IC_RECYCLE, IC_HELP, IC_FOLDER, IC_LOGO,
    IC_DRIVE, IC_PAD, IC_POWER, IC_RESTART, IC_LOGOFF, IC_RUN, IC_KEYBOARD,
    IC_USER, IC_DISC, IC_SOUND, IC_INFO, IC_WARN,
    IC_RECYCLE_FULL, IC_EXPLORER, IC_TERMINAL, IC_SOLITAIRE, IC_BLOCKS, IC_VIEWER,
    IC_MUSIC, IC_STICKY, IC_FILE, IC_TEXTFILE, IC_IMAGEFILE, IC_SONGFILE, IC_SCRIPT,
    IC_DOCS, IC_PICS, IC_MUSICF, IC_DESKTOP, IC_SHOWDESK, IC_SDCARD, IC_ERROR,
    IC_UP, IC_NEWFOLDER, IC_DELETE, IC_BACK, IC_APPFILE, IC_COUNT
};
void draw_icon(int id, int x, int y, int size);
void draw_icon_a(int id, int x, int y, int size, int alpha);
void draw_cursor(int x, int y, int busy);

#endif
