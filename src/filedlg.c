/*
 * This file is part of libmtk.
 * Copyright (C) 2026 Martin Lind
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation
 * (no later version applies).
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 */

/*
 * File selection dialog, in the spirit of XmFileSelectionBox: a
 * directory entry, side-by-side directory and file lists, a
 * selection entry and OK/Cancel.  The dialog owns its toplevel
 * window and frees itself; the application only keeps the pointer
 * to dismiss it early or to tell dialogs apart.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "internal.h"

enum {
    FD_MARGIN = 10,
    FD_GAP = 8,
    FD_ROW_H = 26,
    FD_LABEL_H = 18,
    FD_BTN_W = 90,
    FD_MIN_W = 340,
    FD_MIN_H = 300,
};

static bool fd_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* "jpg jpeg png" — case-insensitive extension list, nullptr = all. */
static bool fd_filter_match(const char *filter, const char *name)
{
    if (!filter)
        return true;
    const char *dot = strrchr(name, '.');
    if (!dot || !dot[1])
        return false;
    const char *ext = dot + 1;
    size_t elen = strlen(ext);
    for (const char *p = filter; *p;) {
        while (*p == ' ')
            p++;
        const char *q = p;
        while (*q && *q != ' ')
            q++;
        if ((size_t)(q - p) == elen && !strncasecmp(p, ext, elen))
            return true;
        p = q;
    }
    return false;
}

static int fd_cmp_names(const void *a, const void *b)
{
    return strcoll(*(char *const *)a, *(char *const *)b);
}

typedef struct FdNames {
    char **v;
    int n, cap;
} FdNames;

static void fd_names_add(FdNames *ns, const char *name)
{
    if (ns->n == ns->cap) {
        ns->cap = ns->cap ? ns->cap * 2 : 32;
        ns->v = realloc(ns->v, sizeof(char *) * (size_t)ns->cap);
    }
    ns->v[ns->n++] = mtk_strdup(name);
}

static void fd_names_free(FdNames *ns)
{
    for (int i = 0; i < ns->n; i++)
        free(ns->v[i]);
    free(ns->v);
    memset(ns, 0, sizeof(*ns));
}

static void fd_scan(MtkFileDialog *fd)
{
    mtk_listbox_clear(fd->dirs);
    mtk_listbox_clear(fd->files);
    mtk_entry_set_text(fd->dir_entry, fd->dir);

    if (strcmp(fd->dir, "/") != 0)
        mtk_listbox_add(fd->dirs, "..");

    DIR *d = opendir(fd->dir);
    if (!d)
        return;
    FdNames dirs = {0}, files = {0};
    const struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue; /* hidden, "." and ".." */
        char path[4400];
        snprintf(path, sizeof(path), "%s/%s", fd->dir, de->d_name);
        if (fd_is_dir(path))
            fd_names_add(&dirs, de->d_name);
        else if (fd_filter_match(fd->filter, de->d_name))
            fd_names_add(&files, de->d_name);
    }
    closedir(d);
    qsort(dirs.v, (size_t)dirs.n, sizeof(char *), fd_cmp_names);
    qsort(files.v, (size_t)files.n, sizeof(char *), fd_cmp_names);
    for (int i = 0; i < dirs.n; i++)
        mtk_listbox_add(fd->dirs, dirs.v[i]);
    for (int i = 0; i < files.n; i++)
        mtk_listbox_add(fd->files, files.v[i]);
    fd_names_free(&dirs);
    fd_names_free(&files);
}

static void fd_set_dir(MtkFileDialog *fd, const char *path)
{
    if (path != fd->dir) /* also called on fd->dir itself */
        snprintf(fd->dir, sizeof(fd->dir), "%s", path);
    /* strip trailing slashes, keep the root one */
    size_t len = strlen(fd->dir);
    while (len > 1 && fd->dir[len - 1] == '/')
        fd->dir[--len] = '\0';
    fd_scan(fd);
}

/* one component up; "/" stays "/" */
static void fd_go_up(MtkFileDialog *fd)
{
    char *s = strrchr(fd->dir, '/');
    if (!s)
        return;
    if (s == fd->dir)
        s[1] = '\0';
    else
        *s = '\0';
    fd_scan(fd);
}

static void fd_descend(MtkFileDialog *fd, const char *name)
{
    if (!strcmp(name, "..")) {
        fd_go_up(fd);
        return;
    }
    char path[4400];
    snprintf(path, sizeof(path), "%s%s%s", fd->dir,
             !strcmp(fd->dir, "/") ? "" : "/", name);
    fd_set_dir(fd, path);
}

/* Fire on_done exactly once, then tear the dialog down. */
static void fd_done(MtkFileDialog *fd, const char *path)
{
    if (fd->done)
        return;
    fd->done = true;
    if (fd->on_done)
        fd->on_done(fd, path, fd->data);
    mtk_window_destroy(fd->win);
}

static void fd_finish(MtkFileDialog *fd)
{
    const char *name = mtk_entry_text(fd->sel_entry);
    if (!name[0]) {
        /* Motif behaviour: OK on a highlighted directory descends. */
        if (fd->dirs->selected >= 0)
            fd_descend(fd, fd->dirs->items[fd->dirs->selected]);
        return;
    }
    char path[4400];
    if (name[0] == '/')
        snprintf(path, sizeof(path), "%s", name);
    else
        snprintf(path, sizeof(path), "%s%s%s", fd->dir,
                 !strcmp(fd->dir, "/") ? "" : "/", name);
    if (fd_is_dir(path)) {
        mtk_entry_set_text(fd->sel_entry, "");
        fd_set_dir(fd, path);
        return;
    }
    struct stat st;
    if (fd->mode == MTK_FILEDLG_OPEN && stat(path, &st) != 0)
        return; /* no such file; stay up */
    fd_done(fd, path);
}

/* ------------------------------------------------------------- callbacks */

static void fd_dir_activate(MtkEntry *e, void *data)
{
    MtkFileDialog *fd = data;
    const char *path = mtk_entry_text(e);
    if (fd_is_dir(path))
        fd_set_dir(fd, path);
    else
        mtk_entry_set_text(e, fd->dir); /* revert */
}

static void fd_sel_activate(MtkEntry *e, void *data)
{
    (void)e;
    fd_finish(data);
}

static void fd_dirs_activate(MtkListbox *lb, int index, void *data)
{
    MtkFileDialog *fd = data;
    if (index >= 0 && index < lb->nitems)
        fd_descend(fd, lb->items[index]);
}

static void fd_files_select(MtkListbox *lb, int index, void *data)
{
    MtkFileDialog *fd = data;
    if (index >= 0 && index < lb->nitems)
        mtk_entry_set_text(fd->sel_entry, lb->items[index]);
}

static void fd_files_activate(MtkListbox *lb, int index, void *data)
{
    MtkFileDialog *fd = data;
    if (index >= 0 && index < lb->nitems) {
        mtk_entry_set_text(fd->sel_entry, lb->items[index]);
        fd_finish(fd);
    }
}

static void fd_ok_click(MtkButton *b, void *data)
{
    (void)b;
    fd_finish(data);
}

static void fd_cancel_click(MtkButton *b, void *data)
{
    MtkFileDialog *fd = data;
    (void)b;
    mtk_window_destroy(fd->win); /* on_destroy fires the cancel */
}

static bool fd_win_key(MtkWindow *win, XKeyEvent *ev, KeySym sym,
                       const char *text)
{
    (void)ev;
    (void)text;
    if (sym == XK_Escape) {
        mtk_window_destroy(win);
        return true;
    }
    return false;
}

static void fd_win_destroyed(MtkWindow *win)
{
    MtkFileDialog *fd = win->user;
    if (!fd->done && fd->on_done)
        fd->on_done(fd, nullptr, fd->data);
    free(fd->filter);
    free(fd);
}

/* ---------------------------------------------------------------- layout */

static void fd_layout(MtkWindow *win)
{
    MtkFileDialog *fd = win->user;
    int W = win->w > FD_MIN_W ? win->w : FD_MIN_W;
    int H = win->h > FD_MIN_H ? win->h : FD_MIN_H;
    int cw = W - 2 * FD_MARGIN;

    int y = FD_MARGIN;
    mtk_widget_set_rect(&fd->dir_label->base, FD_MARGIN, y, cw, FD_LABEL_H);
    y += FD_LABEL_H + 2;
    mtk_widget_set_rect(&fd->dir_entry->base, FD_MARGIN, y, cw, FD_ROW_H);
    y += FD_ROW_H + FD_GAP;

    int by = H - FD_MARGIN - FD_ROW_H;              /* button row */
    int sy = by - FD_GAP - FD_ROW_H;                /* selection entry */
    int sly = sy - 2 - FD_LABEL_H;                  /* selection label */
    int lh = sly - FD_GAP - y - FD_LABEL_H - 2;     /* list height */
    if (lh < 40)
        lh = 40;

    int lw = (cw - FD_GAP) / 2;
    mtk_widget_set_rect(&fd->dirs_label->base, FD_MARGIN, y, lw, FD_LABEL_H);
    mtk_widget_set_rect(&fd->files_label->base, FD_MARGIN + lw + FD_GAP, y,
                        cw - lw - FD_GAP, FD_LABEL_H);
    y += FD_LABEL_H + 2;
    mtk_widget_set_rect(&fd->dirs->base, FD_MARGIN, y, lw, lh);
    mtk_widget_set_rect(&fd->files->base, FD_MARGIN + lw + FD_GAP, y,
                        cw - lw - FD_GAP, lh);

    mtk_widget_set_rect(&fd->sel_label->base, FD_MARGIN, sly, cw,
                        FD_LABEL_H);
    mtk_widget_set_rect(&fd->sel_entry->base, FD_MARGIN, sy, cw, FD_ROW_H);

    mtk_widget_set_rect(&fd->b_ok->base, FD_MARGIN, by, FD_BTN_W, FD_ROW_H);
    mtk_widget_set_rect(&fd->b_cancel->base, W - FD_MARGIN - FD_BTN_W, by,
                        FD_BTN_W, FD_ROW_H);
}

/* ------------------------------------------------------------------- api */

MtkFileDialog *mtk_file_dialog(MtkApp *app, const char *title,
                               MtkFileDialogMode mode,
                               const char *start_path, const char *filter,
                               void (*on_done)(MtkFileDialog *fd,
                                               const char *path, void *data),
                               void *data)
{
    MtkFileDialog *fd = calloc(1, sizeof(*fd));
    fd->mode = mode;
    fd->filter = filter ? mtk_strdup(filter) : nullptr;
    fd->on_done = on_done;
    fd->data = data;

    /* split start_path into directory + preset selection */
    const char *home = getenv("HOME");
    char name[512] = "";
    if (start_path && fd_is_dir(start_path)) {
        snprintf(fd->dir, sizeof(fd->dir), "%s", start_path);
    } else if (start_path && start_path[0]) {
        const char *slash = strrchr(start_path, '/');
        if (slash) {
            snprintf(name, sizeof(name), "%s", slash + 1);
            size_t dlen = slash == start_path
                              ? 1
                              : (size_t)(slash - start_path);
            if (dlen >= sizeof(fd->dir))
                dlen = sizeof(fd->dir) - 1;
            memcpy(fd->dir, start_path, dlen);
            fd->dir[dlen] = '\0';
        } else {
            snprintf(name, sizeof(name), "%s", start_path);
        }
    }
    if (!fd->dir[0] || !fd_is_dir(fd->dir))
        snprintf(fd->dir, sizeof(fd->dir), "%s",
                 home && fd_is_dir(home) ? home : "/");

    MtkWindow *win = mtk_window_create(app, title ? title : "Select File",
                                       460, 420);
    fd->win = win;
    win->user = fd;
    win->on_resize = fd_layout;
    win->on_key = fd_win_key;
    win->on_destroy = fd_win_destroyed;

    fd->dir_label = mtk_label_create(win, nullptr, "Directory:");
    fd->dir_entry = mtk_entry_create(win, nullptr);
    fd->dir_entry->on_activate = fd_dir_activate;
    fd->dir_entry->data = fd;

    fd->dirs_label = mtk_label_create(win, nullptr, "Directories");
    fd->files_label = mtk_label_create(win, nullptr, "Files");
    fd->dirs = mtk_listbox_create(win, nullptr);
    fd->dirs->on_activate = fd_dirs_activate;
    fd->dirs->data = fd;
    fd->files = mtk_listbox_create(win, nullptr);
    fd->files->on_select = fd_files_select;
    fd->files->on_activate = fd_files_activate;
    fd->files->data = fd;

    fd->sel_label = mtk_label_create(win, nullptr, "Selection:");
    fd->sel_entry = mtk_entry_create(win, nullptr);
    fd->sel_entry->on_activate = fd_sel_activate;
    fd->sel_entry->data = fd;
    if (name[0])
        mtk_entry_set_text(fd->sel_entry, name);

    fd->b_ok = mtk_button_create(win, nullptr, "OK", fd_ok_click, fd);
    fd->b_cancel = mtk_button_create(win, nullptr, "Cancel",
                                     fd_cancel_click, fd);

    fd_set_dir(fd, fd->dir);
    fd_layout(win);
    mtk_window_set_focus(win, &fd->sel_entry->base);
    mtk_window_show(win);
    return fd;
}

void mtk_file_dialog_close(MtkFileDialog *fd)
{
    mtk_window_destroy(fd->win); /* cancel fires from on_destroy */
}
