/*
 * c-mpaint.c — Appendix C: a paint program.
 *
 * A fixed-size pixel canvas you draw on with the mouse, a color
 * palette widget, a brush-size spinbox, and Save As (PPM).  Brings
 * together chapter 9's pixel pipeline with interactive drawing.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mtk/mtk.h>

enum { CAN_W = 480, CAN_H = 340 };

/* --- pixel upload helper (chapter 9) ------------------------------- */

typedef struct Pix {
    Pixmap pm;
    Picture pict;
} Pix;

static void pix_upload(MtkApp *app, Pix *px, const uint32_t *data,
                       int w, int h)
{
    Display *dpy = app->dpy;
    if (!px->pm) {
        px->pm = XCreatePixmap(dpy, app->xroot, (unsigned)w, (unsigned)h,
                               32);
        px->pict = XRenderCreatePicture(dpy, px->pm, app->fmt_argb32, 0,
                                        nullptr);
    }
    XImage img = {
        .width = w,
        .height = h,
        .format = ZPixmap,
        .data = (char *)data,
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        .byte_order = LSBFirst,
        .bitmap_bit_order = LSBFirst,
#else
        .byte_order = MSBFirst,
        .bitmap_bit_order = MSBFirst,
#endif
        .bitmap_unit = 32,
        .bitmap_pad = 32,
        .depth = 32,
        .bytes_per_line = w * 4,
        .bits_per_pixel = 32,
    };
    XInitImage(&img);
    GC gc = XCreateGC(dpy, px->pm, 0, nullptr);
    XPutImage(dpy, px->pm, gc, &img, 0, 0, 0, 0, (unsigned)w,
              (unsigned)h);
    XFreeGC(dpy, gc);
}

/* --- the paint canvas ------------------------------------------------ */

typedef struct Canvas {
    MtkWidget base;
    uint32_t *buf;      /* CAN_W x CAN_H, opaque ARGB */
    Pix pix;
    bool dirty;
    uint32_t color;     /* current brush color 0xRRGGBB */
    int brush;          /* radius in pixels */
    bool drawing;
    int last_x, last_y; /* previous point, canvas coordinates */
} Canvas;

static void canvas_stamp(Canvas *c, int cx, int cy)
{
    int r = c->brush;
    uint32_t px = 0xff000000u | c->color;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > r * r)
                continue;
            int x = cx + dx, y = cy + dy;
            if (x >= 0 && x < CAN_W && y >= 0 && y < CAN_H)
                c->buf[y * CAN_W + x] = px;
        }
    }
}

/* Stamp along the segment so fast strokes stay continuous. */
static void canvas_stroke(Canvas *c, int x0, int y0, int x1, int y1)
{
    int steps = (int)hypot(x1 - x0, y1 - y0);
    if (steps < 1)
        steps = 1;
    for (int i = 0; i <= steps; i++) {
        canvas_stamp(c, x0 + (x1 - x0) * i / steps,
                     y0 + (y1 - y0) * i / steps);
    }
    c->dirty = true;
    mtk_window_damage(c->base.win);
}

static void canvas_clear(Canvas *c)
{
    for (int i = 0; i < CAN_W * CAN_H; i++)
        c->buf[i] = 0xffffffffu;
    c->dirty = true;
    mtk_window_damage(c->base.win);
}

static void canvas_draw(MtkWidget *w)
{
    Canvas *c = (Canvas *)w;
    MtkWindow *win = w->win;
    MtkApp *app = win->app;

    mtk_draw_bevel_c(win, w->x - MTK_BEVEL, w->y - MTK_BEVEL,
                     w->w + 2 * MTK_BEVEL, w->h + 2 * MTK_BEVEL,
                     MTK_BEVEL, true, app->pal.muted_top,
                     app->pal.muted_bottom);

    if (c->dirty) {
        pix_upload(app, &c->pix, c->buf, CAN_W, CAN_H);
        c->dirty = false;
    }
    /* drawn 1:1, pixel for pixel — no transform, no filter */
    XRenderComposite(app->dpy, PictOpSrc, c->pix.pict, None,
                     win->back_pict, 0, 0, 0, 0, w->x, w->y,
                     (unsigned)w->w, (unsigned)w->h);
}

static bool canvas_event(MtkWidget *w, XEvent *ev)
{
    Canvas *c = (Canvas *)w;
    switch (ev->type) {
    case ButtonPress:
        if (ev->xbutton.button != Button1)
            return false;
        c->drawing = true;
        c->last_x = ev->xbutton.x - w->x;
        c->last_y = ev->xbutton.y - w->y;
        canvas_stroke(c, c->last_x, c->last_y, c->last_x, c->last_y);
        return true;
    case MotionNotify:
        if (c->drawing) {
            int x = ev->xmotion.x - w->x, y = ev->xmotion.y - w->y;
            canvas_stroke(c, c->last_x, c->last_y, x, y);
            c->last_x = x;
            c->last_y = y;
        }
        return true;
    case ButtonRelease:
        c->drawing = false;
        return true;
    }
    return false;
}

static void canvas_destroy(MtkWidget *w)
{
    Canvas *c = (Canvas *)w;
    if (c->pix.pm) {
        XRenderFreePicture(w->win->app->dpy, c->pix.pict);
        XFreePixmap(w->win->app->dpy, c->pix.pm);
    }
    free(c->buf);
    free(c);
}

static const MtkWidgetOps canvas_ops = {
    .draw = canvas_draw,
    .event = canvas_event,
    .destroy = canvas_destroy,
};

static Canvas *canvas_create(MtkWindow *win, MtkWidget *parent)
{
    Canvas *c = calloc(1, sizeof(*c));
    mtk_widget_init(&c->base, win, parent, &canvas_ops);
    c->buf = malloc(CAN_W * CAN_H * sizeof(uint32_t));
    c->color = 0x202028;
    c->brush = 4;
    canvas_clear(c);
    return c;
}

/* --- the color palette widget ------------------------------------------ */

static const uint32_t palette_colors[] = {
    0x202028, 0x9a2f2f, 0xc97b2a, 0xc9b62a, 0x3f7a34, 0x2f7a7a,
    0x2f4f9a, 0x6a3f9a, 0xb05a86, 0x8a6a4f, 0x9aa0a8, 0xffffff,
};
enum { NCOLORS = sizeof(palette_colors) / sizeof(palette_colors[0]) };

typedef struct Palette {
    MtkWidget base;
    int selected;
    void (*on_pick)(struct Palette *p, uint32_t color, void *data);
    void *data;
} Palette;

static int pal_cell_w(Palette *p)
{
    return p->base.w / NCOLORS;
}

static void pal_draw(MtkWidget *w)
{
    Palette *p = (Palette *)w;
    MtkWindow *win = w->win;
    int cw = pal_cell_w(p);
    for (int i = 0; i < NCOLORS; i++) {
        int x = w->x + i * cw;
        bool sel = i == p->selected;
        mtk_fill_rect(win, x + 2, w->y + 2, cw - 4, w->h - 4,
                      mtk_color(win->app, palette_colors[i]));
        mtk_draw_bevel(win, x, w->y, cw, w->h, MTK_BEVEL, sel);
    }
}

static bool pal_event(MtkWidget *w, XEvent *ev)
{
    Palette *p = (Palette *)w;
    if (ev->type != ButtonPress || ev->xbutton.button != Button1)
        return false;
    int i = (ev->xbutton.x - w->x) / pal_cell_w(p);
    if (i >= 0 && i < NCOLORS) {
        p->selected = i;
        mtk_window_damage(w->win);
        if (p->on_pick)
            p->on_pick(p, palette_colors[i], p->data);
    }
    return true;
}

static void pal_destroy(MtkWidget *w)
{
    free(w);
}

static const MtkWidgetOps pal_ops = {
    .draw = pal_draw,
    .event = pal_event,
    .destroy = pal_destroy,
};

static Palette *palette_create(MtkWindow *win, MtkWidget *parent)
{
    Palette *p = calloc(1, sizeof(*p));
    mtk_widget_init(&p->base, win, parent, &pal_ops);
    return p;
}

/* --- application ---------------------------------------------------------- */

typedef struct App {
    MtkApp *mtk;
    MtkWindow *win;
    MtkMenuBar *menubar;
    Canvas *canvas;
    Palette *palette;
    MtkLabel *size_label;
    MtkSpinbox *size;
    MtkButton *clear;
    MtkLabel *status;
} App;

enum { MENU_FILE, MENU_HELP };

static void pick_color(Palette *p, uint32_t color, void *data)
{
    (void)p;
    App *a = data;
    a->canvas->color = color;
}

static void size_changed(MtkSpinbox *s, void *data)
{
    App *a = data;
    a->canvas->brush = mtk_spinbox_value(s);
}

static void on_clear(MtkButton *b, void *data)
{
    (void)b;
    App *a = data;
    canvas_clear(a->canvas);
    mtk_label_set_text(a->status, "cleared");
}

/* --- save as PPM ------------------------------------------------------------ */

static void save_ppm(App *a, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        mtk_label_set_text(a->status, "cannot write file");
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", CAN_W, CAN_H);
    for (int i = 0; i < CAN_W * CAN_H; i++) {
        uint32_t px = a->canvas->buf[i];
        unsigned char rgb[3] = {(unsigned char)(px >> 16),
                                (unsigned char)(px >> 8),
                                (unsigned char)px};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    char buf[1100];
    snprintf(buf, sizeof(buf), "saved %s", path);
    mtk_label_set_text(a->status, buf);
}

typedef struct Prompt {
    App *a;
    MtkEntry *entry;
} Prompt;

static void prompt_finish(Prompt *p, MtkWindow *win)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s", mtk_entry_text(p->entry));
    App *a = p->a;
    mtk_window_destroy(win);
    if (path[0])
        save_ppm(a, path);
}

static void prompt_ok(MtkButton *b, void *data)
{
    prompt_finish(data, b->base.win);
}

static void prompt_enter(MtkEntry *e, void *data)
{
    prompt_finish(data, e->base.win);
}

static void prompt_cancel(MtkButton *b, void *data)
{
    (void)data;
    mtk_window_destroy(b->base.win);
}

static void prompt_freed(MtkWindow *win)
{
    free(win->user);
}

static void save_dialog(App *a)
{
    Prompt *p = calloc(1, sizeof(*p));
    p->a = a;
    MtkWindow *win = mtk_window_create(a->mtk, "Save As PPM", 420, 86);
    win->user = p;
    win->on_destroy = prompt_freed;
    p->entry = mtk_entry_create(win, nullptr);
    mtk_widget_set_rect(&p->entry->base, 12, 12, 396, 26);
    mtk_entry_set_text(p->entry, "painting.ppm");
    p->entry->on_activate = prompt_enter;
    p->entry->data = p;
    MtkButton *ok = mtk_button_create(win, nullptr, "OK", prompt_ok, p);
    mtk_widget_set_rect(&ok->base, 232, 48, 84, 26);
    MtkButton *cancel =
        mtk_button_create(win, nullptr, "Cancel", prompt_cancel, nullptr);
    mtk_widget_set_rect(&cancel->base, 324, 48, 84, 26);
    mtk_window_set_focus(win, &p->entry->base);
    mtk_window_show(win);
}

/* --- menu, layout, main -------------------------------------------------------- */

static void menu_pick(MtkMenuBar *mb, int menu, int item, void *data)
{
    (void)mb;
    App *a = data;
    if (menu == MENU_FILE) {
        if (item == 0)
            save_dialog(a);
        else if (item == 2)
            mtk_app_quit(a->mtk);
    } else if (menu == MENU_HELP && item == 0) {
        MtkWindow *win = mtk_window_create(a->mtk, "About mpaint",
                                           300, 84);
        MtkLabel *l = mtk_label_create(win, nullptr,
                                       "mpaint — a tiny paint program");
        l->align = MTK_ALIGN_CENTER;
        mtk_widget_set_rect(&l->base, 10, 12, 280, 20);
        MtkButton *ok = mtk_button_create(win, nullptr, "OK",
                                          prompt_cancel, nullptr);
        mtk_widget_set_rect(&ok->base, 108, 48, 84, 26);
        mtk_window_show(win);
    }
}

static void layout(MtkWindow *win)
{
    App *a = win->user;
    int top = MTK_MENUBAR_H;
    mtk_widget_set_rect(&a->menubar->base, 0, 0, win->w, top);
    mtk_widget_set_rect(&a->palette->base, 8, top + 8, win->w - 208, 26);
    mtk_widget_set_rect(&a->size_label->base, win->w - 192, top + 8, 40,
                        26);
    mtk_widget_set_rect(&a->size->base, win->w - 150, top + 8, 62, 26);
    mtk_widget_set_rect(&a->clear->base, win->w - 78, top + 8, 70, 26);
    mtk_widget_set_rect(&a->canvas->base,
                        (win->w - CAN_W) / 2 > MTK_BEVEL
                            ? (win->w - CAN_W) / 2
                            : MTK_BEVEL,
                        top + 44, CAN_W, CAN_H);
    mtk_widget_set_rect(&a->status->base, 8, win->h - 26, win->w - 16,
                        20);
}

static bool on_key(MtkWindow *win, XKeyEvent *ev, KeySym sym,
                   const char *text)
{
    (void)text;
    App *a = win->user;
    return mtk_menubar_key(a->menubar, ev, sym);
}

int main(void)
{
    MtkApp *mtk = mtk_app_create("mpaint");
    if (!mtk)
        return 1;

    App a = { .mtk = mtk };
    a.win = mtk_window_create(mtk, "mpaint", CAN_W + 16,
                              CAN_H + MTK_MENUBAR_H + 82);
    a.win->user = &a;
    a.win->on_resize = layout;
    a.win->on_key = on_key;

    a.menubar = mtk_menubar_create(a.win, nullptr, menu_pick, &a);
    static const MtkMenuEntry file_menu[] = {
        {"Save As...", nullptr},
        {"-", nullptr},
        {"Quit", nullptr},
    };
    static const MtkMenuEntry help_menu[] = {
        {"About mpaint", nullptr},
    };
    mtk_menubar_add(a.menubar, "File", file_menu, 3);
    int help = mtk_menubar_add(a.menubar, "Help", help_menu, 1);
    mtk_menubar_set_help(a.menubar, help);

    a.palette = palette_create(a.win, nullptr);
    a.palette->on_pick = pick_color;
    a.palette->data = &a;
    a.size_label = mtk_label_create(a.win, nullptr, "Size:");
    a.size = mtk_spinbox_create(a.win, nullptr, 1, 32, 4);
    a.size->on_change = size_changed;
    a.size->data = &a;
    a.clear = mtk_button_create(a.win, nullptr, "Clear", on_clear, &a);
    a.canvas = canvas_create(a.win, nullptr);
    a.status = mtk_label_create(a.win, nullptr,
                                "drag to paint — File > Save As");

    layout(a.win);
    mtk_window_show(a.win);
    mtk_app_run(mtk);
    mtk_app_destroy(mtk);
    return 0;
}
