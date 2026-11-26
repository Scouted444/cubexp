#ifndef VT_H
#define VT_H
#include "os.h"

#define VC 80
#define VR 24
#define SBL 300
typedef struct { char c; uint8_t fg, bg, fl; } Cell;   /* fl: 1 bold, 2 reverse, 4 underline */
typedef struct VT VT;

/* reply() receives terminal answers (cursor-position reports etc.) to send back to the program */
VT  *vt_create(void (*reply)(void *ctx, const char *s), void *ctx);
void vt_destroy(VT *vt);
void vt_reset(VT *vt);
void vt_put(VT *vt, int c);
void vt_write(VT *vt, const char *s, int n);
void vt_draw(VT *vt, int x, int y, int cursor);
void vt_scroll_view(VT *vt, int delta);
int  vt_viewing_history(VT *vt);
int  vt_cursor_x(VT *vt);
int  vt_cursor_y(VT *vt);
uint32_t vt_bg_color(void);

#endif
