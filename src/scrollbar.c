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

#include "internal.h"

enum { ARROW = MTK_SCROLLBAR_W };

static int sb_span(MtkScrollbar *sb)
{
    MtkWidget *w = &sb->base;
    return (sb->horizontal ? w->w : w->h) - 2 * ARROW;
}

static int sb_range(MtkScrollbar *sb)
{
    int r = sb->maxval - sb->minval;
    return r > 0 ? r : 1;
}

static void sb_thumb(MtkScrollbar *sb, int *pos, int *size)
{
    int span = sb_span(sb);
    int range = sb_range(sb);
    int page = sb->page > 0 ? sb->page : range;
    if (page > range)
        page = range;
    *size = span * page / range;
    if (*size < 12)
        *size = 12;
    if (*size > span)
        *size = span;
    int max_val = sb->maxval - page;
    int denom = max_val - sb->minval;
    *pos = denom > 0 ? (span - *size) * (sb->value - sb->minval) / denom : 0;
}

static void sb_set(MtkScrollbar *sb, int value)
{
    int page = sb->page > 0 ? sb->page : 0;
    int hi = sb->maxval - page;
    if (hi < sb->minval)
        hi = sb->minval;
    if (value < sb->minval)
        value = sb->minval;
    if (value > hi)
        value = hi;
    if (value == sb->value)
        return;
    sb->value = value;
    mtk_window_damage(sb->base.win);
    if (sb->on_change)
        sb->on_change(sb, sb->data);
}

static void sb_draw(MtkWidget *w)
{
    MtkScrollbar *sb = (MtkScrollbar *)w;
    MtkWindow *win = w->win;
    MtkPalette *p = &win->app->pal;

    /* Trough. */
    mtk_fill_rect(win, w->x, w->y, w->w, w->h, p->select);
    mtk_draw_bevel(win, w->x, w->y, w->w, w->h, 1, true);

    int pos, size;
    sb_thumb(sb, &pos, &size);

    if (sb->horizontal) {
        mtk_fill_rect(win, w->x, w->y, ARROW, w->h, p->bg);
        mtk_draw_bevel(win, w->x, w->y, ARROW, w->h, MTK_BEVEL, sb->arm == 1);
        mtk_draw_arrow(win, w->x + 5, w->y + 5, ARROW - 10, w->h - 10,
                       MTK_ARROW_LEFT, sb->arm == 1);
        mtk_fill_rect(win, w->x + w->w - ARROW, w->y, ARROW, w->h, p->bg);
        mtk_draw_bevel(win, w->x + w->w - ARROW, w->y, ARROW, w->h, MTK_BEVEL,
                       sb->arm == 2);
        mtk_draw_arrow(win, w->x + w->w - ARROW + 5, w->y + 5, ARROW - 10,
                       w->h - 10, MTK_ARROW_RIGHT, sb->arm == 2);
        int tx = w->x + ARROW + pos;
        mtk_fill_rect(win, tx, w->y + 2, size, w->h - 4, p->bg);
        mtk_draw_bevel(win, tx, w->y + 2, size, w->h - 4, MTK_BEVEL, false);
    } else {
        mtk_fill_rect(win, w->x, w->y, w->w, ARROW, p->bg);
        mtk_draw_bevel(win, w->x, w->y, w->w, ARROW, MTK_BEVEL, sb->arm == 1);
        mtk_draw_arrow(win, w->x + 5, w->y + 5, w->w - 10, ARROW - 10,
                       MTK_ARROW_UP, sb->arm == 1);
        mtk_fill_rect(win, w->x, w->y + w->h - ARROW, w->w, ARROW, p->bg);
        mtk_draw_bevel(win, w->x, w->y + w->h - ARROW, w->w, ARROW, MTK_BEVEL,
                       sb->arm == 2);
        mtk_draw_arrow(win, w->x + 5, w->y + w->h - ARROW + 5, w->w - 10,
                       ARROW - 10, MTK_ARROW_DOWN, sb->arm == 2);
        int ty = w->y + ARROW + pos;
        mtk_fill_rect(win, w->x + 2, ty, w->w - 4, size, p->bg);
        mtk_draw_bevel(win, w->x + 2, ty, w->w - 4, size, MTK_BEVEL, false);
    }
}

static int sb_value_for_thumb_pos(MtkScrollbar *sb, int pos)
{
    int span = sb_span(sb);
    int tpos, tsize;
    sb_thumb(sb, &tpos, &tsize);
    int track = span - tsize;
    if (track <= 0)
        return sb->minval;
    int page = sb->page > 0 ? sb->page : 0;
    int hi = sb->maxval - page;
    return sb->minval + (int)((long)pos * (hi - sb->minval) / track);
}

static bool sb_event(MtkWidget *w, XEvent *ev)
{
    MtkScrollbar *sb = (MtkScrollbar *)w;

    switch (ev->type) {
    case ButtonPress: {
        int b = (int)ev->xbutton.button;
        if (b == Button4) {
            sb_set(sb, sb->value - sb->line * 3);
            return true;
        }
        if (b == Button5) {
            sb_set(sb, sb->value + sb->line * 3);
            return true;
        }
        if (b != Button1)
            return false;
        int c = sb->horizontal ? ev->xbutton.x - w->x : ev->xbutton.y - w->y;
        int extent = sb->horizontal ? w->w : w->h;
        int pos, size;
        sb_thumb(sb, &pos, &size);
        if (c < ARROW) {
            sb->arm = 1;
            sb_set(sb, sb->value - sb->line);
        } else if (c >= extent - ARROW) {
            sb->arm = 2;
            sb_set(sb, sb->value + sb->line);
        } else if (c - ARROW >= pos && c - ARROW < pos + size) {
            sb->dragging = true;
            sb->drag_off = c - ARROW - pos;
        } else if (c - ARROW < pos) {
            sb_set(sb, sb->value - (sb->page > 0 ? sb->page : sb->line));
        } else {
            sb_set(sb, sb->value + (sb->page > 0 ? sb->page : sb->line));
        }
        mtk_window_damage(w->win);
        return true;
    }
    case MotionNotify:
        if (sb->dragging) {
            int c = sb->horizontal ? ev->xmotion.x - w->x
                                   : ev->xmotion.y - w->y;
            sb_set(sb, sb_value_for_thumb_pos(sb, c - ARROW - sb->drag_off));
        }
        return true;
    case ButtonRelease:
        sb->dragging = false;
        if (sb->arm) {
            sb->arm = 0;
            mtk_window_damage(w->win);
        }
        return true;
    }
    return false;
}

static void sb_destroy(MtkWidget *w)
{
    free(w);
}

static const MtkWidgetOps sb_ops = {
    .draw = sb_draw,
    .event = sb_event,
    .destroy = sb_destroy,
};

MtkScrollbar *mtk_scrollbar_create(MtkWindow *win, MtkWidget *parent,
                                   bool horizontal)
{
    MtkScrollbar *sb = calloc(1, sizeof(*sb));
    mtk_widget_init(&sb->base, win, parent, &sb_ops);
    sb->horizontal = horizontal;
    sb->minval = 0;
    sb->maxval = 100;
    sb->page = 100;
    sb->line = 10;
    return sb;
}

void mtk_scrollbar_config(MtkScrollbar *sb, int minval, int maxval,
                          int page, int line)
{
    sb->minval = minval;
    sb->maxval = maxval > minval ? maxval : minval;
    sb->page = page;
    sb->line = line > 0 ? line : 1;
    int page_c = sb->page > 0 ? sb->page : 0;
    int hi = sb->maxval - page_c;
    if (hi < sb->minval)
        hi = sb->minval;
    if (sb->value < sb->minval)
        sb->value = sb->minval;
    if (sb->value > hi)
        sb->value = hi;
    mtk_window_damage(sb->base.win);
}

void mtk_scrollbar_set_value(MtkScrollbar *sb, int value)
{
    sb_set(sb, value);
}
