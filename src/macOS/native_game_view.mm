#include "native_game_view.hpp"
#include "native_window.hpp"

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include <algorithm>
#include <cstring>

namespace {
constexpr int kFrameW = 256;
constexpr int kFrameH = 224;
std::optional<std::string> g_droppedRomPath;

bool hasRomExtension(NSString* path) {
    NSString* ext = [[path pathExtension] lowercaseString];
    return [ext isEqualToString:@"sfc"] || [ext isEqualToString:@"smc"];
}

// Decorative pillarbox/letterbox fill for fullscreen mode, in place of plain black bars —
// matches display.cpp's drawCheckerRect (same constants) so bare mode looks identical to how
// it did under SDL. Only used in fullscreen (see drawRect: below); windowed mode keeps plain
// black bars, same as before.
constexpr int kCheckerCellSize = 28;
constexpr CGFloat kCheckerDark[4]  = {38.0 / 255.0, 39.0 / 255.0, 44.0 / 255.0, 1.0};
constexpr CGFloat kCheckerLight[4] = {56.0 / 255.0, 58.0 / 255.0, 65.0 / 255.0, 1.0};

void drawCheckerRect(CGContextRef ctx, CGRect area) {
    if (area.size.width <= 0 || area.size.height <= 0) return;
    const int startX = static_cast<int>(area.origin.x);
    const int startY = static_cast<int>(area.origin.y);
    const int endX = static_cast<int>(area.origin.x + area.size.width);
    const int endY = static_cast<int>(area.origin.y + area.size.height);
    for (int y = startY; y < endY; y += kCheckerCellSize) {
        for (int x = startX; x < endX; x += kCheckerCellSize) {
            const bool dark = ((x / kCheckerCellSize) + (y / kCheckerCellSize)) % 2 == 0;
            CGContextSetFillColor(ctx, dark ? kCheckerDark : kCheckerLight);
            CGContextFillRect(ctx, CGRectMake(x, y, std::min(kCheckerCellSize, endX - x),
                                               std::min(kCheckerCellSize, endY - y)));
        }
    }
}
} // namespace

@interface SnesFoxGameView : NSView {
    uint32_t _pixels[kFrameW * kFrameH];
    BOOL _hasFrame;
}
- (void)uploadFrame:(const uint32_t*)pixels;
@end

@implementation SnesFoxGameView

- (BOOL)isOpaque {
    return YES;
}

- (void)uploadFrame:(const uint32_t*)pixels {
    std::memcpy(_pixels, pixels, sizeof(_pixels));
    _hasFrame = YES;
}

// Drag-and-drop ROM loading — replaces SDL_DROPFILE (docs/tickets/04-native-drag-and-drop.md).
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    NSPasteboard* pasteboard = [sender draggingPasteboard];
    NSArray<NSURL*>* urls = [pasteboard readObjectsForClasses:@[[NSURL class]] options:nil];
    if (urls.count == 1 && hasRomExtension(urls.firstObject.path)) {
        return NSDragOperationCopy;
    }
    return NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSPasteboard* pasteboard = [sender draggingPasteboard];
    NSArray<NSURL*>* urls = [pasteboard readObjectsForClasses:@[[NSURL class]] options:nil];
    if (urls.count != 1 || !hasRomExtension(urls.firstObject.path)) return NO;
    g_droppedRomPath = std::string(urls.firstObject.path.UTF8String);
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    CGContextSetRGBFillColor(ctx, 0, 0, 0, 1);
    CGContextFillRect(ctx, NSRectToCGRect(self.bounds));
    if (!_hasFrame) return;

    // SDL_PIXELFORMAT_ARGB8888 on little-endian stores each pixel as bytes B,G,R,A in memory
    // (alpha in the highest byte of the 32-bit ARGB word) — AlphaNoneSkipFirst says "ignore the
    // first (highest) byte", matching our framebuffer's always-opaque alpha exactly.
    //
    // Explicitly sRGB, not CGColorSpaceCreateDeviceRGB() — "device RGB" is an uncalibrated,
    // device-dependent space with no real profile attached, which on a wide-gamut/HDR panel
    // (this Mac's Liquid Retina XDR) can get inconsistent EDR/tone-mapping treatment from the
    // system compositor instead of a normal, predictable color match. Untested until now: every
    // other rendering path this session (SDL/Metal, CVDisplayLink, native NSView) still fed the
    // display uncalibrated/default-tagged pixel data one way or another.
    // CFDataCreate copies the bytes into a new immutable buffer rather than aliasing _pixels
    // directly (CGDataProviderCreateWithData would have handed CGImage a raw pointer into this
    // view's own mutable ivar, with no copy and a null release callback) — the next
    // uploadFrame: call overwrites _pixels in place, and if CGImage/Core Animation ever
    // consumes the provider's bytes lazily (deferred GPU upload on a layer-backed view, rather
    // than fully finishing during this synchronous drawRect:), that write could race the read
    // and tear this exact image, worse the bigger the frame-to-frame delta (fast scrolling) —
    // matching the reported horizontal "sweep" artifact.
    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CFDataRef pixelData = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(_pixels), sizeof(_pixels));
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(pixelData);
    CFRelease(pixelData);
    CGImageRef image = CGImageCreate(kFrameW, kFrameH, 8, 32, kFrameW * 4, colorSpace,
                                      static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst),
                                      provider, nullptr, false, kCGRenderingIntentDefault);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);

    const NSRect bounds = self.bounds;
    const float scale = std::min(static_cast<float>(bounds.size.width) / kFrameW,
                                  static_cast<float>(bounds.size.height) / kFrameH);
    const float dstW = kFrameW * scale;
    const float dstH = kFrameH * scale;
    const CGRect dst = CGRectMake((bounds.size.width - dstW) / 2.0, (bounds.size.height - dstH) / 2.0, dstW, dstH);

    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
    CGContextDrawImage(ctx, dst, image);
    CGImageRelease(image);

    if (isNativeFullscreen(nullptr)) {
        // See docs/tickets/03-native-window-lifecycle.md: F11 fullscreen must preserve the
        // existing checkerboard-pillarbox look, not fall back to plain black bars.
        drawCheckerRect(ctx, CGRectMake(0, 0, dst.origin.x, bounds.size.height));
        drawCheckerRect(ctx, CGRectMake(dst.origin.x + dst.size.width, 0,
                                         bounds.size.width - (dst.origin.x + dst.size.width), bounds.size.height));
        drawCheckerRect(ctx, CGRectMake(0, 0, bounds.size.width, dst.origin.y));
        drawCheckerRect(ctx, CGRectMake(0, dst.origin.y + dst.size.height,
                                         bounds.size.width, bounds.size.height - (dst.origin.y + dst.size.height)));
    }
}

@end

void* attachNativeGameView(void* nativeWindow) {
    @autoreleasepool {
        NSWindow* window = static_cast<NSWindow*>(nativeWindow);
        SnesFoxGameView* view = [[SnesFoxGameView alloc] initWithFrame:window.contentView.bounds];
        view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [view registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
        [window setContentView:view];
        return static_cast<void*>(view);
    }
}

void presentNativeGameFrame(void* view, const uint32_t* pixels) {
    if (!view || !pixels) return;
    @autoreleasepool {
        SnesFoxGameView* gameView = static_cast<SnesFoxGameView*>(view);
        [gameView uploadFrame:pixels];
        [gameView setNeedsDisplay:YES];
        [gameView displayIfNeeded];
    }
}

std::optional<std::string> takeNativeDroppedRomPath() {
    std::optional<std::string> result = g_droppedRomPath;
    g_droppedRomPath.reset();
    return result;
}
#else
void* attachNativeGameView(void*) { return nullptr; }
void presentNativeGameFrame(void*, const uint32_t*) {}
std::optional<std::string> takeNativeDroppedRomPath() { return std::nullopt; }
#endif
