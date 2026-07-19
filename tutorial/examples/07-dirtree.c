/*
 * 07-dirtree.c — trees and lazy data.
 *
 * A filesystem directory browser: the tree populates each node the
 * first time it is expanded, and a label follows the selection.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <mtk/mtk.h>

typedef struct {
    MtkTree *tree;
    MtkLabel *path_label;
} Ui;

/*
 * Rebuild a node's absolute path by walking its ancestry.  Top-level
 * nodes carry their full path in `user`; everything below is just a
 * directory name joined with '/'.
 */
static void node_path(MtkTreeNode *n, char *buf, size_t len)
{
    if (n->user) {
        snprintf(buf, len, "%s", (const char *)n->user);
        return;
    }
    char parent[2048];
    node_path(n->parent, parent, sizeof(parent));
    bool slash = parent[0] && parent[strlen(parent) - 1] == '/';
    snprintf(buf, len, "%s%s%s", parent, slash ? "" : "/", n->label);
}

static int cmp_names(const void *a, const void *b)
{
    return strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

/* Called once per node, on its first expansion. */
static void on_expand(MtkTree *t, MtkTreeNode *n, void *data)
{
    (void)data;
    char path[2048];
    node_path(n, path, sizeof(path));

    DIR *d = opendir(path);
    if (!d)
        return;

    char **names = nullptr;
    int count = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        char full[2560];
        snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
        struct stat st;
        if (lstat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            names = realloc(names, sizeof(char *) * (size_t)(count + 1));
            names[count++] = strdup(de->d_name);
        }
    }
    closedir(d);

    if (count)
        qsort(names, (size_t)count, sizeof(char *), cmp_names);
    for (int i = 0; i < count; i++) {
        mtk_tree_node_add(t, n, names[i], nullptr);
        free(names[i]);
    }
    free(names);
}

static void on_select(MtkTree *t, MtkTreeNode *n, void *data)
{
    (void)t;
    Ui *ui = data;
    char path[2048];
    node_path(n, path, sizeof(path));
    mtk_label_set_text(ui->path_label, path);
}

static void layout(MtkWindow *win)
{
    Ui *ui = win->user;
    mtk_widget_set_rect(&ui->tree->base, 10, 10, win->w - 20,
                        win->h - 46);
    mtk_widget_set_rect(&ui->path_label->base, 10, win->h - 30,
                        win->w - 20, 20);
}

int main(void)
{
    MtkApp *app = mtk_app_create("dirtree");
    if (!app)
        return 1;

    MtkWindow *win = mtk_window_create(app, "Directories", 380, 420);

    Ui ui = {0};
    ui.tree = mtk_tree_create(win, nullptr);
    ui.tree->on_expand = on_expand;
    ui.tree->on_select = on_select;
    ui.tree->data = &ui;
    ui.path_label = mtk_label_create(win, nullptr, "select a directory");

    /* two roots; their `user` anchors path reconstruction */
    mtk_tree_node_add(ui.tree, nullptr, "/", "/");
    const char *home = getenv("HOME");
    MtkTreeNode *nhome =
        mtk_tree_node_add(ui.tree, nullptr, "Home",
                          (void *)(home ? home : "/"));
    mtk_tree_expand(ui.tree, nhome, true);

    win->user = &ui;
    win->on_resize = layout;
    layout(win);

    mtk_window_show(win);
    mtk_app_run(app);
    mtk_app_destroy(app);
    return 0;
}
