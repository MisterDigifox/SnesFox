#include "display.hpp"
#include <algorithm>
#include <stdexcept>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

namespace {
// Layout uses (2/3) × the original design so window dimensions are ÷1.5 (~3× framebuffer → ~2×).
constexpr int scale23(int v) { return (v * 2 + 1) / 3; }

// Baseline window height keeps scaled SNES framebuffer centred with proportional inset (~32 px).
constexpr int WINDOW_HEIGHT = scale23(768);
constexpr int LEFT_PANEL_WIDTH = scale23(440);
constexpr int PANEL_WIDTH   = scale23(700);

constexpr int GAME_SCALE  = 2;
constexpr int GAME_DST_W  = 256 * GAME_SCALE;                    // 512
constexpr int GAME_DST_H  = 224 * GAME_SCALE;                    // 448
constexpr int GAME_DST_X  = LEFT_PANEL_WIDTH;

constexpr int TEXT_PANEL_X = LEFT_PANEL_WIDTH + GAME_DST_W;

constexpr ImVec4 LABEL_COLOR{0.55f, 0.56f, 0.59f, 1.0f};
constexpr ImVec4 VALUE_COLOR{0.46f, 0.84f, 0.77f, 1.0f};
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

    style.WindowRounding    = 8.0f;
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
    m_windowHeight = WINDOW_HEIGHT;

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
    if (m_frameTex)  SDL_DestroyTexture(m_frameTex);
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
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
            switch (event.key.keysym.sym) {
                case SDLK_SPACE:
                    action = DebugAction::StepOne;
                    break;
            }
        }
    }
    return true;
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

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(LEFT_PANEL_WIDTH), static_cast<float>(m_windowHeight)));
    ImGui::Begin("Left", nullptr, kPanelWindowFlags);

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
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(TEXT_PANEL_X), 0.0f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(PANEL_WIDTH), static_cast<float>(m_windowHeight)));
    ImGui::Begin("Right", nullptr, kPanelWindowFlags);

    if (panel.showPalette) {
        ImGui::SeparatorText("Palette");
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(PALETTE_SWATCH_SPACING, PALETTE_SWATCH_SPACING));
        for (int row = 0; row < PALETTE_ROWS; ++row) {
            ImGui::PushID(row);
            ImGui::Text("Pal %2d", row);
            ImGui::SameLine();
            for (int col = 0; col < PALETTE_COLS; ++col) {
                ImGui::PushID(col);
                ImGui::ColorButton("##swatch", bgr555ToImVec4(panel.palette[row * PALETTE_COLS + col]),
                                   ImGuiColorEditFlags_AlphaOpaque,
                                   ImVec2(PALETTE_SWATCH_SIZE, PALETTE_SWATCH_SIZE));
                ImGui::PopID();
                if (col != PALETTE_COLS - 1) ImGui::SameLine();
            }
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
    }

    ImGui::End();
}

void Display::presentWithFrame(const uint32_t* pixels, const DebugPanel& panel) {
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
        const int gameDstY = std::max(0, (m_windowHeight - GAME_DST_H) / 2);
        const SDL_Rect dst{GAME_DST_X, gameDstY, GAME_DST_W, GAME_DST_H};
        SDL_RenderCopy(m_renderer, m_frameTex, nullptr, &dst);
    }

    drawLeftPanel(panel.sections, panel.instructionLog);
    drawRightPanel(panel);

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_renderer);
    SDL_RenderPresent(m_renderer);
}
