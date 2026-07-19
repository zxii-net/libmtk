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

enum { PAD_X = 12 };

static void button_draw(MtkWidget *w)
{
    MtkButton *b = (MtkButton *)w;
    MtkWindow *win = w->win;
    MtkPalette *p = &win->app->pal;
    bool on = b->toggle && b->toggled;   /* latched: primary colors  */
    bool sunken = b->armed || on;        /* pressed: active fill     */

    unsigned long fill = b->armed ? p->active : on ? p->primary : p->bg;
    unsigned long fg = !b->armed && on ? p->primary_text : p->text;
    mtk_fill_rect(win, w->x, w->y, w->w, w->h, fill);
    if (on && !b->armed)
        mtk_draw_bevel_c(win, w->x, w->y, w->w, w->h, MTK_BEVEL, true,
                         p->primary_top, p->primary_bottom);
    else
        mtk_draw_bevel(win, w->x, w->y, w->w, w->h, MTK_BEVEL, sunken);

    XFontSet font = win->app->font;
    int tw = mtk_text_width(font, b->label);
    int tx = w->x + (w->w - tw) / 2 + (sunken ? 1 : 0);
    int ty = w->y + (sunken ? 1 : 0);
    mtk_draw_text_centered(win, font, tx, ty, w->h, b->label, fg);
}

static bool button_event(MtkWidget *w, XEvent *ev)
{
    MtkButton *b = (MtkButton *)w;
    switch (ev->type) {
    case ButtonPress:
        if (ev->xbutton.button != Button1)
            return false;
        b->armed = true;
        mtk_window_damage(w->win);
        return true;
    case MotionNotify: {
        bool inside = mtk_widget_contains(w, ev->xmotion.x, ev->xmotion.y);
        if (w->win->grab == w && b->armed != inside) {
            b->armed = inside;
            mtk_window_damage(w->win);
        }
        return true;
    }
    case ButtonRelease:
        if (ev->xbutton.button != Button1)
            return false;
        if (b->armed) {
            b->armed = false;
            if (b->toggle)
                b->toggled = !b->toggled;
            mtk_window_damage(w->win);
            if (b->on_click)
                b->on_click(b, b->data);
        }
        return true;
    }
    return false;
}

static void button_destroy(MtkWidget *w)
{
    MtkButton *b = (MtkButton *)w;
    free(b->label);
    free(b);
}

static void button_measure(MtkWidget *w, int *nw, int *nh)
{
    MtkButton *b = (MtkButton *)w;
    *nw = mtk_text_width(w->win->app->font, b->label) + 2 * PAD_X;
    *nh = MTK_ROW_H;
}

static const MtkWidgetOps button_ops = {
    .draw = button_draw,
    .event = button_event,
    .destroy = button_destroy,
    .measure = button_measure,
};

MtkButton *mtk_button_create(MtkWindow *win, MtkWidget *parent,
                             const char *label,
                             void (*on_click)(MtkButton *b, void *data),
                             void *data)
{
    MtkButton *b = calloc(1, sizeof(*b));
    mtk_widget_init(&b->base, win, parent, &button_ops);
    b->label = mtk_strdup(label);
    b->on_click = on_click;
    b->data = data;
    return b;
}

void mtk_button_set_toggle(MtkButton *b, bool toggle)
{
    b->toggle = toggle;
}

void mtk_button_set_toggled(MtkButton *b, bool toggled)
{
    if (b->toggled != toggled) {
        b->toggled = toggled;
        mtk_window_damage(b->base.win);
    }
}

void mtk_button_set_label(MtkButton *b, const char *label)
{
    free(b->label);
    b->label = mtk_strdup(label);
    mtk_window_damage(b->base.win);
}

int mtk_button_natural_width(MtkApp *app, const char *label)
{
    return mtk_text_width(app->font, label) + 2 * PAD_X;
}
