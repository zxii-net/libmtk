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

#include <string.h>

#include "internal.h"

static GC prep(MtkWindow *win, unsigned long pixel)
{
    XSetForeground(win->app->dpy, win->gc, pixel);
    return win->gc;
}

void mtk_fill_rect(MtkWindow *win, int x, int y, int w, int h,
                   unsigned long pixel)
{
    if (w <= 0 || h <= 0)
        return;
    XFillRectangle(win->app->dpy, win->back, prep(win, pixel),
                   x, y, (unsigned)w, (unsigned)h);
}

void mtk_draw_rect(MtkWindow *win, int x, int y, int w, int h,
                   unsigned long pixel)
{
    if (w <= 0 || h <= 0)
        return;
    XDrawRectangle(win->app->dpy, win->back, prep(win, pixel),
                   x, y, (unsigned)(w - 1), (unsigned)(h - 1));
}

/*
 * Motif bevel: light on top/left, dark on bottom/right (raised);
 * swapped when sunken.  Mitred corners via per-line trapezoid fill.
 */
void mtk_draw_bevel(MtkWindow *win, int x, int y, int w, int h,
                    int thickness, bool sunken)
{
    MtkPalette *p = &win->app->pal;
    mtk_draw_bevel_c(win, x, y, w, h, thickness, sunken, p->top_shadow,
                     p->bottom_shadow);
}

void mtk_draw_bevel_c(MtkWindow *win, int x, int y, int w, int h,
                      int thickness, bool sunken,
                      unsigned long light, unsigned long dark)
{
    unsigned long tl = sunken ? dark : light;
    unsigned long br = sunken ? light : dark;
    Display *dpy = win->app->dpy;

    for (int i = 0; i < thickness; i++) {
        GC gc = prep(win, tl);
        /* top */
        XDrawLine(dpy, win->back, gc, x + i, y + i, x + w - 1 - i, y + i);
        /* left */
        XDrawLine(dpy, win->back, gc, x + i, y + i, x + i, y + h - 1 - i);
        gc = prep(win, br);
        /* bottom */
        XDrawLine(dpy, win->back, gc, x + i + 1, y + h - 1 - i,
                  x + w - 1 - i, y + h - 1 - i);
        /* right */
        XDrawLine(dpy, win->back, gc, x + w - 1 - i, y + i + 1,
                  x + w - 1 - i, y + h - 1 - i);
    }
}

void mtk_draw_etched(MtkWindow *win, int x, int y, int w, int h)
{
    MtkPalette *p = &win->app->pal;
    Display *dpy = win->app->dpy;
    GC gc = prep(win, p->bottom_shadow);
    XDrawRectangle(dpy, win->back, gc, x, y, (unsigned)(w - 2),
                   (unsigned)(h - 2));
    gc = prep(win, p->top_shadow);
    XDrawRectangle(dpy, win->back, gc, x + 1, y + 1, (unsigned)(w - 2),
                   (unsigned)(h - 2));
}

/* Motif-style shaded triangle arrow filling (x,y,w,h). */
void mtk_draw_arrow(MtkWindow *win, int x, int y, int w, int h,
                    MtkArrowDir dir, bool sunken)
{
    MtkPalette *p = &win->app->pal;
    Display *dpy = win->app->dpy;
    XPoint pt[3];

    switch (dir) {
    case MTK_ARROW_UP:
        pt[0] = (XPoint){(short)(x + w / 2), (short)y};
        pt[1] = (XPoint){(short)x, (short)(y + h - 1)};
        pt[2] = (XPoint){(short)(x + w - 1), (short)(y + h - 1)};
        break;
    case MTK_ARROW_DOWN:
        pt[0] = (XPoint){(short)x, (short)y};
        pt[1] = (XPoint){(short)(x + w - 1), (short)y};
        pt[2] = (XPoint){(short)(x + w / 2), (short)(y + h - 1)};
        break;
    case MTK_ARROW_LEFT:
        pt[0] = (XPoint){(short)x, (short)(y + h / 2)};
        pt[1] = (XPoint){(short)(x + w - 1), (short)y};
        pt[2] = (XPoint){(short)(x + w - 1), (short)(y + h - 1)};
        break;
    case MTK_ARROW_RIGHT:
    default:
        pt[0] = (XPoint){(short)x, (short)y};
        pt[1] = (XPoint){(short)(x + w - 1), (short)(y + h / 2)};
        pt[2] = (XPoint){(short)x, (short)(y + h - 1)};
        break;
    }

    GC gc = prep(win, sunken ? p->active : p->bg);
    XFillPolygon(dpy, win->back, gc, pt, 3, Convex, CoordModeOrigin);

    /* Shade the triangle edges: light on the top-ish edges, dark below. */
    unsigned long light = sunken ? p->bottom_shadow : p->top_shadow;
    unsigned long dark = sunken ? p->top_shadow : p->bottom_shadow;
    gc = prep(win, dir == MTK_ARROW_DOWN || dir == MTK_ARROW_RIGHT ? light
                                                                   : dark);
    XDrawLine(dpy, win->back, gc, pt[1].x, pt[1].y, pt[2].x, pt[2].y);
    gc = prep(win, light);
    XDrawLine(dpy, win->back, gc, pt[0].x, pt[0].y, pt[1].x, pt[1].y);
    gc = prep(win, dir == MTK_ARROW_UP || dir == MTK_ARROW_LEFT ? dark
                                                                : light);
    XDrawLine(dpy, win->back, gc, pt[0].x, pt[0].y, pt[2].x, pt[2].y);
}

void mtk_draw_text(MtkWindow *win, XFontSet font, int x, int baseline,
                   const char *str, unsigned long pixel)
{
    Display *dpy = win->app->dpy;
    GC gc = prep(win, pixel);
    XmbDrawString(dpy, win->back, font, gc, x, baseline, str,
                  (int)strlen(str));
}

void mtk_draw_text_centered(MtkWindow *win, XFontSet font, int x,
                            int y, int h, const char *str,
                            unsigned long pixel)
{
    XFontSetExtents *ex = XExtentsOfFontSet(font);
    int ascent = -ex->max_logical_extent.y;
    int descent = ex->max_logical_extent.height - ascent;
    int baseline = y + (h + ascent - descent) / 2;
    mtk_draw_text(win, font, x, baseline, str, pixel);
}

int mtk_text_width(XFontSet font, const char *str)
{
    return XmbTextEscapement(font, str, (int)strlen(str));
}

int mtk_font_height(XFontSet font)
{
    return XExtentsOfFontSet(font)->max_logical_extent.height;
}

void mtk_set_clip(MtkWindow *win, int x, int y, int w, int h)
{
    XRectangle r = {(short)x, (short)y, (unsigned short)(w > 0 ? w : 0),
                    (unsigned short)(h > 0 ? h : 0)};
    XSetClipRectangles(win->app->dpy, win->gc, 0, 0, &r, 1, Unsorted);
    /* GC clips only cover core drawing; XRender composites onto the
     * back buffer need their own clip on the destination picture. */
    XRenderSetPictureClipRectangles(win->app->dpy, win->back_pict,
                                    0, 0, &r, 1);
}

void mtk_clear_clip(MtkWindow *win)
{
    XSetClipMask(win->app->dpy, win->gc, None);
    XRenderPictureAttributes pa = { .clip_mask = None };
    XRenderChangePicture(win->app->dpy, win->back_pict, CPClipMask, &pa);
}
