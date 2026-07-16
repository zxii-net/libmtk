/*
 * 02-counter.c — events, keyboard input and timers.
 *
 * A counter with +/- buttons that also reacts to the arrow keys,
 * plus a live clock driven by a repeating timer.
 */
#include <stdio.h>
#include <time.h>

#include <mtk/mtk.h>

typedef struct {
    MtkApp *app;
    MtkLabel *value;
    MtkLabel *clock;
    MtkButton *minus, *plus, *reset;
    int count;
} Ui;

static void show_count(Ui *ui)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", ui->count);
    mtk_label_set_text(ui->value, buf);
}

static void on_minus(MtkButton *b, void *data)
{
    (void)b;
    Ui *ui = data;
    ui->count--;
    show_count(ui);
}

static void on_plus(MtkButton *b, void *data)
{
    (void)b;
    Ui *ui = data;
    ui->count++;
    show_count(ui);
}

static void on_reset(MtkButton *b, void *data)
{
    (void)b;
    Ui *ui = data;
    ui->count = 0;
    show_count(ui);
}

/* Keys unclaimed by a focused widget end up here. */
static bool on_key(MtkWindow *win, XKeyEvent *ev, KeySym sym,
                   const char *text)
{
    (void)ev;
    (void)text;
    Ui *ui = win->user;
    switch (sym) {
    case XK_Up:
    case XK_plus:
        ui->count++;
        show_count(ui);
        return true;
    case XK_Down:
    case XK_minus:
        ui->count--;
        show_count(ui);
        return true;
    case XK_q:
    case XK_Escape:
        mtk_window_destroy(win);
        return true;
    }
    return false;
}

/* Timers are one-shot: re-add from the callback to repeat. */
static void tick(void *data)
{
    Ui *ui = data;
    time_t now = time(nullptr);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&now));
    mtk_label_set_text(ui->clock, buf);
    mtk_timer_add(ui->app, 1000, tick, ui);
}

static void layout(MtkWindow *win)
{
    Ui *ui = win->user;
    int mid = win->w / 2;
    mtk_widget_set_rect(&ui->value->base, 12, 14, win->w - 24, 28);
    mtk_widget_set_rect(&ui->minus->base, mid - 100, 52, 60, 26);
    mtk_widget_set_rect(&ui->reset->base, mid - 32, 52, 64, 26);
    mtk_widget_set_rect(&ui->plus->base, mid + 40, 52, 60, 26);
    mtk_widget_set_rect(&ui->clock->base, 12, win->h - 30, win->w - 24,
                        20);
}

int main(void)
{
    MtkApp *app = mtk_app_create("counter");
    if (!app)
        return 1;

    MtkWindow *win = mtk_window_create(app, "Counter", 320, 130);

    Ui ui = { .app = app };
    ui.value = mtk_label_create(win, nullptr, "0");
    ui.value->align = MTK_ALIGN_CENTER;
    ui.value->bold = true;
    ui.minus = mtk_button_create(win, nullptr, "-", on_minus, &ui);
    ui.reset = mtk_button_create(win, nullptr, "Reset", on_reset, &ui);
    ui.plus = mtk_button_create(win, nullptr, "+", on_plus, &ui);
    ui.clock = mtk_label_create(win, nullptr, "");
    ui.clock->align = MTK_ALIGN_CENTER;

    win->user = &ui;
    win->on_resize = layout;
    win->on_key = on_key;
    layout(win);
    tick(&ui); /* first tick schedules the next one */

    mtk_window_show(win);
    mtk_app_run(app);
    mtk_app_destroy(app);
    return 0;
}
