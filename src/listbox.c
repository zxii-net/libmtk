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

enum { PAD = 4, DRAG_SLOP = 4 };

static int lb_row_h(MtkListbox *lb)
{
    return mtk_font_height(lb->base.win->app->font) + 6;
}

static int lb_rows_visible(MtkListbox *lb)
{
    int rows = (lb->base.h - 2 * MTK_BEVEL) / lb_row_h(lb);
    return rows > 0 ? rows : 1;
}

static int lb_row_at(MtkListbox *lb, int y)
{
    int row = lb->vbar->value +
              (y - lb->base.y - MTK_BEVEL) / lb_row_h(lb);
    return row >= 0 && row < lb->nitems ? row : -1;
}

static void lb_sync_scrollbar(MtkListbox *lb)
{
    MtkWidget *w = &lb->base;
    int rows = lb_rows_visible(lb);
    bool need = lb->nitems > rows;
    mtk_widget_set_visible(&lb->vbar->base, need);
    if (need) {
        mtk_widget_set_rect(&lb->vbar->base,
                            w->x + w->w - MTK_SCROLLBAR_W - MTK_BEVEL,
                            w->y + MTK_BEVEL, MTK_SCROLLBAR_W,
                            w->h - 2 * MTK_BEVEL);
        mtk_scrollbar_config(lb->vbar, 0, lb->nitems, rows, 1);
    } else {
        mtk_scrollbar_set_value(lb->vbar, 0);
    }
}

static void lb_scroll_to(MtkListbox *lb, int index)
{
    int rows = lb_rows_visible(lb);
    if (index < lb->vbar->value)
        mtk_scrollbar_set_value(lb->vbar, index);
    else if (index >= lb->vbar->value + rows)
        mtk_scrollbar_set_value(lb->vbar, index - rows + 1);
}

static void lb_draw(MtkWidget *w)
{
    MtkListbox *lb = (MtkListbox *)w;
    MtkWindow *win = w->win;
    MtkApp *app = win->app;
    MtkPalette *p = &app->pal;

    lb_sync_scrollbar(lb);
    mtk_fill_rect(win, w->x, w->y, w->w, w->h, p->surface);
    mtk_draw_bevel_c(win, w->x, w->y, w->w, w->h, MTK_BEVEL, true,
                     p->surface_top, p->surface_bottom);

    int rh = lb_row_h(lb);
    int rows = lb_rows_visible(lb);
    int top = lb->vbar->value;
    int text_w = w->w - 2 * MTK_BEVEL -
                 (lb->vbar->base.visible ? MTK_SCROLLBAR_W : 0);

    mtk_set_clip(win, w->x + MTK_BEVEL, w->y + MTK_BEVEL, text_w,
                 w->h - 2 * MTK_BEVEL);
    for (int i = 0; i < rows && top + i < lb->nitems; i++) {
        int idx = top + i;
        int ry = w->y + MTK_BEVEL + i * rh;
        bool hilit = lb->multi ? lb->marked[idx] : idx == lb->selected;
        if (hilit) {
            mtk_fill_rect(win, w->x + MTK_BEVEL, ry, text_w, rh,
                          p->primary);
            mtk_draw_text_centered(win, app->font, w->x + MTK_BEVEL + PAD,
                                   ry, rh, lb->items[idx], p->primary_text);
        } else {
            mtk_draw_text_centered(win, app->font, w->x + MTK_BEVEL + PAD,
                                   ry, rh, lb->items[idx], p->surface_text);
        }
        if (lb->multi && idx == lb->selected) /* lead-row outline */
            mtk_draw_rect(win, w->x + MTK_BEVEL, ry, text_w, rh,
                          p->surface_text);
    }

    /* drag-reorder insertion indicator */
    if (lb->dragging) {
        int gy = w->y + MTK_BEVEL + (lb->drop_pos - top) * rh;
        mtk_fill_rect(win, w->x + MTK_BEVEL, gy - 1, text_w, 3,
                      p->active);
    }
    mtk_clear_clip(win);
}

static void lb_notify(MtkListbox *lb, int index)
{
    if (lb->on_select && index >= 0)
        lb->on_select(lb, index, lb->data);
}

static void lb_single_select(MtkListbox *lb, int index)
{
    if (lb->multi)
        for (int i = 0; i < lb->nitems; i++)
            lb->marked[i] = i == index;
    lb->selected = index;
    lb->anchor = index;
    mtk_window_damage(lb->base.win);
    lb_notify(lb, index);
}

static void lb_press_select(MtkListbox *lb, int row, unsigned state)
{
    if (lb->multi && (state & ControlMask)) {
        lb->marked[row] = !lb->marked[row];
        lb->selected = row;
        lb->anchor = row;
        mtk_window_damage(lb->base.win);
        lb_notify(lb, row);
    } else if (lb->multi && (state & ShiftMask) && lb->anchor >= 0) {
        int lo = lb->anchor < row ? lb->anchor : row;
        int hi = lb->anchor > row ? lb->anchor : row;
        for (int i = 0; i < lb->nitems; i++)
            lb->marked[i] = i >= lo && i <= hi;
        lb->selected = row;
        mtk_window_damage(lb->base.win);
        lb_notify(lb, row);
    } else {
        lb_single_select(lb, row);
    }
}

static void lb_move_row(MtkListbox *lb, int from, int gap)
{
    if (gap == from || gap == from + 1)
        return;
    int to = gap > from ? gap - 1 : gap;
    char *item = lb->items[from];
    bool mark = lb->marked[from];
    memmove(&lb->items[from], &lb->items[from + 1],
            sizeof(char *) * (size_t)(lb->nitems - from - 1));
    memmove(&lb->marked[from], &lb->marked[from + 1],
            sizeof(bool) * (size_t)(lb->nitems - from - 1));
    memmove(&lb->items[to + 1], &lb->items[to],
            sizeof(char *) * (size_t)(lb->nitems - to - 1));
    memmove(&lb->marked[to + 1], &lb->marked[to],
            sizeof(bool) * (size_t)(lb->nitems - to - 1));
    lb->items[to] = item;
    lb->marked[to] = mark;
    lb->selected = to;
    lb->anchor = to;
    mtk_window_damage(lb->base.win);
    if (lb->on_reorder)
        lb->on_reorder(lb, from, to, lb->data);
}

static bool lb_event(MtkWidget *w, XEvent *ev)
{
    MtkListbox *lb = (MtkListbox *)w;

    switch (ev->type) {
    case ButtonPress: {
        int b = (int)ev->xbutton.button;
        if (b == Button4 || b == Button5) {
            mtk_scrollbar_set_value(lb->vbar,
                                    lb->vbar->value + (b == Button4 ? -3 : 3));
            return true;
        }
        if (b != Button1)
            return false;
        int row = lb_row_at(lb, ev->xbutton.y);
        if (row < 0)
            return true;
        unsigned st = ev->xbutton.state;
        lb_press_select(lb, row, st);
        if (w->win->click_double && lb->on_activate) {
            lb->on_activate(lb, row, lb->data);
            return true;
        }
        if (lb->reorderable && !(st & (ControlMask | ShiftMask))) {
            lb->press_row = row;
            lb->press_y = ev->xbutton.y;
        }
        return true;
    }
    case MotionNotify:
        if (lb->reorderable && lb->press_row >= 0) {
            if (!lb->dragging &&
                abs(ev->xmotion.y - lb->press_y) > DRAG_SLOP)
                lb->dragging = true;
            if (lb->dragging) {
                int rh = lb_row_h(lb);
                int gap = lb->vbar->value +
                          (ev->xmotion.y - w->y - MTK_BEVEL + rh / 2) / rh;
                if (gap < 0)
                    gap = 0;
                if (gap > lb->nitems)
                    gap = lb->nitems;
                if (gap != lb->drop_pos) {
                    lb->drop_pos = gap;
                    mtk_window_damage(w->win);
                }
            }
        }
        return true;
    case ButtonRelease:
        if (lb->dragging) {
            lb->dragging = false;
            lb_move_row(lb, lb->press_row, lb->drop_pos);
        }
        lb->press_row = -1;
        mtk_window_damage(w->win);
        return true;
    }
    return false;
}

static bool lb_key(MtkWidget *w, XKeyEvent *ev, KeySym sym, const char *text)
{
    (void)ev;
    (void)text;
    MtkListbox *lb = (MtkListbox *)w;
    switch (sym) {
    case XK_Up:
        if (lb->selected > 0) {
            lb_single_select(lb, lb->selected - 1);
            lb_scroll_to(lb, lb->selected);
        }
        return true;
    case XK_Down:
        if (lb->selected < lb->nitems - 1) {
            lb_single_select(lb, lb->selected + 1);
            lb_scroll_to(lb, lb->selected);
        }
        return true;
    case XK_Return:
    case XK_KP_Enter:
        if (lb->selected >= 0 && lb->on_activate)
            lb->on_activate(lb, lb->selected, lb->data);
        return true;
    case XK_Delete:
    case XK_BackSpace:
        if (lb->selected >= 0 && lb->on_delete)
            lb->on_delete(lb, lb->selected, lb->data);
        return true;
    }
    return false;
}

static void lb_destroy(MtkWidget *w)
{
    MtkListbox *lb = (MtkListbox *)w;
    for (int i = 0; i < lb->nitems; i++)
        free(lb->items[i]);
    free(lb->items);
    free(lb->marked);
    free(lb);
}

static const MtkWidgetOps lb_ops = {
    .draw = lb_draw,
    .event = lb_event,
    .key = lb_key,
    .destroy = lb_destroy,
};

static void lb_vbar_change(MtkScrollbar *sb, void *data)
{
    (void)sb;
    mtk_window_damage(((MtkListbox *)data)->base.win);
}

MtkListbox *mtk_listbox_create(MtkWindow *win, MtkWidget *parent)
{
    MtkListbox *lb = calloc(1, sizeof(*lb));
    mtk_widget_init(&lb->base, win, parent, &lb_ops);
    lb->base.can_focus = true;
    lb->selected = -1;
    lb->anchor = -1;
    lb->press_row = -1;
    lb->vbar = mtk_scrollbar_create(win, &lb->base, false);
    lb->vbar->on_change = lb_vbar_change;
    lb->vbar->data = lb;
    return lb;
}

void mtk_listbox_add(MtkListbox *lb, const char *item)
{
    if (lb->nitems == lb->cap) {
        lb->cap = lb->cap ? lb->cap * 2 : 16;
        lb->items = realloc(lb->items, sizeof(char *) * (size_t)lb->cap);
        lb->marked = realloc(lb->marked, sizeof(bool) * (size_t)lb->cap);
    }
    lb->items[lb->nitems] = mtk_strdup(item);
    lb->marked[lb->nitems] = false;
    lb->nitems++;
    mtk_window_damage(lb->base.win);
}

void mtk_listbox_remove(MtkListbox *lb, int index)
{
    if (index < 0 || index >= lb->nitems)
        return;
    free(lb->items[index]);
    memmove(&lb->items[index], &lb->items[index + 1],
            sizeof(char *) * (size_t)(lb->nitems - index - 1));
    memmove(&lb->marked[index], &lb->marked[index + 1],
            sizeof(bool) * (size_t)(lb->nitems - index - 1));
    lb->nitems--;
    if (lb->selected >= lb->nitems)
        lb->selected = lb->nitems - 1;
    if (lb->anchor >= lb->nitems)
        lb->anchor = lb->nitems - 1;
    mtk_window_damage(lb->base.win);
}

void mtk_listbox_clear(MtkListbox *lb)
{
    for (int i = 0; i < lb->nitems; i++)
        free(lb->items[i]);
    lb->nitems = 0;
    lb->selected = -1;
    lb->anchor = -1;
    mtk_window_damage(lb->base.win);
}

bool mtk_listbox_any_marked(MtkListbox *lb)
{
    for (int i = 0; i < lb->nitems; i++)
        if (lb->marked[i])
            return true;
    return false;
}

void mtk_listbox_clear_marks(MtkListbox *lb)
{
    for (int i = 0; i < lb->nitems; i++)
        lb->marked[i] = false;
    mtk_window_damage(lb->base.win);
}
