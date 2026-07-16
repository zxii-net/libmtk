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

static void label_draw(MtkWidget *w)
{
    MtkLabel *l = (MtkLabel *)w;
    MtkWindow *win = w->win;
    MtkApp *app = win->app;
    XFontSet font = l->bold ? app->font_bold : app->font;

    int tw = mtk_text_width(font, l->text);
    int x = w->x;
    if (l->align == MTK_ALIGN_CENTER)
        x = w->x + (w->w - tw) / 2;
    else if (l->align == MTK_ALIGN_RIGHT)
        x = w->x + w->w - tw;

    mtk_set_clip(win, w->x, w->y, w->w, w->h);
    mtk_draw_text_centered(win, font, x, w->y, w->h, l->text, app->pal.text);
    mtk_clear_clip(win);
}

static void label_destroy(MtkWidget *w)
{
    MtkLabel *l = (MtkLabel *)w;
    free(l->text);
    free(l);
}

static const MtkWidgetOps label_ops = {
    .draw = label_draw,
    .destroy = label_destroy,
};

MtkLabel *mtk_label_create(MtkWindow *win, MtkWidget *parent,
                           const char *text)
{
    MtkLabel *l = calloc(1, sizeof(*l));
    mtk_widget_init(&l->base, win, parent, &label_ops);
    l->text = mtk_strdup(text);
    return l;
}

void mtk_label_set_text(MtkLabel *l, const char *text)
{
    free(l->text);
    l->text = mtk_strdup(text);
    mtk_window_damage(l->base.win);
}
