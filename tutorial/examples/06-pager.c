/*
 * 06-pager.c — scrolling your own content.
 *
 * A read-only text pager: a custom widget that renders an array of
 * lines through a viewport, paired with an MtkScrollbar it owns.
 * Open a file (argv[1]) or browse the built-in demo text.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mtk/mtk.h>

/* --- the text view widget ---------------------------------------- */

typedef struct TextView {
    MtkWidget base;
    char **lines;      /* owned copies */
    int nlines;
    MtkScrollbar *vbar;
} TextView;

static int tv_row_h(TextView *tv)
{
    return mtk_font_height(tv->base.win->app->font) + 3;
}

static int tv_rows_visible(TextView *tv)
{
    int rows = (tv->base.h - 2 * MTK_BEVEL) / tv_row_h(tv);
    return rows > 0 ? rows : 1;
}

/* Keep the scrollbar glued to our right edge and told about the
 * content size; called from draw so it tracks every relayout. */
static void tv_sync_scrollbar(TextView *tv)
{
    MtkWidget *w = &tv->base;
    int rows = tv_rows_visible(tv);
    bool need = tv->nlines > rows;
    mtk_widget_set_visible(&tv->vbar->base, need);
    if (need) {
        mtk_widget_set_rect(&tv->vbar->base,
                            w->x + w->w - MTK_SCROLLBAR_W - MTK_BEVEL,
                            w->y + MTK_BEVEL, MTK_SCROLLBAR_W,
                            w->h - 2 * MTK_BEVEL);
        mtk_scrollbar_config(tv->vbar, 0, tv->nlines, rows, 1);
    } else {
        mtk_scrollbar_set_value(tv->vbar, 0);
    }
}

static void tv_draw(MtkWidget *w)
{
    TextView *tv = (TextView *)w;
    MtkWindow *win = w->win;
    MtkApp *app = win->app;
    MtkPalette *p = &app->pal;

    tv_sync_scrollbar(tv);
    mtk_fill_rect(win, w->x, w->y, w->w, w->h, p->surface);
    mtk_draw_bevel_c(win, w->x, w->y, w->w, w->h, MTK_BEVEL, true,
                     p->surface_top, p->surface_bottom);

    int rh = tv_row_h(tv);
    int rows = tv_rows_visible(tv);
    int top = tv->vbar->value;
    int text_w = w->w - 2 * MTK_BEVEL -
                 (tv->vbar->base.visible ? MTK_SCROLLBAR_W : 0);

    mtk_set_clip(win, w->x + MTK_BEVEL, w->y + MTK_BEVEL, text_w,
                 w->h - 2 * MTK_BEVEL);
    for (int i = 0; i < rows && top + i < tv->nlines; i++)
        mtk_draw_text_centered(win, app->font, w->x + MTK_BEVEL + 6,
                               w->y + MTK_BEVEL + i * rh, rh,
                               tv->lines[top + i], p->surface_text);
    mtk_clear_clip(win);
}

static void tv_scroll_by(TextView *tv, int lines)
{
    mtk_scrollbar_set_value(tv->vbar, tv->vbar->value + lines);
}

static bool tv_event(MtkWidget *w, XEvent *ev)
{
    TextView *tv = (TextView *)w;
    if (ev->type == ButtonPress) {
        if (ev->xbutton.button == Button4) {
            tv_scroll_by(tv, -3);
            return true;
        }
        if (ev->xbutton.button == Button5) {
            tv_scroll_by(tv, 3);
            return true;
        }
        return true; /* claim clicks so we can take focus */
    }
    return false;
}

static bool tv_key(MtkWidget *w, XKeyEvent *ev, KeySym sym,
                   const char *text)
{
    (void)ev;
    (void)text;
    TextView *tv = (TextView *)w;
    int page = tv_rows_visible(tv) - 1;
    switch (sym) {
    case XK_Up:        tv_scroll_by(tv, -1); return true;
    case XK_Down:      tv_scroll_by(tv, 1); return true;
    case XK_Page_Up:   tv_scroll_by(tv, -page); return true;
    case XK_Page_Down:
    case XK_space:     tv_scroll_by(tv, page); return true;
    case XK_Home:      mtk_scrollbar_set_value(tv->vbar, 0); return true;
    case XK_End:       mtk_scrollbar_set_value(tv->vbar, tv->nlines);
                       return true;
    }
    return false;
}

static void tv_clear(TextView *tv)
{
    for (int i = 0; i < tv->nlines; i++)
        free(tv->lines[i]);
    free(tv->lines);
    tv->lines = nullptr;
    tv->nlines = 0;
}

static void tv_destroy(MtkWidget *w)
{
    TextView *tv = (TextView *)w;
    tv_clear(tv);
    free(tv);
}

static const MtkWidgetOps tv_ops = {
    .draw = tv_draw,
    .event = tv_event,
    .key = tv_key,
    .destroy = tv_destroy,
};

static void tv_vbar_changed(MtkScrollbar *sb, void *data)
{
    (void)sb;
    mtk_window_damage(((TextView *)data)->base.win);
}

static TextView *tv_create(MtkWindow *win, MtkWidget *parent)
{
    TextView *tv = calloc(1, sizeof(*tv));
    mtk_widget_init(&tv->base, win, parent, &tv_ops);
    tv->base.can_focus = true;
    tv->vbar = mtk_scrollbar_create(win, &tv->base, false);
    tv->vbar->on_change = tv_vbar_changed;
    tv->vbar->data = tv;
    return tv;
}

static void tv_add_line(TextView *tv, const char *line)
{
    tv->lines = realloc(tv->lines,
                        sizeof(char *) * (size_t)(tv->nlines + 1));
    tv->lines[tv->nlines++] = strdup(line);
}

/* --- application -------------------------------------------------- */

typedef struct {
    TextView *view;
    MtkLabel *status;
} Ui;

static void load_file(Ui *ui, const char *path)
{
    FILE *f = fopen(path, "r");
    char buf[512];
    if (!f) {
        snprintf(buf, sizeof(buf), "Cannot open %s", path);
        mtk_label_set_text(ui->status, buf);
        return;
    }
    tv_clear(ui->view);
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        tv_add_line(ui->view, line);
    }
    fclose(f);
    snprintf(buf, sizeof(buf), "%s — %d lines", path, ui->view->nlines);
    mtk_label_set_text(ui->status, buf);
    mtk_scrollbar_set_value(ui->view->vbar, 0);
    mtk_window_damage(ui->view->base.win);
}

static void demo_text(Ui *ui)
{
    for (int i = 1; i <= 200; i++) {
        char line[128];
        snprintf(line, sizeof(line),
                 "%4d  The quick brown fox jumps over the lazy dog.", i);
        tv_add_line(ui->view, line);
    }
    mtk_label_set_text(ui->status, "demo text — 200 lines");
}

static void layout(MtkWindow *win)
{
    Ui *ui = win->user;
    mtk_widget_set_rect(&ui->view->base, 10, 10, win->w - 20,
                        win->h - 46);
    mtk_widget_set_rect(&ui->status->base, 10, win->h - 30,
                        win->w - 20, 20);
}

int main(int argc, char **argv)
{
    MtkApp *app = mtk_app_create("pager");
    if (!app)
        return 1;

    MtkWindow *win = mtk_window_create(app, "Pager", 520, 400);

    Ui ui = {0};
    ui.view = tv_create(win, nullptr);
    ui.status = mtk_label_create(win, nullptr, "");

    win->user = &ui;
    win->on_resize = layout;
    layout(win);
    if (argc > 1)
        load_file(&ui, argv[1]);
    else
        demo_text(&ui);
    mtk_window_set_focus(win, &ui.view->base);

    mtk_window_show(win);
    mtk_app_run(app);
    mtk_app_destroy(app);
    return 0;
}
