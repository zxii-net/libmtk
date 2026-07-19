/*
 * 10-contacts.c — layouts: widgets placed by a geometry tree.
 *
 * A contact editor with no layout() function and no on_resize: the
 * whole window is one mtk_lay_* tree attached with
 * mtk_window_set_layout.  Resize the window, drag the sash, toggle
 * the notes row — geometry follows.
 */
#include <stdio.h>
#include <stdlib.h>

#include <mtk/mtk.h>

typedef struct App {
    MtkApp *mtk;
    MtkWindow *win;
    MtkMenuBar *menubar;
    MtkSash *sash;
    MtkListbox *list;
    MtkFrame *frame;
    MtkEntry *name, *email, *notes;
    MtkLabel *notes_label;
    MtkSpinbox *age;
    MtkButton *toggle, *save, *clear;
    MtkLabel *status;
} App;

static const struct {
    const char *name, *email, *notes;
    int age;
} demo[] = {
    {"Ada Lovelace", "ada@analytical.example", "prefers punch cards", 36},
    {"Konrad Zuse", "kz@z3.example", "ask about the Plankalkül", 85},
    {"Grace Hopper", "grace@cobol.example", "keeps a nanosecond", 85},
    {"Dennis Ritchie", "dmr@unix.example", "", 70},
};
enum { NDEMO = sizeof(demo) / sizeof(demo[0]) };

static void fill_form(App *a, int i)
{
    char buf[32];
    mtk_entry_set_text(a->name, demo[i].name);
    mtk_entry_set_text(a->email, demo[i].email);
    mtk_entry_set_text(a->notes, demo[i].notes);
    snprintf(buf, sizeof(buf), "editing %s", demo[i].name);
    mtk_label_set_text(a->status, buf);
    mtk_spinbox_set_value(a->age, demo[i].age);
}

static void on_select(MtkListbox *lb, int index, void *data)
{
    (void)lb;
    fill_form(data, index);
}

static void on_save(MtkButton *b, void *data)
{
    (void)b;
    App *a = data;
    char buf[256];
    snprintf(buf, sizeof(buf), "saved %s (not really)",
             mtk_entry_text(a->name));
    mtk_label_set_text(a->status, buf);
}

static void on_clear(MtkButton *b, void *data)
{
    (void)b;
    App *a = data;
    mtk_entry_set_text(a->name, "");
    mtk_entry_set_text(a->email, "");
    mtk_entry_set_text(a->notes, "");
    mtk_label_set_text(a->status, "cleared");
}

/* Hiding widgets collapses their layout nodes; the window relayouts
 * by itself — this callback contains zero geometry code. */
static void on_toggle_notes(MtkButton *b, void *data)
{
    App *a = data;
    mtk_widget_set_visible(&a->notes_label->base, b->toggled);
    mtk_widget_set_visible(&a->notes->base, b->toggled);
}

static void menu_pick(MtkMenuBar *mb, int menu, int item, void *data)
{
    (void)mb;
    (void)menu;
    App *a = data;
    if (item == 0)
        mtk_app_quit(a->mtk);
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
    MtkApp *mtk = mtk_app_create("contacts");
    if (!mtk)
        return 1;

    App a = { .mtk = mtk };
    a.win = mtk_window_create(mtk, "Contacts", 560, 320);
    a.win->user = &a;
    a.win->on_key = on_key;

    /* --- widgets, created as before ------------------------------ */
    a.menubar = mtk_menubar_create(a.win, nullptr, menu_pick, &a);
    static const MtkMenuEntry file_menu[] = { {"Quit", nullptr} };
    mtk_menubar_add(a.menubar, "File", file_menu, 1);

    a.sash = mtk_sash_create(a.win, nullptr, nullptr, nullptr);
    a.list = mtk_listbox_create(a.win, nullptr);
    a.list->on_select = on_select;
    a.list->data = &a;
    for (int i = 0; i < NDEMO; i++)
        mtk_listbox_add(a.list, demo[i].name);

    a.frame = mtk_frame_create(a.win, nullptr, "Contact");
    MtkLabel *l_name = mtk_label_create(a.win, nullptr, "Name:");
    a.name = mtk_entry_create(a.win, nullptr);
    MtkLabel *l_email = mtk_label_create(a.win, nullptr, "Email:");
    a.email = mtk_entry_create(a.win, nullptr);
    MtkLabel *l_age = mtk_label_create(a.win, nullptr, "Age:");
    a.age = mtk_spinbox_create(a.win, nullptr, 0, 150, 30);
    a.notes_label = mtk_label_create(a.win, nullptr, "Notes:");
    a.notes = mtk_entry_create(a.win, nullptr);

    a.toggle = mtk_button_create(a.win, nullptr, "Notes",
                                 on_toggle_notes, &a);
    mtk_button_set_toggle(a.toggle, true);
    mtk_button_set_toggled(a.toggle, true);
    a.save = mtk_button_create(a.win, nullptr, "Save", on_save, &a);
    a.clear = mtk_button_create(a.win, nullptr, "Clear", on_clear, &a);
    a.status = mtk_label_create(a.win, nullptr, "pick a contact");

    /* --- the layout: the whole window in one expression ---------- */
    mtk_window_set_layout(a.win, mtk_lay_appframe(&a.menubar->base,
        mtk_lay_pad(mtk_lay_split(a.sash,
            mtk_lay_min(mtk_lay_widget(&a.list->base), 120, 0),
            mtk_lay_pad(mtk_lay_col(8,
                mtk_lay_framed(a.frame, mtk_lay_grid(2, 6,
                    mtk_lay_widget(&l_name->base),
                    mtk_lay_widget(&a.name->base),
                    mtk_lay_widget(&l_email->base),
                    mtk_lay_widget(&a.email->base),
                    mtk_lay_widget(&l_age->base),
                    mtk_lay_align(mtk_lay_widget(&a.age->base),
                                  MTK_LAY_START, MTK_LAY_FILL),
                    mtk_lay_widget(&a.notes_label->base),
                    mtk_lay_widget(&a.notes->base),
                    nullptr)),
                mtk_lay_spacer(),
                mtk_lay_row(6,
                    mtk_lay_widget(&a.toggle->base),
                    mtk_lay_spacer(),
                    mtk_lay_widget(&a.save->base),
                    mtk_lay_widget(&a.clear->base),
                    nullptr),
                nullptr), 6)),
            6),
        &a.status->base));

    fill_form(&a, 0);
    mtk_window_show(a.win);
    mtk_app_run(mtk);
    mtk_app_destroy(mtk);
    return 0;
}
