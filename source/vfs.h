#ifndef VFS_H
#define VFS_H
#include <stdint.h>

/* In-memory file system ("Local Disk C:"), saved to the SD card as one image file. */
typedef struct VNode {
    char name[40];
    int dir;
    int app;                 /* >= 0: program shortcut (launches an app), not saved */
    uint8_t *data;
    int size, cap;
    uint32_t mtime;
    char orig[96];           /* original path for items in the Recycle Bin */
    struct VNode *parent, *child, *next;
} VNode;

#define VFS_MAX_FILE (600 * 1024)

extern VNode *vfs_root, *vfs_desktop, *vfs_docs, *vfs_pics, *vfs_music, *vfs_recycle, *vfs_programs, *vfs_system;
extern int vfs_changes;   /* bumps on every modification (UIs use it to refresh) */

void   vfs_init(void);
void   vfs_tick(void);                       /* autosave */
void   vfs_save_now(void);
VNode *vfs_lookup(VNode *cwd, const char *path);
VNode *vfs_parent_for(VNode *cwd, const char *path, char *leaf);   /* dir that would hold path */
VNode *vfs_child(VNode *dir, const char *name);
VNode *vfs_mkdir(VNode *cwd, const char *path, int parents);
VNode *vfs_create(VNode *cwd, const char *path);                   /* existing file or new empty file */
int    vfs_write(VNode *f, const void *d, int n);
int    vfs_append(VNode *f, const void *d, int n);
int    vfs_remove(VNode *n, int to_recycle);
int    vfs_restore(VNode *n);
int    vfs_move(VNode *n, VNode *dst_dir, const char *newname);
VNode *vfs_copy(VNode *n, VNode *dst_dir, const char *newname);
void   vfs_path(VNode *n, char *buf, int len);
int    vfs_is_inside(VNode *n, VNode *dir);
int    vfs_count(VNode *dir);
long   vfs_total_size(VNode *n);
const char *vfs_ext(const VNode *n);
int    vfs_icon(const VNode *n);
int    vfs_valid_name(const char *s);
void   vfs_unique_name(VNode *dir, const char *base, const char *ext, char *out, int len);
int    vfs_alive(VNode *n);                 /* still in the tree? */
void   vfs_open(VNode *n);                  /* open with the default program */

/* BMP helpers (24-bit) */
uint8_t  *bmp_encode(const uint32_t *px, int w, int h, int *size);
uint32_t *bmp_decode(const uint8_t *d, int size, int *w, int *h);

#endif
