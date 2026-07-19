/*
 * 09-pixels.c — pixel buffers and XRender.
 *
 * An animated plasma: pixels are computed on the CPU into an ARGB
 * buffer, uploaded to a server-side picture, and composited into the
 * widget scaled to fit — the full image pipeline in miniature.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <mtk/mtk.h>

enum { IMG_W = 320, IMG_H = 240 };

/* --- a reusable "upload pixels, get a Picture" helper ------------- */

typedef struct Pix {
    Pixmap pm;
    Picture pict;
    int w, h;
} Pix;

/*
 * Pixels are premultiplied ARGB32 words (A<<24 | R<<16 | G<<8 | B),
 * the layout of PictStandardARGB32.  For opaque pixels (A = 0xff)
 * premultiplication is a no-op.
 */
static void pix_upload(MtkApp *app, Pix *px, const uint32_t *data,
                       int w, int h)
{
    Display *dpy = app->dpy;

    if (px->pm && (px->w != w || px->h != h)) {
        XRenderFreePicture(dpy, px->pict);
        XFreePixmap(dpy, px->pm);
        px->pm = 0;
    }
    if (!px->pm) {
        px->pm = XCreatePixmap(dpy, app->xroot, (unsigned)w, (unsigned)h,
                               32);
        px->pict = XRenderCreatePicture(dpy, px->pm, app->fmt_argb32, 0,
                                        nullptr);
        px->w = w;
        px->h = h;
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

static void pix_free(MtkApp *app, Pix *px)
{
    if (px->pm) {
        XRenderFreePicture(app->dpy, px->pict);
        XFreePixmap(app->dpy, px->pm);
        px->pm = 0;
    }
}

/* --- the canvas widget -------------------------------------------- */

typedef struct Canvas {
    MtkWidget base;
    uint32_t *buf;    /* IMG_W x IMG_H, owned */
    Pix pix;
    bool dirty;       /* buffer changed since last upload */
} Canvas;

static void canvas_draw(MtkWidget *w)
{
    Canvas *c = (Canvas *)w;
    MtkWindow *win = w->win;
    MtkApp *app = win->app;

    mtk_fill_rect(win, w->x, w->y, w->w, w->h, app->pal.muted);
    mtk_draw_bevel_c(win, w->x, w->y, w->w, w->h, MTK_BEVEL, true,
                     app->pal.muted_top, app->pal.muted_bottom);

    if (c->dirty) {
        pix_upload(app, &c->pix, c->buf, IMG_W, IMG_H);
        c->dirty = false;
    }

    /* scale to fit the widget, keeping the aspect ratio */
    int aw = w->w - 2 * MTK_BEVEL, ah = w->h - 2 * MTK_BEVEL;
    double s = (double)aw / IMG_W;
    if ((double)ah / IMG_H < s)
        s = (double)ah / IMG_H;
    int dw = (int)(IMG_W * s), dh = (int)(IMG_H * s);
    int dx = w->x + MTK_BEVEL + (aw - dw) / 2;
    int dy = w->y + MTK_BEVEL + (ah - dh) / 2;

    /* the transform maps destination coordinates into source space */
    XTransform xf = {{
        {XDoubleToFixed(1.0 / s), 0, 0},
        {0, XDoubleToFixed(1.0 / s), 0},
        {0, 0, XDoubleToFixed(1.0)},
    }};
    XRenderSetPictureTransform(app->dpy, c->pix.pict, &xf);
    XRenderSetPictureFilter(app->dpy, c->pix.pict,
                            s == 1.0 ? FilterNearest : FilterBilinear,
                            nullptr, 0);
    mtk_set_clip(win, dx, dy, dw, dh);
    XRenderComposite(app->dpy, PictOpOver, c->pix.pict, None,
                     win->back_pict, 0, 0, 0, 0, dx, dy,
                     (unsigned)dw, (unsigned)dh);
    mtk_clear_clip(win);
}

static void canvas_destroy(MtkWidget *w)
{
    Canvas *c = (Canvas *)w;
    pix_free(w->win->app, &c->pix);
    free(c->buf);
    free(c);
}

static const MtkWidgetOps canvas_ops = {
    .draw = canvas_draw,
    .destroy = canvas_destroy,
};

static Canvas *canvas_create(MtkWindow *win, MtkWidget *parent)
{
    Canvas *c = calloc(1, sizeof(*c));
    mtk_widget_init(&c->base, win, parent, &canvas_ops);
    c->buf = calloc(IMG_W * IMG_H, sizeof(uint32_t));
    return c;
}

/* --- the plasma --------------------------------------------------- */

typedef struct {
    MtkApp *app;
    Canvas *canvas;
    double t;
} Ui;

static void render_plasma(Canvas *c, double t)
{
    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            double v = sin(x / 23.0 + t) + sin(y / 17.0 - t * 0.7) +
                       sin((x + y) / 31.0 + t * 1.3) +
                       sin(hypot(x - IMG_W / 2, y - IMG_H / 2) / 14.0);
            uint32_t r = (uint32_t)(127.5 + 127.5 * sin(v * 1.5707963));
            uint32_t g = (uint32_t)(127.5 + 127.5 * sin(v * 1.5707963 + 2.1));
            uint32_t b = (uint32_t)(127.5 + 127.5 * sin(v * 1.5707963 + 4.2));
            c->buf[y * IMG_W + x] = 0xff000000u | r << 16 | g << 8 | b;
        }
    }
    c->dirty = true;
}

static void tick(void *data)
{
    Ui *ui = data;
    ui->t += 0.06;
    render_plasma(ui->canvas, ui->t);
    mtk_window_damage(ui->canvas->base.win);
    mtk_timer_add(ui->app, 50, tick, ui);
}

static void layout(MtkWindow *win)
{
    Ui *ui = win->user;
    mtk_widget_set_rect(&ui->canvas->base, 10, 10, win->w - 20,
                        win->h - 20);
}

int main(void)
{
    MtkApp *app = mtk_app_create("plasma");
    if (!app)
        return 1;

    MtkWindow *win = mtk_window_create(app, "Plasma", 360, 300);

    Ui ui = { .app = app };
    ui.canvas = canvas_create(win, nullptr);

    win->user = &ui;
    win->on_resize = layout;
    layout(win);
    tick(&ui);

    mtk_window_show(win);
    mtk_app_run(app);
    mtk_app_destroy(app);
    return 0;
}
