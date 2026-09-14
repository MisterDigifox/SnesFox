// Windows implementation of the ../macOS/native_game_view.hpp interface (declared under
// src/macOS/ — see native_file_dialog.cpp's comment on that convention). Draws the game
// framebuffer through plain GDI (StretchDIBits) onto the same top-level window
// native_window.cpp creates — Win32 has no separate "content view" object the way an NSWindow
// has an NSView, so the window itself both owns the message loop and draws its own client area.
#include "../macOS/native_game_view.hpp"
#include "../macOS/native_window.hpp"
#include "win_internal.hpp"

#ifdef _WIN32
#include <shellapi.h>
#include <algorithm>
#include <cstring>

namespace {
constexpr int kFrameW = 256;
constexpr int kFrameH = 224;

uint32_t g_pixels[kFrameW * kFrameH];
bool g_hasFrame = false;
std::optional<std::string> g_droppedRomPath;

// Decorative pillarbox/letterbox fill for fullscreen mode, in place of plain black bars —
// matches display.cpp's drawCheckerRect and native_game_view.mm's copy of the same constants,
// so bare mode looks identical across platforms. Only used in fullscreen; windowed mode keeps
// plain black bars, same as macOS.
constexpr int kCheckerCellSize = 28;
constexpr COLORREF kCheckerDark = RGB(38, 39, 44);
constexpr COLORREF kCheckerLight = RGB(56, 58, 65);

void fillRect(HDC hdc, RECT rect, COLORREF color) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

void drawCheckerRect(HDC hdc, RECT area) {
    if (area.right <= area.left || area.bottom <= area.top) return;
    for (int y = area.top; y < area.bottom; y += kCheckerCellSize) {
        for (int x = area.left; x < area.right; x += kCheckerCellSize) {
            const bool dark = ((x / kCheckerCellSize) + (y / kCheckerCellSize)) % 2 == 0;
            RECT cell{x, y, std::min<LONG>(x + kCheckerCellSize, area.right), std::min<LONG>(y + kCheckerCellSize, area.bottom)};
            fillRect(hdc, cell, dark ? kCheckerDark : kCheckerLight);
        }
    }
}

void drawSurroundBars(HDC hdc, RECT client, int dstX, int dstY, int dstW, int dstH, bool fullscreen) {
    const RECT left{0, 0, dstX, client.bottom};
    const RECT right{dstX + dstW, 0, client.right, client.bottom};
    const RECT top{0, 0, client.right, dstY};
    const RECT bottom{0, dstY + dstH, client.right, client.bottom};
    if (fullscreen) {
        drawCheckerRect(hdc, left);
        drawCheckerRect(hdc, right);
        drawCheckerRect(hdc, top);
        drawCheckerRect(hdc, bottom);
    } else {
        fillRect(hdc, left, RGB(0, 0, 0));
        fillRect(hdc, right, RGB(0, 0, 0));
        fillRect(hdc, top, RGB(0, 0, 0));
        fillRect(hdc, bottom, RGB(0, 0, 0));
    }
}
} // namespace

void* attachNativeGameView(void* nativeWindow) {
    if (HWND hwnd = static_cast<HWND>(nativeWindow)) {
        DragAcceptFiles(hwnd, TRUE);
    }
    return nativeWindow;
}

void presentNativeGameFrame(void* view, const uint32_t* pixels) {
    if (!view || !pixels) return;
    std::memcpy(g_pixels, pixels, sizeof(g_pixels));
    g_hasFrame = true;

    HWND hwnd = static_cast<HWND>(view);
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateWindow(hwnd); // synchronous WM_PAINT — mirrors macOS's displayIfNeeded call
}

std::optional<std::string> takeNativeDroppedRomPath() {
    std::optional<std::string> result = g_droppedRomPath;
    g_droppedRomPath.reset();
    return result;
}

void windowsSetDroppedRomPath(const std::string& path) {
    g_droppedRomPath = path;
}

void windowsRedrawGameView(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT client{};
    GetClientRect(hwnd, &client);
    const int clientW = client.right - client.left;
    const int clientH = client.bottom - client.top;

    if (!g_hasFrame || clientW <= 0 || clientH <= 0) {
        fillRect(hdc, client, RGB(0, 0, 0));
        EndPaint(hwnd, &ps);
        return;
    }

    const float scale = std::min(static_cast<float>(clientW) / kFrameW, static_cast<float>(clientH) / kFrameH);
    const int dstW = static_cast<int>(kFrameW * scale);
    const int dstH = static_cast<int>(kFrameH * scale);
    const int dstX = (clientW - dstW) / 2;
    const int dstY = (clientH - dstH) / 2;

    // SDL_PIXELFORMAT_ARGB8888 on little-endian stores each pixel as bytes B,G,R,A in memory —
    // a 32bpp BI_RGB top-down DIB (negative biHeight) expects the exact same byte layout, so
    // no pixel-format conversion is needed here (same reasoning as native_game_view.mm's
    // kCGImageAlphaNoneSkipFirst use on macOS).
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kFrameW;
    bmi.bmiHeader.biHeight = -kFrameH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(hdc, COLORONCOLOR); // no blending — nearest-neighbor-ish pixel-art scaling
    StretchDIBits(hdc, dstX, dstY, dstW, dstH, 0, 0, kFrameW, kFrameH,
                  g_pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);

    drawSurroundBars(hdc, client, dstX, dstY, dstW, dstH, isNativeFullscreen(hwnd));

    EndPaint(hwnd, &ps);
}
#endif
