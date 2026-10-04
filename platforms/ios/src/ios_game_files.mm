// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The iOS and iPadOS side of the Game files screen: the GameFilesHooks
// (oa/app/game_files_hooks.hpp) that only the system can fill. The engine draws the screen,
// plans and checks the copy; this file shows the system's document picker from the game's
// window, holds the access the picker grants to what the player chose until the engine gives
// it back, lists a chosen folder (reporting files a cloud service holds, and their
// placeholders, under their own names), reads each file through a coordinated read so the
// system downloads it first, cancels a wait for a download when the player stops, reports the
// free space the system counts for important use, asks for time to finish a copy away from
// the screen, keeps folders out of device backups, and supplies the words that name the
// device, the Files app, the Finder and iCloud. Every hook catches what it meets, so nothing
// here throws into the game.
#import <FileProvider/FileProvider.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <SDL3/SDL.h>

#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ios_game_files.hpp"
#include "oa/app/game_files_hooks.hpp"

namespace {

using oa::app::CopyOutcome;
using oa::app::FileCopy;
using oa::app::GameFilesCapability;
using oa::app::GameFilesHooks;
using oa::app::GameFilesText;
using oa::app::PickerDone;
using oa::app::PickKind;
using oa::app::SourceEntry;

/// The game folder's name in the app's Documents folder.
NSString* const kGameFolderName = @"Total Annihilation";
/// The type Info.plist declares for the game's archives (.hpi, .ufo, .ccx, .gp3).
NSString* const kArchiveType = @"net.coreprime.open-annihilation.game-archive";
/// The suffix of the placeholder a cloud service leaves for a file it has not downloaded,
/// named .<name>.icloud.
NSString* const kPlaceholderSuffix = @".icloud";
/// The key of a placeholder's property list that holds the real file's size.
NSString* const kPlaceholderSizeKey = @"NSURLFileSizeKey";
/// The key of a placeholder's property list that says whether the real item is a folder.
NSString* const kPlaceholderTypeKey = @"NSURLFileResourceTypeKey";
/// That key's value for a folder.
NSString* const kPlaceholderFolderType = @"NSURLFileResourceTypeDirectory";
/// The folder in the app's temporary folder where a copy the picker made for the game (the
/// demo's installer) is kept, one sub-folder per pick, until the engine moves it or gives it
/// back: the system empties its own Inbox folder, where the picker puts the copy, soon after
/// the pick.
NSString* const kChosenCopiesFolder = @"Chosen files";

/// How often a copy waiting for a download looks at the engine's stop flag.
constexpr int64_t kStopPollNanoseconds = 100 * NSEC_PER_MSEC;
/// How long the end of the background time waits for the copy to stop and give the time
/// back before it ends the background task itself.
constexpr int64_t kExpiryGraceNanoseconds = 2 * NSEC_PER_SEC;
/// A listing at least this deep is a whole listing, for a copy: a sub-folder it cannot read
/// fails it, so no file is left behind unnoticed. A shallower one (the engine's quick check
/// by name) passes over sub-folders it cannot read.
constexpr uint32_t kWholeListingDepth = 1000;

/// The platform's words for the screen's texts, composed once for the device's kind.
std::array<std::string, oa::app::game_files_text_count> g_words{};

/// The game's window, from whose root view controller the picker is shown.
std::atomic<SDL_Window*> g_window{nullptr};

/// The application object, kept from the main thread so the copy's worker thread can ask for
/// and give back background time without asking UIKit for it.
UIApplication* g_application = nil;

/// Writes `text` into `error` when the engine asked for one.
///
/// @param error the engine's error string, or null
/// @param text what went wrong
void set_error(std::string* error, const std::string& text) {
    if (error != nullptr)
        *error = text;
}

/// The UTF-8 text of an NSString, empty for nil.
///
/// @param text the string
/// @return its UTF-8 bytes
std::string utf8(NSString* text) {
    if (text == nil)
        return {};
    const char* bytes = text.UTF8String;
    return bytes == nullptr ? std::string{} : std::string(bytes);
}

/// An NSError's description as a clause the engine can put after a colon: its failure
/// reason when it gives one, else its description, without the final full stop.
///
/// @param error the error
/// @return the clause
std::string describe(NSError* error) {
    if (error == nil)
        return "the system gave no reason";
    NSString* text = error.localizedFailureReason;
    if (text.length == 0)
        text = error.localizedDescription;
    std::string clause = utf8(text);
    while (!clause.empty() && (clause.back() == '.' || clause.back() == ' '))
        clause.pop_back();
    return clause.empty() ? std::string("the system gave no reason") : clause;
}

/// A path the engine gave, as an NSString.
///
/// @param path absolute, UTF-8
/// @return the path, nil when it cannot be decoded
NSString* path_string(const char* path) {
    if (path == nullptr || path[0] == '\0')
        return nil;
    return [NSFileManager.defaultManager stringWithFileSystemRepresentation:path
                                                                     length:std::strlen(path)];
}

/// Whether something (a file, a folder, a link) is at path, without following a link.
///
/// @param path absolute
/// @return true when lstat finds it
bool exists(const char* path) {
    struct stat status{};
    return path != nullptr && lstat(path, &status) == 0;
}

/// The path of the placeholder a cloud service leaves for the file at path until it is
/// downloaded: .<name>.icloud beside it.
///
/// @param path absolute
/// @return the placeholder's path, empty when path has no name
std::string placeholder_path(const char* path) {
    const std::string whole = path == nullptr ? std::string{} : std::string(path);
    const auto slash = whole.find_last_of('/');
    if (slash == std::string::npos || slash + 1 >= whole.size())
        return {};
    return whole.substr(0, slash + 1) + "." + whole.substr(slash + 1) + utf8(kPlaceholderSuffix);
}

// --- Access the picker grants -----------------------------------------------------------

/// What the picker chose and the system made readable, held until the engine gives it back
/// (release_source). Read by the worker threads too, so it is kept under a lock.
struct ScopedSources {
    std::mutex mutex;
    NSMutableDictionary<NSString*, NSURL*>* urls = nil; ///< by path
    NSMutableSet<NSString*>* started = nil;             ///< paths whose access was started
};

/// The run's scoped sources.
///
/// @return the one record
ScopedSources& scoped_sources() {
    static ScopedSources sources;
    return sources;
}

/// Remembers a URL the picker gave, starting the access its security scope grants.
///
/// @param url the chosen item
/// @param scoped whether it carries a security scope (an item opened in place)
/// @return the path the engine is given
std::string hold_source(NSURL* url, bool scoped) {
    const char* representation = url.fileSystemRepresentation;
    if (representation == nullptr || representation[0] == '\0')
        return {};
    std::string path(representation);
    NSString* key = path_string(representation);
    const bool started = scoped && [url startAccessingSecurityScopedResource];
    ScopedSources& sources = scoped_sources();
    const std::lock_guard<std::mutex> lock(sources.mutex);
    if (sources.urls == nil) {
        sources.urls = [NSMutableDictionary dictionary];
        sources.started = [NSMutableSet set];
    }
    if (key == nil)
        return path;
    if ([sources.started containsObject:key]) {
        // The same item chosen again keeps the access it holds, so one release ends it.
        if (started)
            [url stopAccessingSecurityScopedResource];
        return path;
    }
    if (started)
        [sources.started addObject:key];
    sources.urls[key] = url;
    return path;
}

/// A file URL for a path, built on the chosen item that holds it when there is one, so the
/// access granted to that item covers it.
///
/// @param path absolute, UTF-8
/// @param folder whether the path names a folder
/// @return the URL, nil when the path cannot be decoded
NSURL* url_for_path(const char* path, bool folder) {
    NSString* wanted = path_string(path);
    if (wanted == nil)
        return nil;
    NSURL* base = nil;
    NSString* base_path = nil;
    {
        ScopedSources& sources = scoped_sources();
        const std::lock_guard<std::mutex> lock(sources.mutex);
        for (NSString* candidate in sources.urls) {
            const bool inside = [wanted isEqualToString:candidate] ||
                                [wanted hasPrefix:[candidate stringByAppendingString:@"/"]];
            if (inside && candidate.length > base_path.length) {
                base = sources.urls[candidate];
                base_path = candidate;
            }
        }
    }
    if (base != nil) {
        if (wanted.length == base_path.length)
            return base;
        NSString* rest = [wanted substringFromIndex:base_path.length + 1];
        return [base URLByAppendingPathComponent:rest isDirectory:folder];
    }
    return [NSURL fileURLWithFileSystemRepresentation:path
                                          isDirectory:folder ? YES : NO
                                        relativeToURL:nil];
}

/// The folder copies the picker made for the game are kept in (kChosenCopiesFolder).
///
/// @return its path
NSString* chosen_copies_folder() {
    return [NSTemporaryDirectory() stringByAppendingPathComponent:kChosenCopiesFolder];
}

/// Moves a copy the picker made for the game out of the system's Inbox folder into a folder
/// of its own under chosen_copies_folder(), before the system empties the Inbox; copies it
/// when it cannot be moved.
///
/// @param url the copy as the picker gave it
/// @return where it is kept; url itself when it could be neither moved nor copied
NSURL* keep_chosen_copy(NSURL* url) {
    NSFileManager* files = NSFileManager.defaultManager;
    NSString* folder =
        [chosen_copies_folder() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    if (![files createDirectoryAtPath:folder
            withIntermediateDirectories:YES
                             attributes:nil
                                  error:nil])
        return url;
    NSURL* kept = [[NSURL fileURLWithPath:folder
                              isDirectory:YES] URLByAppendingPathComponent:url.lastPathComponent
                                                               isDirectory:NO];
    if ([files moveItemAtURL:url toURL:kept error:nil] || [files copyItemAtURL:url
                                                                         toURL:kept
                                                                         error:nil])
        return kept;
    [files removeItemAtPath:folder error:nil];
    return url;
}

/// Removes a copy the picker made for the game that the engine gave back, with its folder,
/// when it is still there (the engine moves the installer it copies).
///
/// @param path the copy's path
void remove_chosen_copy(NSString* path) {
    NSString* folder = path.stringByDeletingLastPathComponent;
    if ([folder.stringByDeletingLastPathComponent isEqualToString:chosen_copies_folder()])
        [NSFileManager.defaultManager removeItemAtPath:folder error:nil];
}

/// The release_source hook: ends the access the picker gave to path, or removes the copy the
/// picker made for the game when the engine has not moved it.
///
/// @param path a path show_picker answered with
void release_source(void*, const char* path) noexcept {
    @try {
        @autoreleasepool {
            NSString* key = path_string(path);
            if (key == nil)
                return;
            remove_chosen_copy(key);
            NSURL* url = nil;
            bool started = false;
            {
                ScopedSources& sources = scoped_sources();
                const std::lock_guard<std::mutex> lock(sources.mutex);
                url = sources.urls[key];
                started = [sources.started containsObject:key];
                [sources.urls removeObjectForKey:key];
                [sources.started removeObject:key];
            }
            if (url != nil && started)
                [url stopAccessingSecurityScopedResource];
        }
    } @catch (...) {
    }
}

} // namespace

// --- The picker -------------------------------------------------------------------------

/// The document picker's delegate for one pick: hands the chosen items, or a cancel, to the
/// engine's answer once.
@interface OAGameFilesPickerDelegate : NSObject <UIDocumentPickerDelegate>
/// The engine's answer.
@property (nonatomic) PickerDone done;
/// The engine's value for the answer.
@property (nonatomic) void* userdata;
/// Whether the chosen items are copies the system made for the game (the installer).
@property (nonatomic) BOOL copies;
/// Whether the answer was given.
@property (nonatomic) BOOL answered;
@end

namespace {

/// The delegate of the picker on screen, kept here because the picker holds its delegate
/// weakly; nil when no picker is up. Main thread only.
OAGameFilesPickerDelegate* g_picker_delegate = nil;

/// Gives the engine its answer on the main thread, as the run loop SDL's event pump runs
/// next handles it.
///
/// @param done the engine's answer
/// @param userdata its value
/// @param paths the chosen items, none for a cancel or an error
/// @param movable whether the engine may move them
/// @param error why the picker failed, empty when it did not
void answer_later(
    PickerDone done, void* userdata, std::vector<std::string> paths, bool movable, std::string error
) {
    if (done == nullptr)
        return;
    dispatch_async(dispatch_get_main_queue(), ^{
        @try {
            done(userdata, paths, movable, error.empty() ? nullptr : error.c_str());
        } @catch (...) {
        }
    });
}

/// Ends a pick: holds the chosen items' access and gives the engine the answer.
///
/// @param delegate the pick's delegate
/// @param urls the chosen items, empty for a cancel
void finish_pick(OAGameFilesPickerDelegate* delegate, NSArray<NSURL*>* urls) {
    if (delegate == nil || delegate.answered)
        return;
    delegate.answered = YES;
    if (g_picker_delegate == delegate)
        g_picker_delegate = nil;
    std::vector<std::string> paths;
    for (NSURL* url in urls) {
        if (!url.isFileURL)
            continue;
        std::string path;
        if (delegate.copies == YES) {
            const char* kept = keep_chosen_copy(url).fileSystemRepresentation;
            path = kept == nullptr ? std::string{} : std::string(kept);
        } else {
            path = hold_source(url, true);
        }
        if (!path.empty())
            paths.push_back(path);
    }
    answer_later(delegate.done, delegate.userdata, std::move(paths), delegate.copies == YES, {});
}

/// The game's UIWindow: the SDL window's, else the key window of the app's scenes.
///
/// @return the window, nil when none is open
UIWindow* game_window() {
    if (SDL_Window* window = g_window.load()) {
        const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        void* pointer =
            SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
        if (pointer != nullptr)
            return (__bridge UIWindow*)pointer;
    }
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]])
            continue;
        id candidate = scene;
        UIWindowScene* window_scene = candidate;
        for (UIWindow* window in window_scene.windows) {
            if (window.isKeyWindow)
                return window;
        }
    }
    return nil;
}

/// The view controller the picker is presented from: the one on top of the game window's
/// root view controller.
///
/// @return the controller, nil when the window is not open
UIViewController* presenting_controller() {
    UIViewController* controller = game_window().rootViewController;
    while (controller.presentedViewController != nil &&
           !controller.presentedViewController.isBeingDismissed)
        controller = controller.presentedViewController;
    return controller;
}

/// Makes the system's picker for kind: one folder, opened in place (the game folder, or an
/// expansion, extra maps or a mod); the demo's installer, one file of any kind, copied for
/// the game; or several archives of the game's archive type, opened in place.
///
/// @param kind what the player chooses
/// @return the picker
UIDocumentPickerViewController* make_picker(PickKind kind) {
    UIDocumentPickerViewController* picker = nil;
    switch (kind) {
    case PickKind::game_folder:
    case PickKind::additions_folder:
        picker =
            [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeFolder ]
                                                                        asCopy:NO];
        break;
    case PickKind::demo_installer:
        picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeData ]
                                                                             asCopy:YES];
        break;
    case PickKind::archives: {
        UTType* archive = [UTType typeWithIdentifier:kArchiveType];
        NSArray<UTType*>* types = archive != nil ? @[ archive ] : @[ UTTypeData ];
        picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:types
                                                                             asCopy:NO];
        picker.allowsMultipleSelection = YES;
        break;
    }
    }
    picker.shouldShowFileExtensions = YES;
    return picker;
}

/// Shows the picker; main thread only.
///
/// @param kind what the player chooses
/// @param done the engine's answer
/// @param userdata its value
void show_picker_now(PickKind kind, PickerDone done, void* userdata) {
    UIViewController* presenter = presenting_controller();
    if (g_picker_delegate != nil) {
        if ([presenter isKindOfClass:[UIDocumentPickerViewController class]]) {
            answer_later(done, userdata, {}, false, "a file picker is open already");
            return;
        }
        // An earlier picker left the screen without saying so; the engine asking again shows
        // it has stopped waiting for that one.
        g_picker_delegate.answered = YES;
        g_picker_delegate = nil;
    }
    if (presenter == nil) {
        answer_later(done, userdata, {}, false, "the game's window is not open");
        return;
    }
    UIDocumentPickerViewController* picker = make_picker(kind);
    if (picker == nil) {
        answer_later(done, userdata, {}, false, "the system made no file picker");
        return;
    }
    OAGameFilesPickerDelegate* delegate = [[OAGameFilesPickerDelegate alloc] init];
    delegate.done = done;
    delegate.userdata = userdata;
    delegate.copies = kind == PickKind::demo_installer ? YES : NO;
    picker.delegate = delegate;
    g_picker_delegate = delegate;
    [presenter presentViewController:picker animated:YES completion:nil];
}

/// The show_picker hook: shows the system's document picker for kind from the game's
/// window. The answer comes on the main thread, as SDL's event pump runs: the chosen paths
/// (folders and archives opened in place, readable until release_source; the installer as a
/// copy the system made for the game, which the engine may move), none for a cancel, or why
/// the picker could not be shown.
///
/// @param kind what the player chooses
/// @param done the engine's answer
/// @param userdata its value
void show_picker(void*, PickKind kind, PickerDone done, void* userdata) noexcept {
    if (done == nullptr)
        return;
    @try {
        if (NSThread.isMainThread) {
            show_picker_now(kind, done, userdata);
            return;
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            @try {
                show_picker_now(kind, done, userdata);
            } @catch (NSException* exception) {
                g_picker_delegate = nil;
                answer_later(done, userdata, {}, false, utf8(exception.reason));
            } @catch (...) {
                g_picker_delegate = nil;
                answer_later(done, userdata, {}, false, "the system refused to show it");
            }
        });
    } @catch (NSException* exception) {
        g_picker_delegate = nil;
        answer_later(done, userdata, {}, false, utf8(exception.reason));
    } @catch (...) {
        g_picker_delegate = nil;
        answer_later(done, userdata, {}, false, "the system refused to show it");
    }
}

} // namespace

@implementation OAGameFilesPickerDelegate

- (void)documentPicker:(UIDocumentPickerViewController*)controller
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    (void)controller;
    @try {
        finish_pick(self, urls);
    } @catch (...) {
    }
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    (void)controller;
    @try {
        finish_pick(self, @[]);
    } @catch (...) {
    }
}

@end

namespace {

// --- Listing ----------------------------------------------------------------------------

/// The resource values a listing asks for, fetched with each entry.
NSArray<NSURLResourceKey>* listing_keys() {
    return @[
        NSURLIsRegularFileKey,
        NSURLIsDirectoryKey,
        NSURLIsSymbolicLinkKey,
        NSURLFileSizeKey,
        NSURLContentModificationDateKey,
        NSURLUbiquitousItemDownloadingStatusKey,
    ];
}

/// Seconds since 1970 of a modification date, rounded down to a whole second as the C
/// library and the engine round a file's time.
///
/// @param date the date, or nil
/// @return the seconds, 0 for nil
int64_t seconds_since_1970(NSDate* date) {
    return date == nil ? 0 : static_cast<int64_t>(std::floor(date.timeIntervalSince1970));
}

/// The file's modification time as the C library reports it, which is what the engine
/// compares and stamps; the listed date when it cannot be read.
///
/// @param url the item
/// @param listed the date the listing reported
/// @return seconds since 1970
int64_t modified_seconds(NSURL* url, NSDate* listed) {
    struct stat status{};
    const char* path = url.fileSystemRepresentation;
    if (path != nullptr && lstat(path, &status) == 0)
        return static_cast<int64_t>(status.st_mtimespec.tv_sec);
    return seconds_since_1970(listed);
}

/// What one listed item is, read from the listing's resource values.
///
/// @param url the item as the listing met it
/// @param name the name the engine is given
/// @param[out] entry receives the size, time, kind and whether it waits to be downloaded
/// @return false when the item is a placeholder whose real file is listed too
bool describe_entry(NSURL* url, NSString* name, SourceEntry* entry) {
    NSDictionary<NSURLResourceKey, id>* values = [url resourceValuesForKeys:listing_keys()
                                                                      error:nil];
    const bool link = [values[NSURLIsSymbolicLinkKey] boolValue];
    const bool folder = !link && [values[NSURLIsDirectoryKey] boolValue];
    entry->link = link;
    entry->folder = folder;
    entry->size = 0;
    entry->size_known = true;
    entry->remote = false;
    NSDate* date = values[NSURLContentModificationDateKey];
    const bool placeholder = !link && !folder && name.length > kPlaceholderSuffix.length + 1 &&
                             [name hasPrefix:@"."] && [name hasSuffix:kPlaceholderSuffix];
    if (placeholder) {
        NSString* real =
            [name substringWithRange:NSMakeRange(1, name.length - 1 - kPlaceholderSuffix.length)];
        NSURL* real_url = [url.URLByDeletingLastPathComponent URLByAppendingPathComponent:real];
        if (exists(real_url.fileSystemRepresentation))
            return false;
        // The placeholder's property list says how large the real file is, and whether it is
        // a folder; its own time stands in for the real one until the file is downloaded.
        NSDictionary* info = [NSDictionary dictionaryWithContentsOfURL:url error:nil];
        id size = info[kPlaceholderSizeKey];
        entry->folder = [info[kPlaceholderTypeKey] isEqual:kPlaceholderFolderType];
        entry->remote = true;
        if ([size isKindOfClass:[NSNumber class]] && !entry->folder) {
            entry->size = [size unsignedLongLongValue];
        } else if (!entry->folder) {
            entry->size_known = false;
        }
        entry->modified = seconds_since_1970(date);
        return true;
    }
    if (!folder && !link) {
        NSNumber* size = values[NSURLFileSizeKey];
        if (size != nil)
            entry->size = size.unsignedLongLongValue;
        else
            entry->size_known = false;
        NSString* status = values[NSURLUbiquitousItemDownloadingStatusKey];
        entry->remote = status != nil &&
                        [status isEqualToString:NSURLUbiquitousItemDownloadingStatusNotDownloaded];
    }
    entry->modified = modified_seconds(url, date);
    return true;
}

/// The name the engine is given for an item: a placeholder's real name.
///
/// @param name the name on disk
/// @return the name the item has once downloaded
NSString* listed_name(NSString* name) {
    if (name.length > kPlaceholderSuffix.length + 1 && [name hasPrefix:@"."] &&
        [name hasSuffix:kPlaceholderSuffix])
        return
            [name substringWithRange:NSMakeRange(1, name.length - 1 - kPlaceholderSuffix.length)];
    return name;
}

/// The list_source hook: lists path down to max_depth levels below it with the system's
/// enumerator, which also reaches folders a file provider holds. Each entry's path is
/// relative and '/'-separated; a file a cloud service has not downloaded is reported under
/// its own name, remote, whether the listing shows it so or as its .<name>.icloud
/// placeholder; a size the provider does not report is unknown; links are reported and never
/// followed.
///
/// @param path the folder, absolute UTF-8
/// @param max_depth levels below path to list (0: its own entries only)
/// @param entry called once per entry, on this thread
/// @param userdata entry's value
/// @param error receives why the folder could not be listed
/// @return false when it could not be listed
bool list_source(
    void*,
    const char* path,
    uint32_t max_depth,
    void (*entry)(void* userdata, const SourceEntry& entry),
    void* userdata,
    std::string* error
) noexcept {
    if (path == nullptr || entry == nullptr) {
        set_error(error, "no folder was named");
        return false;
    }
    @try {
        @autoreleasepool {
            NSURL* root = url_for_path(path, true);
            if (root == nil) {
                set_error(error, "its name cannot be read");
                return false;
            }
            NSFileManager* files = [[NSFileManager alloc] init];
            NSError* problem = nil;
            NSNumber* is_folder = nil;
            if (![root getResourceValue:&is_folder forKey:NSURLIsDirectoryKey error:&problem]) {
                set_error(error, describe(problem));
                return false;
            }
            if (!is_folder.boolValue) {
                set_error(error, "it is not a folder");
                return false;
            }
            // Reading the top first finds a folder that cannot be read at all.
            if ([files contentsOfDirectoryAtURL:root
                     includingPropertiesForKeys:nil
                                        options:0
                                          error:&problem] == nil) {
                set_error(error, describe(problem));
                return false;
            }
            __block NSError* unreadable = nil;
            __block NSURL* unreadable_url = nil;
            NSDirectoryEnumerator<NSURL*>* walk =
                [files enumeratorAtURL:root
                    includingPropertiesForKeys:listing_keys()
                                       options:0
                                  errorHandler:^BOOL(NSURL* url, NSError* failure) {
                                      if (unreadable == nil) {
                                          unreadable = failure;
                                          unreadable_url = url;
                                      }
                                      return YES;
                                  }];
            if (walk == nil) {
                set_error(error, "the system could not list it");
                return false;
            }
            std::vector<std::string> names;
            for (;;) {
                @autoreleasepool {
                    NSURL* url = [walk nextObject];
                    if (url == nil)
                        break;
                    const NSUInteger level = walk.level;
                    if (level == 0)
                        continue;
                    const uint64_t depth = level - 1;
                    NSString* name = url.lastPathComponent;
                    SourceEntry listed{};
                    if (!describe_entry(url, name, &listed))
                        continue;
                    if ((listed.folder || listed.link) && depth >= max_depth)
                        [walk skipDescendants];
                    if (depth > max_depth)
                        continue;
                    names.resize(static_cast<std::size_t>(depth));
                    names.push_back(utf8(listed_name(name)));
                    std::string relative;
                    for (const std::string& part : names) {
                        if (!relative.empty())
                            relative += '/';
                        relative += part;
                    }
                    listed.path = std::move(relative);
                    try {
                        entry(userdata, listed);
                    } catch (...) {
                    }
                }
            }
            if (unreadable != nil && max_depth >= kWholeListingDepth) {
                NSString* where = unreadable_url.lastPathComponent;
                set_error(
                    error,
                    (where.length > 0 ? utf8(where) + " cannot be read: " : std::string{}) +
                        describe(unreadable)
                );
                return false;
            }
            return true;
        }
    } @catch (NSException* exception) {
        set_error(error, utf8(exception.reason));
    } @catch (...) {
        set_error(error, "the system could not list it");
    }
    return false;
}

// --- Copying ----------------------------------------------------------------------------

/// Whether the engine asked the copy to stop.
///
/// @param file the copy
/// @return true when its stop flag is set
bool stop_requested(const FileCopy& file) {
    return file.stop != nullptr && file.stop->load(std::memory_order_relaxed);
}

/// The environment variable a test sets to hold copying to a rate, in bytes a second, as a
/// slow shared folder or download would (a copy within the simulator's own storage takes about
/// a second, too short to stop it or to leave the game while it runs). Unset or 0: no wait.
constexpr const char* kCopyRateVariable = "OA_IOS_COPY_RATE";

/// The rate kCopyRateVariable asks for, read once.
///
/// @return bytes a second; 0 for no limit
uint64_t test_copy_rate() {
    static const uint64_t rate = [] {
        const char* text = std::getenv(kCopyRateVariable);
        if (text == nullptr)
            return uint64_t{};
        char* end = nullptr;
        const unsigned long long value = std::strtoull(text, &end, 10);
        return end != text && *end == '\0' ? static_cast<uint64_t>(value) : uint64_t{};
    }();
    return rate;
}

/// Waits as long as the file would take at the rate kCopyRateVariable asks for, looking at
/// the stop flag every 100 ms; returns at once without it.
///
/// @param file the copy
/// @return false when the engine asked the copy to stop meanwhile
bool wait_at_test_rate(const FileCopy& file) {
    const uint64_t rate = test_copy_rate();
    if (rate == 0 || !file.size_known)
        return true;
    const auto end = std::chrono::steady_clock::now() +
                     std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                         std::chrono::duration<double>(static_cast<double>(file.size) / rate)
                     );
    for (auto now = std::chrono::steady_clock::now(); now < end;
         now = std::chrono::steady_clock::now()) {
        if (stop_requested(file))
            return false;
        std::this_thread::sleep_for(
            std::min<std::chrono::steady_clock::duration>(
                end - now, std::chrono::nanoseconds(kStopPollNanoseconds)
            )
        );
    }
    return true;
}

/// Cancels a coordinated read once the engine's stop flag is set, looking at the flag every
/// 100 ms on a queue of its own, so Stop ends a wait for a download. finish() returns once
/// the watch can no longer look at the flag.
class StopWatch {
  public:

    /// Starts watching.
    ///
    /// @param coordinator the read to cancel
    /// @param stop the engine's stop flag, or null (nothing to watch)
    StopWatch(NSFileCoordinator* coordinator, const std::atomic<bool>* stop) {
        if (stop == nullptr)
            return;
        queue_ = dispatch_queue_create("net.coreprime.open-annihilation.copy-stop", nullptr);
        timer_ = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue_);
        finished_ = dispatch_semaphore_create(0);
        __block bool cancelled = false;
        dispatch_source_set_timer(
            timer_,
            dispatch_time(DISPATCH_TIME_NOW, kStopPollNanoseconds),
            static_cast<uint64_t>(kStopPollNanoseconds),
            static_cast<uint64_t>(kStopPollNanoseconds / 10)
        );
        dispatch_source_set_event_handler(timer_, ^{
            if (!cancelled && stop->load(std::memory_order_relaxed)) {
                cancelled = true;
                [coordinator cancel];
            }
        });
        dispatch_semaphore_t finished = finished_;
        dispatch_source_set_cancel_handler(timer_, ^{
            dispatch_semaphore_signal(finished);
        });
        dispatch_resume(timer_);
    }

    StopWatch(const StopWatch&) = delete;
    StopWatch& operator=(const StopWatch&) = delete;

    /// Stops watching (finish()).
    ~StopWatch() { finish(); }

    /// Stops watching and waits until the watch no longer runs.
    void finish() {
        if (timer_ == nil)
            return;
        dispatch_source_cancel(timer_);
        dispatch_semaphore_wait(finished_, DISPATCH_TIME_FOREVER);
        timer_ = nil;
        queue_ = nil;
    }

  private:

    dispatch_queue_t queue_ = nil;
    dispatch_source_t timer_ = nil;
    dispatch_semaphore_t finished_ = nil;
};

/// Whether an error says the network or the cloud service could not be reached.
///
/// @param error one error of the chain
/// @param[out] offline set when the device itself is offline
/// @return true for a download that failed for the network
bool network_failure(NSError* error, bool* offline) {
    if ([error.domain isEqualToString:NSURLErrorDomain]) {
        switch (error.code) {
        case NSURLErrorNotConnectedToInternet:
        case NSURLErrorNetworkConnectionLost:
        case NSURLErrorDataNotAllowed:
        case NSURLErrorInternationalRoamingOff:
            *offline = true;
            return true;
        case NSURLErrorTimedOut:
        case NSURLErrorCannotFindHost:
        case NSURLErrorCannotConnectToHost:
        case NSURLErrorDNSLookupFailed:
            return true;
        default:
            return false;
        }
    }
    if ([error.domain isEqualToString:NSCocoaErrorDomain])
        return error.code == NSUbiquitousFileUnavailableError ||
               error.code == NSUbiquitousFileUbiquityServerNotAvailable;
    if ([error.domain isEqualToString:NSFileProviderErrorDomain])
        return error.code == NSFileProviderErrorServerUnreachable;
    if ([error.domain isEqualToString:NSPOSIXErrorDomain]) {
        switch (error.code) {
        case ENETDOWN:
        case ENETUNREACH:
            *offline = true;
            return true;
        case EHOSTUNREACH:
        case ETIMEDOUT:
        case ENOTCONN:
            return true;
        default:
            return false;
        }
    }
    return false;
}

/// Whether an error says access to the item was withdrawn.
///
/// @param error one error of the chain
/// @return true for a refusal
bool access_refused(NSError* error) {
    if ([error.domain isEqualToString:NSCocoaErrorDomain])
        return error.code == NSFileReadNoPermissionError;
    if ([error.domain isEqualToString:NSFileProviderErrorDomain])
        return error.code == NSFileProviderErrorNotAuthenticated;
    if ([error.domain isEqualToString:NSPOSIXErrorDomain])
        return error.code == EACCES || error.code == EPERM;
    return false;
}

/// Whether an error says the item, or the drive or share holding it, went away.
///
/// @param error one error of the chain
/// @return true for an item that is no longer there
bool item_gone(NSError* error) {
    if ([error.domain isEqualToString:NSCocoaErrorDomain])
        return error.code == NSFileNoSuchFileError || error.code == NSFileReadNoSuchFileError;
    if ([error.domain isEqualToString:NSFileProviderErrorDomain])
        return error.code == NSFileProviderErrorNoSuchItem;
    if ([error.domain isEqualToString:NSPOSIXErrorDomain])
        return error.code == ENOENT || error.code == ENXIO || error.code == ENODEV;
    return false;
}

/// How a coordinated read that never reached the file ended, and why in words the engine
/// puts after a colon.
///
/// @param failure the coordinator's error
/// @param source the file
/// @param error receives the reason
/// @return offline, gone, denied or unreadable
CopyOutcome failed_read(NSError* failure, const char* source, std::string* error) {
    for (NSError* step = failure; step != nil; step = step.userInfo[NSUnderlyingErrorKey]) {
        if (![step isKindOfClass:[NSError class]])
            break;
        bool offline = false;
        if (network_failure(step, &offline)) {
            set_error(
                error, offline ? "you are offline" : "the cloud service could not be reached"
            );
            return CopyOutcome::offline;
        }
        if (access_refused(step)) {
            set_error(error, "access to it was withdrawn");
            return CopyOutcome::denied;
        }
        if (item_gone(step)) {
            // A folder that is gone too was on a drive or share that went away.
            const std::string whole = source == nullptr ? std::string{} : std::string(source);
            const auto slash = whole.find_last_of('/');
            const bool folder_gone =
                slash != std::string::npos && slash > 0 && !exists(whole.substr(0, slash).c_str());
            set_error(
                error,
                folder_gone ? "the drive or shared folder was disconnected"
                            : "it is no longer there"
            );
            return CopyOutcome::gone;
        }
    }
    set_error(error, describe(failure));
    return CopyOutcome::unreadable;
}

/// The copy_file hook: reads one file inside a coordinated read on the calling (worker)
/// thread, which makes the system download a file a cloud service or file provider holds
/// before the accessor runs, and calls the engine's chunked copy on the readable path the
/// accessor gives. Stop cancels a wait for the download. A file that was listed as its
/// placeholder takes its real modification time once downloaded, so it is not taken for a
/// changed file.
///
/// @param file the copy
/// @param error receives why it failed
/// @return how it ended
CopyOutcome copy_file(void*, const FileCopy& file, std::string* error) noexcept {
    if (file.copy == nullptr || file.source == nullptr || file.target == nullptr) {
        set_error(error, "nothing was named to copy");
        return CopyOutcome::unreadable;
    }
    if (stop_requested(file) || !wait_at_test_rate(file))
        return CopyOutcome::stopped;
    @try {
        @autoreleasepool {
            NSURL* source = url_for_path(file.source, false);
            if (source == nil) {
                set_error(error, "its name cannot be read");
                return CopyOutcome::unreadable;
            }
            const std::string placeholder = placeholder_path(file.source);
            const bool waits_for_download =
                !exists(file.source) && !placeholder.empty() && exists(placeholder.c_str());
            NSFileCoordinator* coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
            StopWatch watch(coordinator, file.stop);
            const FileCopy* plan = &file;
            __block CopyOutcome outcome = CopyOutcome::unreadable;
            __block bool read = false;
            NSError* failure = nil;
            [coordinator
                coordinateReadingItemAtURL:source
                                   options:0
                                     error:&failure
                                byAccessor:^(NSURL* readable) {
                                    read = true;
                                    const char* readable_path = readable.fileSystemRepresentation;
                                    if (readable_path == nullptr) {
                                        set_error(error, "its name cannot be read");
                                        return;
                                    }
                                    FileCopy copy = *plan;
                                    struct stat status{};
                                    if (waits_for_download && stat(readable_path, &status) == 0)
                                        copy.modified =
                                            static_cast<int64_t>(status.st_mtimespec.tv_sec);
                                    outcome = plan->copy(readable_path, copy, error);
                                }];
            watch.finish();
            if (read)
                return outcome;
            if (stop_requested(file))
                return CopyOutcome::stopped;
            return failed_read(failure, file.source, error);
        }
    } @catch (NSException* exception) {
        set_error(error, utf8(exception.reason));
    } @catch (...) {
        set_error(error, "the system could not read it");
    }
    return CopyOutcome::unreadable;
}

// --- Space, background time, backups ----------------------------------------------------

/// The free_space hook: the space the system counts as available for important use on the
/// volume of folder (or of its nearest folder that exists), which includes space the system
/// frees by itself; the plain available capacity when that is not reported.
///
/// @param folder absolute UTF-8
/// @param bytes receives the bytes
/// @return true when the system reported a figure
bool free_space(void*, const char* folder, uint64_t* bytes) noexcept {
    if (bytes == nullptr)
        return false;
    @try {
        @autoreleasepool {
            NSString* path = path_string(folder);
            NSFileManager* files = [[NSFileManager alloc] init];
            while (path.length > 1 && ![files fileExistsAtPath:path])
                path = path.stringByDeletingLastPathComponent;
            if (path.length == 0)
                return false;
            NSURL* url = [NSURL fileURLWithPath:path isDirectory:YES];
            NSNumber* important = nil;
            if ([url getResourceValue:&important
                               forKey:NSURLVolumeAvailableCapacityForImportantUsageKey
                                error:nil] &&
                important != nil && important.longLongValue > 0) {
                *bytes = important.unsignedLongLongValue;
                return true;
            }
            NSNumber* available = nil;
            if ([url getResourceValue:&available
                               forKey:NSURLVolumeAvailableCapacityKey
                                error:nil] &&
                available != nil) {
                *bytes = available.unsignedLongLongValue;
                return true;
            }
        }
    } @catch (...) {
    }
    return false;
}

/// The background task a copy holds, and the engine's word for its end.
struct BackgroundTime {
    std::mutex mutex;
    UIBackgroundTaskIdentifier task = UIBackgroundTaskInvalid;
    void (*expiring)(void* userdata) = nullptr;
    void* userdata = nullptr;
    dispatch_semaphore_t given_back = nil; ///< signalled when the engine gives the time back
};

/// The run's background time.
///
/// @return the one record
BackgroundTime& background_time() {
    static BackgroundTime time;
    return time;
}

/// Ends the background task, if one is held; the caller holds the lock.
///
/// @param time the record
void end_background_task(BackgroundTime& time) {
    if (time.task != UIBackgroundTaskInvalid && g_application != nil)
        [g_application endBackgroundTask:time.task];
    time.task = UIBackgroundTaskInvalid;
    time.expiring = nullptr;
    time.userdata = nullptr;
}

/// The end of the background time, on the main thread: tells the engine, which stops the
/// copy at the next chunk boundary and gives the time back, waits a moment for that, then
/// ends the task so the system can suspend the game with no file held.
void background_time_expired() {
    BackgroundTime& time = background_time();
    void (*expiring)(void*) = nullptr;
    void* userdata = nullptr;
    dispatch_semaphore_t given_back = dispatch_semaphore_create(0);
    {
        const std::lock_guard<std::mutex> lock(time.mutex);
        if (time.task == UIBackgroundTaskInvalid)
            return;
        expiring = time.expiring;
        userdata = time.userdata;
        time.given_back = given_back;
    }
    if (expiring != nullptr) {
        try {
            expiring(userdata);
        } catch (...) {
        }
        dispatch_semaphore_wait(
            given_back, dispatch_time(DISPATCH_TIME_NOW, kExpiryGraceNanoseconds)
        );
    }
    const std::lock_guard<std::mutex> lock(time.mutex);
    end_background_task(time);
    time.given_back = nil;
}

/// The keep_running hook: begins a background task while a copy runs (true) and ends it
/// when the copy ends (false). The system calls the task's end when the time runs out,
/// which calls expiring. Safe on any thread.
///
/// @param running whether a copy runs
/// @param expiring the engine's word for the end of the time
/// @param userdata its value
void keep_running(void*, bool running, void (*expiring)(void* userdata), void* userdata) noexcept {
    @try {
        BackgroundTime& time = background_time();
        if (!running) {
            dispatch_semaphore_t given_back = nil;
            {
                const std::lock_guard<std::mutex> lock(time.mutex);
                end_background_task(time);
                given_back = time.given_back;
                time.given_back = nil;
            }
            if (given_back != nil)
                dispatch_semaphore_signal(given_back);
            return;
        }
        const std::lock_guard<std::mutex> lock(time.mutex);
        time.expiring = expiring;
        time.userdata = userdata;
        if (time.task != UIBackgroundTaskInvalid || g_application == nil)
            return;
        time.task = [g_application beginBackgroundTaskWithName:@"Copying game files"
                                             expirationHandler:^{
                                                 @try {
                                                     background_time_expired();
                                                 } @catch (...) {
                                                 }
                                             }];
    } @catch (...) {
    }
}

/// The set_backed_up hook: keeps path out of device backups (false) or puts it back (true),
/// through the item's excluded-from-backup property, which covers a folder's contents.
///
/// @param path absolute UTF-8
/// @param backed_up whether the item is backed up
/// @return true when the property was set
bool set_backed_up(void*, const char* path, bool backed_up) noexcept {
    @try {
        @autoreleasepool {
            if (!exists(path))
                return false;
            NSURL* url = [NSURL fileURLWithFileSystemRepresentation:path
                                                        isDirectory:NO
                                                      relativeToURL:nil];
            return [url setResourceValue:@(!backed_up)
                                  forKey:NSURLIsExcludedFromBackupKey
                                   error:nil];
        }
    } @catch (...) {
    }
    return false;
}

// --- Where the game folder goes, and the words ------------------------------------------

/// The game_folder hook: Documents/Total Annihilation in the app's container, where the
/// Files app and the Finder show it.
///
/// @param folder receives the absolute path
/// @return true when the Documents folder is known
bool game_folder(void*, std::string* folder) noexcept {
    if (folder == nullptr)
        return false;
    @try {
        @autoreleasepool {
            NSArray<NSString*>* documents =
                NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
            if (documents.count == 0)
                return false;
            const char* path =
                [documents.firstObject stringByAppendingPathComponent:kGameFolderName]
                    .fileSystemRepresentation;
            if (path == nullptr || path[0] == '\0')
                return false;
            *folder = path;
            return true;
        }
    } @catch (...) {
    }
    return false;
}

/// The capabilities hook: every way in. The picker chooses folders and several files at
/// once, the Files app and the Finder reach the game folder, sources may be in iCloud Drive
/// or another cloud service, and a copy may go on for a while away from the screen.
///
/// @return the GameFilesCapability bits
uint32_t capabilities(void*) noexcept {
    return static_cast<uint32_t>(GameFilesCapability::pick_folder) |
           static_cast<uint32_t>(GameFilesCapability::pick_files) |
           static_cast<uint32_t>(GameFilesCapability::shared_documents) |
           static_cast<uint32_t>(GameFilesCapability::remote_files) |
           static_cast<uint32_t>(GameFilesCapability::background_time);
}

/// The text hook: the platform's words, composed for the device's kind at install.
///
/// @param which the word
/// @return the word in static storage, null when there is none
const char* text(void*, GameFilesText which) noexcept {
    const auto index = static_cast<std::size_t>(which);
    if (index >= g_words.size() || g_words[index].empty())
        return nullptr;
    return g_words[index].c_str();
}

/// Composes the words for an iPad or an iPhone, in the shapes the engine's sentences take
/// them: the device as a noun ("this iPad"); the steps of Copy it yourself as whole sentences,
/// before the engine's "Name it Total Annihilation."; its short form, the places the picker
/// reaches and their short form, and the advice on freeing space as phrases without a full
/// stop, which the engine continues; where the game folder shows in the Files app; and the
/// cloud service's name.
///
/// @param device "iPad" or "iPhone"
void compose_words(const std::string& device) {
    const auto set = [](GameFilesText which, std::string word) {
        g_words[static_cast<std::size_t>(which)] = std::move(word);
    };
    set(GameFilesText::device, device);
    set(GameFilesText::copy_yourself_steps,
        "Copy your Total Annihilation folder into On My " + device +
            " › Open Annihilation in the Files app, or drag it onto Open Annihilation in the "
            "Finder with this " +
            device + " connected.");
    set(GameFilesText::copy_yourself_short, "Into On My " + device + " › Open Annihilation");
    set(GameFilesText::picker_places,
        "in iCloud Drive, on a USB drive, in a computer's shared folder or on this " + device);
    set(GameFilesText::picker_places_short, "iCloud Drive, a USB drive, a shared folder");
    set(GameFilesText::free_space_advice,
        "Free up space in Settings › General › " + device + " Storage");
    set(GameFilesText::folder_location,
        "In Files: On My " + device + " › Open Annihilation › Total Annihilation");
    set(GameFilesText::cloud_name, "iCloud");
}

} // namespace

void install_ios_game_files_hooks() {
    @try {
        g_application = UIApplication.sharedApplication;
        // Copies an earlier run kept are of no use now: a pick is made again to continue.
        [NSFileManager.defaultManager removeItemAtPath:chosen_copies_folder() error:nil];
        const bool pad = UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad;
        compose_words(pad ? "iPad" : "iPhone");
    } @catch (...) {
        compose_words("iPhone");
    }
    GameFilesHooks hooks{};
    hooks.capabilities = capabilities;
    hooks.text = text;
    hooks.game_folder = game_folder;
    hooks.show_picker = show_picker;
    hooks.release_source = release_source;
    hooks.list_source = list_source;
    hooks.copy_file = copy_file;
    hooks.free_space = free_space;
    hooks.keep_running = keep_running;
    hooks.set_backed_up = set_backed_up;
    oa::app::set_game_files_hooks(hooks);
}

void ios_game_files_window_ready(SDL_Window* window) {
    g_window.store(window);
    @try {
        if (g_application == nil)
            g_application = UIApplication.sharedApplication;
    } @catch (...) {
    }
}
