#include "display.hpp"
#include "native_file_dialog.hpp"
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
constexpr int PANEL_WIDTH   = scale23(700);

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
}

Display::Display(const std::string& title) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    m_windowWidth = TEXT_PANEL_X + PANEL_WIDTH;
    m_windowHeight = WINDOW_HEIGHT + BOTTOM_PANEL_HEIGHT;

    m_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                m_windowWidth, m_windowHeight, SDL_WINDOW_SHOWN);
    if (!m_window) {
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }
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
}

Display::~Display() {
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (m_frameTex)      SDL_DestroyTexture(m_frameTex);
    if (m_tileSheetTex)  SDL_DestroyTexture(m_tileSheetTex);
    if (m_renderer)  SDL_DestroyRenderer(m_renderer);
    if (m_window)    SDL_DestroyWindow(m_window);
    SDL_Quit();
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
                    action = DebugAction::TogglePause;
                    break;
            }
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
}

DebugAction Display::drawControls(bool paused) {
    DebugAction action = DebugAction::None;
    m_pendingRomLoadPath.clear();

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

    ImGui::SeparatorText("Tiles Viewer");

    ImGui::BeginChild("TilesScrollRegion", ImGui::GetContentRegionAvail(), false);

    if (panel.showTiles) {
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
    } else {
        ImGui::TextDisabled("(coming soon)");
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

    ImGui::End();
}

PaletteEdit Display::presentWithFrame(const uint32_t* pixels, const DebugPanel& panel) {
    // Create streaming texture once
    if (!m_frameTex) {
        m_frameTex = SDL_CreateTexture(m_renderer,
                                       SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       256, 224);
        if (m_frameTex) {
            SDL_SetTextureBlendMode(m_frameTex, SDL_BLENDMODE_NONE);
        }
    }

    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);

    if (m_frameTex && pixels) {
        SDL_UpdateTexture(m_frameTex, nullptr, pixels, 256 * static_cast<int>(sizeof(uint32_t)));
        const int gameDstY = std::max(0, (WINDOW_HEIGHT - GAME_DST_H) / 2);
        const SDL_Rect dst{GAME_DST_X, gameDstY, GAME_DST_W, GAME_DST_H};
        SDL_RenderCopy(m_renderer, m_frameTex, nullptr, &dst);
    }

    drawLeftPanel(panel.sections, panel.instructionLog);
    drawRightPanel(panel);
    drawBottomPanel(panel);
    drawGameInfoPanel(panel);

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

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_renderer);

    SDL_RenderPresent(m_renderer);
    return m_pendingPaletteEdit;
}
