// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The macOS application menu's Settings… item. Every call runs on the thread
// that runs the application's event loop, as AppKit requires.

#include "oa/platform/app_menu.hpp"

#import <AppKit/AppKit.h>

#include <algorithm>

/// The Settings… item's target: calls the game's hook and says whether the
/// item is enabled. One instance lives for the whole run.
@interface OASettingsItemTarget : NSObject {
  @public
    /// What choosing the item does.
    oa::platform::app_menu::SettingsItemHooks hooks;
    /// Whether the item can be chosen.
    BOOL enabled;
}
/// The item's action: calls the open hook unless the item was chosen
/// through its key equivalent.
- (void)openSettings:(id)sender;
/// Tells AppKit whether the item can be chosen.
- (BOOL)validateMenuItem:(NSMenuItem*)item;
@end

@implementation OASettingsItemTarget

- (void)openSettings:(id)sender {
    (void)sender;
    // Cmd+, reaches the game as a key press before AppKit matches it to this
    // item, and the game opens its settings from that key; calling the hook
    // as well would ask twice.
    NSEvent* event = [NSApp currentEvent];
    if (event != nil && [event type] == NSEventTypeKeyDown &&
        ([event modifierFlags] & NSEventModifierFlagCommand) != 0 &&
        [[event charactersIgnoringModifiers] isEqualToString:@","])
        return;
    if (hooks.open != nullptr)
        hooks.open(hooks.context);
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    (void)item;
    return enabled && hooks.open != nullptr;
}

@end

namespace oa::platform::app_menu {
namespace {

/// The item's title, as macOS 13 and later name it.
NSString* const kSettingsTitle = @"Settings…";
/// The item's key equivalent, with the Command modifier.
NSString* const kSettingsKey = @",";
/// Where the item goes when the menu has none to rename: after "About" and
/// its separator.
constexpr NSInteger kSettingsIndex = 2;

/// The item's target; nil until the first install or enable.
OASettingsItemTarget* settings_target = nil;
/// The item, retained; nil until an install finds the application menu.
NSMenuItem* settings_item = nil;

/// Returns the application menu (the first item of the menu bar), or nil
/// when the application has no menu bar.
NSMenu* application_menu() {
    if (NSApp == nil)
        return nil;
    NSMenu* bar = [NSApp mainMenu];
    if (bar == nil || [bar numberOfItems] == 0)
        return nil;
    return [[bar itemAtIndex:0] submenu];
}

/// Finds the item answering Cmd+, in a menu.
///
/// @param menu the application menu
/// @return the item, or nil when the menu has none
NSMenuItem* find_comma_item(NSMenu* menu) {
    for (NSMenuItem* item in [menu itemArray]) {
        if ([[item keyEquivalent] isEqualToString:kSettingsKey] &&
            ([item keyEquivalentModifierMask] & NSEventModifierFlagCommand) != 0)
            return item;
    }
    return nil;
}

/// Returns the item's target, made on first use and kept for the whole run.
OASettingsItemTarget* target() {
    if (settings_target == nil)
        settings_target = [[OASettingsItemTarget alloc] init];
    return settings_target;
}

} // namespace

bool settings_item_supported() noexcept {
    return true;
}

void install_settings_item(const SettingsItemHooks& hooks) {
    @autoreleasepool {
        OASettingsItemTarget* item_target = target();
        item_target->hooks = hooks;
        NSMenu* menu = application_menu();
        if (menu == nil)
            return;
        NSMenuItem* item = find_comma_item(menu);
        if (item == nil) {
            item = [[[NSMenuItem alloc] initWithTitle:kSettingsTitle
                                               action:nil
                                        keyEquivalent:kSettingsKey] autorelease];
            [menu insertItem:item atIndex:std::min(kSettingsIndex, [menu numberOfItems])];
        }
        [item setTitle:kSettingsTitle];
        [item setTarget:item_target];
        [item setAction:@selector(openSettings:)];
        [item setEnabled:item_target->enabled];
        if (settings_item != item) {
            [settings_item release];
            settings_item = [item retain];
        }
    }
}

void enable_settings_item(bool enabled) {
    OASettingsItemTarget* item_target = target();
    item_target->enabled = enabled ? YES : NO;
    // A menu that enables its items itself asks the target (validateMenuItem:)
    // when it opens; one that does not keeps this state.
    [settings_item setEnabled:item_target->enabled];
}

} // namespace oa::platform::app_menu
