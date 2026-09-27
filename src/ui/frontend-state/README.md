# Frontend state dispatcher

`oa-ui-frontend-state` is the frontend's state machine, stepped by the
application's frontend mode (app mode 2). It runs the intro, main menu,
single-player, skirmish, options and briefing states (0-15). An extension may
run any state in place of the engine through a `StateHandler`, including the
states from 16 up, which do nothing without one. States and signals are the
game's numeric values; each step makes its field updates and resource choices
and calls the host in a fixed order. The component supplies no UI, movie
player, loading implementation, or fallback host.

The public API is `oa::ui::frontend_state::dispatch(State&, Host&, const
StateHandler&)` in `oa/ui/frontend_state/dispatcher.hpp`. State fields are named for what they
do. Host callbacks can change the state, and a step reads each field only
after the callbacks that come before it. The host must share these fields
with the engine, including the gameplay coordinator; copying disconnected
state snapshots between systems would lose callback effects.

All host methods are mandatory. The `Step` enum names the host calls that
take no scalar arguments; the header says which object each one acts on. Its
numeric values only identify the steps; an extension labels the steps and
queries of its own states with the values the enums leave unnamed. Other
host methods take their scalar arguments or the movie filename. The state
checksum step (`Step::check_state_checksum`) takes no arguments; each state
update runs it at fixed points of its host-call sequence, some more than
once, and the callback-order tests pin those points.

Build and run the callback-order tests:

```sh
cmake -S src/ui/frontend-state -B local/build-frontend-state
cmake --build local/build-frontend-state
ctest --test-dir local/build-frontend-state --output-on-failure
```

The tests cover intro resource/exit paths, main-menu transitions, the
extension's first say on a state and its mode handover, mode changes,
callback mutation timing, and the states the engine does not own.

`main_menu::handle_event` is the MAINMENU.GUI event policy the main-menu setup
installs: SINGLE/MULTI/INTRO/EXIT/Credits, disc/fullscreen gates,
Shift-controlled intro looping, and document cleanup. Its required
`main_menu::Host` connects GUI hit testing, resource resolution, audio, and
dialogs; this component draws no widgets of its own. The `frontend-main-menu`
test checks the callback order.

`game_entry.hpp` adds the complete SINGLE.GUI event policy and the Start branch
of SKIRMISH.GUI. Mandatory hosts perform disc/archive, GUI, audio, map, player
setup and preference operations. Start validation and slot/alliance decisions
follow the game; success sets the pending signal to `signal_id::proceed` and
does not implement loading or simulation. Other skirmish controls and the
selected-widget bookkeeping remain outside this API. `frontend-game-entry`
tests callback order, low-byte disc gates, all failure stages, player-count
sequencing, alliance rules, and bounded slot access.

`initialization.hpp` provides player reset, preferences loading, and map-list
lifecycle helpers. Preferences use an explicit settings/platform host with the
game's missing-value defaults and persisted-default decisions. Player reset
owns eleven complete byte images plus descriptors, and updates the ten players
represented by the dispatcher. Settings storage, the sound device and the
map-list constructor all come from the host.

`skirmish_ui.hpp` holds the skirmish setup/population and the full
selected-control handler. It creates the game's six widgets per player row,
sets the default participants only when all slots are disabled, updates rule
tooltips/stages, and implements player/side/alliance/color/resource controls.
The mandatory host supplies real GUI, sprite, map, sound and modal operations.
`frontend-skirmish-ui` covers screen setup and every control branch.

`map_selection.hpp` supplies the skirmish map-selection modal: actual-name
list sorting/binding, current-map selection, preview update policy, LOAD/list
commits, cancellation and resource cleanup. Its Host binds SELMAP.GUI, real
eligible maps and metadata, actual TNT radar pixels, and parent-screen
updates. Browsing updates the preview while committing changes the saved map.
`frontend-map-selection` verifies the modal's callback order.
