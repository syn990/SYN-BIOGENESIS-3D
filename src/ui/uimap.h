/* syn-nav-alt prototype: the interface reports itself.
 *
 * With SYN3D_UIMAP=FILE set, every frame's named widgets (what was drawn
 * with a label and can be hit), the pointer as the interface saw it, and
 * the last presses and clicks it took are written to FILE as one JSON
 * object (FILE.tmp, then renamed, so a reader never sees half of one).
 * Coordinates are drawable pixels, as the interface is drawn and as
 * uiin_event() hit-tests them. Without the variable nothing is recorded. */
#ifndef UI_UIMAP_H
#define UI_UIMAP_H

#include <stdbool.h>

#include "ui/skin.h"

/* r can be hit this frame and is called label; role: tab, button, ... */
void uiin_name(UIRect r, const char *role, const char *label, bool disabled, bool active);
/* the drawable's size this frame (shell_begin) */
void uiin_map_size(int w, int h);

#endif
