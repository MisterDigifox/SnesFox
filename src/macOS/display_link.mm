#include "display_link.hpp"

#ifdef __APPLE__
#import <CoreVideo/CVDisplayLink.h>
#import <dispatch/dispatch.h>

namespace {
CVDisplayLinkRef g_displayLink = nullptr;
dispatch_semaphore_t g_vsyncSemaphore = nullptr;

CVReturn displayLinkCallback(CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp*,
                              CVOptionFlags, CVOptionFlags*, void* userInfo) {
    dispatch_semaphore_signal(static_cast<dispatch_semaphore_t>(userInfo));
    return kCVReturnSuccess;
}
} // namespace

void startDisplayLink() {
    if (g_displayLink) return;
    g_vsyncSemaphore = dispatch_semaphore_create(0);
    CVDisplayLinkCreateWithActiveCGDisplays(&g_displayLink);
    CVDisplayLinkSetCurrentCGDisplay(g_displayLink, CGMainDisplayID());
    CVDisplayLinkSetOutputCallback(g_displayLink, &displayLinkCallback, g_vsyncSemaphore);
    CVDisplayLinkStart(g_displayLink);
}

void stopDisplayLink() {
    if (!g_displayLink) return;
    CVDisplayLinkStop(g_displayLink);
    CVDisplayLinkRelease(g_displayLink);
    g_displayLink = nullptr;
}

void waitForVsync() {
    if (!g_vsyncSemaphore) return;
    while (dispatch_semaphore_wait(g_vsyncSemaphore, DISPATCH_TIME_NOW) == 0) {
        // Drain any backlog so we wait for a tick that hasn't happened yet.
    }
    dispatch_semaphore_wait(g_vsyncSemaphore, DISPATCH_TIME_FOREVER);
}
#else
void startDisplayLink() {}
void stopDisplayLink() {}
void waitForVsync() {}
#endif
