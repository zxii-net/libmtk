/*
 * 04-lights.c — writing a custom widget.
 *
 * A "lights out" puzzle board: clicking a cell toggles it and its
 * four neighbours; turn everything off to win.  The board is a
 * from-scratch widget with its own ops table — the same mechanism
 * every built-in widget uses.
 */
#include <stdio.h>
#include <stdlib.h>

#include <mtk/mtk.h>

enum { GRID = 5 };

typedef struct LightBoard {
    MtkWidget base;                    /* must be the first member */
    bool on[GRID][GRID];
    int moves;
    void (*on_change)(struct LightBoard *lb, void *data);
    void *data;
} LightBoard;

/* --- widget implementation ------------------------------------- */

static void board_cell_rect(LightBoard *lb, int cx, int cy, int *x,
                            int *y, int *w, int *h)
{
    MtkWidget *b = &lb->base;
    int inner_w = b->w - 2 * MTK_BEVEL;
    int inner_h = b->h - 2 * MTK_BEVEL;
    *x = b->x + MTK_BEVEL + cx * inner_w / GRID;
    *y = b->y + MTK_BEVEL + cy * inner_h / GRID;
    *w = (cx + 1) * inner_w / GRID - cx * inner_w / GRID;
    *h = (cy + 1) * inner_h / GRID - cy * inner_h / GRID;
}

static void board_draw(MtkWidget *w)
{
    LightBoard *lb = (LightBoard *)w;
    MtkWindow *win = w->win;
    MtkPalette *p = &win->app->pal;

    /* the board is a sunken content well */
    mtk_fill_rect(win, w->x, w->y, w->w, w->h, p->muted);
    mtk_draw_bevel_c(win, w->x, w->y, w->w, w->h, MTK_BEVEL, true,
                     p->muted_top, p->muted_bottom);

    for (int cy = 0; cy < GRID; cy++) {
        for (int cx = 0; cx < GRID; cx++) {
            int x, y, cw, ch;
            board_cell_rect(lb, cx, cy, &x, &y, &cw, &ch);
            bool lit = lb->on[cy][cx];
            mtk_fill_rect(win, x + 2, y + 2, cw - 4, ch - 4,
                          lit ? p->active : p->bg);
            mtk_draw_bevel(win, x + 2, y + 2, cw - 4, ch - 4, MTK_BEVEL,
                           lit);
        }
    }
}

static void board_toggle(LightBoard *lb, int cx, int cy)
{
    static const int dx[] = {0, 1, -1, 0, 0};
    static const int dy[] = {0, 0, 0, 1, -1};
    for (int i = 0; i < 5; i++) {
        int nx = cx + dx[i], ny = cy + dy[i];
        if (nx >= 0 && nx < GRID && ny >= 0 && ny < GRID)
            lb->on[ny][nx] = !lb->on[ny][nx];
    }
}

static bool board_event(MtkWidget *w, XEvent *ev)
{
    LightBoard *lb = (LightBoard *)w;
    if (ev->type != ButtonPress || ev->xbutton.button != Button1)
        return false;
    for (int cy = 0; cy < GRID; cy++) {
        for (int cx = 0; cx < GRID; cx++) {
            int x, y, cw, ch;
            board_cell_rect(lb, cx, cy, &x, &y, &cw, &ch);
            if (ev->xbutton.x >= x && ev->xbutton.x < x + cw &&
                ev->xbutton.y >= y && ev->xbutton.y < y + ch) {
                board_toggle(lb, cx, cy);
                lb->moves++;
                mtk_window_damage(w->win);
                if (lb->on_change)
                    lb->on_change(lb, lb->data);
                return true;
            }
        }
    }
    return true;
}

static void board_destroy(MtkWidget *w)
{
    free(w); /* frees the whole LightBoard */
}

static const MtkWidgetOps board_ops = {
    .draw = board_draw,
    .event = board_event,
    .destroy = board_destroy,
};

static LightBoard *board_create(MtkWindow *win, MtkWidget *parent)
{
    LightBoard *lb = calloc(1, sizeof(*lb));
    mtk_widget_init(&lb->base, win, parent, &board_ops);
    return lb;
}

static void board_scramble(LightBoard *lb)
{
    /* toggling random cells keeps the puzzle solvable */
    for (int i = 0; i < 12; i++)
        board_toggle(lb, rand() % GRID, rand() % GRID);
    lb->moves = 0;
    mtk_window_damage(lb->base.win);
    if (lb->on_change)
        lb->on_change(lb, lb->data);
}

/* --- application ------------------------------------------------ */

typedef struct {
    LightBoard *board;
    MtkButton *scramble;
    MtkLabel *status;
} Ui;

static bool board_solved(LightBoard *lb)
{
    for (int y = 0; y < GRID; y++)
        for (int x = 0; x < GRID; x++)
            if (lb->on[y][x])
                return false;
    return true;
}

static void board_changed(LightBoard *lb, void *data)
{
    Ui *ui = data;
    char buf[64];
    if (board_solved(lb))
        snprintf(buf, sizeof(buf), "Solved in %d moves!", lb->moves);
    else
        snprintf(buf, sizeof(buf), "%d move%s", lb->moves,
                 lb->moves == 1 ? "" : "s");
    mtk_label_set_text(ui->status, buf);
}

static void on_scramble(MtkButton *b, void *data)
{
    (void)b;
    board_scramble(((Ui *)data)->board);
}

static void layout(MtkWindow *win)
{
    Ui *ui = win->user;
    int side = win->w - 20 < win->h - 56 ? win->w - 20 : win->h - 56;
    mtk_widget_set_rect(&ui->board->base, (win->w - side) / 2, 10, side,
                        side);
    mtk_widget_set_rect(&ui->scramble->base, 10, win->h - 36, 90, 26);
    mtk_widget_set_rect(&ui->status->base, 112, win->h - 36,
                        win->w - 122, 26);
}

int main(void)
{
    MtkApp *app = mtk_app_create("lights");
    if (!app)
        return 1;

    MtkWindow *win = mtk_window_create(app, "Lights", 300, 350);

    Ui ui = {0};
    ui.board = board_create(win, nullptr);
    ui.board->on_change = board_changed;
    ui.board->data = &ui;
    ui.scramble = mtk_button_create(win, nullptr, "Scramble",
                                    on_scramble, &ui);
    ui.status = mtk_label_create(win, nullptr, "");
    ui.status->align = MTK_ALIGN_RIGHT;

    win->user = &ui;
    win->on_resize = layout;
    layout(win);
    srand((unsigned)mtk_now_ms());
    board_scramble(ui.board);

    mtk_window_show(win);
    mtk_app_run(app);
    mtk_app_destroy(app);
    return 0;
}
