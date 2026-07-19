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

/**
 * @file mtk.h
 * @brief libmtk — a Motif-style widget toolkit on raw Xlib.
 *
 * One X window per toplevel; widgets are lightweight rectangles
 * ("gadgets" in Motif terms) drawn into a shared double buffer.
 * The toolkit owns the event loop, timers and an idle hook.
 *
 * This is the entire public API.  Start with mtk_app_create(),
 * mtk_window_create() and mtk_app_run(); see the tutorial for a
 * guided introduction.
 */
#ifndef MTK_H
#define MTK_H

#include <stdbool.h>
#include <stdint.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/Xrender.h>

typedef struct MtkApp MtkApp;
typedef struct MtkWindow MtkWindow;
typedef struct MtkWidget MtkWidget;
/** Opaque layout node; see @ref layout. */
typedef struct MtkLay MtkLay;

/**
 * @defgroup theme Colors and themes
 * @brief The palette, theme seeds and the built-in themes.
 *
 * Colors come in four groups plus one accent:
 * - **body** — windows, buttons, panels, pulldown menus
 * - **surface** — compact content wells: entries, lists, trees
 * - **muted** — large canvas wells: icon grids, previews (defaults
 *   to the surface tone when a theme does not specify it)
 * - **primary** — menubar, selections, toggled-on buttons
 * - **active** — momentarily armed controls (pressed buttons, arrows)
 *
 * Each group carries its own bevel shadow/highlight pair and text
 * color, so dark surfaces get light text and their own shading.
 * @{
 */

/** Allocated X pixels for every UI color; read it from
 *  `widget->win->app->pal` inside draw handlers. */
typedef struct MtkPalette {
    /* body */
    unsigned long bg;            /**< body background */
    unsigned long top_shadow;    /**< body bevel light edge */
    unsigned long bottom_shadow; /**< body bevel dark edge */
    unsigned long text;          /**< text on body */
    unsigned long select;        /**< trough / inactive-tab shade */
    unsigned long active;        /**< armed control fill */
    /* surface */
    unsigned long surface;        /**< content-well background */
    unsigned long surface_top;    /**< well bevel light edge */
    unsigned long surface_bottom; /**< well bevel dark edge */
    unsigned long surface_text;   /**< text on wells */
    /* muted */
    unsigned long muted;          /**< canvas-well background */
    unsigned long muted_top;      /**< canvas bevel light edge */
    unsigned long muted_bottom;   /**< canvas bevel dark edge */
    unsigned long muted_text;     /**< text on canvas wells */
    /* primary */
    unsigned long primary;        /**< selection / menubar background */
    unsigned long primary_top;    /**< primary bevel light edge */
    unsigned long primary_bottom; /**< primary bevel dark edge */
    unsigned long primary_text;   /**< text on primary */

    unsigned long highlight;     /**< focus outline; follows body text */
    unsigned long white;         /**< white pixel */
    unsigned long black;         /**< black pixel */
} MtkPalette;

/** One theme color group; zero fields are derived from @ref bg the
 *  way Motif derived shadows from `XmNbackground`. */
typedef struct MtkThemeGroup {
    uint32_t bg;         /**< 0xRRGGBB; 0 = theme-specific default */
    uint32_t shadow;     /**< bevel dark edge; 0 = derive (bg × 0.45) */
    uint32_t highlight;  /**< bevel light edge; 0 = derive (bg × 1.45) */
    bool light_text;     /**< white text instead of black */
} MtkThemeGroup;

/**
 * A theme: the seed the palette is computed from.
 *
 * Built-in themes:
 * | name       | look                                                  |
 * |------------|-------------------------------------------------------|
 * | `steel`    | bluish grey (mwm-ish default)                        |
 * | `desert`   | CDE: clay body, slate wells, teal primary, orange active accent (alias `cde`) |
 * | `platinum` | grey/white/black IceWM Motif look (alias `ice`)      |
 * | `graphite` | dark: charcoal body, near-black wells, slate-blue primary, amber active accent |
 */
typedef struct MtkTheme {
    const char *name;    /**< lookup key */
    const char *label;   /**< human-readable name, e.g. "Desert" */
    const char *alias;   /**< alternate lookup key; may be nullptr */
    MtkThemeGroup body;      /**< windows, buttons, panels */
    MtkThemeGroup surface;   /**< content wells; bg 0 = lightened body */
    MtkThemeGroup muted;     /**< canvas wells; bg 0 = same as surface */
    MtkThemeGroup primary;   /**< menubar, selections */
    uint32_t active;         /**< armed accent; 0 = darkened body */
} MtkTheme;

/** Look up a built-in theme by name or alias.
 *  @return the theme, or nullptr if unknown. */
const MtkTheme *mtk_theme_find(const char *name);

/** The `$MTK_THEME` theme if set and valid, else `"steel"`. */
const MtkTheme *mtk_theme_default(void);

/** Recompute the palette from a theme (nullptr = default theme).
 *  May be called at any time; existing windows are damaged and
 *  repaint in the new colors. */
void mtk_app_set_theme(MtkApp *app, const MtkTheme *theme);

/** @} */

/**
 * @defgroup widget Widget base
 * @brief The gadget model every widget is built on.
 *
 * A widget is a struct embedding #MtkWidget as its **first member**
 * plus a static const #MtkWidgetOps table.  Constructors allocate;
 * `ops->destroy` frees (including the struct itself for
 * heap-allocated widgets); never touch a widget after
 * mtk_widget_destroy().
 * @{
 */

/** Virtual operations of a widget; unset slots are simply skipped. */
typedef struct MtkWidgetOps {
    /** Draw into the window back buffer (window coordinates). */
    void (*draw)(MtkWidget *w);
    /** Mouse events (ButtonPress/ButtonRelease/MotionNotify), window
     *  coordinates.  @return true if consumed. */
    bool (*event)(MtkWidget *w, XEvent *ev);
    /** Key events, delivered to the focused widget only.
     *  @param sym  the keysym, layout independent
     *  @param text the produced text, UTF-8, possibly empty
     *  @return true if consumed. */
    bool (*key)(MtkWidget *w, XKeyEvent *ev, KeySym sym, const char *text);
    /** Free widget-owned resources, including the widget memory
     *  itself when it was heap allocated by its constructor. */
    void (*destroy)(MtkWidget *w);
    /** Report the preferred size for layout (see @ref layout); write
     *  -1 on an axis that is elastic.  An unset slot means elastic in
     *  both axes (typical for custom canvases). */
    void (*measure)(MtkWidget *w, int *natural_w, int *natural_h);
} MtkWidgetOps;

/** Base of every widget; embed as the first struct member. */
struct MtkWidget {
    const MtkWidgetOps *ops; /**< virtual operations */
    MtkWindow *win;          /**< owning toplevel */
    MtkWidget *parent;       /**< parent widget (window root if none) */
    MtkWidget **children;    /**< child array, creation order = z-order */
    int nchildren;           /**< child count */
    int childcap;            /**< child array capacity */
    int x;                   /**< left edge, window coordinates */
    int y;                   /**< top edge, window coordinates */
    int w;                   /**< width */
    int h;                   /**< height */
    bool visible;            /**< hidden widgets neither draw nor hit */
    bool can_focus;          /**< clicking it takes keyboard focus */
    void *user;              /**< free for the application */
};

/** Initialize an embedded #MtkWidget base and link it into the tree.
 *  @param w      the widget base to initialize
 *  @param win    the owning toplevel
 *  @param parent parent widget, or nullptr for the window root
 *  @param ops    the widget's operations table */
void mtk_widget_init(MtkWidget *w, MtkWindow *win, MtkWidget *parent,
                     const MtkWidgetOps *ops);
/** Set geometry (window coordinates) and schedule a repaint. */
void mtk_widget_set_rect(MtkWidget *w, int x, int y, int width, int height);
/** Show or hide a widget subtree. */
void mtk_widget_set_visible(MtkWidget *w, bool visible);
/** Point-in-rectangle test in window coordinates. */
bool mtk_widget_contains(const MtkWidget *w, int x, int y);
/** Destroy a widget subtree (calls `ops->destroy` bottom-up,
 *  unlinks from the parent).  The widget is gone afterwards. */
void mtk_widget_destroy(MtkWidget *w);

/** @} */

/**
 * @defgroup window Windows
 * @brief Managed toplevels and popup windows.
 * @{
 */

/** A managed toplevel (or popup) X window. */
struct MtkWindow {
    MtkApp *app;      /**< owning application */
    Window xwin;      /**< the X window */
    XIC xic;          /**< input context; may be null (no input method) */
    int w;            /**< current width */
    int h;            /**< current height */
    Pixmap back;      /**< double buffer */
    Picture back_pict;/**< XRender picture of the back buffer */
    int back_w;       /**< back buffer width (>= window width) */
    int back_h;       /**< back buffer height (>= window height) */
    GC gc;            /**< shared GC for core drawing */
    MtkWidget root;   /**< plain container covering the window */
    MtkWidget *grab;  /**< widget owning the pointer during a drag */
    MtkWidget *focus; /**< keyboard focus, may be null */
    bool damage;      /**< repaint scheduled */
    bool mapped;      /**< shown */
    bool dying;       /**< deferred destroy pending */
    bool fullscreen;  /**< EWMH fullscreen state as last requested */
    /** True while dispatching a ButtonPress that is the second click
     *  of a double click; widgets read this in their event handler. */
    bool click_double;

    bool popup; /**< override-redirect (menus); no WM interaction */

    /** Size changed — do your layout here. */
    void (*on_resize)(MtkWindow *win);
    /** Key press not consumed by the focused widget.
     *  @return true if consumed. */
    bool (*on_key)(MtkWindow *win, XKeyEvent *ev, KeySym sym,
                   const char *text);
    /** ButtonPress that hit no widget (used by grabbing popups to
     *  detect clicks outside themselves). */
    void (*on_unhandled_press)(MtkWindow *win, XButtonEvent *ev);
    /** WM close button; if unset the window is destroyed. */
    void (*on_close)(MtkWindow *win);
    /** Called during teardown; free your `user` state here. */
    void (*on_destroy)(MtkWindow *win);
    void *user;       /**< free for the application */

    Time last_click_time;       /**< double-click tracking */
    int last_click_x;           /**< double-click tracking */
    int last_click_y;           /**< double-click tracking */
    unsigned last_click_button; /**< double-click tracking */

    MtkLay *lay;      /**< attached layout tree; may be null */

    MtkWindow *next;  /**< application window list */
};

/** Create a managed toplevel window (not yet shown). */
MtkWindow *mtk_window_create(MtkApp *app, const char *title,
                             int width, int height);
/** Create an override-redirect toplevel at root coordinates
 *  (menus, tooltips); it bypasses the window manager. */
MtkWindow *mtk_window_create_popup(MtkApp *app, int x, int y,
                                   int width, int height);
/** Map the window. */
void mtk_window_show(MtkWindow *win);
/** Set the window title (UTF-8; also sets `_NET_WM_NAME`). */
void mtk_window_set_title(MtkWindow *win, const char *title);
/** Ask the window manager for EWMH fullscreen on/off. */
void mtk_window_set_fullscreen(MtkWindow *win, bool fullscreen);
/** Destroy the window.  Safe to call from event handlers: actual
 *  teardown is deferred to the end of the loop iteration. */
void mtk_window_destroy(MtkWindow *win);
/** Schedule a full repaint of the window. */
void mtk_window_damage(MtkWindow *win);
/** Move keyboard focus to a widget (or nullptr to clear). */
void mtk_window_set_focus(MtkWindow *win, MtkWidget *w);
/** Attach a layout tree (see @ref layout).  The window takes
 *  ownership, applies it now and on every resize (before
 *  `on_resize`, which still fires afterwards for manual tweaks).
 *  Replaces and frees any previously attached tree. */
void mtk_window_set_layout(MtkWindow *win, MtkLay *root);
/** Re-apply the attached layout (after model changes).  Visibility
 *  changes trigger this automatically. */
void mtk_window_relayout(MtkWindow *win);

/** @} */

/**
 * @defgroup app Application and main loop
 * @brief Connection, fonts, palette, timers, the event loop.
 * @{
 */

/** The application: one X connection and everything shared. */
struct MtkApp {
    Display *dpy;     /**< the X connection */
    int screen;       /**< default screen */
    Window xroot;     /**< root window */
    Visual *visual;   /**< default visual */
    int depth;        /**< default depth */
    Colormap cmap;    /**< default colormap */
    XRenderPictFormat *fmt_window; /**< XRender format of windows */
    XRenderPictFormat *fmt_argb32; /**< premultiplied ARGB32 format */
    MtkPalette pal;   /**< current palette; see @ref theme */
    /** Font sets: multiple core bitmap fonts covering the locale, so
     *  text is UTF-8 while keeping the classic bitmap look. */
    XFontSet font;
    XFontSet font_bold; /**< bold variant of @ref font */
    XIM im;           /**< input method; may be null */
    Atom wm_protocols;             /**< cached atom */
    Atom wm_delete_window;         /**< cached atom */
    Atom net_wm_state;             /**< cached atom */
    Atom net_wm_state_fullscreen;  /**< cached atom */
    Atom net_wm_name;              /**< cached atom */
    Atom utf8_string;              /**< cached atom */
    MtkWindow *windows; /**< list of live windows */
    bool running;     /**< main loop flag */

    struct MtkTimer *timers; /**< pending one-shot timers */
    int ntimers;             /**< pending timer count */
    int timercap;            /**< timer array capacity */
    int next_timer_id;       /**< id source, never returns 0 */

    /** Idle hook: run when no X events or due timers are pending.
     *  @return true if more idle work remains. */
    bool (*idle)(void *data);
    void *idle_data;  /**< passed to @ref idle */
};

/**
 * Connect to the display and initialize fonts, palette and input.
 *
 * @param res_name the application's X resource name (e.g. "myapp");
 *   its class is the same with the first letter capitalized.
 *
 * The starting theme resolves in this order:
 * 1. the `mtkTheme` / `MtkTheme` X resource (set with xrdb), e.g.
 *    `myapp*MtkTheme: desert` for one application or
 *    `*MtkTheme: graphite` for every libmtk application;
 * 2. the `MTK_THEME` environment variable;
 * 3. the built-in default (`steel`).
 *
 * Applications may still override with mtk_app_set_theme().
 * @return the application, or nullptr if no display is available.
 */
MtkApp *mtk_app_create(const char *res_name);
/** Tear down all windows and close the display. */
void mtk_app_destroy(MtkApp *app);
/** Run the main loop until mtk_app_quit() or the last window
 *  closes. */
void mtk_app_run(MtkApp *app);
/** Make mtk_app_run() return. */
void mtk_app_quit(MtkApp *app);
/** Install the idle hook (nullptr callback removes it). */
void mtk_app_set_idle(MtkApp *app, bool (*idle)(void *data), void *data);

/** Schedule a one-shot timer; re-add from the callback for periodic
 *  behaviour.  @return a timer id (never 0). */
int mtk_timer_add(MtkApp *app, uint32_t delay_ms,
                  void (*cb)(void *data), void *data);
/** Cancel a pending timer by id; unknown ids are ignored. */
void mtk_timer_cancel(MtkApp *app, int id);
/** Monotonic milliseconds (CLOCK_MONOTONIC). */
uint64_t mtk_now_ms(void);

/** Allocate a pixel for a 0xRRGGBB value. */
unsigned long mtk_color(MtkApp *app, uint32_t rgb);

/** @} */

/**
 * @defgroup draw Drawing
 * @brief Primitives for widget draw handlers.
 *
 * All drawing targets the window back buffer and happens inside
 * `ops->draw`.  All strings are UTF-8 in the program's locale.
 * @{
 */

enum { MTK_BEVEL = 2 };  /**< standard Motif shadow thickness */

/** Direction of mtk_draw_arrow(). */
typedef enum MtkArrowDir {
    MTK_ARROW_UP,
    MTK_ARROW_DOWN,
    MTK_ARROW_LEFT,
    MTK_ARROW_RIGHT,
} MtkArrowDir;

/** Fill a rectangle with a palette pixel. */
void mtk_fill_rect(MtkWindow *win, int x, int y, int w, int h,
                   unsigned long pixel);
/** Outline a rectangle (1 px). */
void mtk_draw_rect(MtkWindow *win, int x, int y, int w, int h,
                   unsigned long pixel);
/** The Motif 3D edge, shaded with the body colors. */
void mtk_draw_bevel(MtkWindow *win, int x, int y, int w, int h,
                    int thickness, bool sunken);
/** Bevel with explicit edge colors (for surface/muted/primary
 *  areas). */
void mtk_draw_bevel_c(MtkWindow *win, int x, int y, int w, int h,
                      int thickness, bool sunken,
                      unsigned long light, unsigned long dark);
/** Etched-in frame (groove), Motif separator/frame style. */
void mtk_draw_etched(MtkWindow *win, int x, int y, int w, int h);
/** Shaded triangle arrow filling the rectangle. */
void mtk_draw_arrow(MtkWindow *win, int x, int y, int w, int h,
                    MtkArrowDir dir, bool sunken);
/** Draw UTF-8 text at a baseline. */
void mtk_draw_text(MtkWindow *win, XFontSet font, int x, int baseline,
                   const char *str, unsigned long pixel);
/** Draw UTF-8 text vertically centered in [y, y+h). */
void mtk_draw_text_centered(MtkWindow *win, XFontSet font, int x,
                            int y, int h, const char *str,
                            unsigned long pixel);
/** Pixel width of a UTF-8 string. */
int mtk_text_width(XFontSet font, const char *str);
/** Line height (max ascent + descent) of a font set. */
int mtk_font_height(XFontSet font);
/** Byte offset of the next UTF-8 code point boundary after `i`. */
int mtk_utf8_next(const char *s, int len, int i);
/** Byte offset of the previous UTF-8 code point boundary before
 *  `i`. */
int mtk_utf8_prev(const char *s, int i);
/** Clip subsequent drawing — core GC *and* XRender composites onto
 *  the back buffer — to the rectangle.  Always pair with
 *  mtk_clear_clip() on every exit path. */
void mtk_set_clip(MtkWindow *win, int x, int y, int w, int h);
/** Remove the clip installed by mtk_set_clip(). */
void mtk_clear_clip(MtkWindow *win);

/** @} */

/**
 * @defgroup widgets Standard widgets
 * @brief Buttons, labels, entries, lists, trees, menus and friends.
 *
 * Constructors return the concrete struct; position widgets with
 * `mtk_widget_set_rect(&w->base, ...)` or through a layout tree
 * (see @ref layout).  Simple properties are plain struct fields;
 * assign them directly.
 * @{
 */

enum { MTK_ROW_H = 26 }; /**< standard control-row height (buttons,
                          *   entries, spinboxes) */

/** Push button, or latching toggle when @ref toggle is set. */
typedef struct MtkButton {
    MtkWidget base;  /**< widget base */
    char *label;     /**< UTF-8 label (owned) */
    bool armed;      /**< pointer currently pressed inside */
    bool toggled;    /**< on/off state; only meaningful when toggle */
    bool toggle;     /**< latch on click instead of springing back */
    void (*on_click)(struct MtkButton *b, void *data); /**< after release inside */
    void *data;      /**< passed to on_click */
} MtkButton;

/** Create a push button. */
MtkButton *mtk_button_create(MtkWindow *win, MtkWidget *parent,
                             const char *label,
                             void (*on_click)(MtkButton *b, void *data),
                             void *data);
/** Switch between push and toggle behaviour. */
void mtk_button_set_toggle(MtkButton *b, bool toggle);
/** Set the toggle state (visual + logical). */
void mtk_button_set_toggled(MtkButton *b, bool toggled);
/** Replace the label. */
void mtk_button_set_label(MtkButton *b, const char *label);
/** Width that fits the label with standard padding. */
int mtk_button_natural_width(MtkApp *app, const char *label);

/** Horizontal text alignment. */
typedef enum MtkAlign { MTK_ALIGN_LEFT, MTK_ALIGN_CENTER, MTK_ALIGN_RIGHT } MtkAlign;

/** Static single-line text. */
typedef struct MtkLabel {
    MtkWidget base;  /**< widget base */
    char *text;      /**< UTF-8 text (owned) */
    MtkAlign align;  /**< horizontal alignment */
    bool bold;       /**< use the bold font */
} MtkLabel;

/** Create a label. */
MtkLabel *mtk_label_create(MtkWindow *win, MtkWidget *parent,
                           const char *text);
/** Replace the text. */
void mtk_label_set_text(MtkLabel *l, const char *text);

/** Single-line UTF-8 text editor; the cursor moves by code point. */
typedef struct MtkEntry {
    MtkWidget base;  /**< widget base */
    char *text;      /**< UTF-8 contents (owned) */
    int len;         /**< byte length of text */
    int cap;         /**< buffer capacity */
    int cursor;      /**< byte offset, always on a code-point boundary */
    int scroll_px;   /**< horizontal scroll */
    /** Input filter, called per code point before insertion.
     *  @param ch one UTF-8 encoded code point.
     *  @return true to accept the character. */
    bool (*validate)(struct MtkEntry *e, const char *ch, void *data);
    void (*on_activate)(struct MtkEntry *e, void *data); /**< Return key */
    void (*on_change)(struct MtkEntry *e, void *data);   /**< after edits */
    void *data;      /**< passed to the hooks */
} MtkEntry;

/** Create an entry. */
MtkEntry *mtk_entry_create(MtkWindow *win, MtkWidget *parent);
/** Replace the contents (cursor moves to the end). */
void mtk_entry_set_text(MtkEntry *e, const char *text);
/** Current contents (borrowed, valid until the next edit). */
const char *mtk_entry_text(MtkEntry *e);

/** Integer spinbox: an entry with joined up/down arrows, digits
 *  only, clamped to [minval, maxval]. */
typedef struct MtkSpinbox {
    MtkWidget base;   /**< widget base */
    MtkEntry *entry;  /**< embedded entry (owned) */
    int minval;       /**< inclusive minimum */
    int maxval;       /**< inclusive maximum */
    void (*on_change)(struct MtkSpinbox *s, void *data); /**< value changed */
    void *data;       /**< passed to on_change */
    int arm;          /**< 0 none, 1 up arrow, 2 down arrow */
} MtkSpinbox;

/** Create a spinbox with an initial value. */
MtkSpinbox *mtk_spinbox_create(MtkWindow *win, MtkWidget *parent,
                               int minval, int maxval, int value);
/** Current value, clamped to the range. */
int mtk_spinbox_value(MtkSpinbox *s);
/** Set the value (clamped). */
void mtk_spinbox_set_value(MtkSpinbox *s, int value);

/** Motif scrollbar: arrows, trough and a draggable thumb. */
typedef struct MtkScrollbar {
    MtkWidget base;  /**< widget base */
    bool horizontal; /**< orientation */
    int minval;           /**< content range start */
    int maxval;           /**< content range end */
    int page;             /**< visible extent */
    int value;            /**< clamped to [minval, max(minval, maxval-page)] */
    int line;             /**< arrow-click step */
    void (*on_change)(struct MtkScrollbar *sb, void *data); /**< value changed */
    void *data;           /**< passed to on_change */
    int drag_off;         /**< pointer offset inside thumb while dragging */
    int arm;              /**< 0 none, 1 dec arrow, 2 inc arrow */
    bool dragging;        /**< thumb drag in progress */
} MtkScrollbar;

enum { MTK_SCROLLBAR_W = 18 }; /**< standard scrollbar thickness */

/** Create a scrollbar. */
MtkScrollbar *mtk_scrollbar_create(MtkWindow *win, MtkWidget *parent,
                                   bool horizontal);
/** Configure range, page size and line step (value is re-clamped). */
void mtk_scrollbar_config(MtkScrollbar *sb, int minval, int maxval,
                          int page, int line);
/** Set the value (clamped; fires on_change when it changes). */
void mtk_scrollbar_set_value(MtkScrollbar *sb, int value);

/** Tab *bar* only; show/hide your panels in on_change. */
typedef struct MtkTabs {
    MtkWidget base;  /**< widget base */
    char **labels;   /**< tab labels (owned) */
    int ntabs;       /**< number of tabs */
    int active;      /**< active tab index */
    void (*on_change)(struct MtkTabs *t, int index, void *data); /**< tab switched */
    void *data;      /**< passed to on_change */
} MtkTabs;

enum { MTK_TABS_H = 28 }; /**< standard tab bar height */

/** Create a tab bar. */
MtkTabs *mtk_tabs_create(MtkWindow *win, MtkWidget *parent,
                         const char *const *labels, int ntabs,
                         void (*on_change)(MtkTabs *t, int index, void *data),
                         void *data);
/** Switch the active tab (fires on_change). */
void mtk_tabs_set_active(MtkTabs *t, int index);

/**
 * Scrolling string list.
 *
 * With @ref multi enabled, plain click selects one row, Ctrl+click
 * toggles a row, Shift+click selects the range from the last plain
 * click; `marked[i]` holds the multi-selection and @ref selected is
 * the lead row.  With @ref reorderable enabled, rows can be dragged
 * to a new position; on_reorder fires after the move.
 */
typedef struct MtkListbox {
    MtkWidget base;       /**< widget base */
    char **items;         /**< row strings (owned) */
    bool *marked;         /**< multi-selection flags, nitems entries */
    int nitems;           /**< row count */
    int cap;              /**< row array capacity */
    int selected;         /**< lead row, -1 none */
    int anchor;           /**< range anchor for Shift+click */
    bool multi;           /**< enable Ctrl/Shift multi-selection */
    bool reorderable;     /**< enable drag-to-reorder */
    MtkScrollbar *vbar;   /**< internal scrollbar (owned) */
    void (*on_select)(struct MtkListbox *lb, int index, void *data);   /**< lead changed */
    void (*on_activate)(struct MtkListbox *lb, int index, void *data); /**< double click / Return */
    /** Delete/BackSpace key with a selection. */
    void (*on_delete)(struct MtkListbox *lb, int index, void *data);
    /** Row `from` was dragged and now lives at index `to`. */
    void (*on_reorder)(struct MtkListbox *lb, int from, int to, void *data);
    void *data;           /**< passed to the hooks */
    /* drag-reorder state */
    int press_row;        /**< pressed row for drag detection */
    int press_y;          /**< press y for drag detection */
    int drop_pos;         /**< insertion gap 0..nitems while dragging */
    bool dragging;        /**< reorder drag in progress */
} MtkListbox;

/** Create an empty list box. */
MtkListbox *mtk_listbox_create(MtkWindow *win, MtkWidget *parent);
/** Append a row (the string is copied). */
void mtk_listbox_add(MtkListbox *lb, const char *item);
/** Remove one row. */
void mtk_listbox_remove(MtkListbox *lb, int index);
/** Remove all rows. */
void mtk_listbox_clear(MtkListbox *lb);
/** True if any row is marked (multi mode). */
bool mtk_listbox_any_marked(MtkListbox *lb);
/** Unmark every row. */
void mtk_listbox_clear_marks(MtkListbox *lb);

/** A tree row. */
typedef struct MtkTreeNode {
    char *label;     /**< UTF-8 label (owned) */
    void *user;      /**< free for the application (not freed) */
    struct MtkTreeNode *parent; /**< parent node */
    struct MtkTreeNode **kids;  /**< children */
    int nkids;       /**< child count */
    int kidcap;      /**< child array capacity */
    bool expanded;   /**< children visible */
    bool loaded;     /**< kids have been populated */
    bool leaf;       /**< never shows an expander */
} MtkTreeNode;

/** Lazily populated tree view. */
typedef struct MtkTree {
    MtkWidget base;      /**< widget base */
    MtkTreeNode *nroot;  /**< invisible super-root; its kids are top level */
    MtkTreeNode *selected; /**< selected node, may be null */
    MtkScrollbar *vbar;  /**< internal scrollbar (owned) */
    int scroll;          /**< row offset */
    /** Populate `n->kids` with mtk_tree_node_add(); called on first
     *  expansion of a node with `loaded == false`. */
    void (*on_expand)(struct MtkTree *t, MtkTreeNode *n, void *data);
    void (*on_select)(struct MtkTree *t, MtkTreeNode *n, void *data); /**< selection changed */
    void *data;          /**< passed to the hooks */
} MtkTree;

/** Create an empty tree. */
MtkTree *mtk_tree_create(MtkWindow *win, MtkWidget *parent);
/** Add a node (parent nullptr = top level).  @return the new node. */
MtkTreeNode *mtk_tree_node_add(MtkTree *t, MtkTreeNode *parent,
                               const char *label, void *user);
/** Remove all children of a node. */
void mtk_tree_node_clear(MtkTree *t, MtkTreeNode *n);
/** Expand or collapse (triggers on_expand on first expansion). */
void mtk_tree_expand(MtkTree *t, MtkTreeNode *n, bool expand);
/** Select a node (fires on_select). */
void mtk_tree_select(MtkTree *t, MtkTreeNode *n);

/** One pulldown entry; label "-" renders as a separator. */
typedef struct MtkMenuEntry {
    const char *label;   /**< UTF-8 label */
    const char *accel;   /**< right-aligned hint like "Ctrl+Q"; optional.
                          *   Display only — bind the real shortcut in
                          *   the window's on_key. */
} MtkMenuEntry;

/** One menu of a menu bar. */
typedef struct MtkMenu {
    char *title;           /**< UTF-8 title (owned) */
    KeySym mnemonic;       /**< lowercase keysym; auto: first letter */
    MtkMenuEntry *entries; /**< deep-copied entries */
    int n;                 /**< entry count */
} MtkMenu;

/**
 * Motif-style menu bar.
 *
 * Pulldowns are override-redirect popup windows with a pointer grab:
 * click a title to open, click an item (or press-drag-release) to
 * pick, click elsewhere or Escape to close.  Separators are not
 * pickable.  on_pick receives the menu and item indices as passed to
 * mtk_menubar_add().
 *
 * Keyboard: each title's mnemonic (its first letter, drawn
 * underlined) opens the menu with Alt+letter — route your window's
 * unconsumed keys through mtk_menubar_key() to enable it.  F10 opens
 * the first menu.  While a pulldown is open, Up/Down move, Return
 * picks, Left/Right switch menus, Escape closes.
 */
typedef struct MtkMenuBar {
    MtkWidget base;      /**< widget base */
    MtkMenu *menus;      /**< menus (owned) */
    int nmenus;          /**< menu count */
    int open;            /**< open menu index, -1 = closed */
    int help_menu;       /**< right-aligned menu index, -1 = none */
    MtkWindow *popup;    /**< open pulldown window, may be null */
    void (*on_pick)(struct MtkMenuBar *mb, int menu, int item, void *data); /**< item picked */
    void *data;          /**< passed to on_pick */
} MtkMenuBar;

enum { MTK_MENUBAR_H = 26 }; /**< standard menu bar height */

/** Create an empty menu bar. */
MtkMenuBar *mtk_menubar_create(MtkWindow *win, MtkWidget *parent,
                               void (*on_pick)(MtkMenuBar *mb, int menu,
                                               int item, void *data),
                               void *data);
/** Append a menu (title and entries are copied).
 *  @return the menu index. */
int mtk_menubar_add(MtkMenuBar *mb, const char *title,
                    const MtkMenuEntry *entries, int n);
/** Attach a menu to the right end of the bar (the Motif help
 *  menu). */
void mtk_menubar_set_help(MtkMenuBar *mb, int menu);
/** Alt+mnemonic / F10 handling; call from the window's on_key.
 *  @return true when it consumed the key. */
bool mtk_menubar_key(MtkMenuBar *mb, XKeyEvent *ev, KeySym sym);

/** What the user is picking in a file dialog. */
typedef enum MtkFileDialogMode {
    MTK_FILEDLG_OPEN, /**< pick an existing file */
    MTK_FILEDLG_SAVE, /**< pick a target name (need not exist) */
} MtkFileDialogMode;

/**
 * File selection dialog in the spirit of XmFileSelectionBox: a
 * directory entry, directory and file lists, a selection entry and
 * OK/Cancel.
 *
 * The dialog owns its toplevel window and destroys itself when the
 * user picks a file, cancels, or the window is closed; never free
 * it yourself.  `on_done` fires exactly once — with the chosen
 * absolute path, or nullptr on cancel — after which the pointer is
 * dead.
 *
 * Double-clicking a directory (or activating ".." ) navigates;
 * clicking a file puts its name in the selection entry;
 * double-click, Return or OK picks it.  In @ref MTK_FILEDLG_OPEN
 * mode the file must exist; in @ref MTK_FILEDLG_SAVE mode any name
 * is accepted.  Escape cancels.
 */
typedef struct MtkFileDialog {
    MtkWindow *win;         /**< the dialog toplevel (owned) */
    MtkFileDialogMode mode; /**< open or save */
    MtkLabel *dir_label;    /**< "Directory:" caption */
    MtkEntry *dir_entry;    /**< editable current directory */
    MtkLabel *dirs_label;   /**< "Directories" caption */
    MtkLabel *files_label;  /**< "Files" caption */
    MtkListbox *dirs;       /**< subdirectories ("..", then sorted) */
    MtkListbox *files;      /**< files passing the filter, sorted */
    MtkLabel *sel_label;    /**< "Selection:" caption */
    MtkEntry *sel_entry;    /**< the file name being picked */
    MtkButton *b_ok;        /**< OK */
    MtkButton *b_cancel;    /**< Cancel */
    char dir[4096];         /**< current directory, no trailing slash */
    char *filter;           /**< owned copy of the extension filter */
    bool done;              /**< on_done already fired */
    void (*on_done)(struct MtkFileDialog *fd, const char *path,
                    void *data); /**< result; path nullptr = cancelled */
    void *data;             /**< passed to on_done */
} MtkFileDialog;

/**
 * Open a file selection dialog.
 *
 * @param app        the application
 * @param title      window title (nullptr = "Select File")
 * @param mode       open an existing file, or name a file to save
 * @param start_path starting directory, or a full path whose
 *                   directory part is used and whose basename
 *                   presets the selection (for save-as); nullptr or
 *                   invalid falls back to `$HOME`
 * @param filter     space-separated case-insensitive extension list
 *                   shown in the file list, e.g. "jpg jpeg png";
 *                   nullptr shows every file
 * @param on_done    called exactly once with the chosen absolute
 *                   path, or nullptr when cancelled
 * @param data       passed to on_done
 * @return the dialog (owned by itself; see #MtkFileDialog).
 */
MtkFileDialog *mtk_file_dialog(MtkApp *app, const char *title,
                               MtkFileDialogMode mode,
                               const char *start_path, const char *filter,
                               void (*on_done)(MtkFileDialog *fd,
                                               const char *path, void *data),
                               void *data);
/** Dismiss a live dialog programmatically (fires on_done with
 *  nullptr). */
void mtk_file_dialog_close(MtkFileDialog *fd);

/** Draggable vertical divider between two panes.  Reports the new x
 *  position of its left edge; the application relayouts. */
typedef struct MtkSash {
    MtkWidget base;  /**< widget base */
    int min_x;       /**< left drag limit */
    int max_x;       /**< right drag limit (<= min_x = unlimited) */
    void (*on_drag)(struct MtkSash *s, int new_x, void *data); /**< dragged */
    void *data;      /**< passed to on_drag */
    bool dragging;   /**< drag in progress */
    int grab_dx;     /**< pointer offset inside the sash */
} MtkSash;

enum { MTK_SASH_W = 6 }; /**< standard sash width */

/** Create a sash. */
MtkSash *mtk_sash_create(MtkWindow *win, MtkWidget *parent,
                         void (*on_drag)(MtkSash *s, int new_x, void *data),
                         void *data);

/** Titled etched frame (the Motif XmFrame look).  It only draws the
 *  border and title; place content inside it yourself or with
 *  mtk_lay_framed(), which sizes the frame around its content. */
typedef struct MtkFrame {
    MtkWidget base;  /**< widget base */
    char *label;     /**< UTF-8 title (owned); may be empty */
} MtkFrame;

/** Create a frame.  Pass "" for an untitled etched box. */
MtkFrame *mtk_frame_create(MtkWindow *win, MtkWidget *parent,
                           const char *label);
/** Replace the title. */
void mtk_frame_set_label(MtkFrame *f, const char *label);
/** Content insets: distance from the frame's rectangle to the area
 *  available for content (top includes the title). */
void mtk_frame_insets(const MtkFrame *f, int *top, int *side,
                      int *bottom);

/** @} */

/**
 * @defgroup layout Layouts
 * @brief Optional geometry trees that place widgets automatically.
 *
 * A layout is a tree of geometry nodes, *not* widgets: nodes do not
 * draw or receive events; applying the tree to a rectangle
 * distributes it recursively and finishes in ordinary
 * mtk_widget_set_rect() calls.  Attach a tree to a window with
 * mtk_window_set_layout() and the resize handler disappears; or
 * apply subtrees manually with mtk_lay_apply() inside a hand-written
 * `on_resize` and mix both styles freely.
 *
 * **Sizing.**  Linear containers distribute their axis in three
 * passes: fixed sizes first, then *natural* sizes (from the widget's
 * `measure` op), then the leftover split between stretch nodes by
 * weight.  Nodes without a measurable size default to stretch 1.
 * On the cross axis children fill unless aligned.
 *
 * **Visibility.**  A hidden widget collapses its node — it takes no
 * space and its gap disappears — and the window relayouts
 * automatically; mtk_lay_keep_space() opts out per node.
 *
 * **Ownership.**  A tree given to mtk_window_set_layout() is owned
 * and freed by the window; a tree used via mtk_lay_apply() is freed
 * by the caller with mtk_lay_free().  Nodes never own widgets.
 * @{
 */

/** Alignment of a node inside its cell when it is smaller. */
typedef enum MtkLayAlign {
    MTK_LAY_FILL,    /**< stretch to the cell (default) */
    MTK_LAY_START,   /**< left / top */
    MTK_LAY_CENTER,  /**< centered */
    MTK_LAY_END,     /**< right / bottom */
} MtkLayAlign;

/* --- leaves --- */

/** A leaf positioning one widget. */
MtkLay *mtk_lay_widget(MtkWidget *w);
/** A stretchable empty gap (weight 1). */
MtkLay *mtk_lay_spacer(void);
/** Shorthand: widget leaf with a fixed size (-1 = elastic axis). */
MtkLay *mtk_lay_wfix(MtkWidget *w, int width, int height);
/** Shorthand: widget leaf stretched with `weight`. */
MtkLay *mtk_lay_wstretch(MtkWidget *w, int weight);

/* --- containers; child lists are nullptr-terminated --- */

/** Children left to right, `gap` pixels apart. */
MtkLay *mtk_lay_row(int gap, ...);
/** Children top to bottom, `gap` pixels apart. */
MtkLay *mtk_lay_col(int gap, ...);
/** Row-major cells in `ncols` columns; column widths and row
 *  heights size to their largest cell. */
MtkLay *mtk_lay_grid(int ncols, int gap, ...);
/** Children overlaid on the same rectangle; toggle visibility to
 *  switch between them. */
MtkLay *mtk_lay_stack(MtkLay *first, ...);
/** Two panes divided by a draggable sash.  The node positions both
 *  panes and the sash, owns the split position and its clamping,
 *  and takes over the sash's `on_drag`. */
MtkLay *mtk_lay_split(MtkSash *sash, MtkLay *first, MtkLay *second);
/** Wrap `inner` in a titled frame sized around it. */
MtkLay *mtk_lay_framed(MtkFrame *frame, MtkLay *inner);
/** The classic application shape: menubar on top, statusbar at the
 *  bottom, `content` stretching between (menubar/statusbar may be
 *  nullptr). */
MtkLay *mtk_lay_appframe(MtkWidget *menubar, MtkLay *content,
                         MtkWidget *statusbar);
/** Append a child to a row/col/grid/stack (for loops and
 *  conditional UIs). */
void mtk_lay_add(MtkLay *container, MtkLay *child);

/* --- per-node policy; each returns its argument for nesting --- */

/** Fix the size (-1 leaves an axis alone). */
MtkLay *mtk_lay_fixed(MtkLay *l, int w, int h);
/** Take a weighted share of the leftover space (weight >= 1). */
MtkLay *mtk_lay_stretch(MtkLay *l, int weight);
/** Margin around the node. */
MtkLay *mtk_lay_pad(MtkLay *l, int pad);
/** Minimum size (also the pane minimum inside mtk_lay_split()). */
MtkLay *mtk_lay_min(MtkLay *l, int w, int h);
/** Alignment inside the cell when smaller than it. */
MtkLay *mtk_lay_align(MtkLay *l, MtkLayAlign halign, MtkLayAlign valign);
/** Keep the node's space when its widget is hidden. */
MtkLay *mtk_lay_keep_space(MtkLay *l);

/* --- applying --- */

/** Distribute a rectangle to the tree (manual mode). */
void mtk_lay_apply(MtkLay *root, int x, int y, int w, int h);
/** Free a tree (never the widgets it references). */
void mtk_lay_free(MtkLay *root);

/** @} */

#endif /* MTK_H */
