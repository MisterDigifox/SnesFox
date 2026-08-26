#include "native_file_dialog.hpp"

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

std::optional<std::string> showOpenRomDialog() {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        if (@available(macOS 12.0, *)) {
            NSMutableArray<UTType*>* types = [NSMutableArray array];
            for (NSString* ext in @[@"sfc", @"smc"]) {
                UTType* type = [UTType typeWithFilenameExtension:ext];
                if (type) [types addObject:type];
            }
            [panel setAllowedContentTypes:types];
        } else {
            // Only reached pre-macOS 12, where allowedContentTypes doesn't exist yet.
            #pragma clang diagnostic push
            #pragma clang diagnostic ignored "-Wdeprecated-declarations"
            [panel setAllowedFileTypes:@[@"sfc", @"smc"]];
            #pragma clang diagnostic pop
        }

        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = [[panel URLs] firstObject];
            if (url && url.path) {
                return std::string(url.path.UTF8String);
            }
        }
        return std::nullopt;
    }
}

namespace {
std::optional<std::string> g_menuOpenRomPath;
}

// Target-action glue for the "Open ROM…" menu item — NSMenuItem needs an Objective-C
// target/selector pair, so this tiny object just forwards clicks into showOpenRomDialog()
// for the C++ side's takeMenuOpenRomPath() to pick up.
@interface SnesFoxMenuTarget : NSObject
- (void)openRom:(id)sender;
@end

@implementation SnesFoxMenuTarget
- (void)openRom:(id)sender {
    g_menuOpenRomPath = showOpenRomDialog();
}
@end

void installOpenRomMenu() {
    @autoreleasepool {
        NSMenu* mainMenu = [NSApp mainMenu];
        if (!mainMenu) return;

        // Kept alive for the process lifetime: NSMenuItem holds a weak-ish (non-retaining
        // in older AppKit terms, but effectively lifetime-tied-to-menu) reference to its
        // target, so the target object itself must be owned elsewhere.
        static SnesFoxMenuTarget* target = [[SnesFoxMenuTarget alloc] init];

        NSMenuItem* fileMenuItem = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
        NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
        NSMenuItem* openItem = [[NSMenuItem alloc] initWithTitle:@"Open ROM…" action:@selector(openRom:) keyEquivalent:@"o"];
        [openItem setTarget:target];
        [fileMenu addItem:openItem];
        [fileMenuItem setSubmenu:fileMenu];

        // Index 0 is the app ("snesfox") menu; inserting at 1 puts File right after it.
        [mainMenu insertItem:fileMenuItem atIndex:1];

        // SDL_Init already installed a default "Window" menu (Minimize/Zoom/Bring All to
        // Front) — drop it entirely. There is no menu-driven fullscreen toggle anymore
        // (only the SDLK_F11 key handled directly in display.cpp).
        NSMenuItem* windowMenuItem = [mainMenu itemWithTitle:@"Window"];
        if (windowMenuItem) {
            [mainMenu removeItem:windowMenuItem];
        }
    }
}

std::optional<std::string> takeMenuOpenRomPath() {
    std::optional<std::string> result = g_menuOpenRomPath;
    g_menuOpenRomPath.reset();
    return result;
}

#else

std::optional<std::string> showOpenRomDialog() {
    return std::nullopt;
}

void installOpenRomMenu() {}

std::optional<std::string> takeMenuOpenRomPath() {
    return std::nullopt;
}

#endif
