# The libmtk tutorial

A hands-on introduction to **libmtk**, the Motif-style widget
toolkit. It is written for someone who has just learned modern C
(C23) and wants to build their first graphical programs — no prior
X11 or GUI-toolkit experience is assumed.

Every chapter builds a complete, runnable program. The sources live
in [`examples/`](examples/) and are compiled as part of the normal
build, so they are guaranteed to match the current API:

```sh
cd libmtk                  # the toolkit is its own meson project
meson setup build          # once
ninja -C build
./build/tutorial/examples/tut-01-hello
```

## Part I — Fundamentals

1. [Your first window](01-first-window.md) —
   application, window, label, button, the main loop.
2. [Events, layout and timers](02-events-layout-timers.md) —
   how input reaches your code, doing layout by hand, keyboard
   shortcuts, repeating timers.
3. [The standard widgets](03-standard-widgets.md) —
   entries with validation, list boxes with multi-selection and
   drag-reordering; a temperature converter and a to-do list.
4. [Writing your own widget](04-custom-widgets.md) —
   the ops table, drawing with the theme palette, hit testing;
   a small puzzle game.
5. [Menus, dialogs and X resources](05-menus-dialogs-resources.md) —
   the menu bar, dialog windows, and theming applications the
   classic way with `xrdb`.

## Part II — Building blocks for real applications

Part II is aimed at three real programs — a file manager, a live log
viewer and a paint program, built in full in the appendices. Each
chapter supplies one capability they need:

6. [Scrolling your own content](06-scrolling-content.md) —
   the viewport pattern: pairing a scrollbar with a custom widget;
   a text pager.
7. [Trees and lazy data](07-trees-lazy-data.md) —
   populating hierarchies on demand and mapping nodes back to real
   resources; a directory browser.
8. [Background work](08-background-work.md) —
   slicing long work through the idle hook, progress and
   cancellation; a disk-usage scanner.
9. [Pixels and XRender](09-pixels-xrender.md) —
   ARGB buffers, uploading to the server, scaled compositing;
   an animated plasma.
10. [Layouts](10-layouts.md) —
    geometry trees that place widgets automatically: rows, grids,
    frames, splits, stacks; a contact editor with zero geometry
    code.

## Appendices — three real programs

Each appendix is a complete application (400–500 lines) with a
guided walkthrough of how the pieces fit:

- [A: **mfm**, a file manager](a-file-manager.md) —
  tree + sash + multi-select listing, create/rename/delete with
  dialogs. *(chapters 3, 5, 7)*
- [B: **tailview**, a live log viewer](b-log-viewer.md) —
  a growing-file view with follow mode (`tail -f`) and search.
  *(chapters 6, 8)*
- [C: **mpaint**, a paint program](c-paint-program.md) —
  an interactive pixel canvas, palette and brush widgets, saving.
  *(chapters 4, 9)*

## What libmtk is (and is not)

libmtk recreates the *look and feel* of classic Motif — beveled 3D
widgets, bitmap fonts, restrained color — as a small C23 library
directly on Xlib. It is not a binding to the original Motif
libraries and does not use Xt. If you know what `XmCreatePushButton`
was, you will feel at home; if you don't, all the better — the API
is plain C structs and function pointers.

Reference documentation lives in [../README.md](../README.md); this
tutorial is the narrative companion to it.
