// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// open_web_address through a stub opener: an http or https address reaches
// the opener unchanged, and every other scheme is refused without a call.
// web_address_available follows the machine: no on a Steam Deck in Game Mode.
#include "web_address.hpp"

#include "oa/app/extension.hpp"
#include "oa/platform/machine.hpp"
#include "oa/test/check.hpp"

#include <string>

namespace {

/// What the stub opener was asked to open.
struct Opened {
    int calls{};
    const void* context_seen{};
    std::string address{};
    bool accept{true};
};

/// Records the address and returns the record's accept flag.
///
/// @param context the Opened record
/// @param address the address the entry handed over
/// @return Opened::accept
bool record_open(void* context, const char* address) {
    auto& opened = *static_cast<Opened*>(context);
    ++opened.calls;
    opened.context_seen = context;
    opened.address = address == nullptr ? std::string{} : address;
    return opened.accept;
}

/// Returns a stub opener that records into `opened`.
///
/// @param opened the record
/// @return the opener
oa::app::WebAddressOpener stub_opener(Opened& opened) {
    oa::app::WebAddressOpener opener{};
    opener.context = &opened;
    opener.open = record_open;
    return opener;
}

/// An http or https address reaches the stub, in the spelling it was given.
void http_and_https_reach_the_opener() {
    const char* addresses[] = {
        "https://example.com/path?q=1#top",
        "http://example.com",
        "HTTPS://Example.COM/Register",
        "hTTp://127.0.0.1:80/",
        "https://user:pass@example.com/a",
    };
    for (const char* address : addresses) {
        Opened opened{};
        OA_CHECK(oa::app::open_web_address_with(address, stub_opener(opened)));
        OA_CHECK(opened.calls == 1);
        OA_CHECK(opened.context_seen == &opened);
        OA_CHECK(opened.address == address);
    }
}

/// A scheme other than http or https, and a malformed address, is refused.
void other_schemes_are_refused() {
    const char* refused[] = {
        nullptr,
        "",
        "example.com",
        "http://",
        "https://",
        "HTTP://",
        "http:/example.com",
        "http:example.com",
        "https:example.com",
        "file:///tmp/page",
        "ftp://example.com",
        "javascript:alert(1)",
        "data:text/plain,hello",
        "steam://open",
        "about:blank",
        " http://example.com",
        "https://example.com/a b",
        "https://example.com/\nfile://elsewhere",
        "http://example.com\t",
        "https://example.com/\x7f",
    };
    for (const char* address : refused) {
        Opened opened{};
        OA_CHECK(!oa::app::open_web_address_with(address, stub_opener(opened)));
        OA_CHECK(opened.calls == 0);
        OA_CHECK(opened.address.empty());
    }
}

/// An opener that cannot open the address is told, and the entry says so.
void an_opener_that_cannot_open_is_still_told() {
    Opened opened{};
    opened.accept = false;
    const char* address = "https://example.com";
    OA_CHECK(!oa::app::open_web_address_with(address, stub_opener(opened)));
    OA_CHECK(opened.calls == 1);
    OA_CHECK(opened.address == address);
}

/// A null open function refuses a good address and is not called.
void a_null_opener_opens_nothing() {
    oa::app::WebAddressOpener opener{};
    OA_CHECK(!oa::app::open_web_address_with("https://example.com", opener));
}

/// The extension's availability entry is the machine's own answer.
void availability_follows_the_machine() {
    const bool steam_deck =
        oa::platform::running_steam_deck_model() != oa::platform::SteamDeckModel::none;
    const bool game_mode = oa::platform::running_in_steam_game_mode();
    OA_CHECK(
        oa::app::web_address_available() ==
        oa::platform::web_address_available(steam_deck, game_mode)
    );
}

} // namespace

int main() {
    http_and_https_reach_the_opener();
    other_schemes_are_refused();
    an_opener_that_cannot_open_is_still_told();
    a_null_opener_opens_nothing();
    availability_follows_the_machine();
    return oa::test::check_exit_status();
}
