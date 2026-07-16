/*
 * 03-convert.c — text entries with validation and live updates.
 *
 * A temperature converter: type in either field and the other one
 * follows.  A guard flag stops the two on_change handlers from
 * feeding each other forever.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mtk/mtk.h>

typedef struct {
    MtkLabel *c_label, *f_label;
    MtkEntry *celsius, *fahrenheit;
    bool updating;
} Ui;

/* Accept digits, one leading minus and one decimal point. */
static bool numeric(MtkEntry *e, const char *ch, void *data)
{
    (void)data;
    if (ch[0] >= '0' && ch[0] <= '9' && ch[1] == '\0')
        return true;
    if (ch[0] == '-' && ch[1] == '\0')
        return e->cursor == 0 && e->text[0] != '-';
    if (ch[0] == '.' && ch[1] == '\0')
        return !strchr(e->text, '.');
    return false;
}

static void set_value(Ui *ui, MtkEntry *target, double value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", value);
    ui->updating = true;
    mtk_entry_set_text(target, buf);
    ui->updating = false;
}

static void c_changed(MtkEntry *e, void *data)
{
    Ui *ui = data;
    if (!ui->updating)
        set_value(ui, ui->fahrenheit, atof(mtk_entry_text(e)) * 9 / 5 + 32);
}

static void f_changed(MtkEntry *e, void *data)
{
    Ui *ui = data;
    if (!ui->updating)
        set_value(ui, ui->celsius, (atof(mtk_entry_text(e)) - 32) * 5 / 9);
}

static void layout(MtkWindow *win)
{
    Ui *ui = win->user;
    int half = (win->w - 36) / 2;
    mtk_widget_set_rect(&ui->c_label->base, 12, 14, half, 20);
    mtk_widget_set_rect(&ui->f_label->base, 24 + half, 14, half, 20);
    mtk_widget_set_rect(&ui->celsius->base, 12, 38, half, 26);
    mtk_widget_set_rect(&ui->fahrenheit->base, 24 + half, 38, half, 26);
}

int main(void)
{
    MtkApp *app = mtk_app_create("convert");
    if (!app)
        return 1;

    MtkWindow *win = mtk_window_create(app, "Temperature", 340, 84);

    Ui ui = {0};
    ui.c_label = mtk_label_create(win, nullptr, "Celsius");
    ui.f_label = mtk_label_create(win, nullptr, "Fahrenheit");
    ui.celsius = mtk_entry_create(win, nullptr);
    ui.fahrenheit = mtk_entry_create(win, nullptr);
    ui.celsius->validate = numeric;
    ui.fahrenheit->validate = numeric;
    ui.celsius->on_change = c_changed;
    ui.celsius->data = &ui;
    ui.fahrenheit->on_change = f_changed;
    ui.fahrenheit->data = &ui;

    win->user = &ui;
    win->on_resize = layout;
    layout(win);
    mtk_entry_set_text(ui.celsius, "20");
    set_value(&ui, ui.fahrenheit, 68);

    mtk_window_show(win);
    mtk_app_run(app);
    mtk_app_destroy(app);
    return 0;
}
