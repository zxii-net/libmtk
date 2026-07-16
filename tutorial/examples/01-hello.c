/*
 * 01-hello.c — a first libmtk program.
 *
 * One window, one label, one button.  Clicking the button changes
 * the label text.
 */
#include <mtk/mtk.h>

typedef struct {
    MtkLabel *message;
    MtkButton *button;
} Ui;

static void on_greet(MtkButton *b, void *data)
{
    (void)b;
    Ui *ui = data;
    mtk_label_set_text(ui->message, "Hello, Motif world!");
}

/* The application owns layout: position every widget here. */
static void layout(MtkWindow *win)
{
    Ui *ui = win->user;
    mtk_widget_set_rect(&ui->message->base, 12, 16, win->w - 24, 24);
    mtk_widget_set_rect(&ui->button->base, (win->w - 96) / 2,
                        win->h - 40, 96, 26);
}

int main(void)
{
    MtkApp *app = mtk_app_create("hello");
    if (!app)
        return 1;

    MtkWindow *win = mtk_window_create(app, "Hello", 300, 120);

    Ui ui = {0};
    ui.message = mtk_label_create(win, nullptr, "A humble beginning.");
    ui.message->align = MTK_ALIGN_CENTER;
    ui.button = mtk_button_create(win, nullptr, "Greet", on_greet, &ui);

    win->user = &ui;
    win->on_resize = layout;
    layout(win);

    mtk_window_show(win);
    mtk_app_run(app);     /* returns when the last window closes */
    mtk_app_destroy(app);
    return 0;
}
