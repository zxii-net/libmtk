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

enum { TAB_PAD = 14, TAB_GAP = 2 };

static int tab_width(MtkApp *app, const char *label)
{
    return mtk_text_width(app->font_bold, label) + 2 * TAB_PAD;
}

static void tabs_draw(MtkWidget *w)
{
    MtkTabs *t = (MtkTabs *)w;
    MtkWindow *win = w->win;
    MtkApp *app = win->app;
    MtkPalette *p = &app->pal;
    Display *dpy = app->dpy;

    int x = w->x + 2;
    int base_y = w->y + w->h - 1;

    for (int i = 0; i < t->ntabs; i++) {
        bool active = i == t->active;
        int tw = tab_width(app, t->labels[i]);
        int ty = active ? w->y : w->y + 3;
        int th = w->h - (active ? 0 : 3);

        mtk_fill_rect(win, x, ty, tw, th, active ? p->bg : p->select);
        /* Top and side bevels only; active tab opens into the body. */
        GC gc = win->gc;
        XSetForeground(dpy, gc, p->top_shadow);
        XDrawLine(dpy, win->back, gc, x, ty, x + tw - 2, ty);
        XDrawLine(dpy, win->back, gc, x + 1, ty + 1, x + tw - 3, ty + 1);
        XDrawLine(dpy, win->back, gc, x, ty, x, base_y - (active ? 0 : 1));
        XDrawLine(dpy, win->back, gc, x + 1, ty + 1, x + 1,
                  base_y - (active ? 0 : 1));
        XSetForeground(dpy, gc, p->bottom_shadow);
        XDrawLine(dpy, win->back, gc, x + tw - 1, ty, x + tw - 1,
                  base_y - (active ? 0 : 1));
        XDrawLine(dpy, win->back, gc, x + tw - 2, ty + 1, x + tw - 2,
                  base_y - (active ? 0 : 1));

        XFontSet font = active ? app->font_bold : app->font;
        int lw = mtk_text_width(font, t->labels[i]);
        mtk_draw_text_centered(win, font, x + (tw - lw) / 2, ty, th,
                               t->labels[i], p->text);
        x += tw + TAB_GAP;
    }

    /* Body top edge under inactive tabs and trailing space. */
    GC gc = win->gc;
    XSetForeground(dpy, gc, p->top_shadow);
    int ax = w->x + 2;
    for (int i = 0; i < t->active; i++)
        ax += tab_width(app, t->labels[i]) + TAB_GAP;
    int aw = tab_width(app, t->labels[t->active]);
    XDrawLine(dpy, win->back, gc, w->x, base_y - 1, ax, base_y - 1);
    XDrawLine(dpy, win->back, gc, w->x, base_y, ax, base_y);
    XDrawLine(dpy, win->back, gc, ax + aw, base_y - 1, w->x + w->w - 1,
              base_y - 1);
    XDrawLine(dpy, win->back, gc, ax + aw, base_y, w->x + w->w - 1, base_y);
}

static bool tabs_event(MtkWidget *w, XEvent *ev)
{
    MtkTabs *t = (MtkTabs *)w;
    if (ev->type != ButtonPress || ev->xbutton.button != Button1)
        return false;
    int x = w->x + 2;
    for (int i = 0; i < t->ntabs; i++) {
        int tw = tab_width(w->win->app, t->labels[i]);
        if (ev->xbutton.x >= x && ev->xbutton.x < x + tw) {
            mtk_tabs_set_active(t, i);
            return true;
        }
        x += tw + TAB_GAP;
    }
    return true;
}

static void tabs_destroy(MtkWidget *w)
{
    MtkTabs *t = (MtkTabs *)w;
    for (int i = 0; i < t->ntabs; i++)
        free(t->labels[i]);
    free(t->labels);
    free(t);
}

static const MtkWidgetOps tabs_ops = {
    .draw = tabs_draw,
    .event = tabs_event,
    .destroy = tabs_destroy,
};

MtkTabs *mtk_tabs_create(MtkWindow *win, MtkWidget *parent,
                         const char *const *labels, int ntabs,
                         void (*on_change)(MtkTabs *t, int index, void *data),
                         void *data)
{
    MtkTabs *t = calloc(1, sizeof(*t));
    mtk_widget_init(&t->base, win, parent, &tabs_ops);
    t->labels = calloc((size_t)ntabs, sizeof(char *));
    for (int i = 0; i < ntabs; i++)
        t->labels[i] = mtk_strdup(labels[i]);
    t->ntabs = ntabs;
    t->on_change = on_change;
    t->data = data;
    return t;
}

void mtk_tabs_set_active(MtkTabs *t, int index)
{
    if (index < 0 || index >= t->ntabs || index == t->active)
        return;
    t->active = index;
    mtk_window_damage(t->base.win);
    if (t->on_change)
        t->on_change(t, index, t->data);
}
