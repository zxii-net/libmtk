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
#include <stdlib.h>
#include <string.h>

#include "internal.h"

enum {
    TITLE_PAD = 10,   /* horizontal padding inside a title */
    TITLE_GAP = 4,
    ITEM_PAD = 12,
    SEP_H = 9,
    ACCEL_GAP = 24,
};

/* ------------------------------------------------------------- geometry */

static int item_row_h(MtkApp *app)
{
    return mtk_font_height(app->font) + 8;
}

static bool entry_is_sep(const MtkMenuEntry *e)
{
    return e->label && e->label[0] == '-' && e->label[1] == '\0';
}

static int title_width(MtkApp *app, const char *title)
{
    return mtk_text_width(app->font_bold, title) + 2 * TITLE_PAD;
}

static int title_x(MtkMenuBar *mb, int idx)
{
    MtkApp *app = mb->base.win->app;
    if (idx == mb->help_menu) /* the help menu hangs on the right */
        return mb->base.x + mb->base.w - 4 -
               title_width(app, mb->menus[idx].title);
    int x = mb->base.x + 4;
    for (int i = 0; i < idx; i++)
        if (i != mb->help_menu)
            x += title_width(app, mb->menus[i].title) + TITLE_GAP;
    return x;
}

static int title_at(MtkMenuBar *mb, int x)
{
    for (int i = 0; i < mb->nmenus; i++) {
        int tx = title_x(mb, i);
        if (x >= tx && x < tx + title_width(mb->base.win->app,
                                            mb->menus[i].title))
            return i;
    }
    return -1;
}

static void pulldown_size(MtkMenuBar *mb, int menu, int *w, int *h)
{
    MtkApp *app = mb->base.win->app;
    MtkMenu *m = &mb->menus[menu];
    int wide = title_width(app, m->title);
    int high = 2 * MTK_BEVEL + 4;
    for (int i = 0; i < m->n; i++) {
        if (entry_is_sep(&m->entries[i])) {
            high += SEP_H;
            continue;
        }
        int lw = mtk_text_width(app->font, m->entries[i].label) +
                 2 * ITEM_PAD;
        if (m->entries[i].accel)
            lw += ACCEL_GAP + mtk_text_width(app->font,
                                             m->entries[i].accel);
        if (lw > wide)
            wide = lw;
        high += item_row_h(app);
    }
    *w = wide;
    *h = high;
}

static int item_at(MtkMenuBar *mb, int menu, int y)
{
    MtkApp *app = mb->base.win->app;
    MtkMenu *m = &mb->menus[menu];
    int cy = MTK_BEVEL + 2;
    for (int i = 0; i < m->n; i++) {
        int ih = entry_is_sep(&m->entries[i]) ? SEP_H : item_row_h(app);
        if (y >= cy && y < cy + ih)
            return entry_is_sep(&m->entries[i]) ? -1 : i;
        cy += ih;
    }
    return -1;
}

/* ------------------------------------------------------------- pulldown */

typedef struct Pulldown {
    MtkWidget base;
    MtkMenuBar *mb;
    int menu;
    int hover; /* item index or -1 */
} Pulldown;

static void menubar_close(MtkMenuBar *mb);
static void menubar_open_at(MtkMenuBar *mb, int idx, bool keyboard);

static void pulldown_draw(MtkWidget *w)
{
    Pulldown *pd = (Pulldown *)w;
    MtkWindow *win = w->win;
    MtkApp *app = win->app;
    MtkPalette *p = &app->pal;
    MtkMenu *m = &pd->mb->menus[pd->menu];

    mtk_fill_rect(win, w->x, w->y, w->w, w->h, p->bg);
    mtk_draw_bevel(win, w->x, w->y, w->w, w->h, MTK_BEVEL, false);

    int rh = item_row_h(app);
    int cy = w->y + MTK_BEVEL + 2;
    for (int i = 0; i < m->n; i++) {
        if (entry_is_sep(&m->entries[i])) {
            int sy = cy + SEP_H / 2 - 1;
            mtk_fill_rect(win, w->x + 3, sy, w->w - 6, 1, p->bottom_shadow);
            mtk_fill_rect(win, w->x + 3, sy + 1, w->w - 6, 1, p->top_shadow);
            cy += SEP_H;
            continue;
        }
        unsigned long fg = p->text;
        if (i == pd->hover) {
            mtk_fill_rect(win, w->x + MTK_BEVEL, cy, w->w - 2 * MTK_BEVEL,
                          rh, p->primary);
            fg = p->primary_text;
        }
        mtk_draw_text_centered(win, app->font, w->x + ITEM_PAD, cy, rh,
                               m->entries[i].label, fg);
        if (m->entries[i].accel) {
            int aw = mtk_text_width(app->font, m->entries[i].accel);
            mtk_draw_text_centered(win, app->font,
                                   w->x + w->w - ITEM_PAD - aw, cy, rh,
                                   m->entries[i].accel,
                                   i == pd->hover ? p->primary_text
                                                  : p->bottom_shadow);
        }
        cy += rh;
    }
}

/* Pick item and close; the on_pick callback runs after teardown. */
static void pulldown_pick(Pulldown *pd, int item)
{
    if (item < 0)
        return;
    MtkMenuBar *mb = pd->mb;
    int menu = pd->menu;
    menubar_close(mb); /* destroys this widget's window (deferred) */
    if (mb->on_pick)
        mb->on_pick(mb, menu, item, mb->data);
}

/* Move the hover to the next pickable item in `dir`, wrapping. */
static void pulldown_move(Pulldown *pd, int dir)
{
    MtkMenu *m = &pd->mb->menus[pd->menu];
    if (m->n == 0)
        return;
    int i = pd->hover;
    for (int steps = 0; steps < m->n; steps++) {
        i = ((i < 0 ? (dir > 0 ? -1 : 0) : i) + dir + m->n) % m->n;
        if (!entry_is_sep(&m->entries[i])) {
            pd->hover = i;
            mtk_window_damage(pd->base.win);
            return;
        }
    }
}

static bool pulldown_event(MtkWidget *w, XEvent *ev)
{
    Pulldown *pd = (Pulldown *)w;

    switch (ev->type) {
    case MotionNotify: {
        int hover = item_at(pd->mb, pd->menu, ev->xmotion.y - w->y);
        if (hover != pd->hover) {
            pd->hover = hover;
            mtk_window_damage(w->win);
        }
        return true;
    }
    case ButtonPress:
        pd->hover = item_at(pd->mb, pd->menu, ev->xbutton.y - w->y);
        mtk_window_damage(w->win);
        return true;
    case ButtonRelease:
        pulldown_pick(pd, item_at(pd->mb, pd->menu, ev->xbutton.y - w->y));
        return true;
    }
    return false;
}

static void pulldown_destroy(MtkWidget *w)
{
    free(w);
}

static const MtkWidgetOps pulldown_ops = {
    .draw = pulldown_draw,
    .event = pulldown_event,
    .destroy = pulldown_destroy,
};

/* -------------------------------------------------------- open and close */

static void menubar_close(MtkMenuBar *mb)
{
    if (!mb->popup)
        return;
    MtkApp *app = mb->base.win->app;
    XUngrabPointer(app->dpy, CurrentTime);
    XUngrabKeyboard(app->dpy, CurrentTime);
    mtk_window_destroy(mb->popup);
    mb->popup = nullptr;
    mb->open = -1;
    mtk_window_damage(mb->base.win);
}

static bool popup_key(MtkWindow *win, XKeyEvent *ev, KeySym sym,
                      const char *text)
{
    (void)text;
    MtkMenuBar *mb = win->user;
    Pulldown *pd = win->root.nchildren > 0
                       ? (Pulldown *)win->root.children[0]
                       : nullptr;
    if (!pd)
        return false;

    if (ev->state & Mod1Mask) /* Alt+mnemonic switches menus */
        return mtk_menubar_key(mb, ev, sym);

    switch (sym) {
    case XK_Escape:
        menubar_close(mb);
        return true;
    case XK_Up:
        pulldown_move(pd, -1);
        return true;
    case XK_Down:
        pulldown_move(pd, 1);
        return true;
    case XK_Left:
    case XK_Right: {
        int dir = sym == XK_Left ? -1 : 1;
        int next = (pd->menu + dir + mb->nmenus) % mb->nmenus;
        menubar_open_at(mb, next, true);
        return true;
    }
    case XK_Return:
    case XK_KP_Enter:
    case XK_space:
        pulldown_pick(pd, pd->hover);
        return true;
    }
    return false;
}

/* A grabbed click that hit nothing: outside the pulldown.  Close, or
 * switch menus when the click lands on another menubar title. */
static void popup_unhandled_press(MtkWindow *win, XButtonEvent *ev)
{
    MtkMenuBar *mb = win->user;
    MtkWindow *owner = mb->base.win;
    MtkApp *app = owner->app;

    int wx, wy;
    Window child;
    XTranslateCoordinates(app->dpy, app->xroot, owner->xwin,
                          ev->x_root, ev->y_root, &wx, &wy, &child);
    int was_open = mb->open;
    if (mtk_widget_contains(&mb->base, wx, wy)) {
        int idx = title_at(mb, wx);
        menubar_close(mb);
        if (idx >= 0 && idx != was_open)
            menubar_open_at(mb, idx, false);
        return;
    }
    menubar_close(mb);
}

static void menubar_open_at(MtkMenuBar *mb, int idx, bool keyboard)
{
    MtkWindow *owner = mb->base.win;
    MtkApp *app = owner->app;

    menubar_close(mb);

    int pw, ph;
    pulldown_size(mb, idx, &pw, &ph);

    int ox, oy;
    Window child;
    XTranslateCoordinates(app->dpy, owner->xwin, app->xroot, 0, 0,
                          &ox, &oy, &child);
    int px = ox + title_x(mb, idx);
    int py = oy + mb->base.y + mb->base.h;
    int screen_w = DisplayWidth(app->dpy, app->screen);
    if (px + pw > screen_w)
        px = screen_w - pw;
    if (px < 0)
        px = 0;

    MtkWindow *popup = mtk_window_create_popup(app, px, py, pw, ph);
    popup->user = mb;
    popup->on_key = popup_key;
    popup->on_unhandled_press = popup_unhandled_press;

    Pulldown *pd = calloc(1, sizeof(*pd));
    mtk_widget_init(&pd->base, popup, nullptr, &pulldown_ops);
    pd->mb = mb;
    pd->menu = idx;
    pd->hover = -1;
    mtk_widget_set_rect(&pd->base, 0, 0, pw, ph);
    if (keyboard) /* start with the first item selected */
        pulldown_move(pd, 1);

    mtk_window_show(popup);
    XRaiseWindow(app->dpy, popup->xwin);
    XGrabPointer(app->dpy, popup->xwin, False,
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(app->dpy, popup->xwin, False, GrabModeAsync,
                  GrabModeAsync, CurrentTime);

    mb->popup = popup;
    mb->open = idx;
    mtk_window_damage(owner);
}

/* ---------------------------------------------------------------- widget */

static void menubar_draw(MtkWidget *w)
{
    MtkMenuBar *mb = (MtkMenuBar *)w;
    MtkWindow *win = w->win;
    MtkApp *app = win->app;
    MtkPalette *p = &app->pal;
    Display *dpy = app->dpy;

    mtk_fill_rect(win, w->x, w->y, w->w, w->h, p->primary);
    mtk_draw_bevel_c(win, w->x, w->y, w->w, w->h, 1, false,
                     p->primary_top, p->primary_bottom);

    XFontSetExtents *ex = XExtentsOfFontSet(app->font_bold);
    int ascent = -ex->max_logical_extent.y;
    int descent = ex->max_logical_extent.height - ascent;
    int baseline = w->y + (w->h + ascent - descent) / 2;

    for (int i = 0; i < mb->nmenus; i++) {
        const char *title = mb->menus[i].title;
        int tx = title_x(mb, i);
        int tw = title_width(app, title);
        if (i == mb->open)
            mtk_draw_bevel_c(win, tx, w->y + 2, tw, w->h - 4, 1, true,
                             p->primary_top, p->primary_bottom);
        int lw = mtk_text_width(app->font_bold, title);
        int lx = tx + (tw - lw) / 2;
        mtk_draw_text_centered(win, app->font_bold, lx, w->y, w->h, title,
                               p->primary_text);
        if (mb->menus[i].mnemonic) {
            /* Motif mnemonic: underline the first character */
            int n = mtk_utf8_next(title, (int)strlen(title), 0);
            int cw = XmbTextEscapement(app->font_bold, title, n);
            XSetForeground(dpy, win->gc, p->primary_text);
            XDrawLine(dpy, win->back, win->gc, lx, baseline + 2,
                      lx + cw - 1, baseline + 2);
        }
    }
}

static bool menubar_event(MtkWidget *w, XEvent *ev)
{
    MtkMenuBar *mb = (MtkMenuBar *)w;
    if (ev->type != ButtonPress || ev->xbutton.button != Button1)
        return false;
    int idx = title_at(mb, ev->xbutton.x);
    if (idx < 0)
        return true;
    if (idx == mb->open)
        menubar_close(mb);
    else
        menubar_open_at(mb, idx, false);
    return true;
}

static void menubar_destroy(MtkWidget *w)
{
    MtkMenuBar *mb = (MtkMenuBar *)w;
    menubar_close(mb);
    for (int i = 0; i < mb->nmenus; i++) {
        for (int e = 0; e < mb->menus[i].n; e++) {
            free((char *)mb->menus[i].entries[e].label);
            free((char *)mb->menus[i].entries[e].accel);
        }
        free(mb->menus[i].entries);
        free(mb->menus[i].title);
    }
    free(mb->menus);
    free(mb);
}

static void menubar_measure(MtkWidget *w, int *nw, int *nh)
{
    (void)w;
    *nw = -1;
    *nh = MTK_MENUBAR_H;
}

static const MtkWidgetOps menubar_ops = {
    .draw = menubar_draw,
    .event = menubar_event,
    .destroy = menubar_destroy,
    .measure = menubar_measure,
};

MtkMenuBar *mtk_menubar_create(MtkWindow *win, MtkWidget *parent,
                               void (*on_pick)(MtkMenuBar *mb, int menu,
                                               int item, void *data),
                               void *data)
{
    MtkMenuBar *mb = calloc(1, sizeof(*mb));
    mtk_widget_init(&mb->base, win, parent, &menubar_ops);
    mb->open = -1;
    mb->help_menu = -1;
    mb->on_pick = on_pick;
    mb->data = data;
    return mb;
}

int mtk_menubar_add(MtkMenuBar *mb, const char *title,
                    const MtkMenuEntry *entries, int n)
{
    mb->menus = realloc(mb->menus,
                        sizeof(MtkMenu) * (size_t)(mb->nmenus + 1));
    MtkMenu *m = &mb->menus[mb->nmenus];
    m->title = mtk_strdup(title);
    m->mnemonic = isalpha((unsigned char)title[0])
                      ? (KeySym)tolower((unsigned char)title[0])
                      : NoSymbol;
    m->entries = calloc((size_t)n, sizeof(MtkMenuEntry));
    m->n = n;
    for (int i = 0; i < n; i++) {
        m->entries[i].label = mtk_strdup(entries[i].label);
        m->entries[i].accel =
            entries[i].accel ? mtk_strdup(entries[i].accel) : nullptr;
    }
    mtk_window_damage(mb->base.win);
    return mb->nmenus++;
}

void mtk_menubar_set_help(MtkMenuBar *mb, int menu)
{
    mb->help_menu = menu;
    mtk_window_damage(mb->base.win);
}

bool mtk_menubar_key(MtkMenuBar *mb, XKeyEvent *ev, KeySym sym)
{
    if (sym == XK_F10 && mb->nmenus > 0) {
        if (mb->open == 0)
            menubar_close(mb);
        else
            menubar_open_at(mb, 0, true);
        return true;
    }
    if (!(ev->state & Mod1Mask))
        return false;
    KeySym low = sym >= XK_A && sym <= XK_Z ? sym + 0x20 : sym;
    for (int i = 0; i < mb->nmenus; i++) {
        if (mb->menus[i].mnemonic != NoSymbol &&
            mb->menus[i].mnemonic == low) {
            if (mb->open == i)
                menubar_close(mb);
            else
                menubar_open_at(mb, i, true);
            return true;
        }
    }
    return false;
}
