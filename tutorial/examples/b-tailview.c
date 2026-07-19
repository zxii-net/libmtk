/*
 * b-tailview.c — Appendix B: a live log viewer.
 *
 * Opens a text file, shows it in a scrolling view, and can follow it
 * as it grows ("tail -f") by polling the file with a timer.  A
 * search field jumps between matches.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <mtk/mtk.h>

/* --- log view: a scrolled line view with a highlighted row --------- */

typedef struct LogView {
    MtkWidget base;
    char *data;        /* whole file, owned */
    long len, cap;
    long *starts;      /* byte offset of each line start */
    int nlines, linecap;
    int hilite;        /* highlighted line, -1 none */
    MtkScrollbar *vbar;
} LogView;

static int lv_row_h(LogView *lv)
{
    return mtk_font_height(lv->base.win->app->font) + 3;
}

static int lv_rows_visible(LogView *lv)
{
    int rows = (lv->base.h - 2 * MTK_BEVEL) / lv_row_h(lv);
    return rows > 0 ? rows : 1;
}

/* Index line starts in newly appended data only. */
static void lv_index_from(LogView *lv, long from)
{
    if (from == 0 && lv->nlines == 0) {
        if (lv->nlines == lv->linecap) {
            lv->linecap = 256;
            lv->starts = realloc(lv->starts,
                                 sizeof(long) * (size_t)lv->linecap);
        }
        lv->starts[lv->nlines++] = 0;
    }
    for (long i = from; i < lv->len; i++) {
        if (lv->data[i] == '\n' && i + 1 < lv->len) {
            if (lv->nlines == lv->linecap) {
                lv->linecap = lv->linecap ? lv->linecap * 2 : 256;
                lv->starts = realloc(lv->starts,
                                     sizeof(long) * (size_t)lv->linecap);
            }
            lv->starts[lv->nlines++] = i + 1;
        }
    }
}

static void lv_line(LogView *lv, int i, char *buf, size_t len)
{
    long start = lv->starts[i];
    long end = i + 1 < lv->nlines ? lv->starts[i + 1] : lv->len;
    long n = end - start;
    while (n > 0 && (lv->data[start + n - 1] == '\n' ||
                     lv->data[start + n - 1] == '\r'))
        n--;
    if (n > (long)len - 1)
        n = (long)len - 1;
    memcpy(buf, lv->data + start, (size_t)n);
    buf[n] = '\0';
}

static void lv_sync_scrollbar(LogView *lv)
{
    MtkWidget *w = &lv->base;
    int rows = lv_rows_visible(lv);
    bool need = lv->nlines > rows;
    mtk_widget_set_visible(&lv->vbar->base, need);
    if (need) {
        mtk_widget_set_rect(&lv->vbar->base,
                            w->x + w->w - MTK_SCROLLBAR_W - MTK_BEVEL,
                            w->y + MTK_BEVEL, MTK_SCROLLBAR_W,
                            w->h - 2 * MTK_BEVEL);
        mtk_scrollbar_config(lv->vbar, 0, lv->nlines, rows, 1);
    } else {
        mtk_scrollbar_set_value(lv->vbar, 0);
    }
}

static void lv_draw(MtkWidget *w)
{
    LogView *lv = (LogView *)w;
    MtkWindow *win = w->win;
    MtkApp *app = win->app;
    MtkPalette *p = &app->pal;

    lv_sync_scrollbar(lv);
    mtk_fill_rect(win, w->x, w->y, w->w, w->h, p->surface);
    mtk_draw_bevel_c(win, w->x, w->y, w->w, w->h, MTK_BEVEL, true,
                     p->surface_top, p->surface_bottom);

    int rh = lv_row_h(lv);
    int rows = lv_rows_visible(lv);
    int top = lv->vbar->value;
    int text_w = w->w - 2 * MTK_BEVEL -
                 (lv->vbar->base.visible ? MTK_SCROLLBAR_W : 0);

    mtk_set_clip(win, w->x + MTK_BEVEL, w->y + MTK_BEVEL, text_w,
                 w->h - 2 * MTK_BEVEL);
    for (int i = 0; i < rows && top + i < lv->nlines; i++) {
        char line[1024];
        lv_line(lv, top + i, line, sizeof(line));
        int ry = w->y + MTK_BEVEL + i * rh;
        unsigned long fg = p->surface_text;
        if (top + i == lv->hilite) {
            mtk_fill_rect(win, w->x + MTK_BEVEL, ry, text_w, rh,
                          p->primary);
            fg = p->primary_text;
        }
        mtk_draw_text_centered(win, app->font, w->x + MTK_BEVEL + 6, ry,
                               rh, line, fg);
    }
    mtk_clear_clip(win);
}

static bool lv_event(MtkWidget *w, XEvent *ev)
{
    LogView *lv = (LogView *)w;
    if (ev->type == ButtonPress) {
        int b = (int)ev->xbutton.button;
        if (b == Button4 || b == Button5) {
            mtk_scrollbar_set_value(lv->vbar,
                                    lv->vbar->value + (b == Button4 ? -3
                                                                    : 3));
            return true;
        }
        return true;
    }
    return false;
}

static bool lv_key(MtkWidget *w, XKeyEvent *ev, KeySym sym,
                   const char *text)
{
    (void)ev;
    (void)text;
    LogView *lv = (LogView *)w;
    int page = lv_rows_visible(lv) - 1;
    switch (sym) {
    case XK_Up:        mtk_scrollbar_set_value(lv->vbar,
                                               lv->vbar->value - 1);
                       return true;
    case XK_Down:      mtk_scrollbar_set_value(lv->vbar,
                                               lv->vbar->value + 1);
                       return true;
    case XK_Page_Up:   mtk_scrollbar_set_value(lv->vbar,
                                               lv->vbar->value - page);
                       return true;
    case XK_Page_Down: mtk_scrollbar_set_value(lv->vbar,
                                               lv->vbar->value + page);
                       return true;
    case XK_Home:      mtk_scrollbar_set_value(lv->vbar, 0); return true;
    case XK_End:       mtk_scrollbar_set_value(lv->vbar, lv->nlines);
                       return true;
    }
    return false;
}

static void lv_destroy(MtkWidget *w)
{
    LogView *lv = (LogView *)w;
    free(lv->data);
    free(lv->starts);
    free(lv);
}

static const MtkWidgetOps lv_ops = {
    .draw = lv_draw,
    .event = lv_event,
    .key = lv_key,
    .destroy = lv_destroy,
};

static void lv_vbar_changed(MtkScrollbar *sb, void *data)
{
    (void)sb;
    mtk_window_damage(((LogView *)data)->base.win);
}

static LogView *lv_create(MtkWindow *win, MtkWidget *parent)
{
    LogView *lv = calloc(1, sizeof(*lv));
    mtk_widget_init(&lv->base, win, parent, &lv_ops);
    lv->base.can_focus = true;
    lv->hilite = -1;
    lv->vbar = mtk_scrollbar_create(win, &lv->base, false);
    lv->vbar->on_change = lv_vbar_changed;
    lv->vbar->data = lv;
    return lv;
}

static void lv_reset(LogView *lv)
{
    free(lv->data);
    free(lv->starts);
    memset(&lv->data, 0, sizeof(lv->data));
    lv->data = nullptr;
    lv->starts = nullptr;
    lv->len = lv->cap = 0;
    lv->nlines = lv->linecap = 0;
    lv->hilite = -1;
}

static void lv_append(LogView *lv, const char *chunk, long n)
{
    if (lv->len + n + 1 > lv->cap) {
        lv->cap = lv->cap ? lv->cap * 2 : 65536;
        if (lv->cap < lv->len + n + 1)
            lv->cap = lv->len + n + 1;
        lv->data = realloc(lv->data, (size_t)lv->cap);
    }
    long old = lv->len;
    memcpy(lv->data + lv->len, chunk, (size_t)n);
    lv->len += n;
    lv->data[lv->len] = '\0';
    lv_index_from(lv, old > 0 ? old - 1 : 0);
}

static void lv_scroll_to_end(LogView *lv)
{
    mtk_scrollbar_set_value(lv->vbar, lv->nlines);
    mtk_window_damage(lv->base.win);
}

/* --- application ---------------------------------------------------- */

typedef struct App {
    MtkApp *mtk;
    MtkWindow *win;
    MtkMenuBar *menubar;
    LogView *view;
    MtkEntry *search;
    MtkButton *find, *follow;
    MtkLabel *status;
    char path[1024];
    long read_pos;     /* bytes consumed from the file so far */
    int poll_timer;    /* 0 = not polling */
} App;

enum { MENU_FILE, MENU_HELP };

static void update_status(App *a)
{
    char buf[1200];
    snprintf(buf, sizeof(buf), "%s — %d lines, %ld bytes%s",
             a->path[0] ? a->path : "(no file)", a->view->nlines,
             a->view->len, a->follow->toggled ? "  [following]" : "");
    mtk_label_set_text(a->status, buf);
}

/* Read anything the file has gained since read_pos. */
static bool read_more(App *a)
{
    FILE *f = fopen(a->path, "r");
    if (!f)
        return false;
    fseek(f, a->read_pos, SEEK_SET);
    char chunk[65536];
    size_t n;
    bool got = false;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        lv_append(a->view, chunk, (long)n);
        a->read_pos += (long)n;
        got = true;
    }
    fclose(f);
    return got;
}

static void poll_tick(void *data)
{
    App *a = data;
    a->poll_timer = 0;
    if (!a->follow->toggled)
        return; /* toggled off: stop rescheduling */
    struct stat st;
    if (stat(a->path, &st) == 0 && (long)st.st_size > a->read_pos) {
        if (read_more(a)) {
            lv_scroll_to_end(a->view);
            update_status(a);
        }
    }
    a->poll_timer = mtk_timer_add(a->mtk, 500, poll_tick, a);
}

static void on_follow(MtkButton *b, void *data)
{
    App *a = data;
    if (b->toggled && !a->poll_timer)
        poll_tick(a); /* first poll now; it reschedules itself */
    update_status(a);
}

static void load_file(App *a, const char *path)
{
    snprintf(a->path, sizeof(a->path), "%s", path);
    lv_reset(a->view);
    a->read_pos = 0;
    if (!read_more(a)) {
        mtk_label_set_text(a->status, "cannot read file");
        return;
    }
    lv_scroll_to_end(a->view);
    update_status(a);
}

/* --- search ----------------------------------------------------------- */

static void do_find(App *a)
{
    const char *needle = mtk_entry_text(a->search);
    LogView *lv = a->view;
    if (!needle[0] || lv->nlines == 0)
        return;
    int start = lv->hilite + 1;
    for (int k = 0; k < lv->nlines; k++) {
        int i = (start + k) % lv->nlines;
        char line[1024];
        lv_line(lv, i, line, sizeof(line));
        if (strstr(line, needle)) {
            lv->hilite = i;
            mtk_scrollbar_set_value(lv->vbar,
                                    i - lv_rows_visible(lv) / 2);
            mtk_window_damage(lv->base.win);
            return;
        }
    }
    mtk_label_set_text(a->status, "no match");
}

static void on_find(MtkButton *b, void *data)
{
    (void)b;
    do_find(data);
}

static void on_search_enter(MtkEntry *e, void *data)
{
    (void)e;
    do_find(data);
}

/* --- open dialog -------------------------------------------------------- */

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
        load_file(a, path);
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

static void open_dialog(App *a)
{
    Prompt *p = calloc(1, sizeof(*p));
    p->a = a;
    MtkWindow *win = mtk_window_create(a->mtk, "Open Log File", 420, 86);
    win->user = p;
    win->on_destroy = prompt_freed;
    p->entry = mtk_entry_create(win, nullptr);
    mtk_widget_set_rect(&p->entry->base, 12, 12, 396, 26);
    mtk_entry_set_text(p->entry, a->path[0] ? a->path : "/var/log/");
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

/* --- menu, layout, main --------------------------------------------------- */

static void menu_pick(MtkMenuBar *mb, int menu, int item, void *data)
{
    (void)mb;
    App *a = data;
    if (menu == MENU_FILE) {
        if (item == 0)
            open_dialog(a);
        else if (item == 2)
            mtk_app_quit(a->mtk);
    } else if (menu == MENU_HELP && item == 0) {
        MtkWindow *win = mtk_window_create(a->mtk, "About tailview",
                                           300, 84);
        MtkLabel *l = mtk_label_create(win, nullptr,
                                       "tailview — a live log viewer");
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
    mtk_widget_set_rect(&a->search->base, 8, top + 8, win->w - 196, 26);
    mtk_widget_set_rect(&a->find->base, win->w - 180, top + 8, 60, 26);
    mtk_widget_set_rect(&a->follow->base, win->w - 112, top + 8, 104, 26);
    mtk_widget_set_rect(&a->view->base, 8, top + 42, win->w - 16,
                        win->h - top - 74);
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

int main(int argc, char **argv)
{
    MtkApp *mtk = mtk_app_create("tailview");
    if (!mtk)
        return 1;

    App a = { .mtk = mtk };
    a.win = mtk_window_create(mtk, "tailview", 640, 440);
    a.win->user = &a;
    a.win->on_resize = layout;
    a.win->on_key = on_key;

    a.menubar = mtk_menubar_create(a.win, nullptr, menu_pick, &a);
    static const MtkMenuEntry file_menu[] = {
        {"Open...", nullptr},
        {"-", nullptr},
        {"Quit", nullptr},
    };
    static const MtkMenuEntry help_menu[] = {
        {"About tailview", nullptr},
    };
    mtk_menubar_add(a.menubar, "File", file_menu, 3);
    int help = mtk_menubar_add(a.menubar, "Help", help_menu, 1);
    mtk_menubar_set_help(a.menubar, help);

    a.search = mtk_entry_create(a.win, nullptr);
    a.search->on_activate = on_search_enter;
    a.search->data = &a;
    a.find = mtk_button_create(a.win, nullptr, "Find", on_find, &a);
    a.follow = mtk_button_create(a.win, nullptr, "Follow", on_follow, &a);
    mtk_button_set_toggle(a.follow, true);
    a.view = lv_create(a.win, nullptr);
    a.status = mtk_label_create(a.win, nullptr, "File > Open a log file");

    layout(a.win);
    if (argc > 1)
        load_file(&a, argv[1]);

    mtk_window_show(a.win);
    mtk_app_run(mtk);
    mtk_app_destroy(mtk);
    return 0;
}
