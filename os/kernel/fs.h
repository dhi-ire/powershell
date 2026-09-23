#ifndef FS_H
#define FS_H

#include "types.h"

#define FS_NAME_MAX 32

struct fs_node {
    char name[FS_NAME_MAX];
    bool is_dir;
    struct fs_node *parent, *child, *next;
    char *data;
    uint32_t size, cap;
};

void fs_init(void);
struct fs_node *fs_root(void);
struct fs_node *fs_resolve(struct fs_node *cwd, const char *path);
struct fs_node *fs_create(struct fs_node *cwd, const char *path, bool is_dir);   /* NULL on error */
int  fs_remove(struct fs_node *n, bool recursive);                                 /* 0 ok, -1 error */
int  fs_write(struct fs_node *n, const char *data, uint32_t len);
int  fs_append(struct fs_node *n, const char *data, uint32_t len);
void fs_path(struct fs_node *n, char *buf, size_t size);
int  fs_count(struct fs_node *dir);
int  fs_move(struct fs_node *n, struct fs_node *new_parent, const char *new_name);

#endif
