# GUI input boundary

This component holds the gadget engine's input side: the per-screen GUI
context and its panel stack, the pointer and keyboard state machines that
drive gadget records, and a few small menu helpers.

- `button_result` checks the selected `0x15B` gadget record and performs a
  case-sensitive `strcmp` against its name. A null layout or selected index
  `-1` returns false.
- `gadget_geometry` reads signed `x`, `y`, `width`, and `height` fields and
  returns inclusive edges (`right=x+width-1`, `bottom=y+height-1`). A zero
  type byte uses origin `(0,0)`, as in 3.1c.
- `clear_selection` writes `-1` to the selected record field.
- `mark_label_shadows` reads the root panel's signed loaded gadget count
  and, for records 1 through that count, ORs attribs bit `0x8` onto
  type-5 labels. The root is not visited. That bit is the label offset-text
  flag the label drawing tests. No coordinates are written. A missing panel
  or a non-positive count is a no-op, and the walk stops at the last stored
  record.

The menu pointer traversal starts at record 1, skips the root panel, checks
only nonzero active records, and retains the last matching rectangle. With a
preexisting selection it processes at most the first record and then stops;
an inactive record stops immediately in that case. Coordinates are expected
after the caller's root-origin subtraction. Those rules are represented by
`hit_test(MenuObject, x, y)`; no generic z-order policy is added.

`physical_key` and `navigation_direction` keep the game's key mapping:
internal control codes `0xF4..0xF7` map to the left/up/right/down virtual
keys `0x25..0x28`; space and modifier mappings are also retained.
`key_is_down` counts a key-state word as down when any bit other than the low
toggle bit is set. These map key states only; moving focus is
`focus_nearest`'s job.

## Scroll bars and the lists they scroll

`oa/ui/gui_input/scroll_bar.hpp` keeps a type-4 gadget as a `ScrollBar`
and a text list as a `ScrollList`, one record each, for screens that keep
their gadgets in a model of their own (the frontend renderer's
`LayoutScrolls`, for one) rather than in the byte records above. The behaviour is the gadget engine's:

- `bind_scroll_bar` binds a bar as its panel's first draw does: the panel's
  own SLIDERS art, else the shared art of COMMONGUI.GAF from frame 10 for a
  bar wider than high and frame 0 for any other, whose first frame then sets
  the bar's thickness. The back and forward arrows are the size of their
  frames, at the bar's start and end, and the bar shrinks to lie between
  them; a horizontal bar's knob is as wide as frame 5 and its positions are
  its width less the knob and 4. Without art a bar has no arrows and its
  longer side less 6 positions.
- A press on the knob drags it pixel for pixel, once an update; a press
  beside it holds the bar, which steps the knob one position toward the
  pointer each tick and once more on release, so a click steps it once. An
  arrow steps at once and, held, repeats once a tick after 15 ticks; its
  bar's handler runs on every step, even at the bar's end. A hidden, grayed
  or unbound bar takes no pointer.
- `scroll_set_value` and `scroll_value` convert between the knob and a value
  up to the bar's maximum, rounding the knob up and the value down.
- A text list is filled (`scroll_list_fill`) with its pitch raised to the
  line height + 1, and its bar shows exactly when the rows overflow it, with
  a knob of rows / count of its length less 3, at least 10 pixels. The list's
  first row follows its bar's knob (`scroll_list_follow_bar`), and the knob
  its first row (`scroll_bar_follow_list`); a screen's pick
  (`scroll_list_select`), the Up and Down keys (`scroll_list_step`), a press
  (`scroll_list_press`) and the wheel (`scroll_list_scroll`) move it.

`scroll_bar_test.cpp` (`gui-scroll-bar`) pins the binding, the pointer and
the lists.

Build independently with:

```sh
cmake -S src/ui/gui-input -B local/build-gui-input
cmake --build local/build-gui-input
ctest --test-dir local/build-gui-input --output-on-failure
```

The vectors cover root exclusion, active-record traversal and overlap behavior,
exact geometry boundaries, case-sensitive selected-name activation, selection
clearing, and the key mappings and predicates.
