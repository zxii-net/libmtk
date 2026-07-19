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

enum { PAD = 5 };

static void entry_reserve(MtkEntry *e, int extra)
{
    if (e->len + extra + 1 <= e->cap)
        return;
    e->cap = e->cap ? e->cap * 2 : 32;
    if (e->cap < e->len + extra + 1)
        e->cap = e->len + extra + 1;
    e->text = realloc(e->text, (size_t)e->cap);
}

static int entry_text_w(MtkEntry *e, int nbytes)
{
    return XmbTextEscapement(e->base.win->app->font, e->text, nbytes);
}

static void entry_keep_cursor_visible(MtkEntry *e)
{
    int inner = e->base.w - 2 * PAD;
    if (inner <= 0)
        return;
    int cx = entry_text_w(e, e->cursor);
    if (cx - e->scroll_px < 0)
        e->scroll_px = cx;
    if (cx - e->scroll_px > inner)
        e->scroll_px = cx - inner;
}

static void entry_draw(MtkWidget *w)
{
    MtkEntry *e = (MtkEntry *)w;
    MtkWindow *win = w->win;
    MtkApp *app = win->app;
    MtkPalette *p = &app->pal;
    bool focused = win->focus == w;

    mtk_fill_rect(win, w->x, w->y, w->w, w->h, p->surface);
    mtk_draw_bevel_c(win, w->x, w->y, w->w, w->h, MTK_BEVEL, true,
                     p->surface_top, p->surface_bottom);
    if (focused)
        mtk_draw_rect(win, w->x - 1, w->y - 1, w->w + 2, w->h + 2,
                      p->highlight);

    mtk_set_clip(win, w->x + MTK_BEVEL, w->y + MTK_BEVEL,
                 w->w - 2 * MTK_BEVEL, w->h - 2 * MTK_BEVEL);
    int tx = w->x + PAD - e->scroll_px;
    mtk_draw_text_centered(win, app->font, tx, w->y, w->h, e->text,
                           p->surface_text);
    if (focused) {
        int cx = tx + entry_text_w(e, e->cursor);
        int fh = mtk_font_height(app->font);
        int cy = w->y + (w->h - fh) / 2;
        mtk_fill_rect(win, cx, cy, 2, fh, p->surface_text);
    }
    mtk_clear_clip(win);
}

static bool entry_event(MtkWidget *w, XEvent *ev)
{
    MtkEntry *e = (MtkEntry *)w;
    if (ev->type != ButtonPress || ev->xbutton.button != Button1)
        return false;
    int click = ev->xbutton.x - (w->x + PAD - e->scroll_px);
    int pos = e->len;
    for (int i = 0; i <= e->len; i = mtk_utf8_next(e->text, e->len, i)) {
        if (entry_text_w(e, i) >= click) {
            pos = i;
            break;
        }
        if (i == e->len)
            break;
    }
    e->cursor = pos;
    mtk_window_damage(w->win);
    return true;
}

static bool entry_key(MtkWidget *w, XKeyEvent *ev, KeySym sym,
                      const char *text)
{
    MtkEntry *e = (MtkEntry *)w;
    bool changed = false;

    /* Alt-modified keys are application accelerators (menus) */
    if (ev->state & Mod1Mask)
        return false;

    switch (sym) {
    case XK_Left:
        e->cursor = mtk_utf8_prev(e->text, e->cursor);
        break;
    case XK_Right:
        e->cursor = mtk_utf8_next(e->text, e->len, e->cursor);
        break;
    case XK_Home:
    case XK_KP_Home:
        e->cursor = 0;
        break;
    case XK_End:
    case XK_KP_End:
        e->cursor = e->len;
        break;
    case XK_BackSpace: {
        int from = mtk_utf8_prev(e->text, e->cursor);
        if (from < e->cursor) {
            memmove(e->text + from, e->text + e->cursor,
                    (size_t)(e->len - e->cursor + 1));
            e->len -= e->cursor - from;
            e->cursor = from;
            changed = true;
        }
        break;
    }
    case XK_Delete:
    case XK_KP_Delete: {
        int to = mtk_utf8_next(e->text, e->len, e->cursor);
        if (to > e->cursor) {
            memmove(e->text + e->cursor, e->text + to,
                    (size_t)(e->len - to + 1));
            e->len -= to - e->cursor;
            changed = true;
        }
        break;
    }
    case XK_Return:
    case XK_KP_Enter:
        if (e->on_activate)
            e->on_activate(e, e->data);
        return true;
    case XK_Escape:
        return false; /* let the window shortcut handler see it */
    default: {
        if (!text || !text[0])
            return false;
        unsigned char first = (unsigned char)text[0];
        if (first < 0x20 || first == 0x7f)
            return false;
        /* insert every code point of the (possibly composed) input */
        int tlen = (int)strlen(text);
        for (int i = 0; i < tlen;) {
            int next = mtk_utf8_next(text, tlen, i);
            char cp[8] = {0};
            memcpy(cp, text + i, (size_t)(next - i));
            i = next;
            if (e->validate && !e->validate(e, cp, e->data))
                continue; /* rejected, but consumed */
            int n = (int)strlen(cp);
            entry_reserve(e, n);
            memmove(e->text + e->cursor + n, e->text + e->cursor,
                    (size_t)(e->len - e->cursor + 1));
            memcpy(e->text + e->cursor, cp, (size_t)n);
            e->cursor += n;
            e->len += n;
            changed = true;
        }
        break;
    }
    }

    entry_keep_cursor_visible(e);
    mtk_window_damage(w->win);
    if (changed && e->on_change)
        e->on_change(e, e->data);
    return true;
}

static void entry_destroy(MtkWidget *w)
{
    MtkEntry *e = (MtkEntry *)w;
    free(e->text);
    free(e);
}

static void entry_measure(MtkWidget *w, int *nw, int *nh)
{
    (void)w;
    *nw = -1; /* elastic */
    *nh = MTK_ROW_H;
}

static const MtkWidgetOps entry_ops = {
    .draw = entry_draw,
    .event = entry_event,
    .key = entry_key,
    .destroy = entry_destroy,
    .measure = entry_measure,
};

MtkEntry *mtk_entry_create(MtkWindow *win, MtkWidget *parent)
{
    MtkEntry *e = calloc(1, sizeof(*e));
    mtk_widget_init(&e->base, win, parent, &entry_ops);
    e->base.can_focus = true;
    entry_reserve(e, 0);
    e->text[0] = '\0';
    return e;
}

void mtk_entry_set_text(MtkEntry *e, const char *text)
{
    int n = (int)strlen(text);
    entry_reserve(e, n);
    memcpy(e->text, text, (size_t)n + 1);
    e->len = n;
    e->cursor = n;
    e->scroll_px = 0;
    entry_keep_cursor_visible(e);
    mtk_window_damage(e->base.win);
}

const char *mtk_entry_text(MtkEntry *e)
{
    return e->text;
}
