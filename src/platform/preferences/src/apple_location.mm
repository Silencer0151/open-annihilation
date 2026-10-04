// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/preferences.hpp"
#import <Foundation/Foundation.h>
#include <stdexcept>

namespace oa::platform::preferences {
namespace {
/// Returns the user's Application Support folder, as Foundation resolves it.
std::filesystem::path application_support() {
    @autoreleasepool {
        NSArray<NSURL*>* directories =
            [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory
                                                   inDomains:NSUserDomainMask];
        NSURL* directory = [directories firstObject];
        if (directory == nil)
            throw std::runtime_error("Application Support directory unavailable");
        // Foundation resolves the sandbox container as appropriate on macOS/iOS.
        return std::filesystem::path([[directory path] fileSystemRepresentation]);
    }
}
} // namespace

std::filesystem::path data_directory() {
    // Settled once per run, so a folder that cannot be renamed is reported once.
    static const std::filesystem::path folder = apple_data_directory(application_support());
    return folder;
}

std::filesystem::path default_file() {
    return data_directory() / "preferences.conf";
}

std::filesystem::path documents_directory() {
    @autoreleasepool {
        NSArray<NSURL*>* directories =
            [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
                                                   inDomains:NSUserDomainMask];
        NSURL* directory = [directories firstObject];
        if (directory == nil)
            throw std::runtime_error("Documents directory unavailable");
        return std::filesystem::path([[directory path] fileSystemRepresentation]);
    }
}
} // namespace oa::platform::preferences
