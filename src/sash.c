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

static void sash_draw(MtkWidget *w)
{
    MtkWindow *win = w->win;
    MtkPalette *p = &win->app->pal;
    mtk_fill_rect(win, w->x, w->y, w->w, w->h, p->bg);
    mtk_draw_bevel(win, w->x, w->y, w->w, w->h, 1, false);
    /* Motif sash grip. */
    int gy = w->y + w->h / 2 - 12;
    mtk_fill_rect(win, w->x, gy, w->w, 24, p->bg);
    mtk_draw_bevel(win, w->x - 1, gy, w->w + 2, 24, MTK_BEVEL, false);
}

static bool sash_event(MtkWidget *w, XEvent *ev)
{
    MtkSash *s = (MtkSash *)w;
    switch (ev->type) {
    case ButtonPress:
        if (ev->xbutton.button != Button1)
            return false;
        s->dragging = true;
        s->grab_dx = ev->xbutton.x - w->x;
        return true;
    case MotionNotify:
        if (s->dragging) {
            int nx = ev->xmotion.x - s->grab_dx;
            if (nx < s->min_x)
                nx = s->min_x;
            if (s->max_x > s->min_x && nx > s->max_x)
                nx = s->max_x;
            if (nx != w->x && s->on_drag)
                s->on_drag(s, nx, s->data);
        }
        return true;
    case ButtonRelease:
        s->dragging = false;
        return true;
    }
    return false;
}

static void sash_destroy(MtkWidget *w)
{
    free(w);
}

static void sash_measure(MtkWidget *w, int *nw, int *nh)
{
    (void)w;
    *nw = MTK_SASH_W;
    *nh = -1;
}

static const MtkWidgetOps sash_ops = {
    .draw = sash_draw,
    .event = sash_event,
    .destroy = sash_destroy,
    .measure = sash_measure,
};

MtkSash *mtk_sash_create(MtkWindow *win, MtkWidget *parent,
                         void (*on_drag)(MtkSash *s, int new_x, void *data),
                         void *data)
{
    MtkSash *s = calloc(1, sizeof(*s));
    mtk_widget_init(&s->base, win, parent, &sash_ops);
    s->on_drag = on_drag;
    s->data = data;
    return s;
}
