#ifndef SH_H
#define SH_H
#include "os.h"
#include "vfs.h"

typedef struct { char *s; int n, cap; } SBuf;
void sb_putc(SBuf *b, char c);
void sb_puts(SBuf *b, const char *s);
void sb_write(SBuf *b, const char *s, int n);
void sb_printf(SBuf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void sb_free(SBuf *b);

/* colour escape inside output text: '\x1b' followed by one hex digit (DOS palette) */
#define COL(c) "\x1b" #c
#define COL_RESET "\x1b" "r"

#define SH_VARS 32
#define SH_HIST 40
typedef struct {
    VNode *cwd;
    char var[SH_VARS][2][72];
    int nvar;
    char alias[16][2][72];
    int nalias;
    char hist[SH_HIST][200];
    int nh;
    int status, style, depth, tty;
    Window *win;
    /* requests for the terminal window */
    int req_clear, req_exit, req_matrix, req_color, fg, bg;
} Sh;

void sh_init(Sh *sh, int style, Window *win);
void sh_run_line(Sh *sh, const char *line, SBuf *out);
void sh_prompt(Sh *sh, char *buf, int n);
void sh_complete(Sh *sh, char *line, int *cur, int max, SBuf *out);
void sh_add_history(Sh *sh, const char *line);
const char *sh_get(Sh *sh, const char *name);
void sh_set(Sh *sh, const char *name, const char *val);

#endif
