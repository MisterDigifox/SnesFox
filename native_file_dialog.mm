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

#else

std::optional<std::string> showOpenRomDialog() {
    return std::nullopt;
}

#endif
