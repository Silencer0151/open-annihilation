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

Build independently with:

```sh
cmake -S src/ui/gui-input -B local/build-gui-input
cmake --build local/build-gui-input
ctest --test-dir local/build-gui-input --output-on-failure
```

The vectors cover root exclusion, active-record traversal and overlap behavior,
exact geometry boundaries, case-sensitive selected-name activation, selection
clearing, and the key mappings and predicates.
