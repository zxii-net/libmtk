/*
 * 03-tasks.c — a to-do list: entry, listbox and buttons together.
 *
 * Return or the Add button appends a task; rows can be
 * multi-selected with Ctrl/Shift and removed with the button or the
 * Delete key; drag a row to reorder it.
 */
#include <stdio.h>

#include <mtk/mtk.h>

typedef struct {
    MtkEntry *input;
    MtkListbox *list;
    MtkButton *add, *remove;
    MtkLabel *status;
} Ui;

static void update_status(Ui *ui)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%d task%s", ui->list->nitems,
             ui->list->nitems == 1 ? "" : "s");
    mtk_label_set_text(ui->status, buf);
}

static void add_task(Ui *ui)
{
    const char *text = mtk_entry_text(ui->input);
    if (!text[0])
        return;
    mtk_listbox_add(ui->list, text);
    mtk_entry_set_text(ui->input, "");
    update_status(ui);
}

static void on_add_button(MtkButton *b, void *data)
{
    (void)b;
    add_task(data);
}

static void on_entry_activate(MtkEntry *e, void *data)
{
    (void)e;
    add_task(data);
}

static void remove_selected(Ui *ui)
{
    MtkListbox *lb = ui->list;
    if (mtk_listbox_any_marked(lb)) {
        for (int i = lb->nitems - 1; i >= 0; i--)
            if (lb->marked[i])
                mtk_listbox_remove(lb, i);
    } else if (lb->selected >= 0) {
        mtk_listbox_remove(lb, lb->selected);
    }
    update_status(ui);
}

static void on_remove_button(MtkButton *b, void *data)
{
    (void)b;
    remove_selected(data);
}

/* Delete key inside the listbox. */
static void on_delete_key(MtkListbox *lb, int index, void *data)
{
    (void)lb;
    (void)index;
    remove_selected(data);
}

static void layout(MtkWindow *win)
{
    Ui *ui = win->user;
    mtk_widget_set_rect(&ui->input->base, 10, 10, win->w - 96, 26);
    mtk_widget_set_rect(&ui->add->base, win->w - 78, 10, 68, 26);
    mtk_widget_set_rect(&ui->list->base, 10, 46, win->w - 20,
                        win->h - 118);
    mtk_widget_set_rect(&ui->remove->base, 10, win->h - 64, 90, 26);
    mtk_widget_set_rect(&ui->status->base, 112, win->h - 64,
                        win->w - 122, 26);
}

int main(void)
{
    MtkApp *app = mtk_app_create("tasks");
    if (!app)
        return 1;

    MtkWindow *win = mtk_window_create(app, "Tasks", 340, 320);

    Ui ui = {0};
    ui.input = mtk_entry_create(win, nullptr);
    ui.input->on_activate = on_entry_activate;
    ui.input->data = &ui;
    ui.add = mtk_button_create(win, nullptr, "Add", on_add_button, &ui);
    ui.list = mtk_listbox_create(win, nullptr);
    ui.list->multi = true;
    ui.list->reorderable = true;
    ui.list->on_delete = on_delete_key;
    ui.list->data = &ui;
    ui.remove = mtk_button_create(win, nullptr, "Remove",
                                  on_remove_button, &ui);
    ui.status = mtk_label_create(win, nullptr, "0 tasks");
    ui.status->align = MTK_ALIGN_RIGHT;

    mtk_listbox_add(ui.list, "Learn C23");
    mtk_listbox_add(ui.list, "Read the libmtk tutorial");
    mtk_listbox_add(ui.list, "Build something");
    update_status(&ui);

    win->user = &ui;
    win->on_resize = layout;
    layout(win);
    mtk_window_set_focus(win, &ui.input->base);

    mtk_window_show(win);
    mtk_app_run(app);
    mtk_app_destroy(app);
    return 0;
}
