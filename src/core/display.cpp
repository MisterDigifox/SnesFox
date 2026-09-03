#include "display.hpp"
#include "sdsp.hpp"
#include "wav_writer.hpp"
#include "../macOS/native_file_dialog.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

namespace {
// Layout uses (2/3) × the original design so window dimensions are ÷1.5 (~3× framebuffer → ~2×).
constexpr int scale23(int v) { return (v * 2 + 1) / 3; }

// Baseline window height keeps scaled SNES framebuffer centred with proportional inset (~32 px).
constexpr int WINDOW_HEIGHT = scale23(768); // height of the top area (left/right menus + game frame)
constexpr int BOTTOM_PANEL_HEIGHT = scale23(440);
constexpr int LEFT_PANEL_WIDTH = scale23(440);
constexpr int PANEL_WIDTH   = scale23(900); // widened for the dir table's entry/start/loop text + Play/Save WAV buttons

constexpr int GAME_SCALE  = 2;
constexpr int GAME_DST_W  = 256 * GAME_SCALE;                    // 512
constexpr int GAME_DST_H  = 224 * GAME_SCALE;                    // 448
constexpr int GAME_DST_X  = LEFT_PANEL_WIDTH;

constexpr int TEXT_PANEL_X = LEFT_PANEL_WIDTH + GAME_DST_W;

constexpr ImVec4 LABEL_COLOR{0.55f, 0.56f, 0.59f, 1.0f};
constexpr ImVec4 VALUE_COLOR{0.46f, 0.84f, 0.77f, 1.0f};
constexpr SDL_Color DIVIDER_COLOR{61, 64, 74, 255};
constexpr float  PALETTE_SWATCH_SIZE = 20.0f;
constexpr float  PALETTE_SWATCH_SPACING = 3.0f; // tight enough for a "Pal NN" label + 16 swatches on one row
constexpr int    PALETTE_ROWS = 16;
constexpr int    PALETTE_COLS = 16;

// SNES CGRAM entries are 15-bit BGR555: bits0-4=R, bits5-9=G, bits10-14=B.
ImVec4 bgr555ToImVec4(uint16_t c) {
    const float r = static_cast<float>(c & 0x1F) / 31.0f;
    const float g = static_cast<float>((c >> 5) & 0x1F) / 31.0f;
    const float b = static_cast<float>((c >> 10) & 0x1F) / 31.0f;
    return ImVec4(r, g, b, 1.0f);
}

// Dark, rounded theme built around the same teal accent used for debug values elsewhere in the panel.
void applyModernDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding      = 6.0f;
    style.TabRounding       = 6.0f;
    style.WindowPadding     = ImVec2(14.0f, 14.0f);
    style.FramePadding      = ImVec2(10.0f, 6.0f);
    style.ItemSpacing       = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);
    style.WindowBorderSize  = 0.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.GrabMinSize       = 10.0f;
    style.ScrollbarSize     = 14.0f;

    const ImVec4 accent      (0.31f, 0.78f, 0.72f, 1.00f);
    const ImVec4 accentHover (0.38f, 0.85f, 0.79f, 1.00f);
    const ImVec4 accentActive(0.26f, 0.68f, 0.63f, 1.00f);
    const ImVec4 bg          (0.10f, 0.11f, 0.13f, 1.00f);
    const ImVec4 bgLight     (0.14f, 0.15f, 0.18f, 1.00f);
    const ImVec4 bgLighter   (0.19f, 0.20f, 0.24f, 1.00f);
    const ImVec4 text        (0.90f, 0.91f, 0.92f, 1.00f);
    const ImVec4 textMuted   (0.55f, 0.56f, 0.60f, 1.00f);
    const ImVec4 border      (0.24f, 0.25f, 0.29f, 0.60f);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                 = text;
    c[ImGuiCol_TextDisabled]         = textMuted;
    c[ImGuiCol_WindowBg]             = bg;
    c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]              = bgLight;
    c[ImGuiCol_Border]               = border;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = bgLighter;
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.23f, 0.28f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.26f, 0.27f, 0.32f, 1.00f);
    c[ImGuiCol_TitleBg]              = bg;
    c[ImGuiCol_TitleBgActive]        = bg;
    c[ImGuiCol_TitleBgCollapsed]     = bg;
    c[ImGuiCol_MenuBarBg]            = bgLight;
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = bgLighter;
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.26f, 0.27f, 0.32f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = accent;
    c[ImGuiCol_CheckMark]            = accent;
    c[ImGuiCol_SliderGrab]           = accent;
    c[ImGuiCol_SliderGrabActive]     = accentActive;
    c[ImGuiCol_Button]               = bgLighter;
    c[ImGuiCol_ButtonHovered]        = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_ButtonActive]         = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    c[ImGuiCol_Header]               = ImVec4(accent.x, accent.y, accent.z, 0.20f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_HeaderActive]         = ImVec4(accent.x, accent.y, accent.z, 0.50f);
    c[ImGuiCol_Separator]            = border;
    c[ImGuiCol_SeparatorHovered]     = accentHover;
    c[ImGuiCol_SeparatorActive]      = accent;
    c[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered]    = accentHover;
    c[ImGuiCol_ResizeGripActive]     = accent;
    c[ImGuiCol_Tab]                  = bgLight;
    c[ImGuiCol_TabHovered]           = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_TabSelected]          = bgLighter;
    c[ImGuiCol_TableHeaderBg]        = bgLight;
    c[ImGuiCol_TableBorderStrong]    = border;
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.20f, 0.21f, 0.25f, 0.50f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(1, 1, 1, 0.03f);
    c[ImGuiCol_TextSelectedBg]       = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_DragDropTarget]       = accent;
}

// Decorative pillarbox/letterbox fill for fullscreen mode, in place of plain black bars.
constexpr int CHECKER_CELL_SIZE = 28;
constexpr SDL_Color CHECKER_DARK{38, 39, 44, 255};
constexpr SDL_Color CHECKER_LIGHT{56, 58, 65, 255};

void drawCheckerRect(SDL_Renderer* renderer, const SDL_Rect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    for (int y = area.y; y < area.y + area.h; y += CHECKER_CELL_SIZE) {
        for (int x = area.x; x < area.x + area.w; x += CHECKER_CELL_SIZE) {
            const bool dark = ((x / CHECKER_CELL_SIZE) + (y / CHECKER_CELL_SIZE)) % 2 == 0;
            const SDL_Color& c = dark ? CHECKER_DARK : CHECKER_LIGHT;
            SDL_Rect cell{x, y,
                          std::min(CHECKER_CELL_SIZE, area.x + area.w - x),
                          std::min(CHECKER_CELL_SIZE, area.y + area.h - y)};
            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(renderer, &cell);
        }
    }
}
}

Display::Display(const std::string& title, bool debugUi)
    : m_state(debugUi ? EmulatorState::EmulatorStateDebug : EmulatorState::EmulatorStateNormal) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    if (m_state == EmulatorState::EmulatorStateDebug) {
        m_windowWidth = TEXT_PANEL_X + PANEL_WIDTH;
        m_windowHeight = WINDOW_HEIGHT + BOTTOM_PANEL_HEIGHT;
    } else {
        // Bare mode: window is exactly the scaled game frame, nothing else.
        m_windowWidth = GAME_DST_W;
        m_windowHeight = GAME_DST_H;
    }

    const bool isNormal = m_state == EmulatorState::EmulatorStateNormal;
    const Uint32 windowFlags = SDL_WINDOW_SHOWN | (isNormal ? SDL_WINDOW_RESIZABLE : 0);
    m_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                m_windowWidth, m_windowHeight, windowFlags);
    if (!m_window) {
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }
    if (isNormal) {
        // Never shrink below the native SNES framebuffer size.
        SDL_SetWindowMinimumSize(m_window, 256, 224);
    }
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE); // drag-and-drop / Finder "open with" a .sfc
    m_renderer = SDL_CreateRenderer(m_window, -1,
                                    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // no persisted UI layout needed
    ImGui::StyleColorsDark();
    applyModernDarkTheme();
    ImGui_ImplSDL2_InitForSDLRenderer(m_window, m_renderer);
    ImGui_ImplSDLRenderer2_Init(m_renderer);

    // On macOS, dragging a window edge runs Cocoa's own live-resize tracking loop; SDL_PollEvent
    // (called from processEvents()) doesn't return to the ordinary main loop until the mouse
    // button is released, so without this the game view freezes mid-drag. SDL_AddEventWatch's
    // callback, unlike SDL_PollEvent, fires synchronously from inside that nested loop too.
    SDL_AddEventWatch(&Display::sdlEventWatch, this);
}

Display::~Display() {
    SDL_DelEventWatch(&Display::sdlEventWatch, this);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (m_previewAudioDevice != 0) SDL_CloseAudioDevice(m_previewAudioDevice);
    if (m_frameTex)      SDL_DestroyTexture(m_frameTex);
    if (m_tileSheetTex)  SDL_DestroyTexture(m_tileSheetTex);
    if (m_gsuRamTex)     SDL_DestroyTexture(m_gsuRamTex);
    if (m_renderer)  SDL_DestroyRenderer(m_renderer);
    if (m_window)    SDL_DestroyWindow(m_window);
    SDL_Quit();
}

void Display::playBrrPreview(const std::vector<int16_t>& pcm, int sampleRateHz) {
    if (pcm.empty() || sampleRateHz <= 0) return;

    if (!m_previewAudioSubsystemInit) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
        m_previewAudioSubsystemInit = true;
    }

    if (m_previewAudioDevice == 0 || m_previewAudioDeviceRate != sampleRateHz) {
        if (m_previewAudioDevice != 0) {
            SDL_CloseAudioDevice(m_previewAudioDevice);
            m_previewAudioDevice = 0;
        }
        SDL_AudioSpec want{};
        want.freq = sampleRateHz;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 1024;
        SDL_AudioSpec have{};
        // allowed_changes=0: SDL guarantees `have` matches `want` exactly, converting
        // internally if the driver needs a different native format.
        m_previewAudioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (m_previewAudioDevice == 0) return;
        m_previewAudioDeviceRate = sampleRateHz;
    }

    SDL_ClearQueuedAudio(m_previewAudioDevice);
    SDL_QueueAudio(m_previewAudioDevice, pcm.data(), static_cast<uint32_t>(pcm.size() * sizeof(int16_t)));
    SDL_PauseAudioDevice(m_previewAudioDevice, 0);
}

int Display::sdlEventWatch(void* userdata, SDL_Event* event) {
    auto* self = static_cast<Display*>(userdata);
    if (event->type == SDL_WINDOWEVENT &&
        (event->window.event == SDL_WINDOWEVENT_RESIZED || event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) &&
        event->window.windowID == SDL_GetWindowID(self->m_window)) {
        self->redrawDuringResize();
    }
    return 0;
}

void Display::redrawDuringResize() {
    // Only the bare game-only window benefits: the debug UI's panels/toolbar are ImGui widgets
    // laid out relative to fixed panel constants, not something safe to redraw mid-drag outside
    // the normal ImGui NewFrame/Render pairing. m_frameTex already holds the last frame this
    // renderer uploaded (presentWithFrame keeps it alive across calls) — just rescale/re-blit it,
    // no ImGui or CPU/PPU involvement needed.
    if (m_state != EmulatorState::EmulatorStateNormal || m_fullscreen || !m_frameTex || !m_hasFrameContent) {
        return;
    }

    SDL_GetWindowSize(m_window, &m_windowWidth, &m_windowHeight);

    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);

    const float scale = std::min(static_cast<float>(m_windowWidth) / 256.0f,
                                  static_cast<float>(m_windowHeight) / 224.0f);
    SDL_Rect dst;
    dst.w = static_cast<int>(256.0f * scale + 0.5f);
    dst.h = static_cast<int>(224.0f * scale + 0.5f);
    dst.x = (m_windowWidth - dst.w) / 2;
    dst.y = (m_windowHeight - dst.h) / 2;
    SDL_RenderCopy(m_renderer, m_frameTex, nullptr, &dst);

    SDL_RenderPresent(m_renderer);
}

bool Display::processEvents(DebugAction& action) {
    action = DebugAction::None;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT) return false;
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0 && !ImGui::GetIO().WantCaptureKeyboard) {
            switch (event.key.keysym.sym) {
                case SDLK_SPACE:
                    action = DebugAction::StepOne;
                    break;
                case SDLK_ESCAPE:
                    if (m_state == EmulatorState::EmulatorStateNormal) {
                        // Normal mode: Escape exits fullscreen instead of pausing — there's no
                        // toolbar/pause indicator in the bare window for this to make sense against.
                        if (m_fullscreen) {
                            m_fullscreen = false;
                            SDL_SetWindowFullscreen(m_window, 0);
                        }
                    } else {
                        action = DebugAction::TogglePause;
                    }
                    break;
                case SDLK_F11:
                    m_fullscreen = !m_fullscreen;
                    SDL_SetWindowFullscreen(m_window, m_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    break;
            }
        }
        // Covers both a live drag-and-drop onto the window and macOS's "open with" /
        // double-click-a-.sfc-in-Finder launch (SDL's Cocoa backend turns the latter into
        // this same event once SDL_Init has run, queued if it arrives before that).
        if (event.type == SDL_DROPFILE) {
            m_pendingRomLoadPath = event.drop.file;
            SDL_free(event.drop.file);
            action = DebugAction::LoadRom;
        }
    }
    return true;
}

bool Display::wantsKeyboardCapture() const {
    return ImGui::GetIO().WantCaptureKeyboard;
}

void Display::beginFrame() {
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

namespace {
constexpr ImGuiWindowFlags kPanelWindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
    | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

// Renders a section as a titled two-column (label/value) table.
void drawSectionTable(const DebugSection& section) {
    ImGui::SeparatorText(section.title.c_str());
    if (ImGui::BeginTable(section.title.c_str(), 2, ImGuiTableFlags_SizingFixedFit)) {
        for (const auto& line : section.lines) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const auto sep = line.find(" : ");
            if (sep != std::string::npos) {
                std::string label = line.substr(0, sep);
                while (!label.empty() && label.back() == ' ') label.pop_back();
                ImGui::TextUnformatted(label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(VALUE_COLOR, "%s", line.substr(sep + 3).c_str());
            } else {
                ImGui::TextUnformatted(line.c_str());
            }
        }
        ImGui::EndTable();
    }
}

// One row of the GSU register table: Address / Name / Value / Value (Hex). `hex` is left blank
// for plain boolean fields, matching a real register-map table where only numeric/enum fields
// carry a hex column.
void gsuRegRow(const char* address, const char* name, const std::string& value, const std::string& hex = "") {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(address);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(name);
    ImGui::TableSetColumnIndex(2);
    ImGui::TextColored(VALUE_COLOR, "%s", value.c_str());
    ImGui::TableSetColumnIndex(3);
    if (!hex.empty()) {
        ImGui::TextColored(VALUE_COLOR, "%s", hex.c_str());
    }
}

std::string gsuBoolStr(bool value) { return value ? "true" : "false"; }

std::string gsuHex8(uint8_t value) {
    char buf[3];
    std::snprintf(buf, sizeof(buf), "%02X", value);
    return buf;
}

const char* gsuColorGradientStr(uint8_t md) {
    switch (md) {
    case 0: return "2 BPP";
    case 1: return "4 BPP";
    case 3: return "8 BPP";
    default: return "reserved";
    }
}
}

DebugAction Display::drawControls(bool paused) {
    DebugAction action = DebugAction::None;
    m_pendingRomLoadPath.clear();
    if (m_state == EmulatorState::EmulatorStateNormal || m_fullscreen) return action; // bare/fullscreen window: no toolbar, no Load button

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(LEFT_PANEL_WIDTH), static_cast<float>(WINDOW_HEIGHT)));
    ImGui::Begin("Left", nullptr, kPanelWindowFlags);

    if (ImGui::Button("Load")) {
        // Blocking native call (NSOpenPanel on macOS) — the SDL/ImGui loop simply
        // pauses on this frame until the user picks a file or cancels, same as any
        // other native modal file dialog.
        const std::optional<std::string> path = showOpenRomDialog();
        if (path) {
            m_pendingRomLoadPath = *path;
            action = DebugAction::LoadRom;
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("Reset")) {
        action = DebugAction::Reset;
    }
    if (ImGui::Button(paused ? "Resume" : "Pause")) {
        action = DebugAction::TogglePause;
    }
    if (paused) {
        ImGui::SameLine();
        if (ImGui::Button("Step")) {
            action = DebugAction::StepOne;
        }
        ImGui::SameLine();
        if (ImGui::Button("Next Frame")) {
            action = DebugAction::NextFrame;
        }
    }

    ImGui::End();
    return action;
}

void Display::drawLeftPanel(const std::vector<DebugSection>& sections, const std::vector<std::string>& instructionLog) {
    ImGui::Begin("Left", nullptr, kPanelWindowFlags); // appends to the window opened by drawControls

    // Scrolls independently of the toolbar drawn by drawControls(), so the buttons stay pinned.
    ImGui::BeginChild("LeftScrollRegion", ImGui::GetContentRegionAvail(), false);

    for (const auto& section : sections) {
        drawSectionTable(section);
    }

    if (!instructionLog.empty()) {
        ImGui::SeparatorText("Instruction Log");
        for (const auto& line : instructionLog) {
            ImGui::TextUnformatted(line.c_str());
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

void Display::drawRightPanel(const DebugPanel& panel) {
    m_pendingPaletteEdit = PaletteEdit{};
    m_apuStepRequested = false;
    m_apuNextFrameRequested = false;

    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(TEXT_PANEL_X), 0.0f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(PANEL_WIDTH), static_cast<float>(WINDOW_HEIGHT)));
    ImGui::Begin("Right", nullptr, kPanelWindowFlags);

    if (panel.showPalette) {
        bool openPaletteEditor = false;
        ImGui::SeparatorText("Palette");
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(PALETTE_SWATCH_SPACING, PALETTE_SWATCH_SPACING));
        for (int row = 0; row < PALETTE_ROWS; ++row) {
            ImGui::PushID(row);
            ImGui::Text("Pal %2d", row);
            ImGui::SameLine();
            for (int col = 0; col < PALETTE_COLS; ++col) {
                ImGui::PushID(col);
                const int index = row * PALETTE_COLS + col;
                const uint16_t raw = panel.palette[index];

                if (ImGui::ColorButton("##swatch", bgr555ToImVec4(raw),
                                       ImGuiColorEditFlags_AlphaOpaque,
                                       ImVec2(PALETTE_SWATCH_SIZE, PALETTE_SWATCH_SIZE))) {
                    m_editingPaletteIndex = index;
                    m_editR = raw & 0x1F;
                    m_editG = (raw >> 5) & 0x1F;
                    m_editB = (raw >> 10) & 0x1F;
                    openPaletteEditor = true;
                }
                ImGui::PopID();
                if (col != PALETTE_COLS - 1) ImGui::SameLine();
            }
            ImGui::PopID();
        }
        ImGui::PopStyleVar();

        // OpenPopup/BeginPopup must resolve to the same ID; deferred here so it's outside the
        // per-swatch PushID(row)/PushID(col) scope the click was detected in.
        if (openPaletteEditor) {
            ImGui::OpenPopup("EditPaletteColor");
        }
        if (ImGui::BeginPopup("EditPaletteColor")) {
            ImGui::Text("Edit palette color #%d (RGB5, 0-31)", m_editingPaletteIndex);
            ImGui::InputInt("R", &m_editR);
            ImGui::InputInt("G", &m_editG);
            ImGui::InputInt("B", &m_editB);
            m_editR = std::clamp(m_editR, 0, 31);
            m_editG = std::clamp(m_editG, 0, 31);
            m_editB = std::clamp(m_editB, 0, 31);
            if (ImGui::Button("Copy")) {
                char clipboardText[16];
                std::snprintf(clipboardText, sizeof(clipboardText), "%d, %d, %d", m_editR, m_editG, m_editB);
                ImGui::SetClipboardText(clipboardText);
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply")) {
                const uint16_t r5 = static_cast<uint16_t>(m_editR);
                const uint16_t g5 = static_cast<uint16_t>(m_editG);
                const uint16_t b5 = static_cast<uint16_t>(m_editB);
                const uint16_t newRaw = static_cast<uint16_t>(r5 | (g5 << 5) | (b5 << 10));
                m_pendingPaletteEdit = PaletteEdit{true, m_editingPaletteIndex, newRaw};
                m_editingPaletteIndex = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::SeparatorText("S-DSP Voices");
    if (ImGui::Button(m_apuPaused ? "Resume" : "Pause")) {
        m_apuPaused = !m_apuPaused;
    }
    if (m_apuPaused) {
        ImGui::SameLine();
        if (ImGui::Button("Step")) {
            m_apuStepRequested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Next Frame")) {
            m_apuNextFrameRequested = true;
        }
    }
    ImGui::Text("DIR:$%02X (table @ $%04X)   KON:$%02X  ENDX:$%02X",
                panel.dspDir, static_cast<unsigned>(panel.dspDir) << 8, panel.dspKon, panel.dspEndx);
    if (ImGui::BeginTable("DspVoices", 11, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Voice");
        ImGui::TableSetupColumn("SRCN");
        ImGui::TableSetupColumn("Load Addr");
        ImGui::TableSetupColumn("Loop Addr");
        ImGui::TableSetupColumn("ARAM Addr");
        ImGui::TableSetupColumn("Playing");
        ImGui::TableSetupColumn("Pitch");
        ImGui::TableSetupColumn("Vol L");
        ImGui::TableSetupColumn("Vol R");
        ImGui::TableSetupColumn("ENVX");
        ImGui::TableSetupColumn("OUTX");
        ImGui::TableHeadersRow();
        for (int v = 0; v < 8; ++v) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", v);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("$%02X", panel.dspSrcn[v]);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("$%04X", panel.dspLoadAddr[v]);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("$%04X", panel.dspLoopAddr[v]);
            ImGui::TableSetColumnIndex(4);
            if (panel.dspActive[v]) {
                ImGui::TextColored(VALUE_COLOR, "$%04X", panel.dspBrrAddr[v]);
            } else {
                ImGui::TextDisabled("--");
            }
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(panel.dspActive[v] ? "yes" : "no");
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("$%04X", panel.dspPitch[v]);
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%d", panel.dspVoll[v]);
            ImGui::TableSetColumnIndex(8);
            ImGui::Text("%d", panel.dspVolr[v]);
            ImGui::TableSetColumnIndex(9);
            ImGui::Text("$%02X", panel.dspEnvx[v]);
            ImGui::TableSetColumnIndex(10);
            ImGui::Text("$%02X", panel.dspOutx[v]);
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Sample Directory (DIR table)");
    // Hardware has no length field for this table — every SRCN 0-255 is a valid read, most
    // just never get referenced by the game. Unlike the voice table above (only the 8 live
    // voices), this walks the whole 256-entry table directly from ARAM so a sample can be
    // located/checked even while nothing is currently playing it.
    constexpr float kDirTableChildHeight = 200.0f;
    ImGui::BeginChild("DirTableScroll", ImVec2(0.0f, kDirTableChildHeight), false);
    ImGuiListClipper dirClipper;
    dirClipper.Begin(256);
    while (dirClipper.Step()) {
        for (int srcn = dirClipper.DisplayStart; srcn < dirClipper.DisplayEnd; ++srcn) {
            // Wrapped to 16 bits like real hardware — DIR=$FF combined with a high SRCN
            // would otherwise index panel.apuRam past its 64 KiB bound (undefined behavior).
            const uint16_t entry = static_cast<uint16_t>((static_cast<uint32_t>(panel.dspDir) << 8) + static_cast<uint32_t>(srcn) * 4);
            const uint16_t startAddr = static_cast<uint16_t>(panel.apuRam[entry] | (panel.apuRam[static_cast<uint16_t>(entry + 1)] << 8));
            const uint16_t loopAddr = static_cast<uint16_t>(panel.apuRam[static_cast<uint16_t>(entry + 2)] | (panel.apuRam[static_cast<uint16_t>(entry + 3)] << 8));

            // For each active voice currently playing this SRCN, its *live* ARAM address —
            // the block actually being decoded right now, which drifts away from `startAddr`
            // as playback advances (and can differ between voices sharing the same SRCN if
            // they triggered at different times / are at different points in the sample).
            char voiceTag[96] = "";
            int voiceTagPos = 0;
            bool rowHasVoice = false;
            for (int v = 0; v < 8; ++v) {
                if (panel.dspActive[v] && panel.dspSrcn[v] == srcn) {
                    rowHasVoice = true;
                    voiceTagPos += std::snprintf(voiceTag + voiceTagPos, sizeof(voiceTag) - static_cast<size_t>(voiceTagPos),
                                                  "%sV%d=$%04X", voiceTagPos > 0 ? "," : " <- ", v, panel.dspBrrAddr[v]);
                }
            }

            char line[64];
            std::snprintf(line, sizeof(line), "SRCN $%02X: start=$%04X loop=$%04X", srcn, startAddr, loopAddr);
            if (rowHasVoice) {
                ImGui::TextColored(VALUE_COLOR, "%s%s", line, voiceTag);
            } else {
                ImGui::TextUnformatted(line);
            }

            // First active voice currently playing this SRCN's real pitch, if any — that's
            // what it's actually being heard at right now — else `fallback`.
            const auto activeVoicePitch = [&](uint16_t fallback) {
                for (int v = 0; v < 8; ++v) {
                    if (panel.dspActive[v] && panel.dspSrcn[v] == srcn) return panel.dspPitch[v];
                }
                return fallback;
            };

            ImGui::SameLine();
            ImGui::PushID(srcn);
            const bool playClicked = ImGui::SmallButton("Play");
            ImGui::SameLine();
            const bool saveClicked = ImGui::SmallButton("Save WAV");
            ImGui::PopID();

            if (playClicked) {
                // No text-entry step for Play — fall back to native rate rather than Save's
                // deliberate $0 default, since a one-shot preview should actually be audible.
                const uint16_t pitch = activeVoicePitch(0x1000);
                const std::vector<int16_t> pcm = decodeBrrSampleForExport(panel.apuRam, startAddr);
                const int sampleRate = static_cast<int>(kBrrSampleRateHz * static_cast<double>(pitch) / 4096.0 + 0.5);
                playBrrPreview(pcm, sampleRate);
            }
            if (saveClicked) {
                m_pitchPromptSrcn = srcn;
                m_pitchPromptStartAddr = startAddr;
                std::snprintf(m_pitchPromptBuffer, sizeof(m_pitchPromptBuffer), "%04X", activeVoicePitch(0));
                m_pitchPromptOpenRequested = true;
            }
        }
    }
    dirClipper.End();
    ImGui::EndChild();

    // Deferred from the button click above: OpenPopup must run at the same ID-stack level as
    // BeginPopupModal below (outside DirTableScroll), not from inside the child the row lives in.
    if (m_pitchPromptOpenRequested) {
        ImGui::OpenPopup("Sample Pitch");
        m_pitchPromptOpenRequested = false;
    }
    if (ImGui::BeginPopupModal("Sample Pitch", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("SRCN $%02X", m_pitchPromptSrcn);
        ImGui::TextUnformatted("Pitch (hex, VxPITCH 14-bit; $1000 = native 32kHz rate):");
        ImGui::InputText("##Pitch", m_pitchPromptBuffer, sizeof(m_pitchPromptBuffer),
                          ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);

        unsigned pitchValue = 0;
        std::sscanf(m_pitchPromptBuffer, "%x", &pitchValue);
        if (pitchValue == 0) pitchValue = 1;
        const int sampleRate = static_cast<int>(kBrrSampleRateHz * pitchValue / 4096.0 + 0.5);
        ImGui::Text("-> %d Hz sample rate", sampleRate);

        if (ImGui::Button("Save WAV...")) {
            char suggestedName[32];
            std::snprintf(suggestedName, sizeof(suggestedName), "srcn_%02X.wav", m_pitchPromptSrcn);
            const std::optional<std::string> path = showSaveSampleDialog(suggestedName);
            if (path) {
                const std::vector<int16_t> pcm = decodeBrrSampleForExport(panel.apuRam, m_pitchPromptStartAddr);
                writeWavFile(*path, pcm, sampleRate);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SeparatorText("APU RAM (ARAM)");
    // Fixed height rather than GetContentRegionAvail(): by this point Palette + S-DSP Voices
    // above have already consumed more than the "Right" window's own declared height, so the
    // avail-space calc would clamp to ~0 — the outer window itself scrolls (kPanelWindowFlags
    // doesn't set NoScrollbar) to reach this section, and this child then scrolls independently
    // within its own fixed height for the 4096 hex-dump rows.
    constexpr float kApuRamChildHeight = 320.0f;
    ImGui::BeginChild("ApuRamScroll", ImVec2(0.0f, kApuRamChildHeight), false);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(panel.apuRam.size() / 16));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const int base = row * 16;

            // Which active voices' BRR sample starts inside this row, if any (drives the
            // highlight below) — direct answer to "which sample plays on which channel".
            char voiceTag[32] = "";
            int voiceTagPos = 0;
            bool rowHasVoice = false;
            for (int v = 0; v < 8; ++v) {
                if (panel.dspActive[v] && panel.dspBrrAddr[v] >= base && panel.dspBrrAddr[v] < base + 16) {
                    rowHasVoice = true;
                    voiceTagPos += std::snprintf(voiceTag + voiceTagPos, sizeof(voiceTag) - static_cast<size_t>(voiceTagPos),
                                                  "%sV%d", voiceTagPos > 0 ? "," : " <- ", v);
                }
            }

            char line[96];
            int pos = std::snprintf(line, sizeof(line), "%04X: ", base);
            for (int i = 0; i < 16; ++i) {
                pos += std::snprintf(line + pos, sizeof(line) - pos, "%02X ", panel.apuRam[base + i]);
            }
            pos += std::snprintf(line + pos, sizeof(line) - pos, " ");
            for (int i = 0; i < 16 && pos < static_cast<int>(sizeof(line)) - 1; ++i) {
                const uint8_t b = panel.apuRam[base + i];
                line[pos++] = (b >= 32 && b < 127) ? static_cast<char>(b) : '.';
            }
            line[pos] = '\0';

            if (rowHasVoice) {
                ImGui::TextColored(VALUE_COLOR, "%s%s", line, voiceTag);
            } else {
                ImGui::TextUnformatted(line);
            }
        }
    }
    clipper.End();
    ImGui::EndChild();

    ImGui::End();
}

namespace {
constexpr ImGuiWindowFlags kBottomPanelFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
    | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
}

void Display::drawBottomPanel(const DebugPanel& panel) {
    // Same width as the Left panel above it, so the child's own scrollbar (which ImGui
    // always draws at the child's right edge) lands right next to the vertical separator
    // instead of at the far right of the whole application window.
    ImGui::SetNextWindowPos(ImVec2(0.0f, static_cast<float>(WINDOW_HEIGHT)));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(LEFT_PANEL_WIDTH), static_cast<float>(BOTTOM_PANEL_HEIGHT)));
    ImGui::Begin("Tiles Viewer", nullptr, kBottomPanelFlags);

    ImGui::SeparatorText("Layers");

    // Layer visibility toggles: click to hide/show that layer in the emulated game view.
    // Highlighted red while hidden. This only affects the debug display (Ppu's
    // m_debugLayerDisable override) — it never touches the actual TM/TS register values,
    // so the game itself still sees whatever it wrote there.
    static constexpr const char* kLayerLabels[5] = {"BG0", "BG1", "BG2", "BG3", "OAM"};
    for (int i = 0; i < 5; ++i) {
        const uint8_t bit = static_cast<uint8_t>(1u << i);
        const bool disabled = (m_layerDisableMask & bit) != 0;
        ImGui::PushID(i);
        if (disabled) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.32f, 0.32f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f, 0.20f, 0.20f, 1.0f));
        }
        if (ImGui::Button(kLayerLabels[i])) {
            m_layerDisableMask ^= bit;
        }
        if (disabled) {
            ImGui::PopStyleColor(3);
        }
        ImGui::PopID();
        if (i != 4) ImGui::SameLine();
    }

    ImGui::BeginChild("TilesScrollRegion", ImGui::GetContentRegionAvail(), false);

    if (panel.showTiles) {
        ImGui::SeparatorText("Tiles Viewer");

        if (!m_tileSheetTex) {
            m_tileSheetTex = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ARGB8888,
                                               SDL_TEXTUREACCESS_STREAMING, kTileSheetW, kTileSheetH);
            if (m_tileSheetTex) {
                SDL_SetTextureBlendMode(m_tileSheetTex, SDL_BLENDMODE_NONE);
                SDL_SetTextureScaleMode(m_tileSheetTex, SDL_ScaleModeNearest); // keep tile edges crisp when upscaled
            }
        }
        if (m_tileSheetTex) {
            SDL_UpdateTexture(m_tileSheetTex, nullptr, panel.tileSheetArgb.data(),
                              kTileSheetW * static_cast<int>(sizeof(uint32_t)));
            constexpr float kScale = 2.0f;
            ImGui::Image(m_tileSheetTex, ImVec2(kTileSheetW * kScale, kTileSheetH * kScale));

            // Grid overlay separating each 8x8 tile, drawn on top so it never alters the decoded pixels.
            const ImVec2 imgMin = ImGui::GetItemRectMin();
            const ImVec2 imgMax = ImGui::GetItemRectMax();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            constexpr ImU32 kGridColor = IM_COL32(180, 180, 190, 130);
            for (int col = 0; col <= kTileSheetCols; ++col) {
                const float x = imgMin.x + static_cast<float>(col * 8) * kScale;
                drawList->AddLine(ImVec2(x, imgMin.y), ImVec2(x, imgMax.y), kGridColor);
            }
            for (int row = 0; row <= kTileSheetRows; ++row) {
                const float y = imgMin.y + static_cast<float>(row * 8) * kScale;
                drawList->AddLine(ImVec2(imgMin.x, y), ImVec2(imgMax.x, y), kGridColor);
            }

            if (ImGui::IsItemHovered()) {
                const ImVec2 mousePos = ImGui::GetMousePos();
                const int col = std::clamp(static_cast<int>((mousePos.x - imgMin.x) / (8.0f * kScale)), 0, kTileSheetCols - 1);
                const int row = std::clamp(static_cast<int>((mousePos.y - imgMin.y) / (8.0f * kScale)), 0, kTileSheetRows - 1);
                const int tileIndex = row * kTileSheetCols + col;
                const int wordAddr = tileIndex * 16;
                ImGui::SetTooltip("Tile #%d\nVRAM $%04X (word)  $%04X (byte)", tileIndex, wordAddr, wordAddr * 2);
            }
        }
    }

    if (panel.hasGsu) {
        ImGui::SeparatorText("GSU RAM Viewer");

        if (!m_gsuRamTex) {
            m_gsuRamTex = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_STREAMING, kGsuRamMaxW, kGsuRamMaxH);
            if (m_gsuRamTex) {
                SDL_SetTextureBlendMode(m_gsuRamTex, SDL_BLENDMODE_NONE);
                SDL_SetTextureScaleMode(m_gsuRamTex, SDL_ScaleModeNearest);
            }
        }
        if (m_gsuRamTex) {
            SDL_UpdateTexture(m_gsuRamTex, nullptr, panel.gsuRamArgb.data(),
                              kGsuRamMaxW * static_cast<int>(sizeof(uint32_t)));

            const int w = panel.gsuRamWidthPx > 0 ? panel.gsuRamWidthPx : kGsuRamMaxW;
            const int h = panel.gsuRamHeightPx > 0 ? panel.gsuRamHeightPx : kGsuRamMaxH;
            constexpr float kGsuScale = 2.0f;
            const ImVec2 uv1(static_cast<float>(w) / kGsuRamMaxW, static_cast<float>(h) / kGsuRamMaxH);
            ImGui::Image(m_gsuRamTex, ImVec2(w * kGsuScale, h * kGsuScale), ImVec2(0.0f, 0.0f), uv1);

            if (ImGui::IsItemHovered()) {
                const ImVec2 imgMin = ImGui::GetItemRectMin();
                const ImVec2 mousePos = ImGui::GetMousePos();
                const int px = std::clamp(static_cast<int>((mousePos.x - imgMin.x) / kGsuScale), 0, w - 1);
                const int py = std::clamp(static_cast<int>((mousePos.y - imgMin.y) / kGsuScale), 0, h - 1);
                ImGui::SetTooltip("Pixel (%d, %d)\nSCBR:$%02X RAMBR:%d  %dbpp  %dx%d",
                                  px, py, panel.gsuScbr, panel.gsuRambr ? 1 : 0, panel.gsuRamBpp, w, h);
            }
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

// Small strip directly under the emulated game framebuffer (same x-span, in the otherwise-empty
// area below it since the Tiles Viewer/right panel columns don't extend under the game view).
void Display::drawGameInfoPanel(const DebugPanel& panel) {
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(GAME_DST_X), static_cast<float>(WINDOW_HEIGHT)));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(GAME_DST_W), static_cast<float>(BOTTOM_PANEL_HEIGHT)));
    ImGui::Begin("Game Info", nullptr, kBottomPanelFlags);

    ImGui::SeparatorText("BG Mode");
    ImGui::Text("Mode: %d", panel.bgMode);

    ImGui::SeparatorText("BG Tilemaps");
    ImGui::Text("BG1: $%04X  BG2: $%04X  BG3: $%04X  BG4: $%04X",
                 panel.bgTilemapBase[0], panel.bgTilemapBase[1],
                 panel.bgTilemapBase[2], panel.bgTilemapBase[3]);

    ImGui::SeparatorText("BG Tilesets");
    ImGui::Text("BG1: $%04X  BG2: $%04X  BG3: $%04X  BG4: $%04X",
                 panel.bgChrBase[0], panel.bgChrBase[1],
                 panel.bgChrBase[2], panel.bgChrBase[3]);

    ImGui::SeparatorText("Joypad");
    ImGui::Text("P1: $%04X   P2: $%04X", panel.joy1, panel.joy2);

    ImGui::End();
}

void Display::drawGsuDebugPanel(const DebugPanel& panel) {
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(TEXT_PANEL_X), static_cast<float>(WINDOW_HEIGHT)));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(PANEL_WIDTH), static_cast<float>(BOTTOM_PANEL_HEIGHT)));
    ImGui::Begin("GSU Debugger", nullptr, kBottomPanelFlags);

    if (!panel.hasGsu) {
        ImGui::TextDisabled("(no SuperFX chip in this ROM)");
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("GSU Debugger");
    ImGui::Text("State: %s   Launches: %u  Stops: %u  Plots: %llu",
                panel.gsuRunning ? "RUNNING" : "stopped",
                panel.gsuLaunches, panel.gsuStops,
                static_cast<unsigned long long>(panel.gsuPlotCount));
    ImGui::Text("PC: $%02X:%04X    SCBR:$%02X SCMR:$%02X ROMBR:$%02X RAMBR:%d",
                panel.gsuPbr, panel.gsuPcAddr, panel.gsuScbr, panel.gsuScmr, panel.gsuRombr,
                panel.gsuRambr ? 1 : 0);
    ImGui::TextColored(VALUE_COLOR, "-> %s", panel.gsuCurrentInstr.c_str());

    ImGui::SeparatorText("Register File & Status");
    if (ImGui::BeginTable("GsuFileRegs", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Value (Hex)");
        ImGui::TableHeadersRow();

        const uint8_t sfrLow = static_cast<uint8_t>(panel.gsuSfr);
        const uint8_t sfrHigh = static_cast<uint8_t>(panel.gsuSfr >> 8);
        const uint16_t r15 = panel.gsuRegs[15];
        const uint8_t r15Low = static_cast<uint8_t>(r15);
        const uint8_t r15High = static_cast<uint8_t>(r15 >> 8);

        gsuRegRow("$301E", "R15 / PC (Low Byte)", std::to_string(r15Low), "$" + gsuHex8(r15Low));
        gsuRegRow("$301F", "R15 / PC (High Byte)", std::to_string(r15High), "$" + gsuHex8(r15High));
        gsuRegRow("$3030", "SFR (Low Byte)", std::to_string(sfrLow), "$" + gsuHex8(sfrLow));
        gsuRegRow("$3031", "SFR (High Byte)", std::to_string(sfrHigh), "$" + gsuHex8(sfrHigh));
        gsuRegRow("$3033", "Backup RAM Enable (BRAMR)", gsuBoolStr(panel.gsuBramr));
        gsuRegRow("$3034", "Program Bank (PBR)", std::to_string(panel.gsuPbr),
                  "$" + gsuHex8(panel.gsuPbr));
        gsuRegRow("$3036", "ROM Bank (ROMBR)", std::to_string(panel.gsuRombr),
                  "$" + gsuHex8(panel.gsuRombr));
        gsuRegRow("$303B", "Version Code (VCR)", std::to_string(panel.gsuVcr),
                  "$" + gsuHex8(panel.gsuVcr));
        gsuRegRow("$303C", "RAM Bank (RAMBR)", gsuBoolStr(panel.gsuRambr));
        gsuRegRow("$303E", "Cache Base (Low Byte)", std::to_string(static_cast<uint8_t>(panel.gsuCbr)),
                  "$" + gsuHex8(static_cast<uint8_t>(panel.gsuCbr)));
        gsuRegRow("$303F", "Cache Base (High Byte)",
                  std::to_string(static_cast<uint8_t>(panel.gsuCbr >> 8)),
                  "$" + gsuHex8(static_cast<uint8_t>(panel.gsuCbr >> 8)));
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Configuration Registers");
    if (ImGui::BeginTable("GsuConfigRegs", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Value (Hex)");
        ImGui::TableHeadersRow();

        gsuRegRow("$3037.5", "High Speed Mode", gsuBoolStr(panel.gsuCfgrHighSpeed));
        gsuRegRow("$3037.7", "IRQ Disabled", gsuBoolStr(panel.gsuCfgrIrqDisabled));
        gsuRegRow("$3038", "Screen Base Address", std::to_string(panel.gsuScbr),
                  "$" + gsuHex8(panel.gsuScbr));
        gsuRegRow("$3039.0", "Clock Select", gsuBoolStr(panel.gsuClsr));
        gsuRegRow("$303A.0-1", "Color Gradient", gsuColorGradientStr(panel.gsuScmrMd),
                  "$" + gsuHex8(panel.gsuScmrMd));
        gsuRegRow("$303A.2+5", "Screen Height", std::to_string(panel.gsuScreenHeightPx) + " px",
                  "$" + gsuHex8(panel.gsuScmrHt));
        gsuRegRow("$303A.3", "GSU RAM Access Enabled", gsuBoolStr(panel.gsuScmrRan));
        gsuRegRow("$303A.4", "GSU ROM Access Enabled", gsuBoolStr(panel.gsuScmrRon));
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Plot Option Register (CMODE)");
    if (ImGui::BeginTable("GsuCmodeRegs", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Value (Hex)");
        ImGui::TableHeadersRow();

        gsuRegRow("", "Transparent", gsuBoolStr(panel.gsuPorTransparent));
        gsuRegRow("", "Dither", gsuBoolStr(panel.gsuPorDither));
        gsuRegRow("", "Color High Nibble", gsuBoolStr(panel.gsuPorHighNibble));
        gsuRegRow("", "Color Freeze High", gsuBoolStr(panel.gsuPorFreezeHigh));
        gsuRegRow("", "Object Mode", gsuBoolStr(panel.gsuPorObj));
        ImGui::EndTable();
    }

    {
        const uint16_t sfr = panel.gsuSfr;
        ImGui::Text("SFR: Z=%d C=%d S=%d OV=%d GO=%d ALT1=%d ALT2=%d B=%d IRQ=%d",
                    (sfr >> 1) & 1, (sfr >> 2) & 1, (sfr >> 3) & 1, (sfr >> 4) & 1,
                    (sfr >> 5) & 1, (sfr >> 8) & 1, (sfr >> 9) & 1, (sfr >> 12) & 1, (sfr >> 15) & 1);
    }

    ImGui::SeparatorText("Registers");
    for (int row = 0; row < 4; ++row) {
        ImGui::Text("r%-2d:%04X  r%-2d:%04X  r%-2d:%04X  r%-2d:%04X",
                    row * 4 + 0, panel.gsuRegs[row * 4 + 0],
                    row * 4 + 1, panel.gsuRegs[row * 4 + 1],
                    row * 4 + 2, panel.gsuRegs[row * 4 + 2],
                    row * 4 + 3, panel.gsuRegs[row * 4 + 3]);
    }

    ImGui::End();
}

PaletteEdit Display::presentWithFrame(const uint32_t* pixels, const DebugPanel& panel) {
    // Bare/fullscreen windows are resizable (or change size via SDL_SetWindowFullscreen) —
    // re-read the live size every frame rather than trusting the constructor's snapshot, so the
    // framebuffer stretch below tracks the actual window.
    if (m_state == EmulatorState::EmulatorStateNormal || m_fullscreen) {
        SDL_GetWindowSize(m_window, &m_windowWidth, &m_windowHeight);
    }

    // Create streaming texture once
    if (!m_frameTex) {
        m_frameTex = SDL_CreateTexture(m_renderer,
                                       SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       256, 224);
        if (m_frameTex) {
            SDL_SetTextureBlendMode(m_frameTex, SDL_BLENDMODE_NONE);
            // Crisp pixel-art upscaling (matches the Tiles/GSU RAM viewers) rather than the
            // blurrier default the renderer would otherwise fall back to at some scales.
            SDL_SetTextureScaleMode(m_frameTex, SDL_ScaleModeNearest);
        }
    }

    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);

    if (m_frameTex && pixels) {
        SDL_UpdateTexture(m_frameTex, nullptr, pixels, 256 * static_cast<int>(sizeof(uint32_t)));
        m_hasFrameContent = true;
        SDL_Rect dst = (m_state == EmulatorState::EmulatorStateDebug)
            ? SDL_Rect{GAME_DST_X, std::max(0, (WINDOW_HEIGHT - GAME_DST_H) / 2), GAME_DST_W, GAME_DST_H}
            : SDL_Rect{0, 0, m_windowWidth, m_windowHeight};
        if (m_fullscreen) {
            // Fullscreen: always fill the full screen height (fractional scale, not snapped to
            // an integer multiple) so there's no letterboxing above/below — only pillarbox bars
            // left/right, sized to keep the 8:7 aspect ratio undistorted.
            const float scale = static_cast<float>(m_windowHeight) / 224.0f;
            dst.h = m_windowHeight;
            dst.w = static_cast<int>(256.0f * scale + 0.5f);
            dst.x = (m_windowWidth - dst.w) / 2;
            dst.y = 0;
        } else if (m_state == EmulatorState::EmulatorStateNormal) {
            // Bare resizable window can be any size/aspect ratio — scale by the largest fractional
            // factor that fits both dimensions (never distorting the 8:7 framebuffer) so the game
            // always fills either the full width or full height, and letterbox/pillarbox only the
            // unavoidable leftover with the black already cleared above.
            const float scale = std::min(static_cast<float>(m_windowWidth) / 256.0f,
                                          static_cast<float>(m_windowHeight) / 224.0f);
            dst.w = static_cast<int>(256.0f * scale + 0.5f);
            dst.h = static_cast<int>(224.0f * scale + 0.5f);
            dst.x = (m_windowWidth - dst.w) / 2;
            dst.y = (m_windowHeight - dst.h) / 2;
        }
        SDL_RenderCopy(m_renderer, m_frameTex, nullptr, &dst);

        if (m_fullscreen && m_state == EmulatorState::EmulatorStateNormal) {
            // Decorative checker bars fill whatever pillarbox/letterbox space is left over
            // around the integer-scaled frame, instead of leaving it plain black. Only shown
            // for the non-debug (--debug-less) window — a debug session gone fullscreen keeps
            // plain black bars since the checker pattern is meant as play-mode decoration.
            drawCheckerRect(m_renderer, SDL_Rect{0, 0, dst.x, m_windowHeight});
            drawCheckerRect(m_renderer, SDL_Rect{dst.x + dst.w, 0, m_windowWidth - (dst.x + dst.w), m_windowHeight});
            drawCheckerRect(m_renderer, SDL_Rect{0, 0, m_windowWidth, dst.y});
            drawCheckerRect(m_renderer, SDL_Rect{0, dst.y + dst.h, m_windowWidth, m_windowHeight - (dst.y + dst.h)});
        }
    }

    if (m_state == EmulatorState::EmulatorStateDebug && !m_fullscreen) {
        drawLeftPanel(panel.sections, panel.instructionLog);
        drawRightPanel(panel);
        drawBottomPanel(panel);
        drawGameInfoPanel(panel);
        drawGsuDebugPanel(panel);

        // Divider lines: drawn via a plain (non-popup) ImGui window's own draw list rather
        // than a raw SDL_RenderDrawLine after ImGui::Render(). A raw SDL draw paints over
        // literally everything ImGui just queued, including any open popup (e.g. the palette
        // color editor); a regular window's draw list still composites on top of the other
        // panels' backgrounds, but ImGui always renders open popups in front of regular
        // windows, so this keeps the dividers visible above panels yet below any popup.
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(m_windowWidth), static_cast<float>(m_windowHeight)));
        ImGui::Begin("##Dividers", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
        ImDrawList* dividerDrawList = ImGui::GetWindowDrawList();
        const ImU32 dividerColor = IM_COL32(DIVIDER_COLOR.r, DIVIDER_COLOR.g, DIVIDER_COLOR.b, DIVIDER_COLOR.a);
        dividerDrawList->AddLine(ImVec2(0.0f, static_cast<float>(WINDOW_HEIGHT)),
            ImVec2(static_cast<float>(m_windowWidth), static_cast<float>(WINDOW_HEIGHT)), dividerColor);
        dividerDrawList->AddLine(ImVec2(static_cast<float>(LEFT_PANEL_WIDTH), 0.0f),
            ImVec2(static_cast<float>(LEFT_PANEL_WIDTH), static_cast<float>(m_windowHeight)), dividerColor);
        dividerDrawList->AddLine(ImVec2(static_cast<float>(TEXT_PANEL_X), 0.0f),
            ImVec2(static_cast<float>(TEXT_PANEL_X), static_cast<float>(WINDOW_HEIGHT)), dividerColor);
        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_renderer);

    SDL_RenderPresent(m_renderer);
    return m_pendingPaletteEdit;
}
