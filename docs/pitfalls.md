# Pitfalls

The mistakes that cost the most debugging time when working with (or
on) libmtk. Each entry states the symptom you will actually see, the
cause, and the fix.

## Leaked clip

**Symptom:** widgets drawn *after* yours are randomly invisible or
truncated; the corruption moves around as you reorder widgets.

**Cause:** a `mtk_set_clip` without a matching `mtk_clear_clip` on
some exit path (usually an early `return` in a draw handler). The
clip lives on the shared per-window GC *and* on the back-buffer
XRender picture, so it silently applies to everything painted next.

**Fix:** treat set/clear as a bracket. Audit every `return` between
them.

## Use after destroy

**Symptom:** crash or heap corruption some time after closing a
widget or dialog.

**Cause:** `mtk_widget_destroy(w)` frees the widget (its `destroy`
op frees the struct itself). Any pointer you kept is dangling.

**Fix:** null your references when you destroy, and let the
widget-tree teardown own everything parented to a window.

## Expecting window destruction to be immediate

**Symptom:** code after `mtk_window_destroy(win)` still sees the
window; or you avoid calling it from a callback out of fear.

**Cause/fact:** destruction is *deferred* to the end of the event
loop iteration. That makes it always safe to call from handlers —
but the window and its widgets remain technically alive until your
callback returns.

**Fix:** copy anything you need out of the window's widgets *before*
calling `mtk_window_destroy`, then stop touching them (the dialog
pattern in tutorial chapter 5 shows the shape).

## Timer fires into freed memory

**Symptom:** rare crash in a timer callback, typically right after
closing a window.

**Cause:** timers are application-global and do not know when the
widget or window that scheduled them died.

**Fix:** store the timer id and `mtk_timer_cancel` it in the owning
widget's `destroy` op (or the window's `on_destroy`).

## Drawing outside `ops->draw`

**Symptom:** your drawing flickers, disappears on the next repaint,
or never shows at all.

**Cause:** the back buffer is repainted wholesale whenever a window
is damaged; anything painted outside the draw pass is overwritten.

**Fix:** change state, call `mtk_window_damage(win)`, and let your
`draw` render the new state. Draw handlers must be cheap — cache
expensive pixel work between repaints.

## Slicing UTF-8 at byte offsets

**Symptom:** glyphs vanish from the end of truncated strings, or the
entry cursor lands "inside" a character.

**Cause:** all toolkit text is UTF-8; cutting at an arbitrary byte
produces an invalid sequence, which the X font machinery drops.

**Fix:** move offsets with `mtk_utf8_prev` / `mtk_utf8_next` so they
always land on code-point boundaries.

## Feeding legacy encodings to the UI

**Symptom:** accented characters silently disappear from labels
(they are not even rendered as boxes).

**Cause:** bytes like Latin-1 `0xE5` (å) are invalid UTF-8, and
invalid sequences are dropped, not replaced.

**Fix:** recode at the input boundary. File formats can be sneaky:
PNG `tEXt` chunks are Latin-1 by specification, EXIF strings are
usually ASCII but not always, and file names are whatever the
filesystem holds.

## Missing glyphs render as garbage

**Symptom:** text in some scripts (e.g. simplified Chinese) shows as
ASCII-looking noise instead of characters or boxes.

**Cause:** X core-font text conversion falls back charset by
charset; when no installed bitmap font covers a required charset,
the converted bytes get drawn through the wrong font.

**Fix:** install a core bitmap font for that charset (the toolkit
picks it up automatically). This is inherent to core X fonts —
misc-fixed covers Latin, Cyrillic, Greek and Japanese nearly
everywhere.

## Alt shortcuts typing into entries

**Symptom** (only if you bypass the toolkit): pressing Alt+F inserts
"f" into a focused text field instead of opening the File menu.

**Fact:** `MtkEntry` ignores Alt-modified input precisely so menu
mnemonics work; custom focusable widgets should do the same — return
`false` from the `key` op when `ev->state & Mod1Mask` is set, so the
key falls through to the window's `on_key`.
