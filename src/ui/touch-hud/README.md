# Touch HUD

The touch controls' model (`oa/ui/touch_hud.hpp`, `oa::ui::touch_hud`): what
the controls are, where they go and what a finger on them means, with no SDL
and no runtime, and the gamepad's part of the same layer: the slim pad HUD,
the FORCE chip, button badges, the build and group rings and the pad's
hints. The app writes a `HudState` (what the controls show), lays it out
with `lay_out` into a `Frame` (canvas rectangles) and hit-tests fingers
against it; the drawing reads both. The gamepad dispatcher writes
`HudState::pad`, `build_ring` and `sheet_focus`; the touch dispatcher writes
`force_touch`.

## Entry points

- `classify_device` gives the window's class from its size in points: a
  phone when the shorter side is under `phone_short_side_points` (460), else
  a tablet. The class never comes from the operating system, so the phone
  layout can be checked on a desktop window of a phone's size.
- `lay_out(viewport, state)` places every control the state shows. On a
  tablet the 3.1c HUD keeps its place (`Viewport::chrome`) and the thumb
  column (QUEUE, ADD, CLEAR, x5 on a build page), the group bar (stored
  groups, STORE, SELECT ▾), the right rail (PAUSE, SPEED unless watching,
  CHAT in shared games, CENTRE, FOLLOW, NEXT, INFO) and MENU (in the top
  bar's blank strip, else on the battlefield's top-right corner) go beside
  it. On a phone the battlefield is full-bleed and the left column
  (minimap, BUILD, QUEUE/ADD, CLEAR/SELECT, zoom), PAUSE and MENU, the order
  rail with MORE last, the resource strip, the status pill and the group
  chips with + sit inside the safe area; the frame also says where the
  placed 3.1c regions go (minimap, resources, drawer cells, MORE cells, the
  panel sheet). Both lay out the banner with its ✕, the placement bar
  (✕ CANCEL), the open sheet with its items, and the radial's
  wedges and hub. `Frame::clear` is the battlefield less the controls,
  where the battlefield overlays go.
- `rail_capacity` gives how many phone rail slots fit, MORE included; the
  app fills `HudState::rail` and `rail_count` from it.
- `Latches` keeps QUEUE, ADD and x5: a press is active at once; a lift
  before the hold delay that no action used toggles the latch; a longer or
  used press only held it. In one-action mode a latch turns off after the
  action that used it. `latch_for` and `action_class` say which latch is
  an action's Shift (ADD for selecting, QUEUE for orders, x5 for build
  buttons).
- `make_radial`, `radial_hit` and `radial_hub_hit` lay out and hit-test the
  radial order menu: 12 fixed slots clockwise from the top (move, patrol,
  attack, D-gun or load, capture, stop, info, type, reclaim, repair, unload,
  guard), inner radius 44 pt, outer 128 pt, greyed wedges keeping their
  slots; the hub is a one-shot QUEUE.
- `hit`, `nearest_rect` and `covers` find the control or rectangle under a
  finger, the nearest within a radius when none is exactly under it.
- `control_help`, `control_label`, `menu_item_label`, `order_name` and
  `order_label` give the texts the controls show, untranslated;
  `order_name` gives the order panel names
  `arm_match_command` takes (MOVE, ATTACK, PATROL, DEFEND, STOP, BLAST,
  RECLAIM, REPAIR, CAPTURE, LOAD, UNLOAD). `control_label_lookup` gives
  the text a label is translated by, which is the label itself but where
  one word stands for two things: the rail's NEXT (next unit) is looked up
  as NEXT UNIT and the build pages' NEXT as NEXT PAGE. `shown_label` draws
  a label from its lookup text's translation, or in English when the
  language has none, never from the bare word's; a lookup text with an
  `{action}` field has it filled with the action's own translation.
  `status_hint` gives the status hint ("TAP: MOVE · ENEMY: ATTACK") in the
  language shown, each piece translated whole as "TAP: {action}" and
  "ENEMY: {action}". `banner_title` gives the banner's title in the
  language shown: "Place {name}" with the building's name as the bottom bar
  gives it (`Placement::name`), and "{order} armed" with the order's word
  translated, or "PATROL armed" in English where the language has no
  translation of the phrase.
- `gadget_help` gives the help line a long press on a 3.1c order panel
  gadget shows when the gadget carries none (the game's GUI files give the
  in-game panels no help text): the order buttons, the standing-order
  toggles, ORDERS, BUILD, PREV and NEXT, found by the word the gadget's
  name holds.
- With `HudState::pad.hud` (a gamepad sent input and no finger has landed)
  `lay_out` gives the **slim pad HUD** over the 3.1c screen's battlefield
  instead: the status pill (`Frame::status`) at its top, the QUEUE, ADD and
  (with `pad.force_shown`) FORCE chips under it, the stored groups' chips
  from its bottom left, an open SELECT ▾ centred on it, and `Frame::clear`
  between them. With touch controls, `pad.force_shown` puts FORCE at the
  top of the tablet's thumb column.
- `make_build_ring` and `build_ring_hit` lay out and hit-test the pad's
  build ring: eight slots clockwise from the top (build buttons at N, NE,
  SE, S, SW and NW, NEXT at E, PREV at W), inner radius 44 pt, outer
  128 pt, 56 pt pictures midway across, the ring kept inside the radial's
  area. `make_group_ring` places the ring of the nine groups beside the
  thumb column's place above the group bar's row (a phone: right of its
  column, above its chips), mirrored for a left-handed viewport. `lay_out`
  lays out a `build_wedge` per filled slot and nine `group_wedge`s while
  they show, over the controls under them.
- `pad_status_hint` and `ring_hint` give the pad's lines as pieces of
  glyphs and words ("R2 MOVE · ENEMY: ATTACK · L2 CANCEL"; "RELEASE R1 GIVE
  · A ARM · B CLOSE"), the buttons found by `pad_controls::chord_for`, so
  the fallback and left-handed maps name the right ones. A piece's
  `lookup` names the text its words are translated by when it is not the
  words themselves: ARM is looked up as ARM ORDER, since the game's own
  table holds ARM as the side's name, and the enemy's piece as
  "ENEMY: {action}" with its `action`. `control_badge`
  gives a control's badge (QUEUE R4, ADD L4, CLEAR B, SELECT ▾ D-pad left,
  PAUSE View+X, CHAT View+A, CENTRE L3, FOLLOW R3, NEXT D-pad right, INFO
  View, FORCE R5, a group chip's groups-layer button), and
  `control_help_with_pad` the help line naming it. `button_name`,
  `chord_words` and `hint_words` say buttons, chords and hints in words, by
  glyph set (Deck, Xbox, PlayStation, Nintendo by place).

## Layout rules

- Sizes are the design's, in points (`touch_hud_layout.cpp` names each):
  the tablet's 76 pt thumb column 12 pt right of the battlefield's left
  edge and 8 pt above the bottom bar, 70x52 chips, 60x52 rail buttons
  8 pt inside the right edge; the phone's 104 pt column 8 pt inside the
  safe area, 64x44 rail slots under PAUSE and MENU, the 276 pt drawer with
  84 pt cells three across, the 420 pt MORE sheet (toggles two across,
  buttons three across, then INFO and SELF-DESTRUCT · HOLD).
- Group chips that do not fit their row shrink to 44 pt, then the highest
  groups are left out; STORE (+) and SELECT always show.
- The placement bar sits centred on the bottom line; when controls are in
  its way it moves right of them, or above them when that does not fit, and
  tries again from there. On a tablet the banner is centred on the
  battlefield and, when that would cover MENU or the rail of a narrow
  window, moves left of the rail (narrowing if it must); the rail's buttons
  shrink to 44 pt on a short window so the rail ends above the bottom line.
- An open sheet keeps its button's neighbours where they are. Controls a
  sheet's panel or the radial covers are left out of the frame while it is
  open (they cannot be touched under it); the phone's drawer moves the
  resource strip, the status pill and the chips right of it and hides the
  minimap. Open sheets and the radial do not change `Frame::clear`; the
  banner and the placement bar do.
- On a phone SELECT ▾ opens beside the left column, MENU's sheet and the
  speed popover left of the rail; on a tablet SELECT ▾ opens upward from its
  button and SPEED's popover left of the rail. Menus use two columns of
  44 pt rows, more only when two do not fit the height.
- The radial's ring stays inside the battlefield (tablet) or between the
  phone's column and rail under the resource strip, inside the safe area,
  so no placed region covers it; its anchor stays where the finger held.
- `hit` returns `sheet_outside` (with an empty rectangle) for a point near
  no control and outside the open sheet's panel while a sheet or the radial
  is open; a point inside the panel but near no item gives nothing, so the
  app can try the placed regions there (the drawer's and MORE's cells).
  Among the radial's wedges, whose rectangles may share corners, the
  nearest label wins.
- `covers` holds for the controls, the open sheet, the banner, the
  placement bar, the status pill and the phone's minimap and resource
  strip, not for the tip.

## Invariants

- `lay_out` reads only the `HudState` fields marked (layout), plus the
  tip's anchor while it shows; the pressed, lit, progress, text and rail
  order fields change how controls look, never where they are, and so do
  the pad's badges, glyphs, maps, aims and hints. The last rail slot laid
  out is always MORE.
- Every rectangle is in canvas pixels, computed from points with
  `Viewport::px_per_point`; positions are rounded in whole points, so at
  3 pixels a point every phone rectangle is exactly three times its size at
  1. Every control lies inside the safe area; a control that cannot fit is
  left out.
- No two controls overlap (only a ring's own wedge rectangles may share
  corners), and `Frame::clear` overlaps no control outside an open sheet or
  a ring.
- The left-handed layout mirrors every control, the phone's regions and
  `Frame::clear`: on a phone about the safe area, on a tablet about the
  battlefield, so the 3.1c panel and MENU in its top bar stay. The banner,
  the placement bar and an open sheet move as a whole and keep their
  insides in reading order; the radial and the tip stay at the points they
  name. `mirrored` mirrors one rectangle about the whole viewport.

## Tests

`ui-touch-hud` (`tests/touch_hud_test.cpp`) pins the tablet layout at
1180x820 (`make_match_layout(1180, 820)`, bottom inset 20) and the phone at
852x393 (insets 59, 0, 59, 21) and 956x440 (62, 0, 62, 21): every control's
rectangle, the rail's capacity (6 and 7), the drawer's and MORE's cells,
the clear area, the banner, the placement bar and the sheets; no overlaps,
everything inside the safe area, nothing on the 3.1c bars, a sweep of
window sizes (portrait and landscape tablets, a 640x480 desktop, small and
large phones, canvases at 2 and 3 pixels a point) in both hands with every
sheet and the radial, the scaling at
3 pixels a point, the same frame for states that differ only in looks, the
left-handed mirror, the latches, the radial, the hit tests and every
control's texts. With a gamepad it checks the Steam Deck's 1280x800 screen
at Control size 1, 1.25 and 1.5 (always the tablet layout, nothing off the
screen), the slim pad HUD's pinned places, FORCE in the thumb column, that
the pad's looks never move a control, the build ring's slots, hit test,
clamp and scale, the group ring's place, the hints, badges, help lines
and button names through each map, and the texts the two-meaning labels
are looked up by.

## Limitations

The drawer shows one page of cells at a time and only as many rows of
84 pt cells as fit the safe height (three on an 852x393 phone); the MORE
sheet leaves out cells that do not fit from the end. The drawer and MORE
are laid out only on a phone. Texts are English; the drawing translates
them.
