/*
 * 05-notes.c — menus, dialogs and X-resource theming.
 *
 * A small note keeper: a menu bar, a list of one-line notes with an
 * entry to add more, save/load through a path dialog, and an About
 * box.  Theme it per application with xrdb:
 *
 *     echo 'notes*MtkTheme: desert' | xrdb -merge
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mtk/mtk.h>

typedef struct App {
    MtkApp *mtk;
    MtkWindow *win;
    MtkMenuBar *menubar;
    MtkEntry *input;
    MtkListbox *list;
    MtkLabel *status;
} App;

enum { MENU_FILE, MENU_HELP };

static void set_status(App *a, const char *msg)
{
    mtk_label_set_text(a->status, msg);
}

/* --- notes ------------------------------------------------------ */

static void add_note(App *a)
{
    if (!mtk_entry_text(a->input)[0])
        return;
    mtk_listbox_add(a->list, mtk_entry_text(a->input));
    mtk_entry_set_text(a->input, "");
    set_status(a, "Added.");
}

static void on_input_activate(MtkEntry *e, void *data)
{
    (void)e;
    add_note(data);
}

static void on_delete_key(MtkListbox *lb, int index, void *data)
{
    mtk_listbox_remove(lb, index);
    set_status(data, "Removed.");
}

/* --- save / load through a path prompt -------------------------- */

typedef struct Prompt {
    App *a;
    MtkEntry *entry;
    void (*done)(App *a, const char *path);
} Prompt;

static void prompt_finish(Prompt *p, MtkWindow *win)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s", mtk_entry_text(p->entry));
    App *a = p->a;
    void (*done)(App *, const char *) = p->done;
    mtk_window_destroy(win); /* deferred: safe inside a callback */
    if (path[0])
        done(a, path);
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

static void prompt_show(App *a, const char *title,
                        void (*done)(App *a, const char *path))
{
    Prompt *p = calloc(1, sizeof(*p));
    p->a = a;
    p->done = done;

    MtkWindow *win = mtk_window_create(a->mtk, title, 420, 86);
    win->user = p;
    win->on_destroy = prompt_freed;

    p->entry = mtk_entry_create(win, nullptr);
    mtk_widget_set_rect(&p->entry->base, 12, 12, 396, 26);
    mtk_entry_set_text(p->entry, "notes.txt");
    p->entry->on_activate = prompt_enter;
    p->entry->data = p;

    MtkButton *ok = mtk_button_create(win, nullptr, "OK", prompt_ok, p);
    mtk_widget_set_rect(&ok->base, 232, 48, 84, 26);
    MtkButton *cancel = mtk_button_create(win, nullptr, "Cancel",
                                          prompt_cancel, nullptr);
    mtk_widget_set_rect(&cancel->base, 324, 48, 84, 26);

    mtk_window_set_focus(win, &p->entry->base);
    mtk_window_show(win);
}

static void do_save(App *a, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        set_status(a, "Cannot write file.");
        return;
    }
    for (int i = 0; i < a->list->nitems; i++)
        fprintf(f, "%s\n", a->list->items[i]);
    fclose(f);
    set_status(a, "Saved.");
}

static void do_load(App *a, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        set_status(a, "Cannot open file.");
        return;
    }
    mtk_listbox_clear(a->list);
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0])
            mtk_listbox_add(a->list, line);
    }
    fclose(f);
    set_status(a, "Loaded.");
}

/* --- about dialog ----------------------------------------------- */

static void about_close(MtkButton *b, void *data)
{
    (void)data;
    mtk_window_destroy(b->base.win);
}

static void show_about(App *a)
{
    MtkWindow *win = mtk_window_create(a->mtk, "About Notes", 300, 104);
    MtkLabel *l1 = mtk_label_create(win, nullptr, "Notes");
    l1->bold = true;
    l1->align = MTK_ALIGN_CENTER;
    MtkLabel *l2 = mtk_label_create(win, nullptr,
                                    "A libmtk tutorial program");
    l2->align = MTK_ALIGN_CENTER;
    mtk_widget_set_rect(&l1->base, 10, 12, 280, 20);
    mtk_widget_set_rect(&l2->base, 10, 34, 280, 20);
    MtkButton *ok = mtk_button_create(win, nullptr, "OK", about_close,
                                      nullptr);
    mtk_widget_set_rect(&ok->base, 108, 66, 84, 26);
    mtk_window_show(win);
}

/* --- menu -------------------------------------------------------- */

static void menu_pick(MtkMenuBar *mb, int menu, int item, void *data)
{
    (void)mb;
    App *a = data;
    if (menu == MENU_FILE) {
        switch (item) {
        case 0: mtk_listbox_clear(a->list); set_status(a, "New."); break;
        case 1: prompt_show(a, "Open Notes", do_load); break;
        case 2: prompt_show(a, "Save Notes As", do_save); break;
        case 4: mtk_app_quit(a->mtk); break;
        }
    } else if (menu == MENU_HELP && item == 0) {
        show_about(a);
    }
}

/* --- layout and main --------------------------------------------- */

/* Alt+F / Alt+H open the menus; F10 opens the first one. */
static bool on_key(MtkWindow *win, XKeyEvent *ev, KeySym sym,
                   const char *text)
{
    (void)text;
    App *a = win->user;
    return mtk_menubar_key(a->menubar, ev, sym);
}

static void layout(MtkWindow *win)
{
    App *a = win->user;
    mtk_widget_set_rect(&a->menubar->base, 0, 0, win->w, MTK_MENUBAR_H);
    int top = MTK_MENUBAR_H;
    mtk_widget_set_rect(&a->input->base, 10, top + 10, win->w - 20, 26);
    mtk_widget_set_rect(&a->list->base, 10, top + 46, win->w - 20,
                        win->h - top - 84);
    mtk_widget_set_rect(&a->status->base, 10, win->h - 30, win->w - 20,
                        20);
}

int main(void)
{
    MtkApp *mtk = mtk_app_create("notes");
    if (!mtk)
        return 1;

    App a = { .mtk = mtk };
    a.win = mtk_window_create(mtk, "Notes", 360, 340);
    a.win->user = &a;
    a.win->on_resize = layout;
    a.win->on_key = on_key;

    a.menubar = mtk_menubar_create(a.win, nullptr, menu_pick, &a);
    static const MtkMenuEntry file_menu[] = {
        {"New", nullptr},
        {"Open...", nullptr},
        {"Save As...", nullptr},
        {"-", nullptr},
        {"Quit", nullptr},
    };
    static const MtkMenuEntry help_menu[] = {
        {"About Notes", nullptr},
    };
    mtk_menubar_add(a.menubar, "File", file_menu, 5);
    int help = mtk_menubar_add(a.menubar, "Help", help_menu, 1);
    mtk_menubar_set_help(a.menubar, help); /* right-aligned, Motif style */

    a.input = mtk_entry_create(a.win, nullptr);
    a.input->on_activate = on_input_activate;
    a.input->data = &a;
    a.list = mtk_listbox_create(a.win, nullptr);
    a.list->multi = true;
    a.list->reorderable = true;
    a.list->on_delete = on_delete_key;
    a.list->data = &a;
    a.status = mtk_label_create(a.win, nullptr,
                                "Type a note and press Return.");

    layout(a.win);
    mtk_window_set_focus(a.win, &a.input->base);
    mtk_window_show(a.win);
    mtk_app_run(mtk);
    mtk_app_destroy(mtk);
    return 0;
}
