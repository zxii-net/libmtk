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
 *
 * ------------------------------------------------------------------
 * The layout engine: a tree of geometry nodes applied to a
 * rectangle.  Nodes are not widgets; leaves end in
 * mtk_widget_set_rect().  See the "layout" group in mtk.h for the
 * public contract.
 */

#include <stdarg.h>
#include <stdlib.h>

#include "internal.h"

typedef enum LayKind {
    LAY_WIDGET,
    LAY_SPACER,
    LAY_ROW,
    LAY_COL,
    LAY_GRID,
    LAY_STACK,
    LAY_SPLIT,
    LAY_FRAMED,
} LayKind;

struct MtkLay {
    LayKind kind;
    MtkWidget *widget;   /* LAY_WIDGET */
    MtkFrame *frame;     /* LAY_FRAMED */
    MtkSash *sash;       /* LAY_SPLIT */
    MtkLay **kids;
    int nkids, kidcap;
    int gap;
    int ncols;           /* LAY_GRID */

    /* policy */
    int fix_w, fix_h;    /* -1 = unset */
    int min_w, min_h;
    int weight;          /* -1 = auto: 0 when measurable, else 1 */
    int pad;
    MtkLayAlign halign, valign;
    bool keep_space;

    /* split state */
    int split_pos;       /* first pane's main size; -1 = unset */
    int ax, ay, aw, ah;  /* last applied rect (after pad) */
};

/* ------------------------------------------------------------ building */

static MtkLay *lay_new(LayKind kind)
{
    MtkLay *l = calloc(1, sizeof(*l));
    l->kind = kind;
    l->fix_w = l->fix_h = -1;
    l->weight = -1;
    l->split_pos = -1;
    return l;
}

void mtk_lay_add(MtkLay *container, MtkLay *child)
{
    if (!container || !child)
        return;
    if (container->nkids == container->kidcap) {
        container->kidcap = container->kidcap ? container->kidcap * 2 : 4;
        container->kids = realloc(container->kids,
                                  sizeof(MtkLay *) *
                                      (size_t)container->kidcap);
    }
    container->kids[container->nkids++] = child;
}

static void lay_add_va(MtkLay *container, va_list ap)
{
    for (MtkLay *k = va_arg(ap, MtkLay *); k; k = va_arg(ap, MtkLay *))
        mtk_lay_add(container, k);
}

MtkLay *mtk_lay_widget(MtkWidget *w)
{
    MtkLay *l = lay_new(LAY_WIDGET);
    l->widget = w;
    return l;
}

MtkLay *mtk_lay_spacer(void)
{
    MtkLay *l = lay_new(LAY_SPACER);
    l->weight = 1;
    return l;
}

MtkLay *mtk_lay_wfix(MtkWidget *w, int width, int height)
{
    return mtk_lay_fixed(mtk_lay_widget(w), width, height);
}

MtkLay *mtk_lay_wstretch(MtkWidget *w, int weight)
{
    return mtk_lay_stretch(mtk_lay_widget(w), weight);
}

MtkLay *mtk_lay_row(int gap, ...)
{
    MtkLay *l = lay_new(LAY_ROW);
    l->gap = gap;
    va_list ap;
    va_start(ap, gap);
    lay_add_va(l, ap);
    va_end(ap);
    return l;
}

MtkLay *mtk_lay_col(int gap, ...)
{
    MtkLay *l = lay_new(LAY_COL);
    l->gap = gap;
    va_list ap;
    va_start(ap, gap);
    lay_add_va(l, ap);
    va_end(ap);
    return l;
}

MtkLay *mtk_lay_grid(int ncols, int gap, ...)
{
    MtkLay *l = lay_new(LAY_GRID);
    l->ncols = ncols > 0 ? ncols : 1;
    l->gap = gap;
    va_list ap;
    va_start(ap, gap);
    lay_add_va(l, ap);
    va_end(ap);
    return l;
}

MtkLay *mtk_lay_stack(MtkLay *first, ...)
{
    MtkLay *l = lay_new(LAY_STACK);
    mtk_lay_add(l, first);
    va_list ap;
    va_start(ap, first);
    lay_add_va(l, ap);
    va_end(ap);
    return l;
}

static void lay_sash_drag(MtkSash *s, int new_x, void *data);

MtkLay *mtk_lay_split(MtkSash *sash, MtkLay *first, MtkLay *second)
{
    MtkLay *l = lay_new(LAY_SPLIT);
    l->sash = sash;
    mtk_lay_add(l, first);
    mtk_lay_add(l, second);
    sash->on_drag = lay_sash_drag;
    sash->data = l;
    return l;
}

MtkLay *mtk_lay_framed(MtkFrame *frame, MtkLay *inner)
{
    MtkLay *l = lay_new(LAY_FRAMED);
    l->frame = frame;
    mtk_lay_add(l, inner);
    return l;
}

MtkLay *mtk_lay_appframe(MtkWidget *menubar, MtkLay *content,
                         MtkWidget *statusbar)
{
    MtkLay *col = mtk_lay_col(0, nullptr);
    if (menubar)
        mtk_lay_add(col, mtk_lay_widget(menubar));
    mtk_lay_add(col, mtk_lay_stretch(content, 1));
    if (statusbar)
        mtk_lay_add(col, mtk_lay_widget(statusbar));
    return col;
}

/* ------------------------------------------------------------ policy */

MtkLay *mtk_lay_fixed(MtkLay *l, int w, int h)
{
    l->fix_w = w;
    l->fix_h = h;
    return l;
}

MtkLay *mtk_lay_stretch(MtkLay *l, int weight)
{
    l->weight = weight > 0 ? weight : 1;
    return l;
}

MtkLay *mtk_lay_pad(MtkLay *l, int pad)
{
    l->pad = pad > 0 ? pad : 0;
    return l;
}

MtkLay *mtk_lay_min(MtkLay *l, int w, int h)
{
    l->min_w = w > 0 ? w : 0;
    l->min_h = h > 0 ? h : 0;
    return l;
}

MtkLay *mtk_lay_align(MtkLay *l, MtkLayAlign halign, MtkLayAlign valign)
{
    l->halign = halign;
    l->valign = valign;
    return l;
}

MtkLay *mtk_lay_keep_space(MtkLay *l)
{
    l->keep_space = true;
    return l;
}

void mtk_lay_free(MtkLay *l)
{
    if (!l)
        return;
    if (l->kind == LAY_SPLIT && l->sash &&
        l->sash->on_drag == lay_sash_drag && l->sash->data == l) {
        l->sash->on_drag = nullptr;
        l->sash->data = nullptr;
    }
    for (int i = 0; i < l->nkids; i++)
        mtk_lay_free(l->kids[i]);
    free(l->kids);
    free(l);
}

/* --------------------------------------------------------- measuring */

/* Does this node currently occupy space? */
static bool lay_alive(const MtkLay *l)
{
    switch (l->kind) {
    case LAY_WIDGET:
        return l->widget->visible || l->keep_space;
    case LAY_FRAMED:
        return l->frame->base.visible || l->keep_space;
    case LAY_SPACER:
    case LAY_SPLIT:
        return true;
    default:
        for (int i = 0; i < l->nkids; i++)
            if (lay_alive(l->kids[i]))
                return true;
        return false;
    }
}

static void lay_natural(const MtkLay *l, int *nw, int *nh);

/* Sum children naturals along the main axis; -1 if any is elastic. */
static void axis_sum_max(const MtkLay *l, bool horiz, int *sum, int *mx)
{
    *sum = 0;
    *mx = -1;
    int alive = 0;
    bool sum_elastic = false;
    for (int i = 0; i < l->nkids; i++) {
        const MtkLay *k = l->kids[i];
        if (!lay_alive(k))
            continue;
        alive++;
        int w, h;
        lay_natural(k, &w, &h);
        int main = horiz ? w : h;
        int cross = horiz ? h : w;
        if (main < 0 || (k->weight > 0))
            sum_elastic = true;
        else
            *sum += main;
        if (cross > *mx)
            *mx = cross;
    }
    if (alive > 1)
        *sum += l->gap * (alive - 1);
    if (sum_elastic)
        *sum = -1;
}

/* Natural (preferred) size including pad; -1 per elastic axis. */
static void lay_natural(const MtkLay *l, int *nw, int *nh)
{
    int w = -1, h = -1;

    switch (l->kind) {
    case LAY_WIDGET:
        if (l->widget->ops && l->widget->ops->measure)
            l->widget->ops->measure(l->widget, &w, &h);
        break;
    case LAY_SPACER:
        break;
    case LAY_ROW:
        axis_sum_max(l, true, &w, &h);
        break;
    case LAY_COL:
        axis_sum_max(l, false, &h, &w);
        break;
    case LAY_GRID: {
        /* elastic if any cell is; else columns of maxima */
        int colw[64] = {0}, rowh_sum = 0, rowh = 0;
        int ncols = l->ncols < 64 ? l->ncols : 64;
        bool elastic_w = false, elastic_h = false;
        int cell = 0, rows = 0;
        for (int i = 0; i < l->nkids; i++) {
            const MtkLay *k = l->kids[i];
            if (!lay_alive(k))
                continue;
            int kw, kh;
            lay_natural(k, &kw, &kh);
            int c = cell % ncols;
            if (kw < 0 || k->weight > 0)
                elastic_w = true;
            else if (kw > colw[c])
                colw[c] = kw;
            if (kh < 0)
                elastic_h = true;
            else if (kh > rowh)
                rowh = kh;
            cell++;
            if (cell % ncols == 0 || i == l->nkids - 1) {
                rowh_sum += rowh;
                rowh = 0;
                rows++;
            }
        }
        if (!elastic_w) {
            w = 0;
            for (int c = 0; c < ncols; c++)
                w += colw[c];
            w += l->gap * (ncols - 1);
        }
        if (!elastic_h)
            h = rowh_sum + (rows > 1 ? l->gap * (rows - 1) : 0);
        break;
    }
    case LAY_STACK:
        for (int i = 0; i < l->nkids; i++) {
            const MtkLay *k = l->kids[i];
            if (!lay_alive(k))
                continue;
            int kw, kh;
            lay_natural(k, &kw, &kh);
            if (kw > w)
                w = kw;
            if (kh > h)
                h = kh;
        }
        break;
    case LAY_SPLIT:
        break; /* elastic both ways */
    case LAY_FRAMED: {
        int t, s, b, kw, kh;
        mtk_frame_insets(l->frame, &t, &s, &b);
        lay_natural(l->kids[0], &kw, &kh);
        if (kw >= 0)
            w = kw + 2 * s;
        if (kh >= 0)
            h = kh + t + b;
        break;
    }
    }

    if (l->fix_w >= 0)
        w = l->fix_w;
    if (l->fix_h >= 0)
        h = l->fix_h;
    if (w >= 0 && w < l->min_w)
        w = l->min_w;
    if (h >= 0 && h < l->min_h)
        h = l->min_h;
    if (w >= 0)
        w += 2 * l->pad;
    if (h >= 0)
        h += 2 * l->pad;
    *nw = w;
    *nh = h;
}

/* 0 = natural (measurable), >0 = share of the leftover. */
static int lay_weight(const MtkLay *l, bool horiz)
{
    if (l->weight >= 0)
        return l->weight;
    int w, h;
    lay_natural(l, &w, &h);
    return (horiz ? w : h) < 0 ? 1 : 0;
}

/* Lower bound along the main axis (for split clamping and floors). */
static int lay_min_main(const MtkLay *l, bool horiz)
{
    int fix = horiz ? l->fix_w : l->fix_h;
    if (fix >= 0)
        return fix + 2 * l->pad;
    int m = horiz ? l->min_w : l->min_h;
    return m + 2 * l->pad;
}

/* ----------------------------------------------------------- placing */

static void lay_place(MtkLay *l, int x, int y, int w, int h);

static void place_linear(MtkLay *l, bool horiz, int x, int y, int w,
                         int h)
{
    int n = 0;
    for (int i = 0; i < l->nkids; i++)
        if (lay_alive(l->kids[i]))
            n++;
    if (n == 0)
        return;

    int avail = (horiz ? w : h) - l->gap * (n - 1);
    int total_weight = 0, used = 0;

    /* pass 1: fixed and natural sizes */
    for (int i = 0; i < l->nkids; i++) {
        MtkLay *k = l->kids[i];
        if (!lay_alive(k))
            continue;
        if (lay_weight(k, horiz) > 0) {
            total_weight += lay_weight(k, horiz);
            continue;
        }
        int kw, kh;
        lay_natural(k, &kw, &kh);
        used += horiz ? kw : kh;
    }

    /* pass 2: distribute the leftover among stretch nodes */
    int leftover = avail - used;
    if (leftover < 0)
        leftover = 0;
    int given = 0, weight_seen = 0;

    int cur = horiz ? x : y;
    for (int i = 0; i < l->nkids; i++) {
        MtkLay *k = l->kids[i];
        if (!lay_alive(k))
            continue;
        int wgt = lay_weight(k, horiz);
        int size;
        if (wgt > 0) {
            weight_seen += wgt;
            size = leftover * weight_seen / total_weight - given;
            given += size;
            int min = lay_min_main(k, horiz);
            if (size < min)
                size = min;
        } else {
            int kw, kh;
            lay_natural(k, &kw, &kh);
            size = horiz ? kw : kh;
        }
        if (horiz)
            lay_place(k, cur, y, size, h);
        else
            lay_place(k, x, cur, w, size);
        cur += size + l->gap;
    }
}

static void place_grid(MtkLay *l, int x, int y, int w, int h)
{
    enum { MAXC = 64 };
    int ncols = l->ncols < MAXC ? l->ncols : MAXC;

    MtkLay *alive[256];
    int n = 0;
    for (int i = 0; i < l->nkids && n < 256; i++)
        if (lay_alive(l->kids[i]))
            alive[n++] = l->kids[i];
    if (n == 0)
        return;
    int nrows = (n + ncols - 1) / ncols;

    /* column widths: natural maxima, elastic columns share leftover */
    int colw[MAXC] = {0};
    bool colstretch[MAXC] = {false};
    for (int i = 0; i < n; i++) {
        int c = i % ncols, kw, kh;
        lay_natural(alive[i], &kw, &kh);
        if (kw < 0 || lay_weight(alive[i], true) > 0)
            colstretch[c] = true;
        else if (kw > colw[c])
            colw[c] = kw;
    }
    int fixed_w = 0, nstretch = 0;
    for (int c = 0; c < ncols; c++) {
        if (colstretch[c])
            nstretch++;
        else
            fixed_w += colw[c];
    }
    int leftover = w - fixed_w - l->gap * (ncols - 1);
    if (leftover < 0)
        leftover = 0;
    for (int c = 0, s = 0; c < ncols; c++)
        if (colstretch[c]) {
            s++;
            colw[c] = leftover * s / nstretch -
                      leftover * (s - 1) / nstretch;
        }

    /* row heights: natural maxima, elastic rows share leftover */
    int rowh[256] = {0};
    bool rowstretch[256] = {false};
    for (int i = 0; i < n; i++) {
        int r = i / ncols, kw, kh;
        lay_natural(alive[i], &kw, &kh);
        if (kh < 0)
            rowstretch[r] = true;
        else if (kh > rowh[r])
            rowh[r] = kh;
    }
    int fixed_h = 0;
    nstretch = 0;
    for (int r = 0; r < nrows; r++) {
        if (rowstretch[r])
            nstretch++;
        else
            fixed_h += rowh[r];
    }
    leftover = h - fixed_h - l->gap * (nrows - 1);
    if (leftover < 0)
        leftover = 0;
    for (int r = 0, s = 0; r < nrows; r++)
        if (rowstretch[r]) {
            s++;
            rowh[r] = leftover * s / nstretch -
                      leftover * (s - 1) / nstretch;
        }

    int cy = y;
    for (int r = 0; r < nrows; r++) {
        int cx = x;
        for (int c = 0; c < ncols; c++) {
            int i = r * ncols + c;
            if (i < n)
                lay_place(alive[i], cx, cy, colw[c], rowh[r]);
            cx += colw[c] + l->gap;
        }
        cy += rowh[r] + l->gap;
    }
}

static void place_split(MtkLay *l, int x, int y, int w, int h)
{
    MtkLay *first = l->kids[0], *second = l->kids[1];
    int avail = w - MTK_SASH_W;
    int min1 = lay_min_main(first, true);
    int min2 = lay_min_main(second, true);

    if (l->split_pos < 0) {
        int nw, nh;
        lay_natural(first, &nw, &nh);
        l->split_pos = nw >= 0 ? nw : avail / 2;
    }
    if (l->split_pos > avail - min2)
        l->split_pos = avail - min2;
    if (l->split_pos < min1)
        l->split_pos = min1;

    lay_place(first, x, y, l->split_pos, h);
    mtk_widget_set_rect(&l->sash->base, x + l->split_pos, y, MTK_SASH_W,
                        h);
    l->sash->min_x = x + min1;
    l->sash->max_x = x + (avail - min2 > min1 ? avail - min2 : min1);
    lay_place(second, x + l->split_pos + MTK_SASH_W, y,
              avail - l->split_pos, h);
}

static void lay_sash_drag(MtkSash *s, int new_x, void *data)
{
    (void)s;
    MtkLay *l = data;
    l->split_pos = new_x - l->ax;
    lay_place(l, l->ax - l->pad, l->ay - l->pad, l->aw + 2 * l->pad,
              l->ah + 2 * l->pad);
}

static void lay_place(MtkLay *l, int x, int y, int w, int h)
{
    x += l->pad;
    y += l->pad;
    w -= 2 * l->pad;
    h -= 2 * l->pad;
    if (w < 0)
        w = 0;
    if (h < 0)
        h = 0;
    l->ax = x;
    l->ay = y;
    l->aw = w;
    l->ah = h;

    switch (l->kind) {
    case LAY_WIDGET: {
        if (!l->widget->visible && !l->keep_space)
            return;
        int nw, nh;
        lay_natural(l, &nw, &nh);
        nw = nw >= 0 ? nw - 2 * l->pad : -1;
        nh = nh >= 0 ? nh - 2 * l->pad : -1;
        int fw = l->fix_w >= 0 ? l->fix_w
                 : (l->halign != MTK_LAY_FILL && nw >= 0) ? nw
                                                          : w;
        int fh = l->fix_h >= 0 ? l->fix_h
                 : (l->valign != MTK_LAY_FILL && nh >= 0) ? nh
                                                          : h;
        if (fw > w)
            fw = w;
        if (fh > h)
            fh = h;
        int fx = x, fy = y;
        if (l->halign == MTK_LAY_CENTER)
            fx = x + (w - fw) / 2;
        else if (l->halign == MTK_LAY_END)
            fx = x + w - fw;
        if (l->valign == MTK_LAY_CENTER)
            fy = y + (h - fh) / 2;
        else if (l->valign == MTK_LAY_END)
            fy = y + h - fh;
        mtk_widget_set_rect(l->widget, fx, fy, fw, fh);
        break;
    }
    case LAY_SPACER:
        break;
    case LAY_ROW:
        place_linear(l, true, x, y, w, h);
        break;
    case LAY_COL:
        place_linear(l, false, x, y, w, h);
        break;
    case LAY_GRID:
        place_grid(l, x, y, w, h);
        break;
    case LAY_STACK:
        for (int i = 0; i < l->nkids; i++)
            if (lay_alive(l->kids[i]))
                lay_place(l->kids[i], x, y, w, h);
        break;
    case LAY_SPLIT:
        place_split(l, x, y, w, h);
        break;
    case LAY_FRAMED: {
        if (!lay_alive(l))
            return;
        int t, s, b;
        mtk_frame_insets(l->frame, &t, &s, &b);
        mtk_widget_set_rect(&l->frame->base, x, y, w, h);
        lay_place(l->kids[0], x + s, y + t, w - 2 * s, h - t - b);
        break;
    }
    }
}

void mtk_lay_apply(MtkLay *root, int x, int y, int w, int h)
{
    if (root)
        lay_place(root, x, y, w, h);
}
