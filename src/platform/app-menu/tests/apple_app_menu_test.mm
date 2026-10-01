// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Settings… item in a real application menu, built as the windowing
// library builds it, without a window: the Preferences… item is renamed and
// given the action, the action calls the hook only while the item is
// enabled and never for Cmd+,, and a menu without the item gains one after
// "About".

#include "oa/platform/app_menu.hpp"

#import <AppKit/AppKit.h>

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (condition)
        return;
    std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
    ++failures;
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

namespace app_menu = oa::platform::app_menu;

/// Gives the application a menu bar whose application menu holds "About", a
/// separator, the comma item when asked, a separator and "Quit".
///
/// @param with_preferences whether to add the windowing library's Preferences… item
/// @return the application menu
NSMenu* install_menu_bar(bool with_preferences) {
    NSMenu* bar = [[[NSMenu alloc] initWithTitle:@""] autorelease];
    NSMenu* menu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
    [menu addItemWithTitle:@"About" action:nil keyEquivalent:@""];
    [menu addItem:[NSMenuItem separatorItem]];
    if (with_preferences)
        [menu addItemWithTitle:@"Preferences…" action:nil keyEquivalent:@","];
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:@"Quit" action:nil keyEquivalent:@"q"];
    NSMenuItem* holder = [bar addItemWithTitle:@"" action:nil keyEquivalent:@""];
    [holder setSubmenu:menu];
    [NSApp setMainMenu:bar];
    return menu;
}

/// Queues an event and takes it from the queue, which makes it the
/// application's current event.
///
/// @param event the event
/// @return true when the event came back from the queue
bool take_through_queue(NSEvent* event) {
    [NSApp postEvent:event atStart:YES];
    NSEvent* taken = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:[NSDate distantPast]
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES];
    return taken != nil && [taken type] == [event type];
}

/// Chooses an item as a click would: a pointer event comes first, then the
/// menu's own validation and the item's action.
///
/// @return true when the item was enabled and its action ran
bool choose(NSMenu* menu, NSMenuItem* item) {
    NSEvent* click = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSZeroPoint
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0];
    CHECK(take_through_queue(click));
    [menu update];
    if (![item isEnabled])
        return false;
    [menu performActionForItemAtIndex:[menu indexOfItem:item]];
    return true;
}

app_menu::SettingsItemHooks counting_hooks(int& opened) {
    app_menu::SettingsItemHooks hooks{};
    hooks.context = &opened;
    hooks.open = [](void* context) { ++*static_cast<int*>(context); };
    return hooks;
}

void renames_the_preferences_item() {
    NSMenu* menu = install_menu_bar(true);
    int opened = 0;
    app_menu::install_settings_item(counting_hooks(opened));
    CHECK([menu numberOfItems] == 5);
    NSMenuItem* item = [menu itemAtIndex:2];
    CHECK([[item title] isEqualToString:@"Settings…"]);
    CHECK([[item keyEquivalent] isEqualToString:@","]);
    CHECK([item action] == @selector(openSettings:));

    app_menu::enable_settings_item(false);
    CHECK(!choose(menu, item));
    CHECK(opened == 0);

    app_menu::enable_settings_item(true);
    CHECK(choose(menu, item));
    CHECK(opened == 1);

    app_menu::enable_settings_item(false);
    CHECK(!choose(menu, item));
    CHECK(opened == 1);
}

/// Presses Cmd+, as the windowing library delivers it: queued, taken from
/// the queue and sent to the application, which matches it to the menu.
void press_command_comma() {
    NSEvent* press = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                      location:NSZeroPoint
                                 modifierFlags:NSEventModifierFlagCommand
                                     timestamp:0
                                  windowNumber:0
                                       context:nil
                                    characters:@","
                   charactersIgnoringModifiers:@","
                                     isARepeat:NO
                                       keyCode:43];
    CHECK(take_through_queue(press));
    [NSApp sendEvent:[NSApp currentEvent]];
}

void the_key_equivalent_calls_nothing() {
    NSMenu* menu = install_menu_bar(true);
    int opened = 0;
    app_menu::install_settings_item(counting_hooks(opened));
    app_menu::enable_settings_item(true);
    press_command_comma();
    CHECK(opened == 0);
    CHECK(choose(menu, [menu itemAtIndex:2]));
    CHECK(opened == 1);
}

void a_null_hook_greys_the_item() {
    NSMenu* menu = install_menu_bar(true);
    app_menu::install_settings_item(app_menu::SettingsItemHooks{});
    app_menu::enable_settings_item(true);
    CHECK(!choose(menu, [menu itemAtIndex:2]));
}

void adds_the_item_after_about() {
    NSMenu* menu = install_menu_bar(false);
    int opened = 0;
    app_menu::install_settings_item(counting_hooks(opened));
    CHECK([menu numberOfItems] == 5);
    NSMenuItem* item = [menu itemAtIndex:2];
    CHECK([[item title] isEqualToString:@"Settings…"]);
    CHECK([[item keyEquivalent] isEqualToString:@","]);
    app_menu::enable_settings_item(true);
    CHECK(choose(menu, item));
    CHECK(opened == 1);
}

} // namespace

int main() {
    @autoreleasepool {
        // The shared application without a window or a run loop: enough for
        // a menu bar.
        [NSApplication sharedApplication];
        CHECK(app_menu::settings_item_supported());
        renames_the_preferences_item();
        the_key_equivalent_calls_nothing();
        a_null_hook_greys_the_item();
        adds_the_item_after_about();
    }
    if (failures != 0)
        return 1;
    std::cout << "apple app menu: ok\n";
    return 0;
}
