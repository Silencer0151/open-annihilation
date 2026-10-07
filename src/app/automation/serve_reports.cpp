// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The endpoint's side of the reports and events (serve_reports.hpp).
#include "serve_reports.hpp"

#include "controls.hpp"
#include "reports.hpp"
#include "requests.hpp"

#include "oa/ui/frontend_multiplayer/lobby.hpp"
#include "oa/ui/frontend_multiplayer/screens.hpp"

#include <optional>
#include <vector>

namespace oa::app::automation {
namespace {

namespace mp = oa::ui::frontend_multiplayer;

/// Returns the hooks through which the event watch sends to the endpoint's client.
///
/// @param[in,out] endpoint the endpoint
/// @return the hooks, their context the endpoint
EventHooks endpoint_hooks(Endpoint& endpoint) {
    EventHooks hooks;
    hooks.context = &endpoint;
    hooks.wanted = [](void* context, uint32_t kind) {
        return static_cast<Endpoint*>(context)->subscribed(kind);
    };
    hooks.begin = [](void* context, std::string_view kind) {
        return static_cast<Endpoint*>(context)->begin_event(kind);
    };
    hooks.send = [](void* context, JsonWriter& event) {
        static_cast<Endpoint*>(context)->send_event(event);
    };
    return hooks;
}

} // namespace

void answer_match(Endpoint& endpoint, const Request& request, Answer& answer) {
    const Json* asked = request.fields.find("state_hash");
    if (asked != nullptr && asked->type() != JsonType::null && !asked->boolean()) {
        answer.refuse("bad_request", "state_hash is true or false", "state_hash");
        return;
    }
    const AutomationHost& host = endpoint.automation_host();
    const oa::World* match = host.match_world(host.context);
    std::optional<uint64_t> digest;
    if (match != nullptr && asked != nullptr && asked->boolean().value_or(false))
        digest = host.match_digest(host.context);
    write_match(answer.json, match, digest);
}

void answer_room(Endpoint& endpoint, const Request& /*request*/, Answer& answer) {
    const CheckHost& host = endpoint.check_host();
    write_room(
        answer.json, &mp::multiplayer_lobby(), host.screen(host.context) == mp::kScreenBattleroom
    );
}

void answer_subscribe(Endpoint& endpoint, const Request& request, Answer& answer) {
    const Json* events = request.fields.find("events");
    uint32_t kinds = 0;
    bool understood = events != nullptr;
    if (understood && events->type() == JsonType::string) {
        understood = *events->string() == "all";
        kinds = event_kind::all;
    } else if (understood && events->type() == JsonType::array) {
        for (const Json& name : events->elements()) {
            const uint32_t kind = name.string() != nullptr ? event_kind_named(*name.string()) : 0;
            understood = understood && kind != 0;
            kinds |= kind;
        }
    } else {
        understood = false;
    }
    if (!understood) {
        answer.refuse(
            "bad_request",
            "events is \"all\" or a list of the kinds of event the endpoint sends",
            "events"
        );
        return;
    }
    if ((kinds & event_kind::screen) != 0 && (endpoint.subscriptions() & event_kind::screen) == 0)
        announce_screen(endpoint_event_watch());
    endpoint.subscribe(kinds);
    JsonWriter& json = answer.json;
    json.key("events");
    json.begin_array();
    for (const EventKindName& kind : event_kind_names())
        if ((kinds & kind.kind) != 0)
            json.string(kind.name);
    json.end_array();
}

EventWatch& endpoint_event_watch() noexcept {
    static EventWatch watch;
    return watch;
}

void send_frame_events(Endpoint& endpoint, FrameStage stage) {
    if (stage != FrameStage::pump)
        return;
    const CheckHost& check = endpoint.check_host();
    const ScreenId screen = check.screen(check.context);
    const AutomationHost& host = endpoint.automation_host();
    Observed now;
    now.screen = screen_name(screen);
    now.match = host.match_world(host.context);
    if (screen == mp::kScreenBattleroom && mp::multiplayer_lobby().game != nullptr)
        now.room = &mp::multiplayer_lobby();
    EventWatch& watch = endpoint_event_watch();
    // The dialogs are read from the screen's controls, only for the frame
    // whose screen event names them.
    if (screen_event_due(watch, now.screen)) {
        std::vector<AutomationControl> controls;
        collect_controls(endpoint, controls);
        now.dialogs = dialog_names(controls);
    }
    watch_events(watch, now, endpoint_hooks(endpoint));
}

void send_loading_events(Endpoint& endpoint, std::span<const uint8_t> rows) {
    const AutomationHost& host = endpoint.automation_host();
    watch_loading(
        endpoint_event_watch(), host.match_world(host.context), rows, endpoint_hooks(endpoint)
    );
}

void send_match_gone(Endpoint& endpoint) {
    watch_match_gone(endpoint_event_watch(), endpoint_hooks(endpoint));
}

} // namespace oa::app::automation
