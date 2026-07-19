/*
 * a-mfm.c — Appendix A: a small file manager.
 *
 * Directory tree on the left, file list on the right, a draggable
 * sash between them, menus, and real file operations (create,
 * rename, delete) with confirmation and prompt dialogs.
 */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mtk/mtk.h>

enum { LEFT_MIN = 140 };

typedef struct App {
    MtkApp *mtk;
    MtkWindow *win;
    MtkMenuBar *menubar;
    MtkTree *tree;
    MtkSash *sash;
    MtkListbox *list;
    MtkLabel *status;
    int split;
    bool show_hidden;
    char curdir[2048];
    char **names;      /* basenames parallel to list rows */
    int nnames;
} App;

enum { MENU_FILE, MENU_VIEW, MENU_HELP };

static void set_status(App *a, const char *msg)
{
    mtk_label_set_text(a->status, msg);
}

/* --- directory listing -------------------------------------------- */

static void names_clear(App *a)
{
    for (int i = 0; i < a->nnames; i++)
        free(a->names[i]);
    free(a->names);
    a->names = nullptr;
    a->nnames = 0;
}

static int cmp_names(const void *x, const void *y)
{
    return strcasecmp(*(const char *const *)x, *(const char *const *)y);
}

static void list_dir(App *a, const char *path)
{
    snprintf(a->curdir, sizeof(a->curdir), "%s", path);
    names_clear(a);
    mtk_listbox_clear(a->list);

    DIR *d = opendir(path);
    if (!d) {
        set_status(a, strerror(errno));
        return;
    }
    char **dirs = nullptr, **files = nullptr;
    int ndirs = 0, nfiles = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        if (de->d_name[0] == '.' && !a->show_hidden)
            continue;
        char full[2560];
        snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
        struct stat st;
        if (lstat(full, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            dirs = realloc(dirs, sizeof(char *) * (size_t)(ndirs + 1));
            dirs[ndirs++] = strdup(de->d_name);
        } else {
            files = realloc(files, sizeof(char *) * (size_t)(nfiles + 1));
            files[nfiles++] = strdup(de->d_name);
        }
    }
    closedir(d);
    if (ndirs)
        qsort(dirs, (size_t)ndirs, sizeof(char *), cmp_names);
    if (nfiles)
        qsort(files, (size_t)nfiles, sizeof(char *), cmp_names);

    a->names = malloc(sizeof(char *) * (size_t)(ndirs + nfiles));
    for (int i = 0; i < ndirs; i++) {
        char row[2600];
        snprintf(row, sizeof(row), "%s/", dirs[i]);
        mtk_listbox_add(a->list, row);
        a->names[a->nnames++] = dirs[i];
    }
    for (int i = 0; i < nfiles; i++) {
        char full[2560], row[2600];
        snprintf(full, sizeof(full), "%s/%s", path, files[i]);
        struct stat st;
        long long sz = lstat(full, &st) == 0 ? (long long)st.st_size : 0;
        snprintf(row, sizeof(row), "%-40s %10lld", files[i], sz);
        mtk_listbox_add(a->list, row);
        a->names[a->nnames++] = files[i];
    }
    free(dirs);
    free(files);

    char buf[2200];
    snprintf(buf, sizeof(buf), "%s — %d directories, %d files",
             a->curdir, ndirs, nfiles);
    set_status(a, buf);
}

/* --- tree ---------------------------------------------------------- */

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

static void tree_expand_cb(MtkTree *t, MtkTreeNode *n, void *data)
{
    App *a = data;
    char path[2048];
    node_path(n, path, sizeof(path));
    DIR *d = opendir(path);
    if (!d)
        return;
    char **names = nullptr;
    int count = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.' && !a->show_hidden)
            continue;
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
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

static void tree_select_cb(MtkTree *t, MtkTreeNode *n, void *data)
{
    (void)t;
    App *a = data;
    char path[2048];
    node_path(n, path, sizeof(path));
    list_dir(a, path);
}

/* --- prompt and confirm dialogs ------------------------------------ */

typedef struct Prompt {
    App *a;
    MtkEntry *entry;           /* nullptr for confirm-only dialogs */
    void (*done)(App *a, const char *text);
} Prompt;

static void prompt_finish(Prompt *p, MtkWindow *win)
{
    char text[1024];
    snprintf(text, sizeof(text), "%s",
             p->entry ? mtk_entry_text(p->entry) : "yes");
    App *a = p->a;
    void (*done)(App *, const char *) = p->done;
    mtk_window_destroy(win);
    if (text[0])
        done(a, text);
}

static void prompt_ok(MtkButton *b, void *data)
{
    prompt_finish(data, b->base.win);
}

static void prompt_enter(MtkEntry *e, void *data)
{
    prompt_finish(data, e->base.win);
}

static void prompt_cancel(MtkButton *b, void *data)
{
    (void)data;
    mtk_window_destroy(b->base.win);
}

static void prompt_freed(MtkWindow *win)
{
    free(win->user);
}

static void prompt_show(App *a, const char *title, const char *initial,
                        void (*done)(App *a, const char *text))
{
    Prompt *p = calloc(1, sizeof(*p));
    p->a = a;
    p->done = done;

    MtkWindow *win = mtk_window_create(a->mtk, title, 420, 86);
    win->user = p;
    win->on_destroy = prompt_freed;

    p->entry = mtk_entry_create(win, nullptr);
    mtk_widget_set_rect(&p->entry->base, 12, 12, 396, 26);
    mtk_entry_set_text(p->entry, initial);
    p->entry->on_activate = prompt_enter;
    p->entry->data = p;

    MtkButton *ok = mtk_button_create(win, nullptr, "OK", prompt_ok, p);
    mtk_widget_set_rect(&ok->base, 232, 48, 84, 26);
    MtkButton *cancel =
        mtk_button_create(win, nullptr, "Cancel", prompt_cancel, nullptr);
    mtk_widget_set_rect(&cancel->base, 324, 48, 84, 26);

    mtk_window_set_focus(win, &p->entry->base);
    mtk_window_show(win);
}

static void confirm_show(App *a, const char *title, const char *question,
                         void (*done)(App *a, const char *unused))
{
    Prompt *p = calloc(1, sizeof(*p));
    p->a = a;
    p->done = done;

    MtkWindow *win = mtk_window_create(a->mtk, title, 380, 92);
    win->user = p;
    win->on_destroy = prompt_freed;

    MtkLabel *l = mtk_label_create(win, nullptr, question);
    mtk_widget_set_rect(&l->base, 12, 12, 356, 22);

    MtkButton *ok = mtk_button_create(win, nullptr, "Delete", prompt_ok,
                                      p);
    mtk_widget_set_rect(&ok->base, 192, 52, 84, 26);
    MtkButton *cancel =
        mtk_button_create(win, nullptr, "Cancel", prompt_cancel, nullptr);
    mtk_widget_set_rect(&cancel->base, 284, 52, 84, 26);
    mtk_window_show(win);
}

/* --- file operations ------------------------------------------------ */

static void do_mkdir(App *a, const char *name)
{
    char full[2600];
    snprintf(full, sizeof(full), "%s/%s", a->curdir, name);
    if (mkdir(full, 0755) != 0)
        set_status(a, strerror(errno));
    else
        list_dir(a, a->curdir);
}

static void do_rename(App *a, const char *newname)
{
    int i = a->list->selected;
    if (i < 0 || i >= a->nnames)
        return;
    char from[2600], to[2600];
    snprintf(from, sizeof(from), "%s/%s", a->curdir, a->names[i]);
    snprintf(to, sizeof(to), "%s/%s", a->curdir, newname);
    if (rename(from, to) != 0)
        set_status(a, strerror(errno));
    else
        list_dir(a, a->curdir);
}

static void do_delete(App *a, const char *unused)
{
    (void)unused;
    int errors = 0, removed = 0;
    for (int i = a->list->nitems - 1; i >= 0; i--) {
        bool pick = a->list->marked[i] ||
                    (!mtk_listbox_any_marked(a->list) &&
                     i == a->list->selected);
        if (!pick)
            continue;
        char full[2600];
        snprintf(full, sizeof(full), "%s/%s", a->curdir, a->names[i]);
        struct stat st;
        int rc = lstat(full, &st) == 0 && S_ISDIR(st.st_mode)
                     ? rmdir(full)
                     : unlink(full);
        if (rc != 0)
            errors++;
        else
            removed++;
    }
    list_dir(a, a->curdir);
    char buf[96];
    snprintf(buf, sizeof(buf), "removed %d%s", removed,
             errors ? " (some failed — directories must be empty)" : "");
    set_status(a, buf);
}

static int selection_count(App *a)
{
    int n = 0;
    for (int i = 0; i < a->list->nitems; i++)
        if (a->list->marked[i])
            n++;
    if (n == 0 && a->list->selected >= 0)
        n = 1;
    return n;
}

static void ask_delete(App *a)
{
    int n = selection_count(a);
    if (n == 0)
        return;
    char q[96];
    snprintf(q, sizeof(q), "Delete %d item%s from this directory?", n,
             n == 1 ? "" : "s");
    confirm_show(a, "Confirm Delete", q, do_delete);
}

/* --- list callbacks -------------------------------------------------- */

static void list_activate(MtkListbox *lb, int index, void *data)
{
    (void)lb;
    App *a = data;
    if (index < 0 || index >= a->nnames)
        return;
    char full[2600];
    snprintf(full, sizeof(full), "%s/%s", a->curdir, a->names[index]);
    struct stat st;
    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
        list_dir(a, full); /* descend on double click */
}

static void list_delete_key(MtkListbox *lb, int index, void *data)
{
    (void)lb;
    (void)index;
    ask_delete(data);
}

/* --- menu ------------------------------------------------------------- */

static void show_about(App *a)
{
    MtkWindow *win = mtk_window_create(a->mtk, "About mfm", 300, 96);
    MtkLabel *l1 = mtk_label_create(win, nullptr, "mfm");
    l1->bold = true;
    l1->align = MTK_ALIGN_CENTER;
    MtkLabel *l2 = mtk_label_create(win, nullptr,
                                    "A libmtk tutorial file manager");
    l2->align = MTK_ALIGN_CENTER;
    mtk_widget_set_rect(&l1->base, 10, 10, 280, 20);
    mtk_widget_set_rect(&l2->base, 10, 32, 280, 20);
    MtkButton *ok = mtk_button_create(win, nullptr, "OK", prompt_cancel,
                                      nullptr);
    mtk_widget_set_rect(&ok->base, 108, 60, 84, 26);
    mtk_window_show(win);
}

static void menu_pick(MtkMenuBar *mb, int menu, int item, void *data)
{
    (void)mb;
    App *a = data;
    if (menu == MENU_FILE) {
        switch (item) {
        case 0:
            prompt_show(a, "New Directory", "newdir", do_mkdir);
            break;
        case 1:
            if (a->list->selected >= 0 &&
                a->list->selected < a->nnames)
                prompt_show(a, "Rename", a->names[a->list->selected],
                            do_rename);
            break;
        case 2:
            ask_delete(a);
            break;
        case 4:
            mtk_app_quit(a->mtk);
            break;
        }
    } else if (menu == MENU_VIEW) {
        switch (item) {
        case 0:
            list_dir(a, a->curdir);
            break;
        case 1:
            a->show_hidden = !a->show_hidden;
            list_dir(a, a->curdir);
            break;
        }
    } else if (menu == MENU_HELP && item == 0) {
        show_about(a);
    }
}

/* --- layout and main ---------------------------------------------------- */

static void layout(MtkWindow *win)
{
    App *a = win->user;
    int top = MTK_MENUBAR_H;
    if (a->split < LEFT_MIN)
        a->split = LEFT_MIN;
    if (a->split > win->w - 200)
        a->split = win->w - 200;

    mtk_widget_set_rect(&a->menubar->base, 0, 0, win->w, top);
    mtk_widget_set_rect(&a->tree->base, 6, top + 6, a->split - 6,
                        win->h - top - 38);
    a->sash->min_x = LEFT_MIN;
    a->sash->max_x = win->w - 200;
    /* stop above the status row, like the panes do */
    mtk_widget_set_rect(&a->sash->base, a->split, top, MTK_SASH_W,
                        win->h - top - 32);
    int lx = a->split + MTK_SASH_W;
    mtk_widget_set_rect(&a->list->base, lx + 6, top + 6,
                        win->w - lx - 12, win->h - top - 38);
    mtk_widget_set_rect(&a->status->base, 6, win->h - 26, win->w - 12,
                        20);
}

static void sash_drag(MtkSash *s, int new_x, void *data)
{
    (void)s;
    App *a = data;
    a->split = new_x;
    layout(a->win);
}

static bool on_key(MtkWindow *win, XKeyEvent *ev, KeySym sym,
                   const char *text)
{
    (void)text;
    App *a = win->user;
    return mtk_menubar_key(a->menubar, ev, sym);
}

int main(int argc, char **argv)
{
    MtkApp *mtk = mtk_app_create("mfm");
    if (!mtk)
        return 1;

    App a = { .mtk = mtk, .split = 200 };
    a.win = mtk_window_create(mtk, "mfm — File Manager", 720, 480);
    a.win->user = &a;
    a.win->on_resize = layout;
    a.win->on_key = on_key;

    a.menubar = mtk_menubar_create(a.win, nullptr, menu_pick, &a);
    static const MtkMenuEntry file_menu[] = {
        {"New Directory...", nullptr},
        {"Rename...", nullptr},
        {"Delete...", "Del"},
        {"-", nullptr},
        {"Quit", nullptr},
    };
    static const MtkMenuEntry view_menu[] = {
        {"Refresh", "F5"},
        {"Toggle Hidden", nullptr},
    };
    static const MtkMenuEntry help_menu[] = {
        {"About mfm", nullptr},
    };
    mtk_menubar_add(a.menubar, "File", file_menu, 5);
    mtk_menubar_add(a.menubar, "View", view_menu, 2);
    int help = mtk_menubar_add(a.menubar, "Help", help_menu, 1);
    mtk_menubar_set_help(a.menubar, help);

    a.tree = mtk_tree_create(a.win, nullptr);
    a.tree->on_expand = tree_expand_cb;
    a.tree->on_select = tree_select_cb;
    a.tree->data = &a;
    mtk_tree_node_add(a.tree, nullptr, "/", "/");
    const char *home = getenv("HOME");
    MtkTreeNode *nhome = mtk_tree_node_add(a.tree, nullptr, "Home",
                                           (void *)(home ? home : "/"));
    mtk_tree_expand(a.tree, nhome, true);

    a.sash = mtk_sash_create(a.win, nullptr, sash_drag, &a);

    a.list = mtk_listbox_create(a.win, nullptr);
    a.list->multi = true;
    a.list->on_activate = list_activate;
    a.list->on_delete = list_delete_key;
    a.list->data = &a;

    a.status = mtk_label_create(a.win, nullptr, "");

    layout(a.win);
    list_dir(&a, argc > 1 ? argv[1] : (home ? home : "/"));

    mtk_window_show(a.win);
    mtk_app_run(mtk);
    names_clear(&a);
    mtk_app_destroy(mtk);
    return 0;
}
