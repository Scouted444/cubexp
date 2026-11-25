/* CubeXP shell: parsing, variables, pipes, redirection, globbing, scripts, builtins */
#include "sh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

/* ---------------- string buffer ---------------- */
static void sb_grow(SBuf *b, int add) {
    if (b->n + add + 1 <= b->cap) return;
    int cap = b->cap ? b->cap : 256;
    while (cap < b->n + add + 1) cap *= 2;
    char *s = realloc(b->s, cap);
    if (!s) return;
    b->s = s; b->cap = cap;
}
void sb_write(SBuf *b, const char *s, int n) {
    sb_grow(b, n);
    if (b->n + n + 1 > b->cap) return;
    memcpy(b->s + b->n, s, n); b->n += n; b->s[b->n] = 0;
}
void sb_putc(SBuf *b, char c) { sb_write(b, &c, 1); }
void sb_puts(SBuf *b, const char *s) { sb_write(b, s, strlen(s)); }
void sb_printf(SBuf *b, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof tmp) n = sizeof tmp - 1;
    sb_write(b, tmp, n);
}
void sb_free(SBuf *b) { free(b->s); b->s = NULL; b->n = b->cap = 0; }
static const char *sb_str(SBuf *b) { return b->s ? b->s : ""; }
static void strip_colors(SBuf *b) {
    if (!b->s) return;
    int j = 0;
    for (int i = 0; i < b->n; i++) {
        if (b->s[i] == '\x1b') { i++; continue; }
        b->s[j++] = b->s[i];
    }
    b->n = j; b->s[j] = 0;
}

/* ---------------- variables ---------------- */
const char *sh_get(Sh *sh, const char *name) {
    static char tmp[96];
    for (int i = 0; i < sh->nvar; i++) if (!strcmp(sh->var[i][0], name)) return sh->var[i][1];
    if (!strcmp(name, "USER") || !strcmp(name, "USERNAME")) return cfg.user;
    if (!strcmp(name, "PWD") || !strcmp(name, "CD")) { vfs_path(sh->cwd, tmp, sizeof tmp); return tmp; }
    if (!strcmp(name, "HOME")) return "/";
    if (!strcmp(name, "HOSTNAME") || !strcmp(name, "COMPUTERNAME")) return "cubexp";
    if (!strcmp(name, "SHELL")) return "/Programs/terminal.exe";
    if (!strcmp(name, "PATH")) return "/Programs";
    if (!strcmp(name, "OS")) return "CubeXP";
    if (!strcmp(name, "RANDOM")) { snprintf(tmp, sizeof tmp, "%d", rand() % 32768); return tmp; }
    if (!strcmp(name, "?")) { snprintf(tmp, sizeof tmp, "%d", sh->status); return tmp; }
    return NULL;
}
void sh_set(Sh *sh, const char *name, const char *val) {
    for (int i = 0; i < sh->nvar; i++)
        if (!strcmp(sh->var[i][0], name)) { snprintf(sh->var[i][1], 72, "%s", val); return; }
    if (sh->nvar >= SH_VARS) return;
    snprintf(sh->var[sh->nvar][0], 72, "%s", name);
    snprintf(sh->var[sh->nvar][1], 72, "%s", val);
    sh->nvar++;
}
static void sh_unset(Sh *sh, const char *name) {
    for (int i = 0; i < sh->nvar; i++)
        if (!strcmp(sh->var[i][0], name)) { memmove(sh->var[i], sh->var[i + 1], sizeof sh->var[0] * (sh->nvar - i - 1)); sh->nvar--; return; }
}

void sh_add_history(Sh *sh, const char *line) {
    if (!*line || (sh->nh && !strcmp(sh->hist[sh->nh - 1], line))) return;
    if (sh->nh == SH_HIST) { memmove(sh->hist[0], sh->hist[1], sizeof sh->hist[0] * (SH_HIST - 1)); sh->nh--; }
    snprintf(sh->hist[sh->nh++], 200, "%s", line);
}

void sh_prompt(Sh *sh, char *buf, int n) {
    char p[160];
    vfs_path(sh->cwd, p, sizeof p);
    if (sh->style == 1) {
        for (char *c = p; *c; c++) if (*c == '/') *c = '\\';
        snprintf(buf, n, "C:%s>", p);
    } else snprintf(buf, n, COL(a) "%s@cubexp" COL(7) ":" COL(9) "%s" COL(7) "$ ", cfg.user, sh->cwd == vfs_root ? "~" : p);
}

/* ---------------- glob ---------------- */
static int wild(const char *p, const char *s) {
    for (; *p; p++, s++) {
        if (*p == '*') {
            while (p[1] == '*') p++;
            if (!p[1]) return 1;
            for (; *s; s++) if (wild(p + 1, s)) return 1;
            return wild(p + 1, s);
        }
        if (!*s) return 0;
        if (*p != '?' && tolower((unsigned char)*p) != tolower((unsigned char)*s)) return 0;
    }
    return !*s;
}

/* ---------------- tokenizer ---------------- */
#define MAXARG 48
typedef struct { char *argv[MAXARG]; int argc; char *redir_out, *redir_in; int append; char store[2048]; int used; } Cmd;

static char *store(Cmd *c, const char *s, int n) {
    if (c->used + n + 1 > (int)sizeof c->store) return NULL;
    char *d = c->store + c->used;
    memcpy(d, s, n); d[n] = 0;
    c->used += n + 1;
    return d;
}

static void add_arg(Sh *sh, Cmd *c, const char *tok, int quoted) {
    if (c->argc >= MAXARG - 1) return;
    if (!quoted && (strchr(tok, '*') || strchr(tok, '?'))) {
        char dirpart[160] = "", pat[64];
        const char *sl = strrchr(tok, '/');
        VNode *dir = sh->cwd;
        if (sl) {
            int dl = (int)(sl - tok);
            memcpy(dirpart, tok, dl); dirpart[dl] = 0;
            dir = vfs_lookup(sh->cwd, dl ? dirpart : "/");
            snprintf(pat, sizeof pat, "%s", sl + 1);
        } else snprintf(pat, sizeof pat, "%s", tok);
        int found = 0;
        for (VNode *n = dir && dir->dir ? dir->child : NULL; n; n = n->next) {
            if (!wild(pat, n->name)) continue;
            char full[200];
            if (sl) snprintf(full, sizeof full, "%s/%s", dirpart, n->name); else snprintf(full, sizeof full, "%s", n->name);
            char *s = store(c, full, strlen(full));
            if (s && c->argc < MAXARG - 1) { c->argv[c->argc++] = s; found = 1; }
        }
        if (found) return;
    }
    char *s = store(c, tok, strlen(tok));
    if (s) c->argv[c->argc++] = s;
}

/* splits one pipeline stage into argv + redirections */
static void tokenize(Sh *sh, const char *s, Cmd *c) {
    memset(c, 0, sizeof *c);
    char tok[512];
    int expect = 0;   /* 1: next token is > target, 2: < target */
    while (*s) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        if (*s == '>' || *s == '<') {
            if (*s == '>' && s[1] == '>') { c->append = 1; s += 2; expect = 1; }
            else { expect = *s == '>' ? 1 : 2; if (*s == '>') c->append = 0; s++; }
            continue;
        }
        int n = 0, quoted = 0;
        while (*s && *s != ' ' && *s != '\t' && *s != '>' && *s != '<' && n < 510) {
            if (*s == '"' || *s == '\'') {
                char q = *s++;
                quoted = 1;
                while (*s && *s != q && n < 510) {
                    if (q == '"' && *s == '\\' && s[1]) s++;
                    tok[n++] = *s++;
                }
                if (*s) s++;
            } else if (*s == '\\' && s[1] && sh->style == 0) { s++; tok[n++] = *s++; }
            else tok[n++] = *s++;
        }
        tok[n] = 0;
        if (expect == 1) c->redir_out = store(c, tok, n);
        else if (expect == 2) c->redir_in = store(c, tok, n);
        else add_arg(sh, c, tok, quoted);
        expect = 0;
    }
    c->argv[c->argc] = NULL;
}

/* variable + $(...) expansion; single quotes protect */
static void run_capture(Sh *sh, const char *line, SBuf *out);
static void expand(Sh *sh, const char *in, SBuf *o) {
    int sq = 0;
    for (const char *p = in; *p; p++) {
        if (*p == '\'' ) { sq = !sq; sb_putc(o, *p); continue; }
        if (sq) { sb_putc(o, *p); continue; }
        if (*p == '$' && p[1] == '(') {
            int depth = 1;
            const char *q = p + 2;
            while (*q && depth) { if (*q == '(') depth++; else if (*q == ')') depth--; if (depth) q++; }
            char sub[400];
            int n = (int)(q - (p + 2));
            if (n > 399) n = 399;
            memcpy(sub, p + 2, n); sub[n] = 0;
            SBuf r = { 0 };
            run_capture(sh, sub, &r);
            while (r.n && (r.s[r.n - 1] == '\n' || r.s[r.n - 1] == ' ')) r.s[--r.n] = 0;
            for (int i = 0; i < r.n; i++) sb_putc(o, r.s[i] == '\n' ? ' ' : r.s[i]);
            sb_free(&r);
            p = *q ? q : q - 1;
            continue;
        }
        if (*p == '$' && (isalpha((unsigned char)p[1]) || p[1] == '_' || p[1] == '?' || p[1] == '{')) {
            char name[64];
            int n = 0;
            const char *q = p + 1;
            if (*q == '{') { q++; while (*q && *q != '}' && n < 63) name[n++] = *q++; if (*q == '}') q++; }
            else if (*q == '?') { name[n++] = '?'; q++; }
            else while ((isalnum((unsigned char)*q) || *q == '_') && n < 63) name[n++] = *q++;
            name[n] = 0;
            const char *v = sh_get(sh, name);
            if (v) sb_puts(o, v);
            p = q - 1;
            continue;
        }
        if (*p == '%' && sh->style == 1) {
            const char *e = strchr(p + 1, '%');
            if (e && e - p > 1 && e - p < 60) {
                char name[64];
                memcpy(name, p + 1, e - p - 1); name[e - p - 1] = 0;
                const char *v = sh_get(sh, name);
                if (v) { sb_puts(o, v); p = e; continue; }
            }
        }
        sb_putc(o, *p);
    }
}

/* ---------------- helpers ---------------- */
static VNode *R(Sh *sh, const char *p) { return vfs_lookup(sh->cwd, p); }
static int err(SBuf *o, const char *cmd, const char *msg, const char *arg) {
    if (arg) sb_printf(o, COL(c) "%s: %s: %s" COL_RESET "\n", cmd, arg, msg);
    else sb_printf(o, COL(c) "%s: %s" COL_RESET "\n", cmd, msg);
    return 1;
}
static int has_flag(int *argc, char **argv, char f) {
    int found = 0;
    for (int i = 1; i < *argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] && !isdigit((unsigned char)argv[i][1])) {
            if (strchr(argv[i] + 1, f)) found = 1;
        } else if (argv[i][0] == '/' && argv[i][1] && !argv[i][2] && tolower((unsigned char)argv[i][1]) == f) found = 1;
    }
    return found;
}
/* remove option tokens, leaving operands */
static int operands(int argc, char **argv, char **out) {
    int n = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] && !isdigit((unsigned char)argv[i][1])) continue;
        if (argv[i][0] == '/' && argv[i][1] && !argv[i][2] && isalpha((unsigned char)argv[i][1])) continue;
        out[n++] = argv[i];
    }
    return n;
}
static int num_opt(int argc, char **argv, char f, int def) {
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == f) {
            if (argv[i][2]) return atoi(argv[i] + 2);
            if (i + 1 < argc) return atoi(argv[i + 1]);
        }
        if (argv[i][0] == '-' && isdigit((unsigned char)argv[i][1])) return atoi(argv[i] + 1);
    }
    return def;
}
/* concatenates file operands (or stdin when none) into dst */
static int gather(Sh *sh, const char *cmd, char **ops, int nops, const char *in, SBuf *dst, SBuf *o) {
    if (!nops) { sb_puts(dst, in ? in : ""); return 0; }
    int rc = 0;
    for (int i = 0; i < nops; i++) {
        VNode *f = R(sh, ops[i]);
        if (!f) { rc = err(o, cmd, "No such file or directory", ops[i]); continue; }
        if (f->dir) { rc = err(o, cmd, "Is a directory", ops[i]); continue; }
        sb_write(dst, (const char *)f->data, f->size);
    }
    return rc;
}
static int lines_of(const char *s, char ***out) {
    int cap = 64, n = 0;
    char **v = malloc(sizeof(char *) * cap);
    const char *p = s;
    while (*p) {
        const char *e = strchr(p, '\n');
        int L = e ? (int)(e - p) : (int)strlen(p);
        if (n == cap) { cap *= 2; v = realloc(v, sizeof(char *) * cap); }
        v[n] = malloc(L + 1); memcpy(v[n], p, L); v[n][L] = 0; n++;
        p += L + (e ? 1 : 0);
    }
    *out = v;
    return n;
}
static void free_lines(char **v, int n) { for (int i = 0; i < n; i++) free(v[i]); free(v); }
static int color_for(VNode *n) {
    if (n->dir) return 9;
    if (n->app >= 0) return 10;
    const char *e = vfs_ext(n);
    if (!strcasecmp(e, ".sh") || !strcasecmp(e, ".bat")) return 10;
    if (!strcasecmp(e, ".bmp")) return 13;
    if (!strcasecmp(e, ".song")) return 14;
    return 7;
}
static void fmt_size(long s, char *b, int n) {
    if (s < 1024) snprintf(b, n, "%ld B", s);
    else if (s < 1024 * 1024) snprintf(b, n, "%.1f KB", s / 1024.0);
    else snprintf(b, n, "%.1f MB", s / 1048576.0);
}

typedef int (*CmdFn)(Sh *, int, char **, const char *, SBuf *);
typedef struct { const char *name; CmdFn fn; const char *help; } Builtin;
static const Builtin *find_builtin(const char *name);
static void run_pipeline(Sh *sh, const char *line, SBuf *out, int tty);

/* ---------------- builtins ---------------- */
static int c_echo(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)sh; (void)in;
    int nl = 1, i = 1;
    if (argc > 1 && !strcmp(argv[1], "-n")) { nl = 0; i = 2; }
    if (argc == 2 && !strcmp(argv[1], ".")) { sb_putc(o, '\n'); return 0; }
    for (; i < argc; i++) { sb_puts(o, argv[i]); if (i < argc - 1) sb_putc(o, ' '); }
    if (nl) sb_putc(o, '\n');
    return 0;
}
static void ls_one(Sh *sh, VNode *n, int lng, int tty, SBuf *o) {
    (void)sh;
    if (lng) {
        char sz[16], when[24];
        time_t t = n->mtime;
        strftime(when, sizeof when, "%b %d %H:%M", localtime(&t));
        if (n->dir) snprintf(sz, sizeof sz, "%d items", vfs_count(n)); else fmt_size(n->size, sz, sizeof sz);
        sb_printf(o, "%s %10s  %s  ", n->dir ? "d" : "-", sz, when);
    }
    if (tty) sb_printf(o, "\x1b%x", color_for(n));
    sb_puts(o, n->name);
    if (n->dir) sb_putc(o, '/');
    if (tty) sb_puts(o, COL_RESET);
}
static int c_ls(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    int lng = has_flag(&argc, argv, 'l');
    int all = has_flag(&argc, argv, 'a');
    char *ops[MAXARG];
    int n = operands(argc, argv, ops), rc = 0;
    int dos = !strcasecmp(argv[0], "dir");
    if (!n) { ops[0] = "."; n = 1; }
    for (int i = 0; i < n; i++) {
        VNode *d = R(sh, ops[i]);
        if (!d) { rc = err(o, argv[0], "No such file or directory", ops[i]); continue; }
        if (n > 1 && d->dir) sb_printf(o, "%s:\n", ops[i]);
        if (dos) {
            char p[160];
            vfs_path(d, p, sizeof p);
            for (char *c = p; *c; c++) if (*c == '/') *c = '\\';
            sb_printf(o, " Directory of C:%s\n\n", p);
            int files = 0, dirs = 0;
            long bytes = 0;
            for (VNode *c = d->dir ? d->child : d; c; c = d->dir ? c->next : NULL) {
                time_t t = c->mtime;
                char when[24];
                strftime(when, sizeof when, "%m/%d/%Y  %I:%M %p", localtime(&t));
                if (c->dir) { sb_printf(o, "%s    <DIR>          %s\n", when, c->name); dirs++; }
                else { sb_printf(o, "%s    %14d %s\n", when, c->size, c->name); files++; bytes += c->size; }
                if (!d->dir) break;
            }
            sb_printf(o, "%16d File(s) %14ld bytes\n%16d Dir(s)\n", files, bytes, dirs);
            continue;
        }
        if (!d->dir) { ls_one(sh, d, lng, sh->tty, o); sb_putc(o, '\n'); continue; }
        if (all) sb_printf(o, lng ? "d                       ./\nd                       ../\n" : "./  ../  ");
        int col = 0;
        for (VNode *c = d->child; c; c = c->next) {
            ls_one(sh, c, lng, sh->tty, o);
            if (lng || !sh->tty) sb_putc(o, '\n');
            else {
                int L = strlen(c->name) + (c->dir ? 1 : 0);
                col++;
                if (col % 3 == 0 || !c->next) sb_putc(o, '\n');
                else for (int k = L; k < 24; k++) sb_putc(o, ' ');
            }
        }
        if (!d->child && !lng && sh->tty) {}
        if (all && !lng && !d->child) sb_putc(o, '\n');
    }
    return rc;
}
static int c_cd(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    if (argc < 2) {
        if (sh->style == 1) { char p[160]; vfs_path(sh->cwd, p, sizeof p); for (char *c = p; *c; c++) if (*c == '/') *c = '\\'; sb_printf(o, "C:%s\n", p); }
        else sh->cwd = vfs_root;
        return 0;
    }
    const char *t = argv[1];
    if (!strcmp(t, "-") ) t = sh_get(sh, "OLDPWD") ? sh_get(sh, "OLDPWD") : "/";
    VNode *d = R(sh, t);
    if (!d) return err(o, "cd", "No such file or directory", argv[1]);
    if (!d->dir) return err(o, "cd", "Not a directory", argv[1]);
    char old[160];
    vfs_path(sh->cwd, old, sizeof old);
    sh_set(sh, "OLDPWD", old);
    sh->cwd = d;
    return 0;
}
static int c_pwd(Sh *sh, int argc, char **argv, const char *in, SBuf *o) { (void)argc; (void)argv; (void)in; char p[160]; vfs_path(sh->cwd, p, sizeof p); sb_printf(o, "%s\n", p); return 0; }
static int c_mkdir(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    int p = has_flag(&argc, argv, 'p');
    char *ops[MAXARG];
    int n = operands(argc, argv, ops), rc = 0;
    if (!n) return err(o, "mkdir", "missing operand", NULL);
    for (int i = 0; i < n; i++) {
        if (!p && R(sh, ops[i])) { rc = err(o, "mkdir", "File exists", ops[i]); continue; }
        if (!vfs_mkdir(sh->cwd, ops[i], p)) rc = err(o, "mkdir", "cannot create directory", ops[i]);
    }
    return rc;
}
static int c_rmdir(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        VNode *d = R(sh, argv[i]);
        if (!d || !d->dir) { rc = err(o, "rmdir", "No such directory", argv[i]); continue; }
        if (d->child) { rc = err(o, "rmdir", "Directory not empty", argv[i]); continue; }
        if (vfs_is_inside(sh->cwd, d)) { rc = err(o, "rmdir", "You are inside it", argv[i]); continue; }
        if (!vfs_remove(d, 0)) rc = err(o, "rmdir", "Permission denied", argv[i]);
    }
    return rc;
}
static int c_rm(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    int rec = has_flag(&argc, argv, 'r') || has_flag(&argc, argv, 's');
    int force = has_flag(&argc, argv, 'f') || has_flag(&argc, argv, 'q');
    char *ops[MAXARG];
    int n = operands(argc, argv, ops), rc = 0;
    if (!n) return err(o, argv[0], "missing operand", NULL);
    for (int i = 0; i < n; i++) {
        VNode *f = R(sh, ops[i]);
        if (!f) { if (!force) rc = err(o, argv[0], "No such file or directory", ops[i]); continue; }
        if (f->dir && !rec) { rc = err(o, argv[0], "Is a directory (use -r)", ops[i]); continue; }
        if (vfs_is_inside(sh->cwd, f)) { rc = err(o, argv[0], "You are inside it", ops[i]); continue; }
        if (!vfs_remove(f, !vfs_is_inside(f, vfs_recycle))) rc = err(o, argv[0], "Permission denied", ops[i]);
    }
    if (!rc && sh->tty && !force) sb_puts(o, COL(8) "(moved to the Recycle Bin)" COL_RESET "\n");
    return rc;
}
static int c_touch(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    int rc = 0;
    if (argc < 2) return err(o, "touch", "missing file operand", NULL);
    for (int i = 1; i < argc; i++) {
        VNode *f = vfs_create(sh->cwd, argv[i]);
        if (!f) rc = err(o, "touch", "cannot touch", argv[i]);
        else { f->mtime = (uint32_t)time(NULL); vfs_changes++; }
    }
    return rc;
}
static int c_cat(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    int num = has_flag(&argc, argv, 'n');
    char *ops[MAXARG];
    int n = operands(argc, argv, ops);
    SBuf b = { 0 };
    int rc = gather(sh, argv[0], ops, n, in, &b, o);
    if (num) {
        char **L; int k = lines_of(sb_str(&b), &L);
        for (int i = 0; i < k; i++) sb_printf(o, "%6d  %s\n", i + 1, L[i]);
        free_lines(L, k);
    } else {
        for (int i = 0; i < b.n; i++) { char c = b.s[i]; if (c == '\n' || c == '\t' || (unsigned char)c >= 32) sb_putc(o, c); else sb_putc(o, '.'); }
        if (b.n && b.s[b.n - 1] != '\n') sb_putc(o, '\n');
    }
    sb_free(&b);
    return rc;
}
static int c_cp(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    int rec = has_flag(&argc, argv, 'r');
    int mv = !strcmp(argv[0], "mv") || !strcasecmp(argv[0], "move") || !strcasecmp(argv[0], "ren") || !strcasecmp(argv[0], "rename");
    char *ops[MAXARG];
    int n = operands(argc, argv, ops), rc = 0;
    if (n < 2) return err(o, argv[0], "usage: <source...> <destination>", NULL);
    VNode *dst = R(sh, ops[n - 1]);
    for (int i = 0; i < n - 1; i++) {
        VNode *s = R(sh, ops[i]);
        if (!s) { rc = err(o, argv[0], "No such file or directory", ops[i]); continue; }
        if (s->dir && !rec && !mv) { rc = err(o, argv[0], "omitting directory (use -r)", ops[i]); continue; }
        VNode *dir; char leaf[40] = "";
        if (dst && dst->dir) dir = dst;
        else {
            if (n > 2) return err(o, argv[0], "target is not a directory", ops[n - 1]);
            dir = vfs_parent_for(sh->cwd, ops[n - 1], leaf);
            if (!dir) { rc = err(o, argv[0], "No such directory", ops[n - 1]); continue; }
            if (dst && !dst->dir && dst != s) vfs_remove(dst, 0);   /* overwrite */
        }
        int ok = mv ? vfs_move(s, dir, leaf) : vfs_copy(s, dir, leaf[0] ? leaf : NULL) != NULL;
        if (!ok) rc = err(o, argv[0], "cannot do that", ops[i]);
    }
    return rc;
}
static void tree_rec(VNode *d, char *pre, SBuf *o, int tty, int *nd, int *nf) {
    for (VNode *c = d->child; c; c = c->next) {
        int last = !c->next;
        sb_printf(o, "%s%s", pre, last ? "`-- " : "|-- ");
        if (tty) sb_printf(o, "\x1b%x", color_for(c));
        sb_puts(o, c->name);
        if (tty) sb_puts(o, COL_RESET);
        sb_putc(o, '\n');
        if (c->dir) {
            (*nd)++;
            int L = strlen(pre);
            if (L < 100) { strcat(pre, last ? "    " : "|   "); tree_rec(c, pre, o, tty, nd, nf); pre[L] = 0; }
        } else (*nf)++;
    }
}
static int c_tree(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    VNode *d = argc > 1 ? R(sh, argv[1]) : sh->cwd;
    if (!d || !d->dir) return err(o, "tree", "No such directory", argc > 1 ? argv[1] : ".");
    char pre[128] = "";
    int nd = 0, nf = 0;
    sb_puts(o, argc > 1 ? argv[1] : ".");
    sb_putc(o, '\n');
    tree_rec(d, pre, o, sh->tty, &nd, &nf);
    sb_printf(o, "\n%d directories, %d files\n", nd, nf);
    return 0;
}
static void find_rec(VNode *d, const char *base, const char *pat, SBuf *o) {
    for (VNode *c = d->child; c; c = c->next) {
        char p[256];
        snprintf(p, sizeof p, "%s/%s", base, c->name);
        if (!pat || wild(pat, c->name)) sb_printf(o, "%s\n", p);
        if (c->dir) find_rec(c, p, pat, o);
    }
}
static int c_find(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    const char *start = ".", *pat = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-name") && i + 1 < argc) pat = argv[++i];
        else if (argv[i][0] != '-') { if (i == 1) start = argv[i]; else pat = argv[i]; }
    }
    VNode *d = R(sh, start);
    if (!d || !d->dir) return err(o, "find", "No such directory", start);
    find_rec(d, strcmp(start, "/") ? start : "", pat, o);
    return 0;
}
static int c_grep(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    int ic = has_flag(&argc, argv, 'i'), num = has_flag(&argc, argv, 'n'), inv = has_flag(&argc, argv, 'v'), cnt = has_flag(&argc, argv, 'c');
    char *ops[MAXARG];
    int n = operands(argc, argv, ops);
    if (!n) return err(o, argv[0], "usage: grep [-i -n -v -c] pattern [file...]", NULL);
    const char *pat = ops[0];
    int matches = 0;
    for (int f = (n > 1 ? 1 : 0); f < (n > 1 ? n : 1); f++) {
        SBuf b = { 0 };
        if (n > 1) gather(sh, argv[0], &ops[f], 1, NULL, &b, o); else sb_puts(&b, in ? in : "");
        char **L; int k = lines_of(sb_str(&b), &L);
        for (int i = 0; i < k; i++) {
            int hit;
            if (ic) {
                char a[512], p2[128];
                int j;
                for (j = 0; L[i][j] && j < 511; j++) a[j] = tolower((unsigned char)L[i][j]);
                a[j] = 0;
                for (j = 0; pat[j] && j < 127; j++) p2[j] = tolower((unsigned char)pat[j]);
                p2[j] = 0;
                hit = strstr(a, p2) != NULL;
            } else hit = strstr(L[i], pat) != NULL;
            if (hit == inv) continue;
            matches++;
            if (cnt) continue;
            if (n > 2) sb_printf(o, COL(d) "%s" COL_RESET ":", ops[f]);
            if (num) sb_printf(o, COL(a) "%d" COL_RESET ":", i + 1);
            if (sh->tty && !inv && !ic) {
                const char *p = L[i], *m;
                while ((m = strstr(p, pat))) { sb_write(o, p, (int)(m - p)); sb_printf(o, COL(c) "%s" COL_RESET, pat); p = m + strlen(pat); }
                sb_printf(o, "%s\n", p);
            } else sb_printf(o, "%s\n", L[i]);
        }
        free_lines(L, k);
        sb_free(&b);
    }
    if (cnt) sb_printf(o, "%d\n", matches);
    return matches ? 0 : 1;
}
static int c_wc(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    int l = has_flag(&argc, argv, 'l'), w = has_flag(&argc, argv, 'w'), c = has_flag(&argc, argv, 'c');
    char *ops[MAXARG];
    int n = operands(argc, argv, ops);
    SBuf b = { 0 };
    int rc = gather(sh, "wc", ops, n, in, &b, o);
    int lines = 0, words = 0, inw = 0;
    for (int i = 0; i < b.n; i++) {
        if (b.s[i] == '\n') lines++;
        if (isspace((unsigned char)b.s[i])) inw = 0; else if (!inw) { inw = 1; words++; }
    }
    if (!l && !w && !c) sb_printf(o, "%7d %7d %7d\n", lines, words, b.n);
    else { if (l) sb_printf(o, "%d ", lines); if (w) sb_printf(o, "%d ", words); if (c) sb_printf(o, "%d", b.n); sb_putc(o, '\n'); }
    sb_free(&b);
    return rc;
}
static int c_headtail(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    int k = num_opt(argc, argv, 'n', 10);
    char *ops[MAXARG];
    int n = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') { if (argv[i][1] == 'n' && !argv[i][2]) i++; continue; }
        ops[n++] = argv[i];
    }
    SBuf b = { 0 };
    int rc = gather(sh, argv[0], ops, n, in, &b, o);
    char **L; int cnt = lines_of(sb_str(&b), &L);
    int from = argv[0][0] == 't' ? (cnt - k > 0 ? cnt - k : 0) : 0, to = argv[0][0] == 't' ? cnt : (k < cnt ? k : cnt);
    for (int i = from; i < to; i++) sb_printf(o, "%s\n", L[i]);
    free_lines(L, cnt);
    sb_free(&b);
    return rc;
}
static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static int cmp_num(const void *a, const void *b) { double x = atof(*(char *const *)a), y = atof(*(char *const *)b); return x < y ? -1 : x > y; }
static int c_sort(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    int rev = has_flag(&argc, argv, 'r'), num = has_flag(&argc, argv, 'n'), uniq = has_flag(&argc, argv, 'u');
    char *ops[MAXARG];
    int n = operands(argc, argv, ops);
    SBuf b = { 0 };
    int rc = gather(sh, "sort", ops, n, in, &b, o);
    char **L; int k = lines_of(sb_str(&b), &L);
    qsort(L, k, sizeof(char *), num ? cmp_num : cmp_str);
    for (int i = 0; i < k; i++) {
        int idx = rev ? k - 1 - i : i;
        if (uniq && i && !strcmp(L[idx], L[rev ? idx + 1 : idx - 1])) continue;
        sb_printf(o, "%s\n", L[idx]);
    }
    free_lines(L, k);
    sb_free(&b);
    return rc;
}
static int c_filter(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    char *ops[MAXARG];
    int n = operands(argc, argv, ops);
    SBuf b = { 0 };
    int rc = gather(sh, argv[0], ops, n, in, &b, o);
    char **L; int k = lines_of(sb_str(&b), &L);
    const char *c = argv[0];
    for (int i = 0; i < k; i++) {
        if (!strcmp(c, "uniq")) { if (i && !strcmp(L[i], L[i - 1])) continue; sb_printf(o, "%s\n", L[i]); }
        else if (!strcmp(c, "rev")) { int L2 = strlen(L[i]); for (int j = L2 - 1; j >= 0; j--) sb_putc(o, L[i][j]); sb_putc(o, '\n'); }
        else if (!strcmp(c, "tac")) sb_printf(o, "%s\n", L[k - 1 - i]);
        else if (!strcmp(c, "nl")) sb_printf(o, "%6d\t%s\n", i + 1, L[i]);
        else if (!strcmp(c, "upper") || !strcmp(c, "lower")) {
            for (char *p = L[i]; *p; p++) sb_putc(o, c[0] == 'u' ? toupper((unsigned char)*p) : tolower((unsigned char)*p));
            sb_putc(o, '\n');
        }
    }
    free_lines(L, k);
    sb_free(&b);
    return rc;
}
static int c_seq(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)sh; (void)in;
    int a = 1, b = 10, s = 1;
    if (argc == 2) b = atoi(argv[1]);
    else if (argc == 3) { a = atoi(argv[1]); b = atoi(argv[2]); }
    else if (argc >= 4) { a = atoi(argv[1]); s = atoi(argv[2]); b = atoi(argv[3]); }
    if (!s) s = 1;
    for (int i = a, k = 0; (s > 0 ? i <= b : i >= b) && k < 5000; i += s, k++) sb_printf(o, "%d\n", i);
    return 0;
}
static int c_history(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)argc; (void)argv; (void)in;
    for (int i = 0; i < sh->nh; i++) sb_printf(o, "%5d  %s\n", i + 1, sh->hist[i]);
    return 0;
}
static int c_clear(Sh *sh, int argc, char **argv, const char *in, SBuf *o) { (void)argc; (void)argv; (void)in; (void)o; sh->req_clear = 1; return 0; }
static int c_exit(Sh *sh, int argc, char **argv, const char *in, SBuf *o) { (void)argc; (void)argv; (void)in; (void)o; sh->req_exit = 1; return 0; }
static int c_edit(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    if (argc < 2) { app_launch(APP_NOTEPAD); return 0; }
    VNode *f = vfs_create(sh->cwd, argv[1]);
    if (!f) return err(o, argv[0], "cannot open", argv[1]);
    char p[160];
    vfs_path(f, p, sizeof p);
    notepad_open_file(p);
    return 0;
}
static int c_open(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    if (argc < 2) return err(o, argv[0], "usage: open <file|folder|program>", NULL);
    for (int i = 1; i < argc; i++) {
        VNode *f = R(sh, argv[i]);
        if (f) { vfs_open(f); continue; }
        int a = app_by_cmd(argv[i]);
        if (a >= 0) app_launch(a); else err(o, argv[0], "cannot find", argv[i]);
    }
    return 0;
}
/* tiny expression evaluator */
static const char *ep; static int eerr;
static double e_expr(void);
static void e_skip(void) { while (*ep == ' ') ep++; }
static double e_atom(void) {
    e_skip();
    if (*ep == '(') { ep++; double v = e_expr(); e_skip(); if (*ep == ')') ep++; else eerr = 1; return v; }
    if (*ep == '-') { ep++; return -e_atom(); }
    if (*ep == '+') { ep++; return e_atom(); }
    static const struct { const char *n; double (*f)(double); } fn[] = { { "sqrt", sqrt }, { "sin", sin }, { "cos", cos }, { "tan", tan }, { "abs", fabs }, { "log", log10 }, { "ln", log } };
    for (unsigned i = 0; i < sizeof fn / sizeof fn[0]; i++)
        if (!strncmp(ep, fn[i].n, strlen(fn[i].n))) { ep += strlen(fn[i].n); return fn[i].f(e_atom()); }
    if (!strncmp(ep, "pi", 2)) { ep += 2; return 3.14159265358979; }
    char *end;
    double v = strtod(ep, &end);
    if (end == ep) { eerr = 1; return 0; }
    ep = end;
    return v;
}
static double e_pow(void) { double b = e_atom(); e_skip(); if (*ep == '^') { ep++; return pow(b, e_pow()); } return b; }
static double e_term(void) {
    double v = e_pow();
    for (;;) {
        e_skip();
        if (*ep == '*') { ep++; v *= e_pow(); }
        else if (*ep == '/') { ep++; double r = e_pow(); if (r == 0) eerr = 2; else v /= r; }
        else if (*ep == '%') { ep++; double r = e_pow(); if (r == 0) eerr = 2; else v = fmod(v, r); }
        else return v;
    }
}
static double e_expr(void) {
    double v = e_term();
    for (;;) { e_skip(); if (*ep == '+') { ep++; v += e_term(); } else if (*ep == '-') { ep++; v -= e_term(); } else return v; }
}
static int c_calc(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)sh; (void)in;
    if (argc < 2) { app_launch(APP_CALC); return 0; }
    char buf[256] = "";
    for (int i = 1; i < argc; i++) { strncat(buf, argv[i], sizeof buf - strlen(buf) - 2); strcat(buf, " "); }
    ep = buf; eerr = 0;
    double v = e_expr();
    e_skip();
    if (*ep) eerr = 1;
    if (eerr == 2) return err(o, argv[0], "division by zero", NULL);
    if (eerr) return err(o, argv[0], "syntax error", buf);
    sb_printf(o, "%.10g\n", v);
    return 0;
}
static int c_date(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)sh; (void)in;
    time_t t = time(NULL);
    char b[80];
    const char *f = argc > 1 && argv[1][0] == '+' ? argv[1] + 1 : !strcasecmp(argv[0], "time") ? "%H:%M:%S" : "%a %b %d %H:%M:%S %Y";
    strftime(b, sizeof b, f, localtime(&t));
    sb_printf(o, "%s\n", b);
    return 0;
}
static int c_uptime(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)sh; (void)argc; (void)argv; (void)in;
    uint64_t s = now_ms / 1000;
    int nw = 0;
    for (int i = 0; i < MAX_WIN; i++) nw += wins[i].used;
    sb_printf(o, "up %d:%02d:%02d, 1 user, %d windows, %d fps\n", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60), nw, fps);
    return 0;
}
static int c_whoami(Sh *sh, int argc, char **argv, const char *in, SBuf *o) { (void)sh; (void)argc; (void)argv; (void)in; sb_printf(o, "%s\n", cfg.user); return 0; }
static int c_hostname(Sh *sh, int argc, char **argv, const char *in, SBuf *o) { (void)sh; (void)argc; (void)argv; (void)in; sb_puts(o, "cubexp\n"); return 0; }
static int c_uname(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)sh; (void)in;
    if (argc > 1) sb_printf(o, "CubeXP cubexp 1.1 %s gekko PowerPC GameCube\n", OS_EDITION);
    else sb_puts(o, "CubeXP\n");
    return 0;
}
static int c_ver(Sh *sh, int argc, char **argv, const char *in, SBuf *o) { (void)sh; (void)argc; (void)argv; (void)in; sb_printf(o, "\nCubeXP [%s]\n\n", OS_VERSION); return 0; }
static int c_neofetch(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)argc; (void)argv; (void)in;
    static const char *art[] = {
        COL(e) "      +---------+    ", COL(e) "     /         /|    ", COL(e) "    /  " COL(f) "CUBE" COL(e) "   / |    ",
        COL(e) "   +---------+  |    ", COL(9) "   |         |  +    ", COL(9) "   |   " COL(a) "XP" COL(9) "    | /     ",
        COL(9) "   |         |/      ", COL(9) "   +---------+       ", "                     ",
    };
    char info[9][96];
    uint64_t s = now_ms / 1000;
    int nw = 0;
    for (int i = 0; i < MAX_WIN; i++) nw += wins[i].used;
    snprintf(info[0], 96, COL(a) "%s" COL(7) "@" COL(a) "cubexp", cfg.user);
    snprintf(info[1], 96, "-------------");
    snprintf(info[2], 96, COL(a) "OS" COL(7) ": CubeXP %s", OS_EDITION);
    snprintf(info[3], 96, COL(a) "Host" COL(7) ": Nintendo GameCube");
    snprintf(info[4], 96, COL(a) "CPU" COL(7) ": IBM Gekko @ 485 MHz");
    snprintf(info[5], 96, COL(a) "GPU" COL(7) ": ATI Flipper @ 162 MHz");
    snprintf(info[6], 96, COL(a) "Memory" COL(7) ": %u KB / 24576 KB", (unsigned)((plat_mem_total() - plat_mem_free()) / 1024));
    snprintf(info[7], 96, COL(a) "Uptime" COL(7) ": %dh %dm %ds  (%d windows)", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60), nw);
    snprintf(info[8], 96, COL(a) "Theme" COL(7) ": %s    " COL(c) "##" COL(e) "##" COL(a) "##" COL(9) "##" COL(d) "##", themes[cfg.theme].name);
    for (int i = 0; i < 9; i++) sb_printf(o, "%s" COL_RESET "  %s" COL_RESET "\n", sh->tty ? art[i] : "", info[i]);
    return 0;
}
static int c_ps(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)sh; (void)argc; (void)argv; (void)in;
    sb_puts(o, "  PID  PROGRAM          TITLE\n");
    sb_puts(o, "    1  shell            CubeXP Desktop\n");
    for (int k = 0; k < MAX_WIN; k++)
        if (wins[k].used) sb_printf(o, "%5d  %-16s %.40s\n", k + 100, apps[wins[k].app].cmd, wins[k].title);
    return 0;
}
static int c_kill(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    if (argc < 2) return err(o, argv[0], "usage: kill <pid|program>", NULL);
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '/' || argv[i][0] == '-') continue;
        int pid = atoi(argv[i]) - 100, done = 0;
        for (int k = 0; k < MAX_WIN; k++) {
            if (!wins[k].used || &wins[k] == sh->win) continue;
            if (k == pid || !strcasecmp(apps[wins[k].app].cmd, argv[i])) { win_close(&wins[k]); done = 1; }
        }
        if (!done) rc = err(o, argv[0], "no such process", argv[i]);
    }
    return rc;
}
static int c_free(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)sh; (void)argc; (void)argv; (void)in;
    size_t t = plat_mem_total() / 1024, f = plat_mem_free() / 1024;
    sb_printf(o, "          total       used       free\nMem:   %8u   %8u   %8u   (KB)\nARAM:     16384          0      16384\n", (unsigned)t, (unsigned)(t - f), (unsigned)f);
    return 0;
}
static int c_df(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)sh; (void)argc; (void)argv; (void)in;
    long used = vfs_total_size(vfs_root);
    sb_printf(o, "Filesystem      Size     Used    Mounted on\nC: (cubexp)    8.0M  %6.1fK    /\n", used / 1024.0);
    sb_printf(o, "Saved to: %s\n", plat_storage_ok() ? plat_storage_name() : "RAM only (no SD card - changes are lost at power off)");
    return 0;
}
static int c_du(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    VNode *d = argc > 1 ? R(sh, argv[1]) : sh->cwd;
    if (!d) return err(o, "du", "No such file or directory", argv[1]);
    for (VNode *c = d->dir ? d->child : NULL; c; c = c->next) { char s[16]; fmt_size(vfs_total_size(c), s, sizeof s); sb_printf(o, "%10s  %s%s\n", s, c->name, c->dir ? "/" : ""); }
    char s[16]; fmt_size(vfs_total_size(d), s, sizeof s);
    sb_printf(o, "%10s  total\n", s);
    return 0;
}
static int c_stat(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    if (argc < 2) return err(o, "stat", "missing operand", NULL);
    VNode *f = R(sh, argv[1]);
    if (!f) return err(o, "stat", "No such file or directory", argv[1]);
    char p[160], when[40];
    vfs_path(f, p, sizeof p);
    time_t t = f->mtime;
    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", localtime(&t));
    sb_printf(o, "  File: %s\n  Type: %s\n  Size: %ld bytes\nModify: %s\n", p, f->dir ? "directory" : f->app >= 0 ? "program shortcut" : "regular file", vfs_total_size(f), when);
    return 0;
}
static int c_xxd(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    char *ops[MAXARG];
    int n = operands(argc, argv, ops);
    SBuf b = { 0 };
    int rc = gather(sh, argv[0], ops, n, in, &b, o);
    for (int i = 0; i < b.n && i < 4096; i += 16) {
        sb_printf(o, "%08x: ", i);
        for (int j = 0; j < 16; j++) { if (i + j < b.n) sb_printf(o, "%02x", (unsigned char)b.s[i + j]); else sb_puts(o, "  "); if (j & 1) sb_putc(o, ' '); }
        sb_putc(o, ' ');
        for (int j = 0; j < 16 && i + j < b.n; j++) { unsigned char c = b.s[i + j]; sb_putc(o, c >= 32 && c < 127 ? c : '.'); }
        sb_putc(o, '\n');
    }
    sb_free(&b);
    return rc;
}
static int c_cowsay(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)sh;
    char msg[200] = "";
    for (int i = 1; i < argc; i++) { strncat(msg, argv[i], sizeof msg - strlen(msg) - 2); if (i < argc - 1) strcat(msg, " "); }
    if (!msg[0] && in && *in) { snprintf(msg, sizeof msg, "%s", in); msg[strcspn(msg, "\n")] = 0; }
    if (!msg[0]) strcpy(msg, "Moo!");
    int L = strlen(msg);
    if (L > 50) { L = 50; msg[50] = 0; }
    sb_putc(o, ' '); for (int i = 0; i < L + 2; i++) sb_putc(o, '_'); sb_putc(o, '\n');
    sb_printf(o, "< %s >\n ", msg);
    for (int i = 0; i < L + 2; i++) sb_putc(o, '-');
    sb_puts(o, "\n        \\   ^__^\n         \\  (oo)\\_______\n            (__)\\       )\\/\\\n                ||----w |\n                ||     ||\n");
    return 0;
}
static int c_fortune(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)sh; (void)argc; (void)argv; (void)in;
    static const char *f[] = {
        "A clean desk is a sign of a full Recycle Bin.", "There are 10 kinds of people: those who know binary and those who don't.",
        "Have you tried turning it off and on again?", "The GameCube handle is load-bearing.", "Today is a good day to beat expert Minesweeper.",
        "Real programmers count from 0.", "Save early, save often.", "It works on my GameCube.", "Press START to continue your adventure.",
        "Your next commit will compile on the first try.",
    };
    sb_printf(o, "%s\n", f[rand() % (int)(sizeof f / sizeof f[0])]);
    return 0;
}
static int c_matrix(Sh *sh, int argc, char **argv, const char *in, SBuf *o) { (void)argc; (void)argv; (void)in; (void)o; sh->req_matrix = 1; return 0; }
static int c_color(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    if (argc < 2) { sh->req_color = 1; sh->bg = 0; sh->fg = 7; return 0; }
    const char *a = argv[1];
    int bg = 0, fg;
    if (strlen(a) >= 2) { char x[2] = { a[0], 0 }, y[2] = { a[1], 0 }; bg = (int)strtol(x, NULL, 16); fg = (int)strtol(y, NULL, 16); }
    else { char y[2] = { a[0], 0 }; fg = (int)strtol(y, NULL, 16); }
    if (!isxdigit((unsigned char)a[0]) || bg == fg) return err(o, "color", "usage: color <bg><fg> in hex, e.g. color 0a", NULL);
    sh->req_color = 1; sh->bg = bg; sh->fg = fg;
    return 0;
}
static int c_title(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in; (void)o;
    if (!sh->win) return 0;
    char t[48] = "";
    for (int i = 1; i < argc; i++) { strncat(t, argv[i], sizeof t - strlen(t) - 2); if (i < argc - 1) strcat(t, " "); }
    snprintf(sh->win->title, sizeof sh->win->title, "%s", t[0] ? t : "Terminal");
    return 0;
}
static int c_power(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)sh; (void)in;
    const char *c = argv[0];
    if (!strcmp(c, "reboot") || (argc > 1 && strstr(argv[1], "r"))) shell_restart();
    else if (!strcmp(c, "logoff") || !strcmp(c, "logout") || (argc > 1 && strstr(argv[1], "l"))) shell_log_off();
    else if (!strcmp(c, "poweroff") || (argc > 1 && (strstr(argv[1], "s") || strstr(argv[1], "h")))) shell_turn_off();
    else sb_puts(o, "usage: shutdown -s (turn off) | -r (restart) | -l (log off)\n");
    return 0;
}
static int c_beep(Sh *sh, int argc, char **argv, const char *in, SBuf *o) { (void)sh; (void)argc; (void)argv; (void)in; (void)o; sound_play(SND_DING); return 0; }
static int c_play(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    if (argc < 2) return err(o, "play", "usage: play C4 E4 G4 ...  or  play song.song", NULL);
    VNode *f = R(sh, argv[1]);
    if (f && !f->dir) { vfs_open(f); return 0; }
    char notes[400] = "";
    for (int i = 1; i < argc; i++) { strncat(notes, argv[i], sizeof notes - strlen(notes) - 2); strcat(notes, " "); }
    music_play_notes(notes, 180);
    return 0;
}
static int c_theme(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)sh; (void)in;
    if (argc < 2) { for (int i = 0; i < 3; i++) sb_printf(o, "%d  %s%s\n", i, themes[i].name, i == cfg.theme ? "  (current)" : ""); return 0; }
    int t = atoi(argv[1]);
    if (!strcasecmp(argv[1], "blue")) t = 0; else if (!strcasecmp(argv[1], "olive")) t = 1; else if (!strcasecmp(argv[1], "silver")) t = 2;
    if (t < 0 || t > 2) return err(o, "theme", "pick 0-2 or blue/olive/silver", NULL);
    cfg.theme = t; settings_save();
    return 0;
}
static int c_wallpaper(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    if (argc < 2) { for (int i = 0; i < wallpaper_count; i++) sb_printf(o, "%d  %s%s\n", i, wallpaper_names[i], i == cfg.wallpaper ? "  (current)" : ""); return 0; }
    VNode *f = R(sh, argv[1]);
    if (f && !f->dir) {
        int w, h;
        uint32_t *px = bmp_decode(f->data, f->size, &w, &h);
        if (!px) return err(o, "wallpaper", "not a BMP picture", argv[1]);
        set_custom_wallpaper(px, w, h); free(px); settings_save();
        return 0;
    }
    int n = atoi(argv[1]);
    if (n < 0 || n >= wallpaper_count) return err(o, "wallpaper", "no such wallpaper", argv[1]);
    cfg.wallpaper = n; shell_wallpaper_dirty(); settings_save();
    return 0;
}
static int c_set(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    if (argc < 2) {
        for (int i = 0; i < sh->nvar; i++) sb_printf(o, "%s=%s\n", sh->var[i][0], sh->var[i][1]);
        sb_printf(o, "USER=%s\nHOME=/\nPATH=/Programs\nPWD=%s\n", cfg.user, sh_get(sh, "PWD"));
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (!eq) { const char *v = sh_get(sh, argv[i]); sb_printf(o, "%s=%s\n", argv[i], v ? v : ""); continue; }
        *eq = 0;
        if (!strcmp(argv[i], "USER") && eq[1]) { snprintf(cfg.user, sizeof cfg.user, "%s", eq + 1); settings_save(); }
        else sh_set(sh, argv[i], eq + 1);
    }
    return 0;
}
static int c_unset(Sh *sh, int argc, char **argv, const char *in, SBuf *o) { (void)in; (void)o; for (int i = 1; i < argc; i++) sh_unset(sh, argv[i]); return 0; }
static int c_alias(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    if (argc < 2) { for (int i = 0; i < sh->nalias; i++) sb_printf(o, "alias %s='%s'\n", sh->alias[i][0], sh->alias[i][1]); return 0; }
    char joined[160] = "";
    for (int i = 1; i < argc; i++) { strncat(joined, argv[i], sizeof joined - strlen(joined) - 2); if (i < argc - 1) strcat(joined, " "); }
    char *eq = strchr(joined, '=');
    if (!eq) return err(o, "alias", "usage: alias name=command", NULL);
    *eq = 0;
    for (int i = 0; i < sh->nalias; i++) if (!strcmp(sh->alias[i][0], joined)) { snprintf(sh->alias[i][1], 72, "%s", eq + 1); return 0; }
    if (sh->nalias < 16) { snprintf(sh->alias[sh->nalias][0], 72, "%s", joined); snprintf(sh->alias[sh->nalias][1], 72, "%s", eq + 1); sh->nalias++; }
    return 0;
}
static int run_script(Sh *sh, VNode *f, SBuf *o) {
    if (sh->depth > 6) return err(o, "sh", "scripts nested too deeply", NULL);
    sh->depth++;
    char *text = malloc(f->size + 1);
    if (!text) { sh->depth--; return 1; }
    memcpy(text, f->data, f->size); text[f->size] = 0;
    char *save = NULL, *line = strtok_r(text, "\n", &save);
    while (line && !sh->req_exit) {
        line[strcspn(line, "\r")] = 0;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p && *p != '#' && strncasecmp(p, "rem ", 4) && strcasecmp(p, "@echo off")) sh_run_line(sh, p, o);
        line = strtok_r(NULL, "\n", &save);
    }
    free(text);
    sh->depth--;
    return sh->status;
}
static int c_sh(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)in;
    if (argc < 2) return err(o, argv[0], "usage: sh <script.sh>", NULL);
    VNode *f = R(sh, argv[1]);
    if (!f || f->dir) return err(o, argv[0], "No such file", argv[1]);
    return run_script(sh, f, o);
}
static int c_which(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)sh; (void)in;
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (find_builtin(argv[i])) sb_printf(o, "%s: shell built-in command\n", argv[i]);
        else if (app_by_cmd(argv[i]) >= 0) sb_printf(o, "/Programs/%s.exe\n", apps[app_by_cmd(argv[i])].cmd);
        else rc = err(o, "which", "not found", argv[i]);
    }
    return rc;
}
static int c_true(Sh *sh, int argc, char **argv, const char *in, SBuf *o) { (void)sh; (void)argc; (void)in; (void)o; return argv[0][0] == 'f'; }
static int c_random(Sh *sh, int argc, char **argv, const char *in, SBuf *o) { (void)sh; (void)in; int m = argc > 1 ? atoi(argv[1]) : 100; if (m < 1) m = 1; sb_printf(o, "%d\n", rand() % m + 1); return 0; }
static int c_apps(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)sh; (void)argc; (void)argv; (void)in;
    for (int a = 0; a < APP_COUNT; a++) if (a != APP_MSGBOX) sb_printf(o, "  " COL(a) "%-10s" COL_RESET " %s\n", apps[a].cmd, apps[a].name);
    return 0;
}
static int c_help(Sh *sh, int argc, char **argv, const char *in, SBuf *o);
static int c_tasks_dummy(Sh *sh, int argc, char **argv, const char *in, SBuf *o) { return c_ps(sh, argc, argv, in, o); }

static const Builtin builtins[] = {
    { "help", c_help, "show this list (help <command> for one)" }, { "man", c_help, NULL },
    { "ls", c_ls, "list files  (-l long, -a all)" }, { "dir", c_ls, NULL },
    { "cd", c_cd, "change folder  (cd .., cd /, cd -)" }, { "chdir", c_cd, NULL }, { "pwd", c_pwd, "print current folder" },
    { "mkdir", c_mkdir, "make folder  (-p makes parents)" }, { "md", c_mkdir, NULL }, { "rmdir", c_rmdir, "remove empty folder" }, { "rd", c_rmdir, NULL },
    { "rm", c_rm, "delete to Recycle Bin  (-r folders, -f quiet)" }, { "del", c_rm, NULL }, { "erase", c_rm, NULL },
    { "touch", c_touch, "create empty file / update time" }, { "cat", c_cat, "show file contents  (-n numbers)" }, { "type", c_cat, NULL },
    { "cp", c_cp, "copy  (-r for folders)" }, { "copy", c_cp, NULL }, { "mv", c_cp, "move or rename" }, { "move", c_cp, NULL }, { "ren", c_cp, NULL }, { "rename", c_cp, NULL },
    { "tree", c_tree, "draw the folder tree" }, { "find", c_find, "find [dir] -name '*.txt'" },
    { "grep", c_grep, "search text  (-i -n -v -c)" }, { "findstr", c_grep, NULL }, { "wc", c_wc, "count lines/words/bytes  (-l -w -c)" },
    { "head", c_headtail, "first lines  (-n N)" }, { "tail", c_headtail, "last lines  (-n N)" }, { "sort", c_sort, "sort lines  (-r -n -u)" },
    { "uniq", c_filter, "drop repeated lines" }, { "rev", c_filter, "reverse each line" }, { "tac", c_filter, "reverse line order" },
    { "nl", c_filter, "number lines" }, { "upper", c_filter, "UPPERCASE text" }, { "lower", c_filter, "lowercase text" },
    { "echo", c_echo, "print text  (echo hi > file.txt)" }, { "seq", c_seq, "count: seq 1 10" }, { "xxd", c_xxd, "hex dump a file" },
    { "stat", c_stat, "file details" }, { "du", c_du, "folder sizes" }, { "df", c_df, "disk usage" },
    { "edit", c_edit, "edit a file in Notepad" }, { "nano", c_edit, NULL }, { "notepad", c_edit, NULL },
    { "open", c_open, "open file/folder/program" }, { "start", c_open, NULL }, { "xdg-open", c_open, NULL },
    { "sh", c_sh, "run a script  (sh hello.sh)" }, { "bash", c_sh, NULL }, { "source", c_sh, NULL }, { "call", c_sh, NULL },
    { "set", c_set, "show/set variables  (set NAME=value)" }, { "export", c_set, NULL }, { "env", c_set, NULL }, { "unset", c_unset, "remove a variable" },
    { "alias", c_alias, "alias ll='ls -l'" }, { "history", c_history, "previous commands" }, { "which", c_which, "where a command lives" },
    { "clear", c_clear, "clear the screen" }, { "cls", c_clear, NULL }, { "exit", c_exit, "close the terminal" },
    { "calc", c_calc, "maths: calc (2+3)*sqrt(16)" }, { "expr", c_calc, NULL }, { "random", c_random, "random number 1..N" },
    { "date", c_date, "date  (date +%H:%M)" }, { "time", c_date, NULL }, { "uptime", c_uptime, "how long CubeXP has run" },
    { "whoami", c_whoami, "current user" }, { "hostname", c_hostname, NULL }, { "uname", c_uname, "system name  (-a)" }, { "ver", c_ver, NULL },
    { "neofetch", c_neofetch, "system summary with logo" }, { "systeminfo", c_neofetch, NULL },
    { "ps", c_ps, "running programs" }, { "tasklist", c_tasks_dummy, NULL }, { "kill", c_kill, "close a program: kill snake" }, { "taskkill", c_kill, NULL },
    { "free", c_free, "memory use" }, { "mem", c_free, NULL },
    { "apps", c_apps, "list programs you can start by name" },
    { "cowsay", c_cowsay, "a cow says something" }, { "fortune", c_fortune, "a random saying" }, { "matrix", c_matrix, "digital rain (any key stops)" },
    { "play", c_play, "play notes: play C4 E4 G4" }, { "beep", c_beep, NULL },
    { "color", c_color, "colours: color 0a" }, { "title", c_title, "set window title" },
    { "theme", c_theme, "theme blue|olive|silver" }, { "wallpaper", c_wallpaper, "wallpaper <n> or <picture.bmp>" },
    { "shutdown", c_power, "-s off, -r restart, -l log off" }, { "reboot", c_power, NULL }, { "poweroff", c_power, NULL }, { "logoff", c_power, NULL }, { "logout", c_power, NULL },
    { "true", c_true, NULL }, { "false", c_true, NULL },
};
#define NBUILTIN ((int)(sizeof builtins / sizeof builtins[0]))
static const Builtin *find_builtin(const char *name) {
    for (int i = 0; i < NBUILTIN; i++) if (!strcasecmp(builtins[i].name, name)) return &builtins[i];
    return NULL;
}
static int c_help(Sh *sh, int argc, char **argv, const char *in, SBuf *o) {
    (void)sh; (void)in;
    if (argc > 1) {
        const Builtin *b = find_builtin(argv[1]);
        if (!b) return err(o, "help", "no such command", argv[1]);
        for (int i = 0; i < NBUILTIN; i++) if (builtins[i].fn == b->fn && builtins[i].help) { sb_printf(o, "%s - %s\n", builtins[i].name, builtins[i].help); return 0; }
        sb_printf(o, "%s\n", b->name);
        return 0;
    }
    sb_puts(o, COL(e) "CubeXP shell" COL_RESET " - commands (Windows names like dir, copy, del work too):\n\n");
    for (int i = 0; i < NBUILTIN; i++) if (builtins[i].help) sb_printf(o, "  " COL(a) "%-10s" COL_RESET " %s\n", builtins[i].name, builtins[i].help);
    sb_puts(o, "\n" COL(e) "Features:" COL_RESET " pipes (ls | grep txt), redirects (> >> <), $VARS, $(command),\n"
               "  wildcards (*.txt), ; and && chaining, for x in a b; do echo $x; done,\n"
               "  scripts (.sh), Tab completion, Up/Down history. Any program name opens it.\n");
    return 0;
}

/* ---------------- execution ---------------- */
static int exec_cmd(Sh *sh, Cmd *c, const char *in, SBuf *out) {
    if (!c->argc) return 0;
    char *a0 = c->argv[0];
    /* VAR=value assignment */
    char *eq = strchr(a0, '=');
    if (eq && eq != a0 && c->argc == 1 && sh->style == 0) { *eq = 0; sh_set(sh, a0, eq + 1); return 0; }
    /* cmd-style drive switch "C:" */
    if (!strcasecmp(a0, "c:")) return 0;
    if (!strncasecmp(a0, "cd..", 4) && !a0[4]) { sh->cwd = sh->cwd->parent ? sh->cwd->parent : sh->cwd; return 0; }
    const Builtin *b = find_builtin(a0);
    if (b) return b->fn(sh, c->argc, c->argv, in, out);
    VNode *f = vfs_lookup(sh->cwd, a0);
    if (!f) {   /* try script extensions */
        char tmp[200];
        snprintf(tmp, sizeof tmp, "%s.sh", a0);
        f = vfs_lookup(sh->cwd, tmp);
        if (!f) { snprintf(tmp, sizeof tmp, "%s.bat", a0); f = vfs_lookup(sh->cwd, tmp); }
    }
    if (f && !f->dir) {
        if (f->app >= 0) { app_launch(f->app); return 0; }
        const char *e = vfs_ext(f);
        if (!strcasecmp(e, ".sh") || !strcasecmp(e, ".bat")) return run_script(sh, f, out);
        vfs_open(f);
        return 0;
    }
    if (f && f->dir && sh->style == 0) { sb_printf(out, COL(c) "%s: Is a directory" COL_RESET "\n", a0); return 126; }
    char base[64];
    snprintf(base, sizeof base, "%s", a0);
    char *dot = strrchr(base, '.');
    if (dot && !strcasecmp(dot, ".exe")) *dot = 0;
    int app = app_by_cmd(base);
    if (app >= 0) { app_launch(app); return 0; }
    if (sh->style == 1) sb_printf(out, "'%s' is not recognized as an internal or external command,\noperable program or batch file.\n", a0);
    else sb_printf(out, COL(c) "%s: command not found" COL_RESET "  (try 'help')\n", a0);
    return 127;
}

/* runs "a | b | c" with redirections; output appended to out */
static void run_pipeline(Sh *sh, const char *seg, SBuf *out, int tty) {
    /* split on unquoted | */
    char parts[8][400];
    int np = 0, q = 0, n = 0;
    for (const char *p = seg;; p++) {
        if (*p == '"' || *p == '\'') q = q == *p ? 0 : q ? q : *p;
        if ((*p == '|' && !q) || !*p) {
            if (np < 8) { parts[np][n < 399 ? n : 399] = 0; np++; }
            n = 0;
            if (!*p) break;
            continue;
        }
        if (np < 8 && n < 399) parts[np][n++] = *p;
    }
    SBuf cur = { 0 };
    int have_in = 0;
    for (int i = 0; i < np; i++) {
        Cmd *c = malloc(sizeof(Cmd));
        if (!c) break;
        tokenize(sh, parts[i], c);
        SBuf inb = { 0 };
        const char *in = have_in ? sb_str(&cur) : NULL;
        if (c->redir_in) {
            VNode *f = vfs_lookup(sh->cwd, c->redir_in);
            if (!f || f->dir) { sb_printf(out, COL(c) "%s: No such file" COL_RESET "\n", c->redir_in); free(c); sh->status = 1; sb_free(&cur); return; }
            sb_write(&inb, (const char *)f->data, f->size);
            in = sb_str(&inb);
        }
        int last = i == np - 1;
        SBuf res = { 0 };
        sh->tty = last && tty && !c->redir_out;
        sh->status = exec_cmd(sh, c, in, &res);
        if (!sh->tty) strip_colors(&res);
        if (c->redir_out) {
            VNode *f = vfs_create(sh->cwd, c->redir_out);
            if (!f) sb_printf(out, COL(c) "cannot write to %s" COL_RESET "\n", c->redir_out);
            else if (c->append) vfs_append(f, res.s ? res.s : "", res.n);
            else vfs_write(f, res.s ? res.s : "", res.n);
            sb_free(&res);
        }
        sb_free(&cur);
        cur = res;
        have_in = 1;
        sb_free(&inb);
        free(c);
        if (sh->req_exit) break;
    }
    if (cur.n) sb_write(out, cur.s, cur.n);
    sb_free(&cur);
}

static void run_capture(Sh *sh, const char *line, SBuf *out) {
    int t = sh->tty;
    sh_run_line(sh, line, out);
    strip_colors(out);
    sh->tty = t;
}

static void run_segment(Sh *sh, const char *seg, SBuf *out, int tty);
void sh_run_line(Sh *sh, const char *line, SBuf *out) {
    while (*line == ' ') line++;
    if (!*line || *line == '#') return;
    int tty = sh->depth == 0 || sh->tty;
    /* for VAR in LIST; do BODY; done */
    if (!strncmp(line, "for ", 4)) {
        const char *in = strstr(line, " in "), *d = strstr(line, "; do "), *e = strstr(line, "; done");
        if (in && d && e && in < d && d < e) {
            char var[32], list[300], body[400];
            snprintf(var, sizeof var, "%.*s", (int)(in - line - 4), line + 4);
            snprintf(list, sizeof list, "%.*s", (int)(d - in - 4), in + 4);
            snprintf(body, sizeof body, "%.*s", (int)(e - d - 5), d + 5);
            SBuf ex = { 0 };
            expand(sh, list, &ex);
            Cmd *c = malloc(sizeof(Cmd));
            if (!c) { sb_free(&ex); return; }
            tokenize(sh, sb_str(&ex), c);
            for (int i = 0; i < c->argc && !sh->req_exit; i++) { sh_set(sh, var, c->argv[i]); sh_run_line(sh, body, out); }
            free(c);
            sb_free(&ex);
            return;
        }
        sb_puts(out, COL(c) "syntax: for x in a b c; do echo $x; done" COL_RESET "\n");
        sh->status = 2;
        return;
    }
    /* repeat N command */
    if (!strncmp(line, "repeat ", 7)) {
        int n = atoi(line + 7);
        const char *rest = strchr(line + 7, ' ');
        for (int i = 0; rest && i < n && i < 200; i++) sh_run_line(sh, rest + 1, out);
        return;
    }
    /* split into ; && || segments (outside quotes and $( )) */
    char seg[600];
    int n = 0, q = 0, par = 0, mode = 0;   /* mode for the *next* segment: 0 always, 1 &&, 2 || */
    int run = 1;
    for (const char *p = line;; p++) {
        if (*p == '"' || *p == '\'') q = q == *p ? 0 : q ? q : *p;
        if (!q && *p == '(') par++;
        if (!q && *p == ')' && par) par--;
        int end = !*p, sep = 0;
        if (!q && !par && *p == ';') sep = 1;
        if (!q && !par && *p == '&' && p[1] == '&') sep = 2;
        if (!q && !par && *p == '|' && p[1] == '|') sep = 3;
        if (end || sep) {
            seg[n] = 0;
            if (run && n) run_segment(sh, seg, out, tty);
            n = 0;
            if (end || sh->req_exit) break;
            if (sep == 2) run = sh->status == 0;
            else if (sep == 3) run = sh->status != 0;
            else run = 1;
            if (sep >= 2) p++;
            (void)mode;
            continue;
        }
        if (n < 599) seg[n++] = *p;
    }
}
static void run_segment(Sh *sh, const char *seg, SBuf *out, int tty) {
    /* alias expansion on the first word */
    char buf[700];
    const char *s = seg;
    while (*s == ' ') s++;
    for (int i = 0; i < sh->nalias; i++) {
        int L = strlen(sh->alias[i][0]);
        if (!strncmp(s, sh->alias[i][0], L) && (s[L] == ' ' || !s[L])) { snprintf(buf, sizeof buf, "%s%s", sh->alias[i][1], s + L); s = buf; break; }
    }
    SBuf ex = { 0 };
    expand(sh, s, &ex);
    run_pipeline(sh, sb_str(&ex), out, tty);
    sb_free(&ex);
}

void sh_init(Sh *sh, int style, Window *win) {
    memset(sh, 0, sizeof *sh);
    sh->style = style;
    sh->win = win;
    sh->cwd = style == 1 ? vfs_root : vfs_root;
    sh->fg = 7; sh->bg = 0;
    snprintf(sh->alias[0][0], 72, "ll"); snprintf(sh->alias[0][1], 72, "ls -l");
    snprintf(sh->alias[1][0], 72, "la"); snprintf(sh->alias[1][1], 72, "ls -a");
    snprintf(sh->alias[2][0], 72, ".."); snprintf(sh->alias[2][1], 72, "cd ..");
    sh->nalias = 3;
}

/* ---------------- tab completion ---------------- */
void sh_complete(Sh *sh, char *line, int *cur, int max, SBuf *out) {
    int start = *cur;
    while (start > 0 && line[start - 1] != ' ') start--;
    char word[128];
    int wl = *cur - start;
    if (wl > 127) return;
    memcpy(word, line + start, wl); word[wl] = 0;
    int first = 1;
    for (int i = 0; i < start; i++) if (line[i] != ' ') first = 0;
    char cands[64][48];
    int nc = 0;
    char prefix[128] = "";
    const char *leaf = word;
    VNode *dir = sh->cwd;
    char *sl = strrchr(word, '/');
    if (sl) {
        int pl = (int)(sl - word) + 1;
        memcpy(prefix, word, pl); prefix[pl] = 0;
        char dp[128];
        memcpy(dp, word, pl); dp[pl] = 0;
        dir = vfs_lookup(sh->cwd, dp);
        leaf = sl + 1;
    }
    int ll = strlen(leaf);
    if (first && !sl) {
        for (int i = 0; i < NBUILTIN && nc < 64; i++) if (!strncasecmp(builtins[i].name, leaf, ll)) snprintf(cands[nc++], 48, "%s ", builtins[i].name);
        for (int a = 0; a < APP_COUNT && nc < 64; a++) if (a != APP_MSGBOX && !strncasecmp(apps[a].cmd, leaf, ll)) snprintf(cands[nc++], 48, "%s ", apps[a].cmd);
    }
    for (VNode *n = dir && dir->dir ? dir->child : NULL; n && nc < 64; n = n->next)
        if (!strncasecmp(n->name, leaf, ll)) snprintf(cands[nc++], 48, "%s%s", n->name, n->dir ? "/" : " ");
    if (!nc) return;
    /* common prefix */
    int cp = strlen(cands[0]);
    for (int i = 1; i < nc; i++) { int k = 0; while (k < cp && tolower((unsigned char)cands[i][k]) == tolower((unsigned char)cands[0][k])) k++; cp = k; }
    char ins[160];
    snprintf(ins, sizeof ins, "%s%.*s", prefix, cp, cands[0]);
    int quote = 0;
    for (int i = 0; i < cp; i++) if (cands[0][i] == ' ' && (i < cp - 1 || nc > 1)) quote = 1;
    if (quote && nc == 1) { char q[170]; snprintf(q, sizeof q, "\"%s%.*s\" ", prefix, cp - 1, cands[0]); snprintf(ins, sizeof ins, "%s", q); }
    int L = strlen(line), il = strlen(ins);
    if (L - wl + il >= max) return;
    memmove(line + start + il, line + *cur, L - *cur + 1);
    memcpy(line + start, ins, il);
    *cur = start + il;
    if (nc > 1) {
        for (int i = 0; i < nc; i++) sb_printf(out, "%-22s%s", cands[i], (i % 3 == 2 || i == nc - 1) ? "\n" : "");
    }
}
