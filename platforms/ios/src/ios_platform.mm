// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The iOS and iPadOS platform's part of the game, registered as an extension of oa-game
// (platforms/ios/CMakeLists.txt). Its init runs before the command line is read and before
// SDL starts: it asks SDL for a landscape window whose home indicator stays out of the way,
// makes SDL's view controller keep that window landscape, full screen, without a status bar
// and never upside down in every orientation the device is held in (on iPad by turning the
// game's view inside a portrait screen), and installs the platform's hooks
// (oa/app/platform_hooks.hpp): the touch controls' haptics, the game folder in the app's
// Documents folder (or the copy built into the bundle), the advice shown without one and the
// label of the button that looks again, and, once the window is open, the end of the system's
// three-finger editing gestures, which would otherwise take the fingers of a three-finger
// touch on the battlefield, and the window handed to the Game files screen's picker. It also
// installs the Game files screen's hooks (ios_game_files.mm). Nothing here may throw into the
// game.
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <objc/runtime.h>

#include <SDL3/SDL.h>

#include <string>

#include "ios_game_files.hpp"
#include "oa/app/extension.hpp"
#include "oa/app/platform_hooks.hpp"

#ifndef OA_IOS_BUNDLED_GAME
#define OA_IOS_BUNDLED_GAME 0
#endif

namespace {

/// The name of the game folder the player copies into the app's Documents folder (and of the
/// copy a build may put in the bundle).
NSString* const kGameFolderName = @"Total Annihilation";

/// What the game says on an iPad when it has no usable game folder, after its own first
/// sentence: where the folder goes, how to put it there with the Files app or the Finder,
/// and to come back to the Check again button when the copy has finished.
constexpr const char* kMissingGameFolderAdviceIpad =
    "In the Files app, open On My iPad › Open Annihilation and copy your Total Annihilation "
    "folder there: the folder that holds totala1.hpi, named Total Annihilation. From a Mac, "
    "drag the folder onto Open Annihilation in the Files tab of this iPad's window in the "
    "Finder.\n\nWhen the copy has finished, come back and tap Check again.";

/// The same on an iPhone.
constexpr const char* kMissingGameFolderAdviceIphone =
    "In the Files app, open On My iPhone › Open Annihilation and copy your Total Annihilation "
    "folder there: the folder that holds totala1.hpi, named Total Annihilation. From a Mac, "
    "drag the folder onto Open Annihilation in the Files tab of this iPhone's window in the "
    "Finder.\n\nWhen the copy has finished, come back and tap Check again.";

/// The label of the button that looks for the game folder again.
constexpr const char* kCheckAgain = "Check again";

/// Whether the device is an iPad, read once on the main thread when the hooks are installed.
bool g_ipad = false;

/// Plays one haptic; runs on the main thread, where UIKit's feedback generators live. Each
/// generator is made once and kept ready for the next haptic.
///
/// @param kind the moment the haptic marks
void play_haptic_now(oa::app::Haptic kind) {
    switch (kind) {
    case oa::app::Haptic::hold_started:
    case oa::app::Haptic::box_started: {
        static UIImpactFeedbackGenerator* impact =
            [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium];
        [impact impactOccurred];
        [impact prepare];
        break;
    }
    case oa::app::Haptic::site_refused: {
        static UINotificationFeedbackGenerator* notification =
            [[UINotificationFeedbackGenerator alloc] init];
        [notification notificationOccurred:UINotificationFeedbackTypeError];
        [notification prepare];
        break;
    }
    case oa::app::Haptic::queue_reduced: {
        static UISelectionFeedbackGenerator* selection =
            [[UISelectionFeedbackGenerator alloc] init];
        [selection selectionChanged];
        [selection prepare];
        break;
    }
    }
}

/// The haptic hook: plays the haptic at once on the main thread, or hands it to the main
/// thread from any other. A device without a haptic engine plays nothing.
///
/// @param kind the moment the haptic marks
void play_haptic(void*, oa::app::Haptic kind) noexcept {
    @try {
        if ([NSThread isMainThread]) {
            play_haptic_now(kind);
            return;
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            @try {
                play_haptic_now(kind);
            } @catch (...) {
            }
        });
    } @catch (...) {
    }
}

/// Whether path names a folder.
///
/// @param path the path
/// @return true when a folder is there
bool is_folder(NSString* path) {
    BOOL folder = NO;
    return path != nil &&
           [NSFileManager.defaultManager fileExistsAtPath:path isDirectory:&folder] && folder;
}

/// The default game folder hook: the Total Annihilation folder in the app's Documents folder,
/// where the Files app and the Finder put it, else the copy built into the bundle (only in a
/// build made with OA_IOS_BUNDLED_GAME_DIR). The container's path changes when the app is
/// installed again, which is why this ranks above the folder the game remembers.
///
/// @param folder receives the folder's absolute path
/// @return true when one of the folders is there
bool default_game_folder(void*, std::string* folder) noexcept {
    if (folder == nullptr)
        return false;
    @try {
        @autoreleasepool {
            NSMutableArray<NSString*>* candidates = [NSMutableArray array];
            NSArray<NSString*>* documents =
                NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
            if (documents.count > 0)
                [candidates addObject:[documents.firstObject
                                          stringByAppendingPathComponent:kGameFolderName]];
            if (OA_IOS_BUNDLED_GAME != 0) {
                NSString* bundle = NSBundle.mainBundle.bundlePath;
                if (bundle != nil)
                    [candidates addObject:[bundle stringByAppendingPathComponent:kGameFolderName]];
            }
            for (NSString* candidate in candidates) {
                if (!is_folder(candidate))
                    continue;
                const char* path = candidate.fileSystemRepresentation;
                if (path == nullptr || path[0] == '\0')
                    continue;
                *folder = path;
                return true;
            }
        }
    } @catch (...) {
    }
    return false;
}

/// The advice hook: where the game folder goes on this iPad or iPhone, and how to come
/// back to the game once it is there.
///
/// @return the advice, in static storage
const char* missing_game_folder_advice(void*) noexcept {
    return g_ipad ? kMissingGameFolderAdviceIpad : kMissingGameFolderAdviceIphone;
}

/// The look-again hook: the missing-folder message stays up with this button, so the player
/// can copy the folder with the Files app or the Finder and come back, and the game never
/// closes for lack of its files.
///
/// @return the label, in static storage
const char* game_folder_check_again(void*) noexcept {
    return kCheckAgain;
}

/// Makes every object of responder's class answer UIEditingInteractionConfigurationNone when
/// the system asks whether its three-finger editing gestures (undo, copy, paste) may act.
/// The property cannot be set, only answered, so the answer is added to the class SDL made
/// the object from, which only the game's window uses (the game has no other window). A
/// class that answers itself already is left alone.
///
/// @param responder the view controller, view or window
void refuse_editing_gestures(UIResponder* responder) {
    if (responder == nil)
        return;
    const SEL selector = @selector(editingInteractionConfiguration);
    const Method inherited = class_getInstanceMethod([UIResponder class], selector);
    if (inherited == nullptr)
        return;
    const IMP answer = imp_implementationWithBlock(^UIEditingInteractionConfiguration(id) {
        return UIEditingInteractionConfigurationNone;
    });
    if (!class_addMethod(
            object_getClass(responder), selector, answer, method_getTypeEncoding(inherited)
        ))
        imp_removeBlock(answer);
}

/// The environment variable a test sets to turn the game's window on an iPhone to one
/// landscape orientation as it opens: "left" or "right", the interface orientation as iOS
/// names it (landscape right puts the top of the iPhone, and its camera housing, on the left).
/// The simulator has no command to turn the device, and this is how both sides of the camera
/// housing are checked there. Unset: the device decides. An iPad ignores it: asked for an
/// orientation its screen is not in, iPadOS puts the app in a window.
constexpr const char* kLandscapeVariable = "OA_IOS_LANDSCAPE";

/// Turns the game's window on an iPhone to the landscape orientation kLandscapeVariable asks
/// for, on iOS 16 and later; does nothing without it. The device turns it again when it is
/// turned itself.
///
/// @param window the game's window
void take_requested_landscape(UIWindow* window) {
    const char* wanted = SDL_getenv(kLandscapeVariable);
    if (wanted == nullptr || window.windowScene == nil || g_ipad)
        return;
    UIInterfaceOrientationMask mask = 0;
    if (SDL_strcmp(wanted, "left") == 0)
        mask = UIInterfaceOrientationMaskLandscapeLeft;
    else if (SDL_strcmp(wanted, "right") == 0)
        mask = UIInterfaceOrientationMaskLandscapeRight;
    else
        return;
    if (@available(iOS 16.0, *)) {
        UIWindowSceneGeometryPreferencesIOS* preferences =
            [[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:mask];
        [window.windowScene requestGeometryUpdateWithPreferences:preferences
                                                    errorHandler:^(NSError*) {
                                                    }];
    }
}

/// The window hook: hands the window to the Game files screen's picker, turns the system's
/// three-finger editing gestures off for the game's window, on its root view controller, that
/// controller's view and the window itself, and turns the window to the landscape orientation
/// a test asks for (take_requested_landscape).
///
/// @param window the game's SDL_Window
void window_ready(void*, void* window) noexcept {
    if (window == nullptr)
        return;
    @try {
        ios_game_files_window_ready(static_cast<SDL_Window*>(window));
        const SDL_PropertiesID properties =
            SDL_GetWindowProperties(static_cast<SDL_Window*>(window));
        void* pointer =
            SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
        if (pointer == nullptr)
            return;
        UIWindow* ui_window = (__bridge UIWindow*)pointer;
        UIViewController* root = ui_window.rootViewController;
        refuse_editing_gestures(root);
        refuse_editing_gestures(root.view);
        // The renderer may give the controller another view later; the controller and the
        // window behind it in the responder chain still answer for it.
        refuse_editing_gestures(ui_window);
        take_requested_landscape(ui_window);
    } @catch (...) {
    }
}

/// The landscape orientation the game's picture keeps while the screen is portrait: the last
/// landscape orientation the game's window had, landscape right (the home indicator to the
/// right of the picture) until it has had one. Read and written on the main thread only.
UIInterfaceOrientation g_kept_landscape = UIInterfaceOrientationLandscapeRight;

/// How far the device is turned clockwise from upright portrait when the interface takes
/// orientation, in quarter turns.
///
/// @param orientation an interface orientation
/// @return 0 for portrait, 1 for landscape left (home button on the left), 2 for portrait
///         upside down, 3 for landscape right
int clockwise_quarter_turns(UIInterfaceOrientation orientation) {
    switch (orientation) {
    case UIInterfaceOrientationLandscapeLeft:
        return 1;
    case UIInterfaceOrientationPortraitUpsideDown:
        return 2;
    case UIInterfaceOrientationLandscapeRight:
        return 3;
    default:
        return 0;
    }
}

/// Keeps the game's view landscape inside a portrait window. An iPad held in portrait gives
/// the game a portrait window (iPadOS letterboxes a landscape-only app in a portrait screen
/// instead of turning it), so the view is made landscape-sized, centred, and turned a quarter
/// turn so the picture is the right way up for the landscape orientation the game had last:
/// the picture stays put on the glass while the device is held portrait or upside down, and
/// fills the screen again when it is turned back. SDL takes the window's size from the view's
/// bounds and each touch's position from the view, so the game only ever sees a landscape
/// window. In a landscape window the view is left as UIKit and SDL place it.
///
/// @param controller SDL's view controller for the game's window
void keep_landscape(UIViewController* controller) {
    UIView* view = controller.viewIfLoaded;
    UIWindow* window = view.window;
    if (view == nil || window == nil)
        return;
    UIView* parent = view.superview != nil ? view.superview : window;
    const UIInterfaceOrientation orientation = window.windowScene.interfaceOrientation;
    if (UIInterfaceOrientationIsLandscape(orientation))
        g_kept_landscape = orientation;
    const CGRect area = parent.bounds;
    const bool turn =
        UIInterfaceOrientationIsPortrait(orientation) && area.size.height > area.size.width;
    if (!turn) {
        if (!CGAffineTransformIsIdentity(view.transform)) {
            view.transform = CGAffineTransformIdentity;
            view.frame = area;
        }
        return;
    }
    const int quarter_turns =
        (clockwise_quarter_turns(orientation) - clockwise_quarter_turns(g_kept_landscape) + 4) % 4;
    const CGAffineTransform rotation = CGAffineTransformMakeRotation(quarter_turns * M_PI_2);
    if (!CGAffineTransformEqualToTransform(view.transform, rotation))
        view.transform = rotation;
    const CGRect bounds = CGRectMake(0, 0, area.size.height, area.size.width);
    if (!CGRectEqualToRect(view.bounds, bounds))
        view.bounds = bounds;
    const CGPoint centre = CGPointMake(CGRectGetMidX(area), CGRectGetMidY(area));
    if (!CGPointEqualToPoint(view.center, centre))
        view.center = centre;
}

/// Makes the instances of cls run after(object) once their own implementation of selector
/// (or, when cls has none, the one it inherits) has run. selector takes no argument and
/// returns nothing.
///
/// @param cls the class to change
/// @param selector the method
/// @param after what runs afterwards
void run_after(Class cls, SEL selector, void (^after)(id)) {
    const Method method = class_getInstanceMethod(cls, selector);
    if (method == nullptr)
        return;
    using Body = void (*)(id, SEL);
    const Body before = reinterpret_cast<Body>(method_getImplementation(method));
    const IMP combined = imp_implementationWithBlock(^(id object) {
        before(object, selector);
        after(object);
    });
    // A method cls only inherits is added to cls; one of its own is replaced.
    if (!class_addMethod(cls, selector, combined, method_getTypeEncoding(method)))
        method_setImplementation(method, combined);
}

/// Changes SDL's view controller class, before SDL makes the window, so the game's window
/// takes every orientation the bundle declares, shows no status bar, and keeps its view
/// landscape (keep_landscape) at every layout and every turn of the device, including a half
/// turn, which changes no size and so lays nothing out. The game has no other window, so no
/// other view controller changes.
void install_landscape_window() {
    Class controller = objc_getClass("SDL_uikitviewcontroller");
    if (controller == nil)
        return;
    // Every orientation the bundle declares: all four on iPad, where keep_landscape turns the
    // view in a portrait screen, and the two landscape ones on iPhone, where iOS keeps the
    // last landscape orientation while the device is held portrait, upside down or flat.
    const SEL orientations = @selector(supportedInterfaceOrientations);
    const Method declared = class_getInstanceMethod([UIViewController class], orientations);
    if (declared != nullptr) {
        const IMP every_declared =
            imp_implementationWithBlock(^UIInterfaceOrientationMask(UIViewController* object) {
                return [UIApplication.sharedApplication
                    supportedInterfaceOrientationsForWindow:object.viewIfLoaded.window];
            });
        class_replaceMethod(
            controller, orientations, every_declared, method_getTypeEncoding(declared)
        );
    }
    // No status bar over the game in any orientation: SDL's controller would ask for one
    // unless the window is full screen, and iPadOS shows it in a portrait screen otherwise.
    const SEL status_bar = @selector(prefersStatusBarHidden);
    const Method hidden = class_getInstanceMethod([UIViewController class], status_bar);
    if (hidden != nullptr) {
        const IMP always_hidden = imp_implementationWithBlock(^BOOL(id) {
            return YES;
        });
        class_replaceMethod(controller, status_bar, always_hidden, method_getTypeEncoding(hidden));
    }
    run_after(controller, @selector(viewWillLayoutSubviews), ^(id object) {
        @try {
            keep_landscape(static_cast<UIViewController*>(object));
        } @catch (...) {
        }
    });
    const SEL transition = @selector(viewWillTransitionToSize:withTransitionCoordinator:);
    const Method turn = class_getInstanceMethod(controller, transition);
    if (turn != nullptr) {
        using Body = void (*)(id, SEL, CGSize, id);
        const Body before = reinterpret_cast<Body>(method_getImplementation(turn));
        const IMP turned = imp_implementationWithBlock(^(
            UIViewController* object,
            CGSize size,
            id<UIViewControllerTransitionCoordinator> coordinator
        ) {
            before(object, transition, size, coordinator);
            @try {
                [coordinator
                    animateAlongsideTransition:^(id<UIViewControllerTransitionCoordinatorContext>) {
                        keep_landscape(object);
                    }
                    completion:^(id<UIViewControllerTransitionCoordinatorContext>) {
                        keep_landscape(object);
                    }];
            } @catch (...) {
            }
        });
        if (!class_addMethod(controller, transition, turned, method_getTypeEncoding(turn)))
            method_setImplementation(turn, turned);
    }
}

} // namespace

/// Fills no hooks of the extension table: sets the SDL hints iOS needs and installs the
/// platform's hooks and the Game files screen's.
///
/// @param table the zeroed extension table (unused)
void oa_extension_init_ios_platform(oa::app::Extension* table) {
    (void)table;
    // A landscape window, either way up, that stays landscape in a portrait screen; the home
    // indicator stays dim while the game is played, and a swipe from the screen's edge only
    // shows it: a second swipe goes home.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");
    @try {
        install_landscape_window();
    } @catch (...) {
    } @
    try {
        g_ipad = UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad;
    } @catch (...) {
    }
    oa::app::PlatformHooks hooks{};
    hooks.haptic = play_haptic;
    hooks.default_game_folder = default_game_folder;
    hooks.missing_game_folder_advice = missing_game_folder_advice;
    hooks.window_ready = window_ready;
    hooks.game_folder_check_again = game_folder_check_again;
    oa::app::set_platform_hooks(hooks);
    install_ios_game_files_hooks();
}
