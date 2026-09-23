/* In-memory hierarchical filesystem (ramfs). */
#include "fs.h"
#include "heap.h"
#include "lib.h"

static struct fs_node *root;

static struct fs_node *node_new(struct fs_node *parent, const char *name, bool is_dir)
{
    struct fs_node *n = kcalloc(sizeof(*n));
    if (!n) return NULL;
    strlcpy(n->name, name, FS_NAME_MAX);
    n->is_dir = is_dir;
    n->parent = parent ? parent : n;
    if (parent) {
        /* keep directory entries sorted: dirs first, then by name */
        struct fs_node **pp = &parent->child;
        while (*pp && ((*pp)->is_dir > is_dir ||
                       ((*pp)->is_dir == is_dir && strcmp((*pp)->name, name) < 0)))
            pp = &(*pp)->next;
        n->next = *pp;
        *pp = n;
    }
    return n;
}

static struct fs_node *find_child(struct fs_node *dir, const char *name)
{
    for (struct fs_node *c = dir->child; c; c = c->next)
        if (!strcmp(c->name, name)) return c;
    return NULL;
}

struct fs_node *fs_root(void) { return root; }

struct fs_node *fs_resolve(struct fs_node *cwd, const char *path)
{
    if (!path || !*path) return cwd;
    struct fs_node *n = (*path == '/') ? root : cwd;
    char part[FS_NAME_MAX];
    while (*path) {
        while (*path == '/') path++;
        if (!*path) break;
        int i = 0;
        while (*path && *path != '/') {
            if (i < FS_NAME_MAX - 1) part[i++] = *path;
            path++;
        }
        part[i] = 0;
        if (!strcmp(part, ".")) continue;
        if (!strcmp(part, "..")) { n = n->parent; continue; }
        if (!n->is_dir) return NULL;
        n = find_child(n, part);
        if (!n) return NULL;
    }
    return n;
}

struct fs_node *fs_create(struct fs_node *cwd, const char *path, bool is_dir)
{
    char buf[256];
    strlcpy(buf, path, sizeof(buf));
    size_t len = strlen(buf);
    while (len > 1 && buf[len - 1] == '/') buf[--len] = 0;

    char *slash = strrchr(buf, '/');
    struct fs_node *parent;
    const char *name;
    if (slash) {
        *slash = 0;
        parent = fs_resolve(cwd, slash == buf ? "/" : buf);
        name = slash + 1;
    } else {
        parent = cwd;
        name = buf;
    }
    if (!parent || !parent->is_dir || !*name || !strcmp(name, ".") || !strcmp(name, ".."))
        return NULL;
    if (find_child(parent, name)) return NULL;
    return node_new(parent, name, is_dir);
}

static void unlink(struct fs_node *n)
{
    struct fs_node **pp = &n->parent->child;
    while (*pp && *pp != n) pp = &(*pp)->next;
    if (*pp) *pp = n->next;
    n->next = NULL;
}

static void free_tree(struct fs_node *n)
{
    struct fs_node *c = n->child;
    while (c) {
        struct fs_node *next = c->next;
        free_tree(c);
        c = next;
    }
    kfree(n->data);
    kfree(n);
}

int fs_remove(struct fs_node *n, bool recursive)
{
    if (!n || n == root) return -1;
    if (n->is_dir && n->child && !recursive) return -1;
    unlink(n);
    free_tree(n);
    return 0;
}

int fs_move(struct fs_node *n, struct fs_node *new_parent, const char *new_name)
{
    if (!n || n == root || !new_parent || !new_parent->is_dir) return -1;
    for (struct fs_node *p = new_parent; ; p = p->parent) {   /* no moving into itself */
        if (p == n) return -1;
        if (p == root) break;
    }
    struct fs_node *existing = find_child(new_parent, new_name);
    if (existing && existing != n) return -1;
    unlink(n);
    strlcpy(n->name, new_name, FS_NAME_MAX);
    n->parent = new_parent;
    struct fs_node **pp = &new_parent->child;
    while (*pp && ((*pp)->is_dir > n->is_dir ||
                   ((*pp)->is_dir == n->is_dir && strcmp((*pp)->name, n->name) < 0)))
        pp = &(*pp)->next;
    n->next = *pp;
    *pp = n;
    return 0;
}

static int reserve(struct fs_node *n, uint32_t need)
{
    if (need + 1 <= n->cap) return 0;
    uint32_t cap = n->cap ? n->cap : 64;
    while (cap < need + 1) cap *= 2;
    char *d = krealloc(n->data, cap);
    if (!d) return -1;
    n->data = d;
    n->cap = cap;
    return 0;
}

int fs_write(struct fs_node *n, const char *data, uint32_t len)
{
    if (!n || n->is_dir || reserve(n, len)) return -1;
    memcpy(n->data, data, len);
    n->size = len;
    n->data[len] = 0;
    return 0;
}

int fs_append(struct fs_node *n, const char *data, uint32_t len)
{
    if (!n || n->is_dir || reserve(n, n->size + len)) return -1;
    memcpy(n->data + n->size, data, len);
    n->size += len;
    n->data[n->size] = 0;
    return 0;
}

void fs_path(struct fs_node *n, char *buf, size_t size)
{
    if (n == root) { strlcpy(buf, "/", size); return; }
    char tmp[256] = "";
    for (; n != root; n = n->parent) {
        char seg[256];
        snprintf(seg, sizeof(seg), "/%s%s", n->name, tmp);
        strlcpy(tmp, seg, sizeof(tmp));
    }
    strlcpy(buf, tmp, size);
}

int fs_count(struct fs_node *dir)
{
    int c = 0;
    for (struct fs_node *n = dir->child; n; n = n->next) c++;
    return c;
}

static void mkfile(const char *path, const char *text)
{
    struct fs_node *n = fs_create(root, path, false);
    if (n) fs_write(n, text, strlen(text));
}

void fs_init(void)
{
    root = node_new(NULL, "", true);
    const char *dirs[] = { "/bin", "/etc", "/home", "/home/user", "/home/user/Documents",
                           "/home/user/Pictures", "/home/user/Desktop", "/tmp", "/var", "/var/log" };
    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) fs_create(root, dirs[i], true);

    const char *bins[] = { "ls", "cd", "cat", "echo", "mkdir", "touch", "rm", "mv", "cp", "pwd",
                           "clear", "uname", "date", "uptime", "free", "neofetch", "edit",
                           "open", "calc", "tree", "help", "reboot", "shutdown" };
    for (unsigned i = 0; i < sizeof(bins) / sizeof(bins[0]); i++) {
        char p[48];
        snprintf(p, sizeof(p), "/bin/%s", bins[i]);
        mkfile(p, "#!builtin\n");
    }

    mkfile("/etc/os-release",
           "NAME=\"NovaOS\"\nVERSION=\"0.1\"\nID=novaos\nPRETTY_NAME=\"NovaOS 0.1 (Aurora)\"\n"
           "HOME_URL=\"https://github.com/dhi-ire/powershell\"\n");
    mkfile("/etc/hostname", "novaos\n");
    mkfile("/etc/motd", "Welcome to NovaOS! Type 'help' to see available commands.\n");
    mkfile("/home/user/readme.txt",
           "Welcome to NovaOS!\n\n"
           "NovaOS is a small hobby operating system written from scratch\n"
           "in C and x86 assembly. It has its own kernel, drivers, window\n"
           "manager, filesystem and apps.\n\n"
           "Try these:\n"
           "  - Double-click the desktop icons\n"
           "  - Open the Terminal and type 'help'\n"
           "  - Drag windows by their title bar\n"
           "  - Press the Super/Windows key for the menu\n");
    mkfile("/home/user/Documents/todo.txt",
           "- Write a kernel      [done]\n- Draw a desktop      [done]\n- Take over the world [todo]\n");
    mkfile("/home/user/Documents/notes.txt", "Edit me with: edit notes.txt  (Ctrl+S saves)\n");
    mkfile("/var/log/boot.log", "");
}
