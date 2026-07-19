/*
 * 08-dusage.c — background work with the idle hook.
 *
 * A disk-usage counter that walks a directory tree without ever
 * blocking the UI: the traversal runs in small slices from the idle
 * hook, one directory per slice, and can be cancelled at any time.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <mtk/mtk.h>

typedef struct {
    MtkApp *app;
    MtkEntry *path;
    MtkButton *scan, *cancel;
    MtkLabel *files_label, *dirs_label, *size_label, *status;

    /* pending directories, a simple LIFO work list */
    char **stack;
    int nstack, cap;

    long long bytes;
    int files, dirs;
    bool scanning;
    uint64_t started_ms;
} Ui;

static void push_dir(Ui *ui, const char *path)
{
    if (ui->nstack == ui->cap) {
        ui->cap = ui->cap ? ui->cap * 2 : 64;
        ui->stack = realloc(ui->stack, sizeof(char *) * (size_t)ui->cap);
    }
    ui->stack[ui->nstack++] = strdup(path);
}

static void show_progress(Ui *ui)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%d files", ui->files);
    mtk_label_set_text(ui->files_label, buf);
    snprintf(buf, sizeof(buf), "%d directories", ui->dirs);
    mtk_label_set_text(ui->dirs_label, buf);
    snprintf(buf, sizeof(buf), "%.1f MiB",
             (double)ui->bytes / (1024.0 * 1024.0));
    mtk_label_set_text(ui->size_label, buf);
}

static void scan_finished(Ui *ui, const char *how)
{
    char buf[96];
    ui->scanning = false;
    show_progress(ui);
    snprintf(buf, sizeof(buf), "%s after %.1f s", how,
             (double)(mtk_now_ms() - ui->started_ms) / 1000.0);
    mtk_label_set_text(ui->status, buf);
}

/*
 * One idle slice: process a single directory, push its
 * subdirectories, and return whether more work remains.  The
 * toolkit calls this only when no events or timers are pending, so
 * the window stays fully responsive during the walk.
 */
static bool scan_idle(void *data)
{
    Ui *ui = data;
    if (!ui->scanning || ui->nstack == 0) {
        if (ui->scanning)
            scan_finished(ui, "finished");
        return false;
    }

    char *path = ui->stack[--ui->nstack];
    DIR *d = opendir(path);
    if (d) {
        ui->dirs++;
        struct dirent *de;
        while ((de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
                continue;
            char full[4096];
            snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
            struct stat st;
            if (lstat(full, &st) != 0)
                continue;
            if (S_ISDIR(st.st_mode)) {
                push_dir(ui, full);
            } else if (S_ISREG(st.st_mode)) {
                ui->files++;
                ui->bytes += (long long)st.st_size;
            }
        }
        closedir(d);
    }
    free(path);

    show_progress(ui);
    if (ui->nstack == 0) {
        scan_finished(ui, "finished");
        return false;
    }
    return true;
}

static void clear_stack(Ui *ui)
{
    for (int i = 0; i < ui->nstack; i++)
        free(ui->stack[i]);
    ui->nstack = 0;
}

static void on_scan(MtkButton *b, void *data)
{
    (void)b;
    Ui *ui = data;
    if (ui->scanning)
        return;
    clear_stack(ui);
    ui->bytes = 0;
    ui->files = 0;
    ui->dirs = 0;
    ui->scanning = true;
    ui->started_ms = mtk_now_ms();
    push_dir(ui, mtk_entry_text(ui->path));
    mtk_label_set_text(ui->status, "scanning...");
    mtk_app_set_idle(ui->app, scan_idle, ui);
}

static void on_cancel(MtkButton *b, void *data)
{
    (void)b;
    Ui *ui = data;
    if (!ui->scanning)
        return;
    clear_stack(ui);
    scan_finished(ui, "cancelled");
}

static void on_path_activate(MtkEntry *e, void *data)
{
    (void)e;
    on_scan(nullptr, data);
}

static void layout(MtkWindow *win)
{
    Ui *ui = win->user;
    mtk_widget_set_rect(&ui->path->base, 10, 10, win->w - 184, 26);
    mtk_widget_set_rect(&ui->scan->base, win->w - 166, 10, 74, 26);
    mtk_widget_set_rect(&ui->cancel->base, win->w - 84, 10, 74, 26);
    mtk_widget_set_rect(&ui->files_label->base, 10, 52, win->w - 20, 20);
    mtk_widget_set_rect(&ui->dirs_label->base, 10, 74, win->w - 20, 20);
    mtk_widget_set_rect(&ui->size_label->base, 10, 96, win->w - 20, 20);
    mtk_widget_set_rect(&ui->status->base, 10, win->h - 30,
                        win->w - 20, 20);
}

int main(void)
{
    MtkApp *app = mtk_app_create("dusage");
    if (!app)
        return 1;

    MtkWindow *win = mtk_window_create(app, "Disk Usage", 420, 170);

    Ui ui = { .app = app };
    ui.path = mtk_entry_create(win, nullptr);
    const char *home = getenv("HOME");
    mtk_entry_set_text(ui.path, home ? home : "/");
    ui.path->on_activate = on_path_activate;
    ui.path->data = &ui;
    ui.scan = mtk_button_create(win, nullptr, "Scan", on_scan, &ui);
    ui.cancel = mtk_button_create(win, nullptr, "Cancel", on_cancel, &ui);
    ui.files_label = mtk_label_create(win, nullptr, "0 files");
    ui.dirs_label = mtk_label_create(win, nullptr, "0 directories");
    ui.size_label = mtk_label_create(win, nullptr, "0.0 MiB");
    ui.size_label->bold = true;
    ui.status = mtk_label_create(win, nullptr, "enter a path and Scan");

    win->user = &ui;
    win->on_resize = layout;
    layout(win);

    mtk_window_show(win);
    mtk_app_run(app);
    clear_stack(&ui);
    free(ui.stack);
    mtk_app_destroy(app);
    return 0;
}
