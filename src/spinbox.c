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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "internal.h"

enum { ARROWS_W = 18 };

static void spin_clamp_and_show(MtkSpinbox *s, int v)
{
    if (v < s->minval)
        v = s->minval;
    if (v > s->maxval)
        v = s->maxval;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", v);
    mtk_entry_set_text(s->entry, buf);
    if (s->on_change)
        s->on_change(s, s->data);
}

int mtk_spinbox_value(MtkSpinbox *s)
{
    int v = atoi(mtk_entry_text(s->entry));
    if (v < s->minval)
        v = s->minval;
    if (v > s->maxval)
        v = s->maxval;
    return v;
}

void mtk_spinbox_set_value(MtkSpinbox *s, int value)
{
    spin_clamp_and_show(s, value);
}

static void spin_step(MtkSpinbox *s, int dir)
{
    spin_clamp_and_show(s, mtk_spinbox_value(s) + dir);
}

static void spin_layout(MtkSpinbox *s)
{
    MtkWidget *w = &s->base;
    mtk_widget_set_rect(&s->entry->base, w->x, w->y, w->w - ARROWS_W, w->h);
}

static void spin_draw(MtkWidget *w)
{
    MtkSpinbox *s = (MtkSpinbox *)w;
    MtkWindow *win = w->win;
    MtkPalette *p = &win->app->pal;
    spin_layout(s); /* track geometry changes */

    int ax = w->x + w->w - ARROWS_W;
    int half = w->h / 2;

    /* Joined up/down arrow buttons. */
    mtk_fill_rect(win, ax, w->y, ARROWS_W, w->h, p->bg);
    mtk_draw_bevel(win, ax, w->y, ARROWS_W, half, MTK_BEVEL, s->arm == 1);
    mtk_draw_bevel(win, ax, w->y + half, ARROWS_W, w->h - half, MTK_BEVEL,
                   s->arm == 2);
    mtk_draw_arrow(win, ax + 5, w->y + 4, ARROWS_W - 10, half - 7,
                   MTK_ARROW_UP, s->arm == 1);
    mtk_draw_arrow(win, ax + 5, w->y + half + 3, ARROWS_W - 10,
                   w->h - half - 7, MTK_ARROW_DOWN, s->arm == 2);
}

static bool spin_event(MtkWidget *w, XEvent *ev)
{
    MtkSpinbox *s = (MtkSpinbox *)w;
    int ax = w->x + w->w - ARROWS_W;
    int half = w->h / 2;

    switch (ev->type) {
    case ButtonPress:
        if (ev->xbutton.button == Button4) {
            spin_step(s, 1);
            return true;
        }
        if (ev->xbutton.button == Button5) {
            spin_step(s, -1);
            return true;
        }
        if (ev->xbutton.button != Button1 || ev->xbutton.x < ax)
            return false;
        s->arm = ev->xbutton.y < w->y + half ? 1 : 2;
        mtk_window_damage(w->win);
        return true;
    case ButtonRelease:
        if (s->arm) {
            int arm = s->arm;
            s->arm = 0;
            mtk_window_damage(w->win);
            if (mtk_widget_contains(w, ev->xbutton.x, ev->xbutton.y))
                spin_step(s, arm == 1 ? 1 : -1);
        }
        return true;
    }
    return false;
}

static bool spin_validate(MtkEntry *e, const char *ch, void *data)
{
    (void)e;
    (void)data;
    return ch[0] >= '0' && ch[0] <= '9' && ch[1] == '\0';
}

static void spin_destroy(MtkWidget *w)
{
    free(w);
}

static const MtkWidgetOps spin_ops = {
    .draw = spin_draw,
    .event = spin_event,
    .destroy = spin_destroy,
};

MtkSpinbox *mtk_spinbox_create(MtkWindow *win, MtkWidget *parent,
                               int minval, int maxval, int value)
{
    MtkSpinbox *s = calloc(1, sizeof(*s));
    mtk_widget_init(&s->base, win, parent, &spin_ops);
    s->minval = minval;
    s->maxval = maxval;
    s->entry = mtk_entry_create(win, &s->base);
    s->entry->validate = spin_validate;
    spin_clamp_and_show(s, value);
    return s;
}
