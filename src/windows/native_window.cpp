// Windows implementation of the ../macOS/native_window.hpp interface (declared under src/macOS/
// since that's where it was first written — see native_file_dialog.cpp's comment for the same
// convention: the header itself is platform-agnostic, only the .mm/.cpp implementation
// differs). Mirrors native_window.mm's NSWindow/NSWindowDelegate approach with a plain Win32
// window class + WndProc.
#include "../macOS/native_window.hpp"
#include "win_internal.hpp"

#ifdef _WIN32
#include <shellapi.h>
#include <algorithm>
#include <string>

namespace {
constexpr wchar_t kClassName[] = L"SnesFoxGameWindow";

struct WindowState {
    bool wantsClose = false;
    bool fullscreen = false;
    WINDOWPLACEMENT savedPlacement{sizeof(WINDOWPLACEMENT)};
    LONG savedStyle = 0;
    int minWidth = 0;
    int minHeight = 0;
};

WindowState* stateFor(HWND hwnd) {
    return reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
            // Sets a flag polled by nativeWindowWantsClose() instead of destroying the window
            // immediately — replaces SDL's SDL_QUIT event (see docs/tickets/01, 03), and lets
            // the caller finish its current frame/cleanup before actually exiting.
            if (WindowState* state = stateFor(hwnd)) state->wantsClose = true;
            return 0;

        case WM_GETMINMAXINFO: {
            if (WindowState* state = stateFor(hwnd); state && (state->minWidth > 0 || state->minHeight > 0)) {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
                mmi->ptMinTrackSize.x = state->minWidth;
                mmi->ptMinTrackSize.y = state->minHeight;
            }
            return 0;
        }

        case WM_ERASEBKGND:
            // WM_PAINT (below) always paints the full client area itself — skip the separate
            // erase pass to avoid a black-then-frame flicker on every repaint.
            return 1;

        case WM_PAINT:
            windowsRedrawGameView(hwnd);
            return 0;

        case WM_SIZE:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            wchar_t wide[MAX_PATH];
            if (DragQueryFileW(drop, 0, wide, MAX_PATH)) {
                char narrow[MAX_PATH * 4];
                if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, narrow, sizeof(narrow), nullptr, nullptr) > 0) {
                    windowsSetDroppedRomPath(std::string(narrow));
                }
            }
            DragFinish(drop);
            return 0;
        }

        case WM_NCDESTROY:
            delete stateFor(hwnd);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void ensureClassRegistered() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassW(&wc);
    registered = true;
}

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), len);
    return wide;
}
} // namespace

void* createNativeWindow(const std::string& title, int width, int height, bool resizable) {
    ensureClassRegistered();

    DWORD style = resizable ? WS_OVERLAPPEDWINDOW
                             : (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX));

    RECT rect{0, 0, width, height};
    AdjustWindowRect(&rect, style, FALSE);

    HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, kClassName, toWide(title).c_str(), style,
                                 CW_USEDEFAULT, CW_USEDEFAULT,
                                 rect.right - rect.left, rect.bottom - rect.top,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return nullptr;

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new WindowState()));

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}

void installNativeWindowDelegate(void*) {
    // No-op on Windows — WM_CLOSE is already handled directly in WndProc above; there's no
    // separate delegate-object concept the way NSWindowDelegate is on macOS.
}

bool nativeWindowWantsClose(void* nativeWindow) {
    if (WindowState* state = stateFor(static_cast<HWND>(nativeWindow))) return state->wantsClose;
    return false;
}

void toggleNativeFullscreen(void* nativeWindow) {
    HWND hwnd = static_cast<HWND>(nativeWindow);
    WindowState* state = stateFor(hwnd);
    if (!state) return;

    if (!state->fullscreen) {
        state->savedPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd, &state->savedPlacement);
        state->savedStyle = GetWindowLongW(hwnd, GWL_STYLE);

        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof(MONITORINFO)};
        GetMonitorInfo(monitor, &mi);

        // Borderless, screen-covering fullscreen — mirrors SDL_WINDOW_FULLSCREEN_DESKTOP (not
        // an exclusive-mode display-resolution switch), matching the checker-pillarbox drawing
        // in native_game_view.cpp, which expects this exact behavior (see docs/tickets/03).
        SetWindowLongW(hwnd, GWL_STYLE,
                       state->savedStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU));
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        state->fullscreen = true;
    } else {
        SetWindowLongW(hwnd, GWL_STYLE, state->savedStyle);
        SetWindowPlacement(hwnd, &state->savedPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
        state->fullscreen = false;
    }
}

bool isNativeFullscreen(void* nativeWindow) {
    if (WindowState* state = stateFor(static_cast<HWND>(nativeWindow))) return state->fullscreen;
    return false;
}

void setNativeMinimumSize(void* nativeWindow, int width, int height) {
    HWND hwnd = static_cast<HWND>(nativeWindow);
    WindowState* state = stateFor(hwnd);
    if (!state) return;

    // ptMinTrackSize is whole-window (frame-included) screen size, not client size — convert
    // using the window's current style, the same way createNativeWindow() sizes the window
    // itself from a requested client size.
    RECT rect{0, 0, width, height};
    AdjustWindowRect(&rect, static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE)), FALSE);
    state->minWidth = rect.right - rect.left;
    state->minHeight = rect.bottom - rect.top;
}

void getNativeContentSize(void* nativeWindow, int* outWidth, int* outHeight) {
    RECT rect{};
    GetClientRect(static_cast<HWND>(nativeWindow), &rect);
    if (outWidth) *outWidth = rect.right - rect.left;
    if (outHeight) *outHeight = rect.bottom - rect.top;
}

#endif
