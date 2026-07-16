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

#include <stdlib.h>
#include <string.h>

#include "internal.h"

enum { INDENT = 16, BOX = 9, PAD = 4 };

static int tree_row_h(MtkTree *t)
{
    return mtk_font_height(t->base.win->app->font) + 6;
}

/* Depth-first walk over visible rows. */
typedef struct {
    MtkTreeNode **rows;
    int *depths;
    int n, cap;
} RowList;

static void collect(RowList *rl, MtkTreeNode *n, int depth)
{
    if (rl->n == rl->cap) {
        rl->cap = rl->cap ? rl->cap * 2 : 64;
        rl->rows = realloc(rl->rows, sizeof(*rl->rows) * (size_t)rl->cap);
        rl->depths = realloc(rl->depths,
                             sizeof(*rl->depths) * (size_t)rl->cap);
    }
    rl->rows[rl->n] = n;
    rl->depths[rl->n] = depth;
    rl->n++;
    if (n->expanded)
        for (int i = 0; i < n->nkids; i++)
            collect(rl, n->kids[i], depth + 1);
}

static void tree_rows(MtkTree *t, RowList *rl)
{
    memset(rl, 0, sizeof(*rl));
    for (int i = 0; i < t->nroot->nkids; i++)
        collect(rl, t->nroot->kids[i], 0);
}

static int tree_rows_visible(MtkTree *t)
{
    int rows = (t->base.h - 2 * MTK_BEVEL) / tree_row_h(t);
    return rows > 0 ? rows : 1;
}

static void tree_sync_scrollbar(MtkTree *t, int total)
{
    MtkWidget *w = &t->base;
    int rows = tree_rows_visible(t);
    bool need = total > rows;
    mtk_widget_set_visible(&t->vbar->base, need);
    if (need) {
        mtk_widget_set_rect(&t->vbar->base,
                            w->x + w->w - MTK_SCROLLBAR_W - MTK_BEVEL,
                            w->y + MTK_BEVEL, MTK_SCROLLBAR_W,
                            w->h - 2 * MTK_BEVEL);
        mtk_scrollbar_config(t->vbar, 0, total, rows, 1);
    } else {
        mtk_scrollbar_set_value(t->vbar, 0);
    }
}

static void tree_draw(MtkWidget *w)
{
    MtkTree *t = (MtkTree *)w;
    MtkWindow *win = w->win;
    MtkApp *app = win->app;
    MtkPalette *p = &app->pal;
    Display *dpy = app->dpy;

    RowList rl;
    tree_rows(t, &rl);
    tree_sync_scrollbar(t, rl.n);

    mtk_fill_rect(win, w->x, w->y, w->w, w->h, p->surface);
    mtk_draw_bevel_c(win, w->x, w->y, w->w, w->h, MTK_BEVEL, true,
                     p->surface_top, p->surface_bottom);

    int rh = tree_row_h(t);
    int rows = tree_rows_visible(t);
    int top = t->vbar->value;
    int text_w = w->w - 2 * MTK_BEVEL -
                 (t->vbar->base.visible ? MTK_SCROLLBAR_W : 0);

    mtk_set_clip(win, w->x + MTK_BEVEL, w->y + MTK_BEVEL, text_w,
                 w->h - 2 * MTK_BEVEL);
    for (int i = 0; i < rows && top + i < rl.n; i++) {
        MtkTreeNode *n = rl.rows[top + i];
        int depth = rl.depths[top + i];
        int ry = w->y + MTK_BEVEL + i * rh;
        int bx = w->x + MTK_BEVEL + PAD + depth * INDENT;
        int by = ry + (rh - BOX) / 2;

        if (!n->leaf) {
            /* Motif expander: small body-colored box with +/-. */
            GC gc = win->gc;
            XSetForeground(dpy, gc, p->bg);
            XFillRectangle(dpy, win->back, gc, bx, by, BOX, BOX);
            XSetForeground(dpy, gc, p->surface_bottom);
            XDrawRectangle(dpy, win->back, gc, bx, by, BOX - 1, BOX - 1);
            XSetForeground(dpy, gc, p->text);
            XDrawLine(dpy, win->back, gc, bx + 2, by + BOX / 2,
                      bx + BOX - 3, by + BOX / 2);
            if (!n->expanded)
                XDrawLine(dpy, win->back, gc, bx + BOX / 2, by + 2,
                          bx + BOX / 2, by + BOX - 3);
        }

        int tx = bx + BOX + PAD;
        if (n == t->selected) {
            int lw = mtk_text_width(app->font, n->label);
            mtk_fill_rect(win, tx - 2, ry, lw + 4, rh, p->primary);
            mtk_draw_text_centered(win, app->font, tx, ry, rh, n->label,
                                   p->primary_text);
        } else {
            mtk_draw_text_centered(win, app->font, tx, ry, rh, n->label,
                                   p->surface_text);
        }
    }
    mtk_clear_clip(win);
    free(rl.rows);
    free(rl.depths);
}

void mtk_tree_expand(MtkTree *t, MtkTreeNode *n, bool expand)
{
    if (n->leaf)
        return;
    if (expand && !n->loaded) {
        n->loaded = true;
        if (t->on_expand)
            t->on_expand(t, n, t->data);
    }
    n->expanded = expand;
    mtk_window_damage(t->base.win);
}

void mtk_tree_select(MtkTree *t, MtkTreeNode *n)
{
    t->selected = n;
    mtk_window_damage(t->base.win);
    if (t->on_select && n)
        t->on_select(t, n, t->data);
}

static bool tree_event(MtkWidget *w, XEvent *ev)
{
    MtkTree *t = (MtkTree *)w;

    if (ev->type != ButtonPress)
        return false;
    int b = (int)ev->xbutton.button;
    if (b == Button4 || b == Button5) {
        mtk_scrollbar_set_value(t->vbar,
                                t->vbar->value + (b == Button4 ? -3 : 3));
        return true;
    }
    if (b != Button1)
        return false;

    RowList rl;
    tree_rows(t, &rl);
    int rh = tree_row_h(t);
    int row = t->vbar->value + (ev->xbutton.y - w->y - MTK_BEVEL) / rh;
    if (row < 0 || row >= rl.n) {
        free(rl.rows);
        free(rl.depths);
        return true;
    }
    MtkTreeNode *n = rl.rows[row];
    int depth = rl.depths[row];
    int bx = w->x + MTK_BEVEL + PAD + depth * INDENT;

    if (!n->leaf && ev->xbutton.x >= bx - 2 &&
        ev->xbutton.x < bx + BOX + 2) {
        mtk_tree_expand(t, n, !n->expanded);
    } else {
        if (w->win->click_double && !n->leaf)
            mtk_tree_expand(t, n, !n->expanded);
        mtk_tree_select(t, n);
    }
    free(rl.rows);
    free(rl.depths);
    return true;
}

static void node_free(MtkTreeNode *n)
{
    for (int i = 0; i < n->nkids; i++)
        node_free(n->kids[i]);
    free(n->kids);
    free(n->label);
    free(n);
}

static bool node_has(MtkTreeNode *root, MtkTreeNode *n)
{
    if (root == n)
        return true;
    for (int i = 0; i < root->nkids; i++)
        if (node_has(root->kids[i], n))
            return true;
    return false;
}

void mtk_tree_node_clear(MtkTree *t, MtkTreeNode *n)
{
    if (t->selected && t->selected != n && node_has(n, t->selected))
        t->selected = nullptr;
    for (int i = 0; i < n->nkids; i++)
        node_free(n->kids[i]);
    n->nkids = 0;
    mtk_window_damage(t->base.win);
}

static void tree_destroy(MtkWidget *w)
{
    MtkTree *t = (MtkTree *)w;
    node_free(t->nroot);
    free(t);
}

static const MtkWidgetOps tree_ops = {
    .draw = tree_draw,
    .event = tree_event,
    .destroy = tree_destroy,
};

static void tree_vbar_change(MtkScrollbar *sb, void *data)
{
    (void)sb;
    mtk_window_damage(((MtkTree *)data)->base.win);
}

MtkTree *mtk_tree_create(MtkWindow *win, MtkWidget *parent)
{
    MtkTree *t = calloc(1, sizeof(*t));
    mtk_widget_init(&t->base, win, parent, &tree_ops);
    t->nroot = calloc(1, sizeof(*t->nroot));
    t->nroot->label = mtk_strdup("");
    t->nroot->expanded = true;
    t->nroot->loaded = true;
    t->vbar = mtk_scrollbar_create(win, &t->base, false);
    t->vbar->on_change = tree_vbar_change;
    t->vbar->data = t;
    return t;
}

MtkTreeNode *mtk_tree_node_add(MtkTree *t, MtkTreeNode *parent,
                               const char *label, void *user)
{
    if (!parent)
        parent = t->nroot;
    MtkTreeNode *n = calloc(1, sizeof(*n));
    n->label = mtk_strdup(label);
    n->user = user;
    n->parent = parent;
    if (parent->nkids == parent->kidcap) {
        parent->kidcap = parent->kidcap ? parent->kidcap * 2 : 8;
        parent->kids = realloc(parent->kids,
                               sizeof(MtkTreeNode *) * (size_t)parent->kidcap);
    }
    parent->kids[parent->nkids++] = n;
    mtk_window_damage(t->base.win);
    return n;
}
