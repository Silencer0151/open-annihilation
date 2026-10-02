// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Metal renderer's adapter on Apple systems: the name of the device of
// the layer SDL draws into, asked by Objective-C messages, so no Metal
// header is needed; SDL's own library already brings in Metal.
#include "readers.hpp"

#import <Foundation/Foundation.h>
#include <objc/message.h>
#include <objc/runtime.h>

#include <SDL3/SDL.h>

namespace oa::platform::render_probe::readers {
namespace {

/// The shape of a message that takes nothing and returns an object.
using ObjectMessage = id (*)(id, SEL);

/// Sends an object a message that takes nothing and returns an object.
///
/// @param receiver the object; nil returns nil
/// @param name the message's name
/// @return what it returned; nil when the object does not answer it
id send_object_message(id receiver, const char* name) {
    if (receiver == nil)
        return nil;
    const SEL selector = sel_registerName(name);
    if (![receiver respondsToSelector:selector])
        return nil;
    return reinterpret_cast<ObjectMessage>(objc_msgSend)(receiver, selector);
}

} // namespace

bool read_metal_adapter(SDL_Renderer* renderer, AdapterFacts& facts) {
    @autoreleasepool {
        void* layer = SDL_GetRenderMetalLayer(renderer);
        if (layer == nullptr)
            return false;
        id device = send_object_message(static_cast<id>(layer), "device");
        id name = send_object_message(device, "name");
        if (name == nil || ![name isKindOfClass:[NSString class]])
            return false;
        NSString* string = name;
        const char* text = [string UTF8String];
        if (text == nullptr)
            return false;
        facts.adapter = text;
        return !facts.adapter.empty();
    }
}

} // namespace oa::platform::render_probe::readers
