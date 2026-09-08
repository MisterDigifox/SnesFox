#include "native_window.hpp"

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>

namespace {
// Only ever one bare-mode window per process, so plain globals (rather than a per-window
// associated-object lookup) are simplest — see docs/tickets/03-native-window-lifecycle.md.
bool g_wantsClose = false;
bool g_isFullscreen = false;
NSRect g_savedFrame = NSZeroRect;
NSUInteger g_savedStyleMask = 0;
} // namespace

@interface SnesFoxWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation SnesFoxWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender {
    g_wantsClose = true;
    return NO; // we own teardown timing — the bare-mode loop checks nativeWindowWantsClose()
               // and exits normally on its own next iteration, rather than AppKit tearing the
               // window down immediately here.
}
@end

void* createNativeWindow(const std::string& title, int width, int height, bool resizable) {
    @autoreleasepool {
        // SDL_Init(SDL_INIT_VIDEO) used to do all of this for us (it's what actually backed
        // every "SDL_CreateWindow activates the app" assumption below) — bare mode no longer
        // calls it at all (ticket 01), so without this NSApp stays nil and every [NSApp ...]
        // call anywhere in the native macOS code (here, native_input.mm's event pump,
        // native_file_dialog.mm's menu setup) silently no-ops: the window never becomes key,
        // never receives events, and never actually appears on screen.
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        if (![NSApp mainMenu]) {
            // installOpenRomMenu()/removeDefaultWindowMenu() (native_file_dialog.mm) both
            // assume a main menu already exists with the app menu at index 0, same shape SDL
            // used to install — provide the minimal equivalent (app menu + Quit) ourselves.
            NSMenu* mainMenu = [[NSMenu alloc] init];
            NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
            [mainMenu addItem:appMenuItem];
            NSMenu* appMenu = [[NSMenu alloc] init];
            NSString* appName = [NSString stringWithUTF8String:title.c_str()];
            NSMenuItem* quitItem = [[NSMenuItem alloc] initWithTitle:[@"Quit " stringByAppendingString:appName]
                                                               action:@selector(terminate:)
                                                        keyEquivalent:@"q"];
            [appMenu addItem:quitItem];
            [appMenuItem setSubmenu:appMenu];
            [NSApp setMainMenu:mainMenu];
        }
        if (![NSApp isRunning]) {
            // Normally set by [NSApp run]; bare mode drives its own loop (pumpNativeEvents in
            // native_input.mm) instead, so this has to be poked manually — without it the app
            // never leaves its pre-launch state and menu/window-server interaction is unreliable.
            [NSApp finishLaunching];
        }

        NSUInteger styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
        if (resizable) {
            styleMask |= NSWindowStyleMaskResizable;
        }

        NSRect frame = NSMakeRect(0, 0, width, height);
        // Deliberately never released (no ARC in this build — see release-emu-binary.sh):
        // this window must outlive this function and live for the rest of the process, same
        // lifetime SDL_CreateWindow() would have given a window it created itself.
        NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                         styleMask:styleMask
                                                           backing:NSBackingStoreBuffered
                                                             defer:NO];
        [window setTitle:[NSString stringWithUTF8String:title.c_str()]];
        // SDL_DestroyWindow() on a foreign (CreateWindowFrom) window doesn't close/release the
        // underlying NSWindow — we own its lifetime. Without this, Cocoa's default
        // releasedWhenClosed=YES would deallocate it out from under us the moment the user
        // clicks the close button, before SDL_DestroyWindow ever runs.
        [window setReleasedWhenClosed:NO];
        [window center];
        // SDL_CreateWindow() activates the app and orders its window front automatically;
        // a foreign window handed to SDL_CreateWindowFrom() doesn't get that for free.
        [NSApp activateIgnoringOtherApps:YES];
        [window makeKeyAndOrderFront:nil];
        return static_cast<void*>(window);
    }
}

void installNativeWindowDelegate(void* nativeWindow) {
    NSWindow* window = static_cast<NSWindow*>(nativeWindow);
    // Deliberately never released, same reasoning as the window itself in createNativeWindow().
    SnesFoxWindowDelegate* delegate = [[SnesFoxWindowDelegate alloc] init];
    [window setDelegate:delegate];
}

bool nativeWindowWantsClose(void*) {
    return g_wantsClose;
}

void toggleNativeFullscreen(void* nativeWindow) {
    NSWindow* window = static_cast<NSWindow*>(nativeWindow);
    if (!g_isFullscreen) {
        g_savedFrame = [window frame];
        g_savedStyleMask = [window styleMask];
        [window setStyleMask:NSWindowStyleMaskBorderless];
        NSScreen* screen = [window screen] ?: [NSScreen mainScreen];
        [window setFrame:[screen frame] display:YES];
        g_isFullscreen = true;
    } else {
        [window setStyleMask:g_savedStyleMask];
        [window setFrame:g_savedFrame display:YES];
        g_isFullscreen = false;
    }
}

bool isNativeFullscreen(void*) {
    return g_isFullscreen;
}

void setNativeMinimumSize(void* nativeWindow, int width, int height) {
    NSWindow* window = static_cast<NSWindow*>(nativeWindow);
    [window setContentMinSize:NSMakeSize(width, height)];
}

void getNativeContentSize(void* nativeWindow, int* outWidth, int* outHeight) {
    NSWindow* window = static_cast<NSWindow*>(nativeWindow);
    const NSRect bounds = [window.contentView bounds];
    if (outWidth) *outWidth = static_cast<int>(bounds.size.width);
    if (outHeight) *outHeight = static_cast<int>(bounds.size.height);
}
#else
void* createNativeWindow(const std::string&, int, int, bool) {
    return nullptr;
}
void installNativeWindowDelegate(void*) {}
bool nativeWindowWantsClose(void*) { return false; }
void toggleNativeFullscreen(void*) {}
bool isNativeFullscreen(void*) { return false; }
void setNativeMinimumSize(void*, int, int) {}
void getNativeContentSize(void*, int*, int*) {}
#endif
