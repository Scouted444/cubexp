#include "os.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <math.h>

VNode *vfs_root, *vfs_desktop, *vfs_docs, *vfs_pics, *vfs_music, *vfs_recycle, *vfs_programs, *vfs_system;
int vfs_changes;
static int dirty;
static uint64_t dirty_t;

static void touch_changed(void) { vfs_changes++; dirty = 1; dirty_t = now_ms; }

static VNode *node_new(const char *name, int dir) {
    VNode *n = calloc(1, sizeof(VNode));
    if (!n) return NULL;
    strncpy(n->name, name, sizeof n->name - 1);
    n->dir = dir; n->app = -1;
    n->mtime = (uint32_t)time(NULL);
    return n;
}

/* children kept sorted: folders first, then case-insensitive name */
static int before(VNode *a, VNode *b) {
    if (a->dir != b->dir) return a->dir > b->dir;
    return strcasecmp(a->name, b->name) < 0;
}
static void link_child(VNode *dir, VNode *n) {
    n->parent = dir;
    VNode **pp = &dir->child;
    while (*pp && !before(n, *pp)) pp = &(*pp)->next;
    n->next = *pp; *pp = n;
}
static void unlink_node(VNode *n) {
    if (!n->parent) return;
    VNode **pp = &n->parent->child;
    while (*pp && *pp != n) pp = &(*pp)->next;
    if (*pp) *pp = n->next;
    n->parent = NULL; n->next = NULL;
}
static void free_tree(VNode *n) {
    VNode *c = n->child;
    while (c) { VNode *nx = c->next; free_tree(c); c = nx; }
    free(n->data);
    free(n);
}

VNode *vfs_child(VNode *dir, const char *name) {
    if (!dir || !dir->dir) return NULL;
    for (VNode *c = dir->child; c; c = c->next) if (!strcasecmp(c->name, name)) return c;
    return NULL;
}

/* normalise: "C:\a\b" -> "/a/b", "~" -> "/" */
static void norm(const char *in, char *out, int len) {
    int j = 0;
    if ((in[0] == 'c' || in[0] == 'C') && in[1] == ':') in += 2;
    if (in[0] == '~') { out[j++] = '/'; in++; }
    for (; *in && j < len - 1; in++) out[j++] = *in == '\\' ? '/' : *in;
    out[j] = 0;
}

VNode *vfs_lookup(VNode *cwd, const char *path) {
    char p[256];
    norm(path, p, sizeof p);
    VNode *n = (p[0] == '/' || !cwd) ? vfs_root : cwd;
    char *save = NULL, *tok = strtok_r(p, "/", &save);
    while (tok) {
        if (!strcmp(tok, ".") || !*tok) {}
        else if (!strcmp(tok, "..")) { if (n->parent) n = n->parent; }
        else { n = vfs_child(n, tok); if (!n) return NULL; }
        tok = strtok_r(NULL, "/", &save);
    }
    return n;
}

VNode *vfs_parent_for(VNode *cwd, const char *path, char *leaf) {
    char p[256];
    norm(path, p, sizeof p);
    int L = strlen(p);
    while (L > 1 && p[L - 1] == '/') p[--L] = 0;
    char *slash = strrchr(p, '/');
    if (!slash) { strncpy(leaf, p, 39); leaf[39] = 0; return cwd ? cwd : vfs_root; }
    strncpy(leaf, slash + 1, 39); leaf[39] = 0;
    if (slash == p) return vfs_root;
    *slash = 0;
    VNode *d = vfs_lookup(cwd, p);
    return d && d->dir ? d : NULL;
}

int vfs_valid_name(const char *s) {
    if (!*s || strlen(s) > 38 || !strcmp(s, ".") || !strcmp(s, "..")) return 0;
    for (; *s; s++) if (strchr("/\\:*?\"<>|", *s) || (unsigned char)*s < 32) return 0;
    return 1;
}

VNode *vfs_mkdir(VNode *cwd, const char *path, int parents) {
    char leaf[40];
    if (parents) {
        char p[256], acc[256] = "";
        norm(path, p, sizeof p);
        VNode *d = p[0] == '/' ? vfs_root : cwd;
        char *save = NULL, *tok = strtok_r(p, "/", &save);
        (void)acc;
        while (tok) {
            VNode *c = !strcmp(tok, "..") ? (d->parent ? d->parent : d) : !strcmp(tok, ".") ? d : vfs_child(d, tok);
            if (!c) {
                if (!vfs_valid_name(tok)) return NULL;
                c = node_new(tok, 1);
                if (!c) return NULL;
                link_child(d, c);
                touch_changed();
            } else if (!c->dir) return NULL;
            d = c;
            tok = strtok_r(NULL, "/", &save);
        }
        return d;
    }
    VNode *dir = vfs_parent_for(cwd, path, leaf);
    if (!dir || !vfs_valid_name(leaf) || vfs_child(dir, leaf)) return NULL;
    VNode *n = node_new(leaf, 1);
    if (!n) return NULL;
    link_child(dir, n);
    touch_changed();
    return n;
}

VNode *vfs_create(VNode *cwd, const char *path) {
    VNode *e = vfs_lookup(cwd, path);
    if (e) return e->dir ? NULL : e;
    char leaf[40];
    VNode *dir = vfs_parent_for(cwd, path, leaf);
    if (!dir || !vfs_valid_name(leaf)) return NULL;
    VNode *n = node_new(leaf, 0);
    if (!n) return NULL;
    link_child(dir, n);
    touch_changed();
    return n;
}

static int reserve(VNode *f, int n) {
    if (n > VFS_MAX_FILE) return 0;
    if (n <= f->cap) return 1;
    int cap = n < 256 ? 256 : n + n / 2;
    if (cap > VFS_MAX_FILE) cap = VFS_MAX_FILE;
    uint8_t *d = realloc(f->data, cap);
    if (!d) return 0;
    f->data = d; f->cap = cap;
    return 1;
}
int vfs_write(VNode *f, const void *d, int n) {
    if (!f || f->dir || !reserve(f, n)) return 0;
    if (n) memcpy(f->data, d, n);
    f->size = n; f->mtime = (uint32_t)time(NULL);
    touch_changed();
    return 1;
}
int vfs_append(VNode *f, const void *d, int n) {
    if (!f || f->dir || !reserve(f, f->size + n)) return 0;
    memcpy(f->data + f->size, d, n);
    f->size += n; f->mtime = (uint32_t)time(NULL);
    touch_changed();
    return 1;
}

void vfs_path(VNode *n, char *buf, int len) {
    if (!n || n == vfs_root) { snprintf(buf, len, "/"); return; }
    char tmp[256] = "";
    while (n && n != vfs_root) {
        char t[256];
        snprintf(t, sizeof t, "/%s%s", n->name, tmp);
        strcpy(tmp, t);
        n = n->parent;
    }
    snprintf(buf, len, "%s", tmp);
}

int vfs_is_inside(VNode *n, VNode *dir) {
    for (; n; n = n->parent) if (n == dir) return 1;
    return 0;
}
int vfs_alive(VNode *n) {
    /* walk the tree; cheap enough for UI use */
    if (!n) return 0;
    VNode *stack[64];
    int sp = 0;
    stack[sp++] = vfs_root;
    while (sp) {
        VNode *d = stack[--sp];
        if (d == n) return 1;
        for (VNode *c = d->child; c; c = c->next) {
            if (c == n) return 1;
            if (c->dir && sp < 64) stack[sp++] = c;
        }
    }
    return 0;
}

void vfs_unique_name(VNode *dir, const char *base, const char *ext, char *out, int len) {
    snprintf(out, len, "%s%s", base, ext);
    for (int i = 2; vfs_child(dir, out) && i < 1000; i++) snprintf(out, len, "%s (%d)%s", base, i, ext);
}

int vfs_remove(VNode *n, int to_recycle) {
    if (!n || n == vfs_root || n == vfs_recycle || n == vfs_programs || !n->parent) return 0;
    if (n == vfs_desktop || n == vfs_docs || n == vfs_pics || n == vfs_music || n == vfs_system) return 0;
    if (n->parent == vfs_programs) return 0;
    if (to_recycle && !vfs_is_inside(n, vfs_recycle)) {
        char p[96], nm[40];
        vfs_path(n, p, sizeof p);
        unlink_node(n);
        vfs_unique_name(vfs_recycle, n->name, "", nm, sizeof nm);
        strcpy(n->orig, p);
        strcpy(n->name, nm);
        link_child(vfs_recycle, n);
    } else {
        unlink_node(n);
        free_tree(n);
    }
    touch_changed();
    return 1;
}
int vfs_restore(VNode *n) {
    if (!n || n->parent != vfs_recycle) return 0;
    char leaf[40] = "";
    VNode *dir = n->orig[0] ? vfs_parent_for(NULL, n->orig, leaf) : NULL;
    if (!dir && n->orig[0]) {
        char pp[96];
        strcpy(pp, n->orig);
        char *sl = strrchr(pp, '/');
        if (sl && sl != pp) { *sl = 0; dir = vfs_mkdir(vfs_root, pp, 1); }
        if (dir) vfs_parent_for(NULL, n->orig, leaf);
    }
    if (!dir) dir = vfs_docs;
    char nm[40];
    char base[40];
    strcpy(base, leaf[0] ? leaf : n->name);
    const char *dot = n->dir ? NULL : strrchr(base, '.');
    char ext[16] = "";
    if (dot && strlen(dot) < sizeof ext) { strcpy(ext, dot); base[dot - base] = 0; }
    vfs_unique_name(dir, base, ext, nm, sizeof nm);
    unlink_node(n);
    strcpy(n->name, nm);
    n->orig[0] = 0;
    link_child(dir, n);
    touch_changed();
    return 1;
}

int vfs_move(VNode *n, VNode *dst, const char *newname) {
    if (!n || !dst || !dst->dir || n == vfs_root || vfs_is_inside(dst, n)) return 0;
    if (n->parent == vfs_programs || n == vfs_recycle || n == vfs_programs || n == vfs_desktop || n == vfs_docs || n == vfs_pics || n == vfs_music || n == vfs_system) return 0;
    const char *nm = newname && *newname ? newname : n->name;
    if (!vfs_valid_name(nm)) return 0;
    VNode *ex = vfs_child(dst, nm);
    if (ex && ex != n) return 0;
    unlink_node(n);
    strncpy(n->name, nm, sizeof n->name - 1);
    n->name[sizeof n->name - 1] = 0;
    link_child(dst, n);
    touch_changed();
    return 1;
}

static VNode *clone(VNode *n) {
    VNode *c = node_new(n->name, n->dir);
    if (!c) return NULL;
    c->app = n->app; c->mtime = n->mtime;
    if (!n->dir && n->size) {
        c->data = malloc(n->size);
        if (!c->data) { free(c); return NULL; }
        memcpy(c->data, n->data, n->size);
        c->size = c->cap = n->size;
    }
    for (VNode *k = n->child; k; k = k->next) {
        VNode *kc = clone(k);
        if (kc) link_child(c, kc);
    }
    return c;
}
VNode *vfs_copy(VNode *n, VNode *dst, const char *newname) {
    if (!n || !dst || !dst->dir || (n->dir && vfs_is_inside(dst, n))) return NULL;
    char nm[40];
    if (newname && *newname) { if (!vfs_valid_name(newname) || vfs_child(dst, newname)) return NULL; strcpy(nm, newname); }
    else if (vfs_child(dst, n->name)) {
        char base[40], ext[16] = "";
        strcpy(base, n->name);
        char *dot = n->dir ? NULL : strrchr(base, '.');
        if (dot && strlen(dot) < sizeof ext) { strcpy(ext, dot); *dot = 0; }
        char b2[40];
        snprintf(b2, sizeof b2, "%.30s - Copy", base);
        vfs_unique_name(dst, b2, ext, nm, sizeof nm);
    } else strcpy(nm, n->name);
    VNode *c = clone(n);
    if (!c) return NULL;
    strcpy(c->name, nm);
    link_child(dst, c);
    touch_changed();
    return c;
}

int vfs_count(VNode *dir) { int k = 0; for (VNode *c = dir ? dir->child : NULL; c; c = c->next) k++; return k; }
long vfs_total_size(VNode *n) {
    if (!n) return 0;
    long s = n->size;
    for (VNode *c = n->child; c; c = c->next) s += vfs_total_size(c);
    return s;
}
const char *vfs_ext(const VNode *n) {
    const char *d = strrchr(n->name, '.');
    return d ? d : "";
}
int vfs_icon(const VNode *n) {
    if (n->app >= 0) return apps[n->app].icon;
    if (n == vfs_docs) return IC_DOCS;
    if (n == vfs_pics) return IC_PICS;
    if (n == vfs_music) return IC_MUSICF;
    if (n == vfs_desktop) return IC_DESKTOP;
    if (n == vfs_recycle) return vfs_recycle->child ? IC_RECYCLE_FULL : IC_RECYCLE;
    if (n->dir) return IC_FOLDER;
    const char *e = vfs_ext(n);
    if (!strcasecmp(e, ".txt") || !strcasecmp(e, ".ini") || !strcasecmp(e, ".log") || !strcasecmp(e, ".md")) return IC_TEXTFILE;
    if (!strcasecmp(e, ".bmp")) return IC_IMAGEFILE;
    if (!strcasecmp(e, ".song")) return IC_SONGFILE;
    if (!strcasecmp(e, ".sh") || !strcasecmp(e, ".bat")) return IC_SCRIPT;
    return IC_FILE;
}

/* ---------- persistence ---------- */
static void wr8(FILE *f, int v) { fputc(v & 255, f); }
static void wr32(FILE *f, uint32_t v) { fputc(v >> 24, f); fputc((v >> 16) & 255, f); fputc((v >> 8) & 255, f); fputc(v & 255, f); }
static void save_node(FILE *f, VNode *n) {
    for (VNode *c = n->child; c; c = c->next) {
        if (c == vfs_programs) continue;
        wr8(f, c->dir ? 2 : 1);
        int L = strlen(c->name);
        wr8(f, L); fwrite(c->name, 1, L, f);
        wr32(f, c->mtime);
        int O = strlen(c->orig);
        wr8(f, O); fwrite(c->orig, 1, O, f);
        if (c->dir) save_node(f, c);
        else if (c->app >= 0) { char t[48]; int L2 = snprintf(t, sizeof t, "#!app %s", apps[c->app].cmd); wr32(f, L2); fwrite(t, 1, L2, f); }
        else { wr32(f, c->size); if (c->size) fwrite(c->data, 1, c->size, f); }
    }
    wr8(f, 0);
}
void vfs_save_now(void) {
    dirty = 0;
    if (!plat_storage_ok()) return;
    char p[96], tmp[96];
    snprintf(p, sizeof p, "%s/disk.cfs", plat_storage_path());
    snprintf(tmp, sizeof tmp, "%s/disk.tmp", plat_storage_path());
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    fwrite("CXFS1", 1, 5, f);
    save_node(f, vfs_root);
    int ok = !ferror(f);
    fclose(f);
    if (ok) { remove(p); rename(tmp, p); }
}
void vfs_tick(void) {
    if (dirty && now_ms - dirty_t > 1500) vfs_save_now();
}

static int rd8(FILE *f) { int c = fgetc(f); return c == EOF ? -1 : c; }
static uint32_t rd32(FILE *f) { uint32_t v = 0; for (int i = 0; i < 4; i++) v = (v << 8) | (uint32_t)(fgetc(f) & 255); return v; }
static int load_node(FILE *f, VNode *dir, int depth) {
    for (;;) {
        int t = rd8(f);
        if (t <= 0) return t == 0;
        if (depth > 32) return 0;
        int L = rd8(f);
        if (L < 0 || L > 39) return 0;
        char name[40] = "";
        if (fread(name, 1, L, f) != (size_t)L) return 0;
        uint32_t mt = rd32(f);
        int O = rd8(f);
        if (O < 0 || O > 95) return 0;
        char orig[96] = "";
        if (fread(orig, 1, O, f) != (size_t)O) return 0;
        VNode *n = vfs_child(dir, name);
        if (!n) { n = node_new(name, t == 2); if (!n) return 0; link_child(dir, n); }
        n->mtime = mt; strcpy(n->orig, orig);
        if (t == 2) { if (!load_node(f, n, depth + 1)) return 0; }
        else {
            uint32_t sz = rd32(f);
            if (sz > VFS_MAX_FILE) return 0;
            free(n->data); n->data = NULL; n->size = n->cap = 0;
            if (sz) {
                n->data = malloc(sz);
                if (!n->data || fread(n->data, 1, sz, f) != sz) return 0;
                n->size = n->cap = sz;
            }
            if (sz > 6 && sz < 40 && !memcmp(n->data, "#!app ", 6)) {
                char cmd[40];
                memcpy(cmd, n->data + 6, sz - 6); cmd[sz - 6] = 0;
                n->app = app_by_cmd(cmd);
                free(n->data); n->data = NULL; n->size = n->cap = 0;
            }
        }
    }
}

static void add_text(VNode *dir, const char *name, const char *text) {
    VNode *f = vfs_create(dir, name);
    if (f) vfs_write(f, text, strlen(text));
}

static void sample_picture(VNode *dir) {
    int w = 200, h = 140;
    uint32_t *px = malloc(sizeof(uint32_t) * w * h);
    if (!px) return;
    GfxState st;
    gfx_push(&st);
    gfx_target(px, 0, 0, w, h);
    grad_v(0, 0, w, h, 0x2A64D8, 0xA8D4F8);
    for (int x = 0; x < w; x++) {
        int y0 = 100 + (int)(12 * sinf(x * 0.03f));
        fill(x, y0, 1, h - y0, mix(0x6CC040, 0x2A7A1A, (x * 3) & 255));
    }
    fill_circle(160, 34, 14, 0xFFF4B0);
    draw_icon(IC_LOGO, 64, 30, 72);
    gfx_pop(&st);
    int sz;
    uint8_t *bmp = bmp_encode(px, w, h, &sz);
    free(px);
    if (!bmp) return;
    VNode *f = vfs_create(dir, "Cube Hills.bmp");
    if (f) vfs_write(f, bmp, sz);
    free(bmp);
}

static void add_shortcut(VNode *dir, int app) {
    char nm[40];
    snprintf(nm, sizeof nm, "%s.exe", apps[app].cmd);
    if (vfs_child(dir, nm)) return;
    VNode *n = node_new(nm, 0);
    n->app = app;
    link_child(dir, n);
}
static void make_programs(void) {
    vfs_programs = vfs_child(vfs_root, "Programs");
    if (!vfs_programs) { vfs_programs = node_new("Programs", 1); link_child(vfs_root, vfs_programs); }
    for (int a = 0; a < APP_COUNT; a++) {
        if (a == APP_MSGBOX || a == APP_RUN) continue;
        char nm[40];
        snprintf(nm, sizeof nm, "%s.exe", apps[a].cmd);
        if (vfs_child(vfs_programs, nm)) continue;
        VNode *n = node_new(nm, 0);
        n->app = a;
        link_child(vfs_programs, n);
    }
}

static VNode *ensure_dir(const char *name) {
    VNode *d = vfs_child(vfs_root, name);
    if (!d) { d = node_new(name, 1); link_child(vfs_root, d); }
    return d;
}

void vfs_init(void) {
    vfs_root = node_new("", 1);
    int loaded = 0;
    if (plat_storage_ok()) {
        char p[96];
        snprintf(p, sizeof p, "%s/disk.cfs", plat_storage_path());
        FILE *f = fopen(p, "rb");
        if (f) {
            char magic[5];
            if (fread(magic, 1, 5, f) == 5 && !memcmp(magic, "CXFS1", 5)) loaded = load_node(f, vfs_root, 0);
            fclose(f);
            if (!loaded) { free_tree(vfs_root); vfs_root = node_new("", 1); }
        }
    }
    vfs_desktop = ensure_dir("Desktop");
    vfs_docs = ensure_dir("Documents");
    vfs_pics = ensure_dir("Pictures");
    vfs_music = ensure_dir("Music");
    vfs_system = ensure_dir("System");
    vfs_recycle = ensure_dir("Recycle Bin");
    make_programs();
    if (!loaded) {
        add_text(vfs_desktop, "Welcome.txt",
                 "Welcome to CubeXP!\n\n"
                 "This file lives on your desktop. Everything you save goes to\n"
                 "Local Disk (C:), which is stored on your SD card.\n\n"
                 "Try these:\n"
                 " - Open the Terminal and type: ls, cd Documents, cat readme.txt\n"
                 " - Paint a picture and use Image > Set as Background\n"
                 " - Compose a tune in Music Maker\n"
                 " - Play Solitaire (and win, for the bouncing cards)\n");
        add_text(vfs_docs, "readme.txt",
                 "CubeXP Local Disk\n=================\n\n"
                 "Folders:\n"
                 "  /Desktop    files shown on the desktop\n"
                 "  /Documents  your text files\n"
                 "  /Pictures   paintings (.bmp)\n"
                 "  /Music      songs from Music Maker (.song)\n"
                 "  /Programs   every program, as .exe shortcuts\n"
                 "  /System     settings and startup script\n");
        add_text(vfs_docs, "todo.txt", "- learn the terminal\n- beat expert minesweeper\n- paint a new wallpaper\n- win solitaire\n");
        add_text(vfs_docs, "hello.sh",
                 "# A tiny CubeXP shell script. Run it with:  sh hello.sh\n"
                 "echo Hello, $USER!\n"
                 "echo Today is $(date)\n"
                 "for f in *.txt; do echo Found text file: $f; done\n"
                 "cowsay Scripts work!\n");
        add_text(vfs_music, "Ode to Joy.song",
                 "tempo 150\ninst bell\n"
                 "E4 E4 F4 G4 G4 F4 E4 D4 C4 C4 D4 E4 E4 - D4 D4 - -\n"
                 "E4 E4 F4 G4 G4 F4 E4 D4 C4 C4 D4 E4 D4 - C4 C4 - -\n");
        add_text(vfs_music, "Twinkle.song",
                 "tempo 160\ninst chip\n"
                 "C4 C4 G4 G4 A4 A4 G4 - F4 F4 E4 E4 D4 D4 C4 -\n"
                 "G4 G4 F4 F4 E4 E4 D4 - G4 G4 F4 F4 E4 E4 D4 -\n"
                 "C4 C4 G4 G4 A4 A4 G4 - F4 F4 E4 E4 D4 D4 C4 -\n");
        add_text(vfs_system, "motd.txt", "Welcome to the CubeXP terminal. Type 'help' to see what you can do.\n");
        add_text(vfs_system, "autorun.sh", "# Commands here run every time a terminal opens.\n");
        sample_picture(vfs_pics);
        static const int sc[] = { APP_TERMINAL, APP_LINUX, APP_NOTEPAD, APP_PAINT, APP_SOLITAIRE, APP_BLOCKS, APP_MUSIC, APP_MINES, APP_CONTROL };
        for (unsigned i = 0; i < sizeof sc / sizeof sc[0]; i++) add_shortcut(vfs_desktop, sc[i]);
    }
    vfs_changes = 1;
    dirty = 0;
}

/* ---------- BMP ---------- */
static void p32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void p16(uint8_t *p, int v) { p[0] = v; p[1] = v >> 8; }
uint8_t *bmp_encode(const uint32_t *px, int w, int h, int *size) {
    int row = (w * 3 + 3) & ~3, sz = 54 + row * h;
    uint8_t *d = calloc(1, sz);
    if (!d) return NULL;
    d[0] = 'B'; d[1] = 'M'; p32(d + 2, sz); p32(d + 10, 54); p32(d + 14, 40); p32(d + 18, w); p32(d + 22, h);
    p16(d + 26, 1); p16(d + 28, 24); p32(d + 34, row * h); p32(d + 38, 2835); p32(d + 42, 2835);
    for (int y = 0; y < h; y++) {
        uint8_t *r = d + 54 + (h - 1 - y) * row;
        for (int x = 0; x < w; x++) { uint32_t c = px[y * w + x]; r[x * 3] = c & 255; r[x * 3 + 1] = (c >> 8) & 255; r[x * 3 + 2] = (c >> 16) & 255; }
    }
    *size = sz;
    return d;
}
static uint32_t g32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint32_t *bmp_decode(const uint8_t *d, int size, int *w, int *h) {
    if (!d || size < 54 || d[0] != 'B' || d[1] != 'M') return NULL;
    int bpp = d[28] | d[29] << 8;
    int W = (int)g32(d + 18), H = (int)g32(d + 22), off = (int)g32(d + 10);
    int flip = 1;
    if (H < 0) { H = -H; flip = 0; }
    if ((bpp != 24 && bpp != 32) || W <= 0 || H <= 0 || W > 2048 || H > 2048) return NULL;
    int bpx = bpp / 8, row = (W * bpx + 3) & ~3;
    if (off + row * H > size) return NULL;
    uint32_t *px = malloc(sizeof(uint32_t) * W * H);
    if (!px) return NULL;
    for (int y = 0; y < H; y++) {
        const uint8_t *r = d + off + (flip ? H - 1 - y : y) * row;
        for (int x = 0; x < W; x++) px[y * W + x] = RGB(r[x * bpx + 2], r[x * bpx + 1], r[x * bpx]);
    }
    *w = W; *h = H;
    return px;
}
