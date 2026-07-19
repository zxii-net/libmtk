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

enum { TITLE_X = 10, TITLE_PAD = 4, SIDE_INSET = 8, BOTTOM_INSET = 8 };

static void frame_draw(MtkWidget *w)
{
    MtkFrame *f = (MtkFrame *)w;
    MtkWindow *win = w->win;
    MtkApp *app = win->app;
    int fh = mtk_font_height(app->font);
    bool titled = f->label[0] != '\0';

    /* the etched box starts at half the title height so the title
     * sits on the border, classic XmFrame style */
    int ty = titled ? w->y + fh / 2 : w->y;
    mtk_draw_etched(win, w->x, ty, w->w, w->h - (ty - w->y));

    if (titled) {
        int tw = mtk_text_width(app->font, f->label);
        mtk_fill_rect(win, w->x + TITLE_X - TITLE_PAD, w->y,
                      tw + 2 * TITLE_PAD, fh, app->pal.bg);
        mtk_draw_text_centered(win, app->font, w->x + TITLE_X, w->y, fh,
                               f->label, app->pal.text);
    }
}

static void frame_destroy(MtkWidget *w)
{
    MtkFrame *f = (MtkFrame *)w;
    free(f->label);
    free(f);
}

static const MtkWidgetOps frame_ops = {
    .draw = frame_draw,
    .destroy = frame_destroy,
};

MtkFrame *mtk_frame_create(MtkWindow *win, MtkWidget *parent,
                           const char *label)
{
    MtkFrame *f = calloc(1, sizeof(*f));
    mtk_widget_init(&f->base, win, parent, &frame_ops);
    f->label = mtk_strdup(label);
    return f;
}

void mtk_frame_set_label(MtkFrame *f, const char *label)
{
    free(f->label);
    f->label = mtk_strdup(label);
    mtk_window_damage(f->base.win);
}

void mtk_frame_insets(const MtkFrame *f, int *top, int *side, int *bottom)
{
    int fh = mtk_font_height(f->base.win->app->font);
    *top = (f->label[0] ? fh : 2) + TITLE_PAD;
    *side = SIDE_INSET;
    *bottom = BOTTOM_INSET;
}
