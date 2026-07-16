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
#include <locale.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <X11/Xatom.h>
#include <X11/Xresource.h>

#include "internal.h"

static const MtkTheme themes[] = {
    /* bluish grey, mwm-ish */
    { .name = "steel", .label = "Steel", .alias = nullptr,
      .body    = { .bg = 0xaeb2c3 },
      .surface = { .bg = 0 },
      .primary = { .bg = 0x5a648c, .light_text = true },
      .active  = 0 },
    /* CDE: clay body, slate wells, muted-clay canvas, teal primary,
     * orange active */
    { .name = "desert", .label = "Desert", .alias = "cde",
      .body    = { .bg = 0xc6b2a8, .shadow = 0x6a605a,
                   .highlight = 0xe7deda },
      .surface = { .bg = 0x686f82, .shadow = 0x31353e,
                   .highlight = 0xbabdc6, .light_text = true },
      .muted   = { .bg = 0xa8988f },
      .primary = { .bg = 0x4992a7, .shadow = 0x244953,
                   .highlight = 0xadced7, .light_text = true },
      .active  = 0xeda870 },
    /* IceWM Motif look: grey body, white wells, near-black primary */
    { .name = "platinum", .label = "Platinum", .alias = "ice",
      .body    = { .bg = 0xd8d8d8 },
      .surface = { .bg = 0xffffff },
      .primary = { .bg = 0x3c3c3c, .light_text = true },
      .active  = 0 },
    /* dark: charcoal body, near-black wells, slate-blue primary,
     * amber active */
    { .name = "graphite", .label = "Graphite", .alias = nullptr,
      .body    = { .bg = 0x3a3d42, .light_text = true },
      .surface = { .bg = 0x26282d, .light_text = true },
      .muted   = { .bg = 0x303338, .light_text = true },
      .primary = { .bg = 0x46708e, .light_text = true },
      .active  = 0xc9834a },
};

const MtkTheme *mtk_theme_find(const char *name)
{
    if (!name)
        return nullptr;
    for (size_t i = 0; i < sizeof(themes) / sizeof(themes[0]); i++)
        if (!strcmp(themes[i].name, name) ||
            (themes[i].alias && !strcmp(themes[i].alias, name)))
            return &themes[i];
    return nullptr;
}

const MtkTheme *mtk_theme_default(void)
{
    const MtkTheme *t = mtk_theme_find(getenv("MTK_THEME"));
    return t ? t : &themes[0];
}

char *mtk_strdup(const char *s)
{
    if (!s)
        s = "";
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

uint64_t mtk_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

unsigned long mtk_alloc_color(MtkApp *app, uint32_t rgb)
{
    XColor c = {
        .red   = (unsigned short)(((rgb >> 16) & 0xff) * 257),
        .green = (unsigned short)(((rgb >> 8) & 0xff) * 257),
        .blue  = (unsigned short)((rgb & 0xff) * 257),
        .flags = DoRed | DoGreen | DoBlue,
    };
    if (!XAllocColor(app->dpy, app->cmap, &c))
        return BlackPixel(app->dpy, app->screen);
    return c.pixel;
}

unsigned long mtk_color(MtkApp *app, uint32_t rgb)
{
    return mtk_alloc_color(app, rgb);
}

unsigned long mtk_shade(MtkApp *app, uint32_t rgb, double factor)
{
    int r = (int)(((rgb >> 16) & 0xff) * factor);
    int g = (int)(((rgb >> 8) & 0xff) * factor);
    int b = (int)((rgb & 0xff) * factor);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return mtk_alloc_color(app, (uint32_t)(r << 16 | g << 8 | b));
}

static uint32_t rgb_shade(uint32_t rgb, double factor)
{
    int r = (int)(((rgb >> 16) & 0xff) * factor);
    int g = (int)(((rgb >> 8) & 0xff) * factor);
    int b = (int)((rgb & 0xff) * factor);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (uint32_t)(r << 16 | g << 8 | b);
}

static void group_colors(MtkApp *app, const MtkThemeGroup *g,
                         uint32_t default_bg, unsigned long *bg,
                         unsigned long *top, unsigned long *bottom,
                         unsigned long *text)
{
    uint32_t base = g->bg ? g->bg : default_bg;
    *bg = mtk_alloc_color(app, base);
    *top = mtk_alloc_color(app, g->highlight ? g->highlight
                                             : rgb_shade(base, 1.45));
    *bottom = mtk_alloc_color(app, g->shadow ? g->shadow
                                             : rgb_shade(base, 0.45));
    *text = g->light_text ? WhitePixel(app->dpy, app->screen)
                          : BlackPixel(app->dpy, app->screen);
}

void mtk_app_set_theme(MtkApp *app, const MtkTheme *theme)
{
    if (!theme)
        theme = mtk_theme_default();
    MtkPalette *p = &app->pal;
    uint32_t body = theme->body.bg;

    uint32_t surface_base = theme->surface.bg ? theme->surface.bg
                                              : rgb_shade(body, 1.28);

    group_colors(app, &theme->body, body, &p->bg, &p->top_shadow,
                 &p->bottom_shadow, &p->text);
    group_colors(app, &theme->surface, surface_base, &p->surface,
                 &p->surface_top, &p->surface_bottom, &p->surface_text);
    if (theme->muted.bg) {
        group_colors(app, &theme->muted, surface_base, &p->muted,
                     &p->muted_top, &p->muted_bottom, &p->muted_text);
    } else { /* default: canvas wells match the surface tone */
        MtkThemeGroup g = theme->surface;
        group_colors(app, &g, surface_base, &p->muted, &p->muted_top,
                     &p->muted_bottom, &p->muted_text);
    }
    group_colors(app, &theme->primary, body, &p->primary, &p->primary_top,
                 &p->primary_bottom, &p->primary_text);
    p->select = mtk_alloc_color(app, rgb_shade(body, 0.84));
    p->active = mtk_alloc_color(app, theme->active ? theme->active
                                                   : rgb_shade(body, 0.84));
    /* focus outline must contrast the body */
    p->highlight = theme->body.light_text
                       ? WhitePixel(app->dpy, app->screen)
                       : BlackPixel(app->dpy, app->screen);
    p->white = WhitePixel(app->dpy, app->screen);
    p->black = BlackPixel(app->dpy, app->screen);
    for (MtkWindow *w = app->windows; w; w = w->next) {
        XSetWindowBackground(app->dpy, w->xwin, p->bg);
        mtk_window_damage(w);
    }
}

/* A font set: the classic bitmap fonts, with misc-fixed (broad
 * iso10646 coverage) filling in whatever helvetica lacks. */
static XFontSet load_fontset(Display *dpy, const char *pattern)
{
    char **missing = nullptr;
    int nmissing = 0;
    char *def = nullptr;
    XFontSet fs = XCreateFontSet(dpy, pattern, &missing, &nmissing, &def);
    if (missing)
        XFreeStringList(missing);
    return fs;
}

/*
 * Look up the theme in the X resource database the way Motif did:
 * instance "name.mtkTheme", class "Name.MtkTheme", so both
 * "myapp*mtkTheme: desert" and "myapp*MtkTheme: desert" match, and
 * "*MtkTheme: graphite" themes every libmtk application.  Reads the
 * xrdb-loaded RESOURCE_MANAGER property, falling back to
 * ~/.Xdefaults when xrdb has not been run.
 */
static const MtkTheme *theme_from_resources(Display *dpy,
                                            const char *res_name)
{
    XrmInitialize();
    XrmDatabase db = nullptr;
    const char *rms = XResourceManagerString(dpy);
    if (rms) {
        db = XrmGetStringDatabase(rms);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/.Xdefaults", home);
            db = XrmGetFileDatabase(path);
        }
    }
    if (!db)
        return nullptr;

    char names[256], classes[256];
    snprintf(names, sizeof(names), "%s.mtkTheme", res_name);
    snprintf(classes, sizeof(classes), "%c%s.MtkTheme",
             toupper((unsigned char)res_name[0]), res_name + 1);

    const MtkTheme *theme = nullptr;
    char *type = nullptr;
    XrmValue val = {0};
    if (XrmGetResource(db, names, classes, &type, &val) && val.addr) {
        char buf[64] = {0};
        size_t n = val.size < sizeof(buf) - 1 ? val.size : sizeof(buf) - 1;
        memcpy(buf, val.addr, n);
        buf[strcspn(buf, " \t\r\n")] = '\0';
        theme = mtk_theme_find(buf);
    }
    XrmDestroyDatabase(db);
    return theme;
}

MtkApp *mtk_app_create(const char *res_name)
{
    setlocale(LC_ALL, "");
    if (!XSupportsLocale())
        setlocale(LC_ALL, "C");
    XSetLocaleModifiers("");

    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        fprintf(stderr, "mtk: cannot open display\n");
        return nullptr;
    }
    MtkApp *app = calloc(1, sizeof(*app));
    app->dpy = dpy;
    app->screen = DefaultScreen(dpy);
    app->xroot = RootWindow(dpy, app->screen);
    app->visual = DefaultVisual(dpy, app->screen);
    app->depth = DefaultDepth(dpy, app->screen);
    app->cmap = DefaultColormap(dpy, app->screen);
    app->fmt_window = XRenderFindVisualFormat(dpy, app->visual);
    app->fmt_argb32 = XRenderFindStandardFormat(dpy, PictStandardARGB32);
    app->next_timer_id = 1;

    /* X resources first (xrdb: "name*MtkTheme: desert"), then
     * $MTK_THEME, then the built-in default. */
    const MtkTheme *theme = nullptr;
    if (res_name && res_name[0])
        theme = theme_from_resources(dpy, res_name);
    mtk_app_set_theme(app, theme ? theme : mtk_theme_default());

    /* No trailing "*": charsets no font really covers should render
     * as blanks, not as random glyphs from an arbitrary fallback. */
    app->font = load_fontset(dpy,
        "-*-helvetica-medium-r-normal--12-*-*-*-*-*-*-*,"
        "-misc-fixed-medium-r-normal--13-*-*-*-*-*-*-*");
    app->font_bold = load_fontset(dpy,
        "-*-helvetica-bold-r-normal--12-*-*-*-*-*-*-*,"
        "-misc-fixed-bold-r-normal--13-*-*-*-*-*-*-*");
    if (!app->font_bold)
        app->font_bold = app->font;
    if (!app->font) {
        fprintf(stderr, "mtk: no usable font set\n");
        XCloseDisplay(dpy);
        free(app);
        return nullptr;
    }

    app->im = XOpenIM(dpy, nullptr, nullptr, nullptr);

    app->wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    app->wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    app->net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    app->net_wm_state_fullscreen =
        XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    app->net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    app->utf8_string = XInternAtom(dpy, "UTF8_STRING", False);
    return app;
}

int mtk_utf8_next(const char *s, int len, int i)
{
    if (i < len) {
        i++;
        while (i < len && (s[i] & 0xc0) == 0x80)
            i++;
    }
    return i;
}

int mtk_utf8_prev(const char *s, int i)
{
    if (i > 0) {
        i--;
        while (i > 0 && (s[i] & 0xc0) == 0x80)
            i--;
    }
    return i;
}

/* ---------------------------------------------------------------- widget */

void mtk_widget_init(MtkWidget *w, MtkWindow *win, MtkWidget *parent,
                     const MtkWidgetOps *ops)
{
    memset(w, 0, sizeof(*w));
    w->ops = ops;
    w->win = win;
    w->visible = true;
    if (!parent && win != nullptr && w != &win->root)
        parent = &win->root;
    w->parent = parent;
    if (parent) {
        if (parent->nchildren == parent->childcap) {
            parent->childcap = parent->childcap ? parent->childcap * 2 : 4;
            parent->children = realloc(parent->children,
                sizeof(MtkWidget *) * (size_t)parent->childcap);
        }
        parent->children[parent->nchildren++] = w;
    }
}

void mtk_widget_set_rect(MtkWidget *w, int x, int y, int width, int height)
{
    w->x = x;
    w->y = y;
    w->w = width;
    w->h = height;
    mtk_window_damage(w->win);
}

void mtk_widget_set_visible(MtkWidget *w, bool visible)
{
    if (w->visible == visible)
        return;
    w->visible = visible;
    mtk_window_damage(w->win);
}

bool mtk_widget_contains(const MtkWidget *w, int x, int y)
{
    return x >= w->x && y >= w->y && x < w->x + w->w && y < w->y + w->h;
}

static bool widget_has(MtkWidget *root, MtkWidget *w)
{
    if (root == w)
        return true;
    for (int i = 0; i < root->nchildren; i++)
        if (widget_has(root->children[i], w))
            return true;
    return false;
}

void mtk_widget_destroy(MtkWidget *w)
{
    MtkWindow *win = w->win;
    if (win) {
        if (win->grab && widget_has(w, win->grab))
            win->grab = nullptr;
        if (win->focus && widget_has(w, win->focus))
            win->focus = nullptr;
    }
    while (w->nchildren > 0)
        mtk_widget_destroy(w->children[w->nchildren - 1]);
    free(w->children);
    w->children = nullptr;
    if (w->parent) {
        MtkWidget *p = w->parent;
        for (int i = 0; i < p->nchildren; i++) {
            if (p->children[i] == w) {
                memmove(&p->children[i], &p->children[i + 1],
                        sizeof(MtkWidget *) * (size_t)(p->nchildren - i - 1));
                p->nchildren--;
                break;
            }
        }
        w->parent = nullptr;
    }
    if (win)
        mtk_window_damage(win);
    if (w->ops && w->ops->destroy)
        w->ops->destroy(w); /* frees widget memory; do not touch w after */
}

/* ---------------------------------------------------------------- window */

static void window_alloc_backbuffer(MtkWindow *win)
{
    MtkApp *app = win->app;
    if (win->back && win->back_w >= win->w && win->back_h >= win->h)
        return;
    if (win->back_pict)
        XRenderFreePicture(app->dpy, win->back_pict);
    if (win->back)
        XFreePixmap(app->dpy, win->back);
    win->back_w = win->w + 64;
    win->back_h = win->h + 64;
    win->back = XCreatePixmap(app->dpy, win->xwin,
                              (unsigned)win->back_w, (unsigned)win->back_h,
                              (unsigned)app->depth);
    win->back_pict = XRenderCreatePicture(app->dpy, win->back,
                                          app->fmt_window, 0, nullptr);
}

static MtkWindow *window_create_common(MtkApp *app, int x, int y,
                                       int width, int height, bool popup)
{
    MtkWindow *win = calloc(1, sizeof(*win));
    win->app = app;
    win->w = width;
    win->h = height;
    win->popup = popup;

    XSetWindowAttributes attrs = {
        .background_pixel = app->pal.bg,
        .event_mask = ExposureMask | StructureNotifyMask |
                      ButtonPressMask | ButtonReleaseMask |
                      PointerMotionMask | KeyPressMask,
        .override_redirect = popup,
        .save_under = popup,
    };
    win->xwin = XCreateWindow(app->dpy, app->xroot, x, y,
                              (unsigned)width, (unsigned)height, 0,
                              app->depth, InputOutput, app->visual,
                              CWBackPixel | CWEventMask |
                                  (popup ? CWOverrideRedirect | CWSaveUnder
                                         : 0UL),
                              &attrs);
    if (!popup)
        XSetWMProtocols(app->dpy, win->xwin, &app->wm_delete_window, 1);
    win->gc = XCreateGC(app->dpy, win->xwin, 0, nullptr);
    if (app->im) {
        win->xic = XCreateIC(app->im,
                             XNInputStyle,
                             XIMPreeditNothing | XIMStatusNothing,
                             XNClientWindow, win->xwin,
                             XNFocusWindow, win->xwin,
                             nullptr);
        if (win->xic)
            XSetICFocus(win->xic);
    }
    window_alloc_backbuffer(win);

    mtk_widget_init(&win->root, win, nullptr, nullptr);
    win->root.w = width;
    win->root.h = height;
    win->damage = true;

    win->next = app->windows;
    app->windows = win;
    return win;
}

MtkWindow *mtk_window_create(MtkApp *app, const char *title,
                             int width, int height)
{
    MtkWindow *win = window_create_common(app, 0, 0, width, height, false);
    mtk_window_set_title(win, title);
    return win;
}

MtkWindow *mtk_window_create_popup(MtkApp *app, int x, int y,
                                   int width, int height)
{
    return window_create_common(app, x, y, width, height, true);
}

void mtk_window_show(MtkWindow *win)
{
    XMapWindow(win->app->dpy, win->xwin);
    win->mapped = true;
}

void mtk_window_set_title(MtkWindow *win, const char *title)
{
    MtkApp *app = win->app;
    if (!title)
        title = "";
    XStoreName(app->dpy, win->xwin, title);
    XSetIconName(app->dpy, win->xwin, title);
    /* modern WMs read the UTF-8 title from _NET_WM_NAME */
    XChangeProperty(app->dpy, win->xwin, app->net_wm_name,
                    app->utf8_string, 8, PropModeReplace,
                    (const unsigned char *)title,
                    (int)strlen(title));
}

void mtk_window_set_fullscreen(MtkWindow *win, bool fullscreen)
{
    MtkApp *app = win->app;
    XEvent ev = {0};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = win->xwin;
    ev.xclient.message_type = app->net_wm_state;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = fullscreen ? 1 : 0; /* _NET_WM_STATE_ADD/REMOVE */
    ev.xclient.data.l[1] = (long)app->net_wm_state_fullscreen;
    ev.xclient.data.l[2] = 0;
    ev.xclient.data.l[3] = 1;
    XSendEvent(app->dpy, app->xroot, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    win->fullscreen = fullscreen;
}

void mtk_window_damage(MtkWindow *win)
{
    if (win)
        win->damage = true;
}

void mtk_window_set_focus(MtkWindow *win, MtkWidget *w)
{
    if (win->focus == w)
        return;
    win->focus = w;
    mtk_window_damage(win);
}

void mtk_window_destroy(MtkWindow *win)
{
    win->dying = true;
}

static void window_free(MtkApp *app, MtkWindow *win)
{
    if (win->on_destroy)
        win->on_destroy(win);
    while (win->root.nchildren > 0)
        mtk_widget_destroy(win->root.children[win->root.nchildren - 1]);
    free(win->root.children);
    if (win->back_pict)
        XRenderFreePicture(app->dpy, win->back_pict);
    if (win->back)
        XFreePixmap(app->dpy, win->back);
    if (win->xic)
        XDestroyIC(win->xic);
    XFreeGC(app->dpy, win->gc);
    XDestroyWindow(app->dpy, win->xwin);

    for (MtkWindow **p = &app->windows; *p; p = &(*p)->next) {
        if (*p == win) {
            *p = win->next;
            break;
        }
    }
    free(win);
}

static void sweep_dying(MtkApp *app)
{
    MtkWindow *win = app->windows;
    while (win) {
        MtkWindow *next = win->next;
        if (win->dying)
            window_free(app, win);
        win = next;
    }
}

/* ----------------------------------------------------------------- paint */

static void draw_widget_rec(MtkWidget *w)
{
    if (!w->visible)
        return;
    if (w->ops && w->ops->draw)
        w->ops->draw(w);
    for (int i = 0; i < w->nchildren; i++)
        draw_widget_rec(w->children[i]);
}

static void window_paint(MtkWindow *win)
{
    MtkApp *app = win->app;
    window_alloc_backbuffer(win);
    XSetForeground(app->dpy, win->gc, app->pal.bg);
    XFillRectangle(app->dpy, win->back, win->gc, 0, 0,
                   (unsigned)win->w, (unsigned)win->h);
    draw_widget_rec(&win->root);
    XCopyArea(app->dpy, win->back, win->xwin, win->gc, 0, 0,
              (unsigned)win->w, (unsigned)win->h, 0, 0);
    win->damage = false;
}

/* -------------------------------------------------------------- dispatch */

static MtkWindow *find_window(MtkApp *app, Window xwin)
{
    for (MtkWindow *w = app->windows; w; w = w->next)
        if (w->xwin == xwin)
            return w;
    return nullptr;
}

static MtkWidget *pick_widget(MtkWidget *w, int x, int y)
{
    if (!w->visible || !mtk_widget_contains(w, x, y))
        return nullptr;
    for (int i = w->nchildren - 1; i >= 0; i--) {
        MtkWidget *hit = pick_widget(w->children[i], x, y);
        if (hit)
            return hit;
    }
    return (w->ops && w->ops->event) ? w : nullptr;
}

static void deliver_mouse(MtkWidget *w, XEvent *ev)
{
    if (w && w->ops && w->ops->event)
        w->ops->event(w, ev);
}

static void handle_button_press(MtkWindow *win, XEvent *ev)
{
    XButtonEvent *b = &ev->xbutton;
    MtkWidget *hit = pick_widget(&win->root, b->x, b->y);

    if (b->button >= 4 && b->button <= 7) { /* scroll wheel */
        win->click_double = false;
        /* bubble until consumed, so e.g. a spinbox steps when the
         * wheel turns over its embedded entry */
        for (MtkWidget *t = hit; t; t = t->parent)
            if (t->ops && t->ops->event && t->ops->event(t, ev))
                break;
        return;
    }

    win->click_double =
        b->button == win->last_click_button &&
        b->time - win->last_click_time < 400 &&
        abs(b->x - win->last_click_x) < 6 &&
        abs(b->y - win->last_click_y) < 6;
    if (win->click_double) {
        win->last_click_time = 0; /* triple click starts over */
    } else {
        win->last_click_time = b->time;
        win->last_click_x = b->x;
        win->last_click_y = b->y;
        win->last_click_button = b->button;
    }

    mtk_window_set_focus(win, hit && hit->can_focus ? hit : nullptr);
    win->grab = hit;
    if (hit)
        deliver_mouse(hit, ev);
    else if (win->on_unhandled_press)
        win->on_unhandled_press(win, b);
    win->click_double = false;
}

static void dispatch_event(MtkApp *app, XEvent *ev)
{
    MtkWindow *win = find_window(app, ev->xany.window);
    if (!win || win->dying)
        return;

    switch (ev->type) {
    case Expose:
        if (ev->xexpose.count == 0)
            win->damage = true;
        break;
    case ConfigureNotify:
        if (ev->xconfigure.width != win->w ||
            ev->xconfigure.height != win->h) {
            win->w = ev->xconfigure.width;
            win->h = ev->xconfigure.height;
            win->root.w = win->w;
            win->root.h = win->h;
            if (win->on_resize)
                win->on_resize(win);
            win->damage = true;
        }
        break;
    case ButtonPress:
        handle_button_press(win, ev);
        break;
    case ButtonRelease: {
        /* No grab (press landed outside every widget, e.g. a menu
         * opened from another window): route by position so
         * press-drag-release onto a widget still works. */
        MtkWidget *target = win->grab
                                ? win->grab
                                : pick_widget(&win->root, ev->xbutton.x,
                                              ev->xbutton.y);
        if (ev->xbutton.button <= 3)
            win->grab = nullptr;
        deliver_mouse(target, ev);
        break;
    }
    case MotionNotify:
        /* Compress queued motion. */
        while (XCheckTypedWindowEvent(app->dpy, win->xwin, MotionNotify, ev))
            ;
        deliver_mouse(win->grab ? win->grab
                                : pick_widget(&win->root,
                                              ev->xmotion.x, ev->xmotion.y),
                      ev);
        break;
    case KeyPress: {
        char buf[64] = {0};
        KeySym sym = NoSymbol;
        if (win->xic) {
            Status st;
            int n = XmbLookupString(win->xic, &ev->xkey, buf,
                                    (int)sizeof(buf) - 1, &sym, &st);
            if (st == XBufferOverflow || (st != XLookupChars &&
                                          st != XLookupBoth))
                n = 0;
            buf[n > 0 ? n : 0] = '\0';
        } else {
            XLookupString(&ev->xkey, buf, sizeof(buf) - 1, &sym, nullptr);
        }
        bool handled = false;
        if (win->focus && win->focus->ops && win->focus->ops->key)
            handled = win->focus->ops->key(win->focus, &ev->xkey, sym, buf);
        if (!handled && win->on_key)
            win->on_key(win, &ev->xkey, sym, buf);
        break;
    }
    case ClientMessage:
        if (ev->xclient.message_type == app->wm_protocols &&
            (Atom)ev->xclient.data.l[0] == app->wm_delete_window) {
            if (win->on_close)
                win->on_close(win);
            else
                mtk_window_destroy(win);
        }
        break;
    case MappingNotify:
        XRefreshKeyboardMapping(&ev->xmapping);
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------- timers */

int mtk_timer_add(MtkApp *app, uint32_t delay_ms,
                  void (*cb)(void *data), void *data)
{
    if (app->ntimers == app->timercap) {
        app->timercap = app->timercap ? app->timercap * 2 : 8;
        app->timers = realloc(app->timers,
                              sizeof(struct MtkTimer) * (size_t)app->timercap);
    }
    struct MtkTimer *t = &app->timers[app->ntimers++];
    t->id = app->next_timer_id++;
    t->deadline = mtk_now_ms() + delay_ms;
    t->cb = cb;
    t->data = data;
    t->dead = false;
    return t->id;
}

void mtk_timer_cancel(MtkApp *app, int id)
{
    for (int i = 0; i < app->ntimers; i++)
        if (app->timers[i].id == id)
            app->timers[i].dead = true;
}

/* Returns ms until the next timer, or -1 if none. */
static int fire_timers(MtkApp *app)
{
    uint64_t now = mtk_now_ms();
    int n = app->ntimers; /* callbacks may append; new ones fire next pass */
    for (int i = 0; i < n; i++) {
        struct MtkTimer *t = &app->timers[i];
        if (t->dead || t->deadline > now)
            continue;
        t->dead = true;
        void (*cb)(void *) = t->cb;
        void *data = t->data;
        cb(data); /* may realloc app->timers */
    }
    int out = 0;
    for (int i = 0; i < app->ntimers; i++)
        if (!app->timers[i].dead)
            app->timers[out++] = app->timers[i];
    app->ntimers = out;

    int next = -1;
    now = mtk_now_ms();
    for (int i = 0; i < app->ntimers; i++) {
        uint64_t dl = app->timers[i].deadline;
        int ms = dl > now ? (int)(dl - now) : 0;
        if (next < 0 || ms < next)
            next = ms;
    }
    return next;
}

/* ------------------------------------------------------------------ loop */

void mtk_app_set_idle(MtkApp *app, bool (*idle)(void *data), void *data)
{
    app->idle = idle;
    app->idle_data = data;
}

void mtk_app_quit(MtkApp *app)
{
    app->running = false;
}

void mtk_app_run(MtkApp *app)
{
    app->running = true;
    struct pollfd pfd = { .fd = ConnectionNumber(app->dpy), .events = POLLIN };

    while (app->running && app->windows) {
        while (XPending(app->dpy)) {
            XEvent ev;
            XNextEvent(app->dpy, &ev);
            if (XFilterEvent(&ev, None))
                continue; /* consumed by the input method */
            dispatch_event(app, &ev);
        }
        int next_timer = fire_timers(app);
        sweep_dying(app);
        if (!app->running || !app->windows)
            break;

        for (MtkWindow *w = app->windows; w; w = w->next)
            if (w->damage && w->mapped)
                window_paint(w);
        XFlush(app->dpy);

        if (XPending(app->dpy))
            continue;

        int timeout = next_timer;
        if (app->idle)
            timeout = 0;
        int n = poll(&pfd, 1, timeout);
        if (n == 0 && app->idle) {
            uint64_t now = mtk_now_ms();
            bool timer_due = false;
            for (int i = 0; i < app->ntimers; i++)
                if (!app->timers[i].dead && app->timers[i].deadline <= now)
                    timer_due = true;
            if (!timer_due && !app->idle(app->idle_data))
                app->idle = nullptr;
        }
    }
}

void mtk_app_destroy(MtkApp *app)
{
    while (app->windows) {
        app->windows->dying = true;
        sweep_dying(app);
    }
    free(app->timers);
    if (app->font_bold && app->font_bold != app->font)
        XFreeFontSet(app->dpy, app->font_bold);
    if (app->font)
        XFreeFontSet(app->dpy, app->font);
    if (app->im)
        XCloseIM(app->im);
    XCloseDisplay(app->dpy);
    free(app);
}
