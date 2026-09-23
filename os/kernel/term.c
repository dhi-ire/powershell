/* Terminal emulator window + a small Unix-like shell. */
#include "wm.h"
#include "fs.h"
#include "lib.h"
#include "heap.h"
#include "font.h"
#include "cpu.h"
#include "rtc.h"
#include "sysinfo.h"

#define COLS 82
#define ROWS 27
#define PAD  6
#define LINE_MAX 240
#define HIST 32

#define C_FG     RGB(220, 223, 228)
#define C_BG     RGB(28, 30, 38)
#define C_GREEN  RGB(80, 250, 123)
#define C_BLUE   RGB(110, 160, 255)
#define C_CYAN   RGB(100, 220, 230)
#define C_YELLOW RGB(241, 250, 140)
#define C_RED    RGB(255, 100, 110)
#define C_MAGENTA RGB(230, 130, 240)
#define C_GRAY   RGB(130, 136, 150)

struct term {
    char ch[ROWS][COLS];
    uint32_t fg[ROWS][COLS];
    int cx, cy;
    int px, py;                    /* where the current input line starts */
    char line[LINE_MAX];
    int len, pos;
    char hist[HIST][LINE_MAX];
    int nhist, hidx;
    struct fs_node *cwd;
    bool blink;
    /* output redirection */
    char *rbuf;
    uint32_t rlen, rcap;
};

static struct term *T;             /* terminal currently executing a command */

/* ---------- screen ---------- */

static void scroll(struct term *t)
{
    memmove(t->ch[0], t->ch[1], sizeof(t->ch[0]) * (ROWS - 1));
    memmove(t->fg[0], t->fg[1], sizeof(t->fg[0]) * (ROWS - 1));
    memset(t->ch[ROWS - 1], ' ', COLS);
    for (int i = 0; i < COLS; i++) t->fg[ROWS - 1][i] = C_FG;
    t->cy = ROWS - 1;
    t->py--;
}

static void putc_screen(struct term *t, char c, uint32_t color)
{
    if (c == '\n') {
        t->cx = 0;
        if (++t->cy >= ROWS) scroll(t);
        return;
    }
    if (c == '\t') {
        do putc_screen(t, ' ', color); while (t->cx % 4);
        return;
    }
    if (t->cx >= COLS) {
        t->cx = 0;
        if (++t->cy >= ROWS) scroll(t);
    }
    t->ch[t->cy][t->cx] = c;
    t->fg[t->cy][t->cx] = color;
    t->cx++;
}

static void clear_screen(struct term *t)
{
    memset(t->ch, ' ', sizeof(t->ch));
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++) t->fg[y][x] = C_FG;
    t->cx = t->cy = 0;
}

/* Shell output: goes to the redirect buffer if one is active. */
static void out_c(const char *s, uint32_t color)
{
    if (T->rbuf) {
        uint32_t n = strlen(s);
        if (T->rlen + n + 1 > T->rcap) {
            uint32_t cap = T->rcap * 2 + n + 64;
            char *nb = krealloc(T->rbuf, cap);
            if (!nb) return;
            T->rbuf = nb;
            T->rcap = cap;
        }
        memcpy(T->rbuf + T->rlen, s, n);
        T->rlen += n;
        return;
    }
    for (; *s; s++) putc_screen(T, *s, color);
}

static void out(const char *s) { out_c(s, C_FG); }

static void outf_c(uint32_t color, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out_c(buf, color);
}

#define outf(...) outf_c(C_FG, __VA_ARGS__)

static void prompt(struct term *t)
{
    T = t;
    char path[256];
    fs_path(t->cwd, path, sizeof(path));
    struct fs_node *home = fs_resolve(fs_root(), "/home/user");
    char shown[260];
    if (t->cwd == home) strcpy(shown, "~");
    else if (!strncmp(path, "/home/user/", 11)) snprintf(shown, sizeof(shown), "~/%s", path + 11);
    else strlcpy(shown, path, sizeof(shown));
    out_c("user@novaos", C_GREEN);
    out(":");
    out_c(shown, C_BLUE);
    out("$ ");
    t->px = t->cx;
    t->py = t->cy;
    t->len = t->pos = 0;
    t->line[0] = 0;
}

/* Re-draw the input line after an edit. */
static void refresh_line(struct term *t)
{
    for (int y = t->py; y < ROWS; y++)
        for (int x = (y == t->py ? t->px : 0); x < COLS; x++) {
            if (y < 0) continue;
            t->ch[y][x] = ' ';
            t->fg[y][x] = C_FG;
        }
    t->cx = t->px;
    t->cy = t->py;
    for (int i = 0; i < t->len; i++) putc_screen(t, t->line[i], RGB(255, 255, 255));
    if (t->cx >= COLS) { t->cx = 0; if (++t->cy >= ROWS) scroll(t); }
}

static void cursor_cell(struct term *t, int *x, int *y)
{
    int off = t->px + t->pos;
    *x = off % COLS;
    *y = t->py + off / COLS;
}

static void term_paint(struct window *w)
{
    struct term *t = w->data;
    struct surface *s = w->canvas;
    gfx_fill(s, 0, 0, s->w, s->h, C_BG);
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
            if (t->ch[y][x] != ' ')
                gfx_char(s, PAD + x * FONT_W, PAD + y * FONT_H, t->ch[y][x], t->fg[y][x]);
    if (t->blink && w == wm_focused()) {
        int cx, cy;
        cursor_cell(t, &cx, &cy);
        if (cy >= 0 && cy < ROWS) {
            gfx_fill(s, PAD + cx * FONT_W, PAD + cy * FONT_H, FONT_W, FONT_H, C_FG);
            if (t->pos < t->len)
                gfx_char(s, PAD + cx * FONT_W, PAD + cy * FONT_H, t->line[t->pos], C_BG);
        }
    }
}

/* ---------- commands ---------- */

typedef void (*cmd_fn)(struct term *t, int argc, char **argv);

static struct fs_node *need(struct term *t, const char *cmd, const char *path)
{
    struct fs_node *n = fs_resolve(t->cwd, path);
    if (!n) outf_c(C_RED, "%s: %s: No such file or directory\n", cmd, path);
    return n;
}

static void cmd_help(struct term *t, int argc, char **argv);

static void cmd_ls(struct term *t, int argc, char **argv)
{
    bool lng = false, all = false;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strchr(argv[i], 'l')) lng = true;
            if (strchr(argv[i], 'a')) all = true;
        } else path = argv[i];
    }
    struct fs_node *d = need(t, "ls", path ? path : ".");
    if (!d) return;
    if (!d->is_dir) { outf("%s\n", d->name); return; }
    if (all) {
        if (lng) out("drwxr-xr-x  user  -      "), out_c(".\n", C_BLUE), out("drwxr-xr-x  user  -      "), out_c("..\n", C_BLUE);
        else out_c(".  ..  ", C_BLUE);
    }
    int col = 0;
    for (struct fs_node *n = d->child; n; n = n->next) {
        uint32_t c = n->is_dir ? C_BLUE : (d == fs_resolve(fs_root(), "/bin") ? C_GREEN : C_FG);
        if (lng) {
            outf("%s  user  %6u ", n->is_dir ? "drwxr-xr-x" : "-rw-r--r--", n->is_dir ? (uint32_t)fs_count(n) : n->size);
            outf_c(c, "%s%s\n", n->name, n->is_dir ? "/" : "");
        } else {
            int wlen = strlen(n->name) + 2;
            if (col + wlen > COLS - 1) { out("\n"); col = 0; }
            outf_c(c, "%s  ", n->name);
            col += wlen;
        }
    }
    if (!lng && col) out("\n");
}

static void cmd_cd(struct term *t, int argc, char **argv)
{
    const char *p = argc > 1 ? argv[1] : "/home/user";
    if (!strcmp(p, "~")) p = "/home/user";
    struct fs_node *n = need(t, "cd", p);
    if (!n) return;
    if (!n->is_dir) { outf_c(C_RED, "cd: %s: Not a directory\n", p); return; }
    t->cwd = n;
}

static void cmd_pwd(struct term *t, int argc, char **argv)
{
    (void)argc; (void)argv;
    char p[256];
    fs_path(t->cwd, p, sizeof(p));
    outf("%s\n", p);
}

static void cmd_cat(struct term *t, int argc, char **argv)
{
    if (argc < 2) { out("usage: cat FILE...\n"); return; }
    for (int i = 1; i < argc; i++) {
        struct fs_node *n = need(t, "cat", argv[i]);
        if (!n) continue;
        if (n->is_dir) { outf_c(C_RED, "cat: %s: Is a directory\n", argv[i]); continue; }
        if (n->size) {
            out(n->data);
            if (n->data[n->size - 1] != '\n' && !T->rbuf) out("\n");
        }
    }
}

static void cmd_echo(struct term *t, int argc, char **argv)
{
    (void)t;
    for (int i = 1; i < argc; i++) { out(argv[i]); if (i + 1 < argc) out(" "); }
    out("\n");
}

static void cmd_mkdir(struct term *t, int argc, char **argv)
{
    bool parents = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p")) { parents = true; continue; }
        if (parents) {
            char buf[256], *p = buf;
            strlcpy(buf, argv[i], sizeof(buf));
            while ((p = strchr(p + 1, '/'))) {
                *p = 0;
                if (!fs_resolve(t->cwd, buf)) fs_create(t->cwd, buf, true);
                *p = '/';
            }
            if (!fs_resolve(t->cwd, buf)) fs_create(t->cwd, buf, true);
        } else if (!fs_create(t->cwd, argv[i], true)) {
            outf_c(C_RED, "mkdir: cannot create directory '%s'\n", argv[i]);
        }
    }
}

static void cmd_touch(struct term *t, int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (!fs_resolve(t->cwd, argv[i]) && !fs_create(t->cwd, argv[i], false))
            outf_c(C_RED, "touch: cannot touch '%s'\n", argv[i]);
}

static void cmd_rm(struct term *t, int argc, char **argv)
{
    bool rec = false;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') { if (strchr(argv[i], 'r')) rec = true; continue; }
        struct fs_node *n = need(t, "rm", argv[i]);
        if (!n) continue;
        if (n->is_dir && !rec) { outf_c(C_RED, "rm: cannot remove '%s': Is a directory (use -r)\n", argv[i]); continue; }
        for (struct fs_node *p = t->cwd; ; p = p->parent) {
            if (p == n) { t->cwd = n->parent; break; }
            if (p == fs_root()) break;
        }
        if (fs_remove(n, rec)) outf_c(C_RED, "rm: cannot remove '%s'\n", argv[i]);
    }
}

static void cmd_mv(struct term *t, int argc, char **argv)
{
    if (argc != 3) { out("usage: mv SOURCE DEST\n"); return; }
    struct fs_node *src = need(t, "mv", argv[1]);
    if (!src) return;
    struct fs_node *dst = fs_resolve(t->cwd, argv[2]);
    struct fs_node *parent;
    const char *name;
    char buf[256];
    if (dst && dst->is_dir) {
        parent = dst;
        name = src->name;
    } else {
        strlcpy(buf, argv[2], sizeof(buf));
        char *slash = strrchr(buf, '/');
        if (slash) { *slash = 0; parent = fs_resolve(t->cwd, slash == buf ? "/" : buf); name = slash + 1; }
        else { parent = t->cwd; name = buf; }
        if (dst) fs_remove(dst, false);
    }
    char nm[FS_NAME_MAX];
    strlcpy(nm, name, sizeof(nm));
    if (!parent || fs_move(src, parent, nm)) outf_c(C_RED, "mv: cannot move '%s' to '%s'\n", argv[1], argv[2]);
}

static void cmd_cp(struct term *t, int argc, char **argv)
{
    if (argc != 3) { out("usage: cp SOURCE DEST\n"); return; }
    struct fs_node *src = need(t, "cp", argv[1]);
    if (!src) return;
    if (src->is_dir) { out_c("cp: directories are not supported\n", C_RED); return; }
    struct fs_node *dst = fs_resolve(t->cwd, argv[2]);
    if (dst && dst->is_dir) {
        char p[256];
        fs_path(dst, p, sizeof(p));
        strlcpy(p + strlen(p), "/", sizeof(p) - strlen(p));
        strlcpy(p + strlen(p), src->name, sizeof(p) - strlen(p));
        dst = fs_resolve(t->cwd, p);
        if (!dst) dst = fs_create(t->cwd, p, false);
    } else if (!dst) {
        dst = fs_create(t->cwd, argv[2], false);
    }
    if (!dst || dst->is_dir || fs_write(dst, src->data ? src->data : "", src->size))
        outf_c(C_RED, "cp: cannot copy to '%s'\n", argv[2]);
}

static void tree_rec(struct fs_node *d, char *prefix, int depth)
{
    for (struct fs_node *n = d->child; n; n = n->next) {
        bool last = !n->next;
        outf_c(C_GRAY, "%s%s", prefix, last ? "`-- " : "|-- ");
        outf_c(n->is_dir ? C_BLUE : C_FG, "%s\n", n->name);
        if (n->is_dir && depth < 6) {
            size_t l = strlen(prefix);
            strcpy(prefix + l, last ? "    " : "|   ");
            tree_rec(n, prefix, depth + 1);
            prefix[l] = 0;
        }
    }
}

static void cmd_tree(struct term *t, int argc, char **argv)
{
    struct fs_node *d = need(t, "tree", argc > 1 ? argv[1] : ".");
    if (!d) return;
    char prefix[64] = "";
    outf_c(C_BLUE, "%s\n", argc > 1 ? argv[1] : ".");
    tree_rec(d, prefix, 0);
}

static void cmd_grep(struct term *t, int argc, char **argv)
{
    if (argc < 3) { out("usage: grep PATTERN FILE...\n"); return; }
    for (int i = 2; i < argc; i++) {
        struct fs_node *n = need(t, "grep", argv[i]);
        if (!n || n->is_dir || !n->data) continue;
        const char *line = n->data;
        size_t plen = strlen(argv[1]);
        while (*line) {
            const char *end = strchr(line, '\n');
            size_t len = end ? (size_t)(end - line) : strlen(line);
            for (size_t k = 0; k + plen <= len; k++)
                if (!strncmp(line + k, argv[1], plen)) {
                    char buf[256];
                    if (argc > 3) outf_c(C_MAGENTA, "%s:", argv[i]);
                    strlcpy(buf, line, len + 1 < sizeof(buf) ? len + 1 : sizeof(buf));
                    buf[k] = 0; out(buf);
                    char m[128]; strlcpy(m, argv[1], sizeof(m)); out_c(m, C_RED);
                    strlcpy(buf, line + k + plen, len - k - plen + 1 < sizeof(buf) ? len - k - plen + 1 : sizeof(buf));
                    out(buf); out("\n");
                    break;
                }
            line += len + (end ? 1 : 0);
        }
    }
}

static void cmd_wc(struct term *t, int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        struct fs_node *n = need(t, "wc", argv[i]);
        if (!n || n->is_dir) continue;
        int lines = 0, words = 0, inword = 0;
        for (uint32_t k = 0; k < n->size; k++) {
            char c = n->data[k];
            if (c == '\n') lines++;
            if (isspace(c)) inword = 0;
            else if (!inword) { inword = 1; words++; }
        }
        outf("%4d %4d %5u %s\n", lines, words, n->size, argv[i]);
    }
}

static void cmd_head(struct term *t, int argc, char **argv)
{
    int lines = 10, i = 1;
    if (argc > 2 && !strcmp(argv[1], "-n")) { lines = atoi(argv[2]); i = 3; }
    for (; i < argc; i++) {
        struct fs_node *n = need(t, "head", argv[i]);
        if (!n || n->is_dir || !n->data) continue;
        char buf[256];
        const char *p = n->data;
        for (int l = 0; l < lines && *p; l++) {
            const char *e = strchr(p, '\n');
            size_t len = e ? (size_t)(e - p) : strlen(p);
            strlcpy(buf, p, len + 1 < sizeof(buf) ? len + 1 : sizeof(buf));
            out(buf); out("\n");
            p += len + (e ? 1 : 0);
        }
    }
}

static void cmd_clear(struct term *t, int argc, char **argv)
{
    (void)argc; (void)argv;
    clear_screen(t);
}

static void cmd_uname(struct term *t, int argc, char **argv)
{
    (void)t;
    if (argc > 1 && !strcmp(argv[1], "-a"))
        out(OS_NAME " novaos " OS_VERSION " #1 SMP " __DATE__ " i686 NovaOS\n");
    else
        out(OS_NAME "\n");
}

static void cmd_whoami(struct term *t, int argc, char **argv) { (void)t; (void)argc; (void)argv; out("user\n"); }
static void cmd_hostname(struct term *t, int argc, char **argv) { (void)t; (void)argc; (void)argv; out("novaos\n"); }

static void cmd_date(struct term *t, int argc, char **argv)
{
    (void)t; (void)argc; (void)argv;
    static const char *mon[] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
    struct datetime d;
    rtc_read(&d);
    outf("%s %2d %02d:%02d:%02d UTC %d\n", mon[(d.month + 11) % 12], d.day, d.hour, d.minute, d.second, d.year);
}

static void cmd_uptime(struct term *t, int argc, char **argv)
{
    (void)t; (void)argc; (void)argv;
    char buf[64];
    uptime_str(buf, sizeof(buf));
    outf("up %s\n", buf);
}

static void cmd_free(struct term *t, int argc, char **argv)
{
    (void)t; (void)argc; (void)argv;
    uint32_t total, used;
    heap_stats(&total, &used);
    out("              total        used        free\n");
    outf("Mem:     %8u KB %8u KB %8u KB\n", sys_mem_kb, used / 1024 + (sys_mem_kb - total / 1024),
         (total - used) / 1024);
    outf("Heap:    %8u KB %8u KB %8u KB\n", total / 1024, used / 1024, (total - used) / 1024);
}

static void cmd_ps(struct term *t, int argc, char **argv)
{
    (void)t; (void)argc; (void)argv;
    out("  PID  STATE      NAME\n");
    out("    0  running    kernel\n");
    out("    1  running    wm (window manager)\n");
    int pid = 2;
    for (struct window *w = wm_windows(); w; w = w->next, pid++)
        outf("%5d  %-9s  %s\n", pid, w->minimized ? "minimized" : "running", w->title);
}

static void cmd_kill(struct term *t, int argc, char **argv)
{
    (void)t;
    if (argc < 2) { out("usage: kill PID   (see ps)\n"); return; }
    int pid = atoi(argv[1]), i = 2;
    for (struct window *w = wm_windows(); w; w = w->next, i++)
        if (i == pid) { wm_close(w); return; }
    outf_c(C_RED, "kill: (%d) - No such process\n", pid);
}

static void cmd_neofetch(struct term *t, int argc, char **argv)
{
    (void)t; (void)argc; (void)argv;
    static const char *logo[] = {
        "      .--.      ",
        "     / .. \\     ",
        "  .-'  ''  '-.  ",
        " /   N O V A  \\ ",
        " \\    .--.    / ",
        "  '-. |  | .-'  ",
        "     \\ '' /     ",
        "      '--'      ",
        "                ",
    };
    char cpu[64], up[64], info[9][80];
    uint32_t total, used;
    cpu_brand(cpu, sizeof(cpu));
    uptime_str(up, sizeof(up));
    heap_stats(&total, &used);
    int n = 0;
    snprintf(info[n++], 80, "user@novaos");
    snprintf(info[n++], 80, "-----------");
    snprintf(info[n++], 80, "OS: " OS_NAME " " OS_VERSION " (" OS_CODENAME ") i686");
    snprintf(info[n++], 80, "Kernel: nova-" OS_VERSION);
    snprintf(info[n++], 80, "Uptime: %s", up);
    snprintf(info[n++], 80, "Shell: nsh 0.1");
    snprintf(info[n++], 80, "Resolution: %s", gfx_mode_name());
    snprintf(info[n++], 80, "CPU: %s", cpu);
    snprintf(info[n++], 80, "Memory: %uMB / %uMB", used / (1024 * 1024) + 1, sys_mem_kb / 1024);
    for (int i = 0; i < 9; i++) {
        out_c(logo[i], i < 4 ? C_CYAN : C_MAGENTA);
        out("  ");
        if (i < n) {
            char *colon = strchr(info[i], ':');
            if (i == 0) out_c(info[i], C_GREEN);
            else if (colon) {
                *colon = 0;
                out_c(info[i], C_CYAN);
                out(":");
                out(colon + 1);
            } else out(info[i]);
        }
        out("\n");
    }
    out("                  ");
    uint32_t cols[] = { C_BG, C_RED, C_GREEN, C_YELLOW, C_BLUE, C_MAGENTA, C_CYAN, C_FG };
    for (int i = 0; i < 8; i++) out_c("###", cols[i] == C_BG ? C_GRAY : cols[i]);
    out("\n");
}

/* calc: integer expression evaluator (+ - * / % and parentheses) */
static const char *ep;
static bool eerr;
static int32_t expr(void);
static void skipws(void) { while (*ep == ' ') ep++; }
static int32_t factor(void)
{
    skipws();
    if (*ep == '(') { ep++; int32_t v = expr(); skipws(); if (*ep == ')') ep++; else eerr = true; return v; }
    if (*ep == '-') { ep++; return -factor(); }
    if (!isdigit(*ep)) { eerr = true; return 0; }
    int32_t v = 0;
    while (isdigit(*ep)) v = v * 10 + (*ep++ - '0');
    return v;
}
static int32_t term_(void)
{
    int32_t v = factor();
    for (;;) {
        skipws();
        char op = *ep;
        if (op != '*' && op != '/' && op != '%') return v;
        ep++;
        int32_t r = factor();
        if ((op == '/' || op == '%') && r == 0) { eerr = true; return 0; }
        v = op == '*' ? v * r : op == '/' ? v / r : v % r;
    }
}
static int32_t expr(void)
{
    int32_t v = term_();
    for (;;) {
        skipws();
        if (*ep == '+') { ep++; v += term_(); }
        else if (*ep == '-') { ep++; v -= term_(); }
        else return v;
    }
}

static void cmd_calc(struct term *t, int argc, char **argv)
{
    (void)t;
    if (argc < 2) { app_calc(); return; }
    char buf[LINE_MAX] = "";
    for (int i = 1; i < argc; i++) strcat(buf, argv[i]);
    ep = buf;
    eerr = false;
    int32_t v = expr();
    skipws();
    if (eerr || *ep) out_c("calc: invalid expression\n", C_RED);
    else outf("%d\n", v);
}

static void cmd_history(struct term *t, int argc, char **argv)
{
    (void)argc; (void)argv;
    for (int i = 0; i < t->nhist; i++) outf("%4d  %s\n", i + 1, t->hist[i]);
}

static void cmd_edit(struct term *t, int argc, char **argv)
{
    if (argc < 2) { app_editor(NULL); return; }
    struct fs_node *n = fs_resolve(t->cwd, argv[1]);
    if (!n) n = fs_create(t->cwd, argv[1], false);
    if (!n || n->is_dir) { outf_c(C_RED, "edit: cannot open '%s'\n", argv[1]); return; }
    char p[256];
    fs_path(n, p, sizeof(p));
    app_editor(p);
}

static void cmd_open(struct term *t, int argc, char **argv)
{
    if (argc < 2) { out("usage: open APP|PATH   (apps: terminal files editor paint calc about)\n"); return; }
    const char *a = argv[1];
    if (!strcmp(a, "terminal")) app_terminal();
    else if (!strcmp(a, "files")) app_files(NULL);
    else if (!strcmp(a, "editor")) app_editor(NULL);
    else if (!strcmp(a, "paint")) app_paint();
    else if (!strcmp(a, "calc")) app_calc();
    else if (!strcmp(a, "about")) app_about();
    else {
        struct fs_node *n = need(t, "open", a);
        if (!n) return;
        char p[256];
        fs_path(n, p, sizeof(p));
        if (n->is_dir) app_files(p); else app_editor(p);
    }
}

static void cmd_exit(struct term *t, int argc, char **argv);
static void cmd_reboot(struct term *t, int argc, char **argv) { (void)t; (void)argc; (void)argv; sys_reboot(); }
static void cmd_shutdown(struct term *t, int argc, char **argv) { (void)t; (void)argc; (void)argv; sys_shutdown(); }

static const struct { const char *name; cmd_fn fn; const char *help; } cmds[] = {
    { "help", cmd_help, "show this help" },
    { "ls", cmd_ls, "list directory  (-l long, -a all)" },
    { "cd", cmd_cd, "change directory" },
    { "pwd", cmd_pwd, "print working directory" },
    { "cat", cmd_cat, "print file contents" },
    { "echo", cmd_echo, "print text  (use > or >> to write files)" },
    { "mkdir", cmd_mkdir, "create directory  (-p parents)" },
    { "touch", cmd_touch, "create empty file" },
    { "rm", cmd_rm, "remove file  (-r recursive)" },
    { "mv", cmd_mv, "move / rename" },
    { "cp", cmd_cp, "copy file" },
    { "tree", cmd_tree, "show directory tree" },
    { "grep", cmd_grep, "search text in files" },
    { "wc", cmd_wc, "count lines, words, bytes" },
    { "head", cmd_head, "first lines of a file  (-n N)" },
    { "clear", cmd_clear, "clear the screen  (Ctrl+L)" },
    { "uname", cmd_uname, "system name  (-a all)" },
    { "whoami", cmd_whoami, "current user" },
    { "hostname", cmd_hostname, "machine name" },
    { "date", cmd_date, "current date and time" },
    { "uptime", cmd_uptime, "time since boot" },
    { "free", cmd_free, "memory usage" },
    { "ps", cmd_ps, "list running programs" },
    { "kill", cmd_kill, "close a program by PID" },
    { "neofetch", cmd_neofetch, "system info with logo" },
    { "calc", cmd_calc, "calculate, e.g. calc (2+3)*4" },
    { "history", cmd_history, "command history" },
    { "edit", cmd_edit, "open file in Text Editor" },
    { "open", cmd_open, "open an app or path" },
    { "exit", cmd_exit, "close this terminal" },
    { "reboot", cmd_reboot, "restart the computer" },
    { "shutdown", cmd_shutdown, "power off" },
};
#define NCMDS (int)(sizeof(cmds) / sizeof(cmds[0]))

static void cmd_help(struct term *t, int argc, char **argv)
{
    (void)t; (void)argc; (void)argv;
    out_c(OS_NAME " shell, built-in commands:\n", C_CYAN);
    for (int i = 0; i < NCMDS; i++) {
        outf_c(C_GREEN, "  %-9s", cmds[i].name);
        outf("%s\n", cmds[i].help);
    }
    out_c("Keys: Up/Down history, Tab complete, Ctrl+C cancel, Ctrl+Alt+T new terminal\n", C_GRAY);
}

static void cmd_exit(struct term *t, int argc, char **argv)
{
    (void)argc; (void)argv;
    for (struct window *w = wm_windows(); w; w = w->next)
        if (w->data == t) wm_close(w);
}

/* ---------- line execution ---------- */

static int split(char *s, char **argv, int max)
{
    int argc = 0;
    while (*s && argc < max) {
        while (*s == ' ') s++;
        if (!*s) break;
        if (*s == '"' || *s == '\'') {
            char q = *s++;
            argv[argc++] = s;
            while (*s && *s != q) s++;
        } else {
            argv[argc++] = s;
            while (*s && *s != ' ') s++;
        }
        if (*s) *s++ = 0;
    }
    return argc;
}

static void execute(struct term *t, char *line)
{
    T = t;
    /* output redirection: cmd > file  /  cmd >> file */
    char *redir = NULL;
    bool append = false;
    char *gt = strchr(line, '>');
    if (gt) {
        append = gt[1] == '>';
        *gt = 0;
        redir = gt + (append ? 2 : 1);
        while (*redir == ' ') redir++;
        char *e = redir + strlen(redir);
        while (e > redir && e[-1] == ' ') *--e = 0;
        if (!*redir) { out_c("nsh: syntax error: missing file after '>'\n", C_RED); return; }
    }

    char *argv[32];
    int argc = split(line, argv, 32);
    if (!argc) return;

    for (int i = 0; i < NCMDS; i++) {
        if (strcmp(argv[0], cmds[i].name)) continue;
        if (redir) {
            t->rcap = 256;
            t->rbuf = kmalloc(t->rcap);
            t->rlen = 0;
        }
        cmds[i].fn(t, argc, argv);
        if (redir) {
            char *buf = t->rbuf;
            uint32_t len = t->rlen;
            t->rbuf = NULL;
            struct fs_node *f = fs_resolve(t->cwd, redir);
            if (!f) f = fs_create(t->cwd, redir, false);
            if (!f || f->is_dir) outf_c(C_RED, "nsh: %s: cannot write\n", redir);
            else if (append) fs_append(f, buf, len);
            else fs_write(f, buf, len);
            kfree(buf);
        }
        return;
    }
    outf_c(C_RED, "nsh: command not found: %s\n", argv[0]);
}

static void complete(struct term *t)
{
    int start = t->len;
    while (start > 0 && t->line[start - 1] != ' ') start--;
    const char *word = t->line + start;
    size_t wl = t->len - start;
    const char *match = NULL;
    int count = 0;
    bool first_word = start == 0;

    /* split word into directory part and name prefix */
    char dir[LINE_MAX] = ".";
    const char *prefix = word;
    const char *slash = NULL;
    for (size_t i = 0; i < wl; i++) if (word[i] == '/') slash = word + i;
    if (slash) {
        size_t dl = slash - word;
        strlcpy(dir, dl ? word : "/", dl ? dl + 1 : 2);
        prefix = slash + 1;
    }
    size_t pl = wl - (prefix - word);
    bool match_dir = false;

    if (first_word && !slash)
        for (int i = 0; i < NCMDS; i++)
            if (!strncmp(cmds[i].name, prefix, pl)) { match = cmds[i].name; count++; }
    if (!first_word || slash) {
        struct fs_node *d = fs_resolve(t->cwd, dir);
        if (d && d->is_dir)
            for (struct fs_node *n = d->child; n; n = n->next)
                if (!strncmp(n->name, prefix, pl)) { match = n->name; match_dir = n->is_dir; count++; }
    }
    if (count != 1) return;
    const char *rest = match + pl;
    size_t rl = strlen(rest);
    if (t->len + rl + 2 >= LINE_MAX) return;
    memcpy(t->line + t->len, rest, rl);
    t->len += rl;
    t->line[t->len++] = match_dir ? '/' : ' ';
    t->line[t->len] = 0;
    t->pos = t->len;
}

static void term_key(struct window *w, struct key_event *ev)
{
    struct term *t = w->data;
    T = t;
    int k = ev->key;
    t->blink = true;

    if (ev->mods & KMOD_CTRL) {
        if (k == 'l' || k == 'L') {
            clear_screen(t);
            char saved[LINE_MAX];
            int pos = t->pos;
            strlcpy(saved, t->line, sizeof(saved));
            prompt(t);
            strlcpy(t->line, saved, sizeof(t->line));
            t->len = strlen(saved);
            t->pos = pos;
            refresh_line(t);
        } else if (k == 'c' || k == 'C') {
            t->pos = t->len;
            refresh_line(t);
            out_c("^C\n", C_GRAY);
            prompt(t);
        }
        wm_invalidate(w);
        return;
    }

    switch (k) {
    case '\n': {
        t->pos = t->len;
        refresh_line(t);
        putc_screen(t, '\n', C_FG);
        if (t->len) {
            if (!t->nhist || strcmp(t->hist[t->nhist - 1], t->line)) {
                if (t->nhist == HIST) { memmove(t->hist[0], t->hist[1], sizeof(t->hist[0]) * (HIST - 1)); t->nhist--; }
                strcpy(t->hist[t->nhist++], t->line);
            }
            char cmd[LINE_MAX];
            strcpy(cmd, t->line);
            execute(t, cmd);
        }
        t->hidx = t->nhist;
        if (t->cx != 0) putc_screen(t, '\n', C_FG);
        prompt(t);
        break;
    }
    case '\b':
        if (t->pos > 0) {
            memmove(t->line + t->pos - 1, t->line + t->pos, t->len - t->pos + 1);
            t->pos--; t->len--;
        }
        break;
    case KEY_DEL:
        if (t->pos < t->len) {
            memmove(t->line + t->pos, t->line + t->pos + 1, t->len - t->pos);
            t->len--;
        }
        break;
    case KEY_LEFT:  if (t->pos > 0) t->pos--; break;
    case KEY_RIGHT: if (t->pos < t->len) t->pos++; break;
    case KEY_HOME:  t->pos = 0; break;
    case KEY_END:   t->pos = t->len; break;
    case KEY_UP:
    case KEY_DOWN:
        if (k == KEY_UP && t->hidx > 0) t->hidx--;
        else if (k == KEY_DOWN && t->hidx < t->nhist) t->hidx++;
        else break;
        strcpy(t->line, t->hidx < t->nhist ? t->hist[t->hidx] : "");
        t->len = t->pos = strlen(t->line);
        break;
    case '\t':
        complete(t);
        break;
    default:
        if (k >= 32 && k < 127 && t->len < LINE_MAX - 1) {
            memmove(t->line + t->pos + 1, t->line + t->pos, t->len - t->pos + 1);
            t->line[t->pos++] = (char)k;
            t->len++;
        }
    }
    refresh_line(t);
    /* keep the cursor on screen */
    int cx, cy;
    cursor_cell(t, &cx, &cy);
    while (cy >= ROWS) { scroll(t); cy--; }
    wm_invalidate(w);
}

static void term_tick(struct window *w)
{
    struct term *t = w->data;
    static uint32_t n;
    if (++n % 5 == 0) { t->blink = !t->blink; wm_invalidate(w); }
}

static void term_close(struct window *w) { kfree(w->data); }

void app_terminal(void)
{
    struct term *t = kcalloc(sizeof(*t));
    if (!t) return;
    struct window *w = wm_create("Terminal", ICON_TERMINAL, COLS * FONT_W + 2 * PAD, ROWS * FONT_H + 2 * PAD);
    if (!w) { kfree(t); return; }
    w->data = t;
    w->on_paint = term_paint;
    w->on_key = term_key;
    w->on_tick = term_tick;
    w->on_close = term_close;
    t->cwd = fs_resolve(fs_root(), "/home/user");
    clear_screen(t);
    T = t;
    struct fs_node *motd = fs_resolve(fs_root(), "/etc/motd");
    out_c(OS_NAME " " OS_VERSION " (" OS_CODENAME ")  tty1\n", C_CYAN);
    if (motd && motd->data) out_c(motd->data, C_GRAY);
    out("\n");
    prompt(t);
    t->blink = true;
    wm_invalidate(w);
}
