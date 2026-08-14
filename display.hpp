#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <SDL2/SDL.h>

enum class DebugAction {
    None,
    TogglePause,
    StepOne,
    NextFrame
};

// A titled group of lines rendered as a two-column (label/value) table.
// A line containing " : " is split into label/value columns; any other line
// spans the row as plain text (e.g. compact one-off debug strings).
struct DebugSection {
    std::string title;
    std::vector<std::string> lines;
};

// SNES CGRAM: 16 palettes × 16 colors each (256 entries total).
struct DebugPanel {
    std::vector<DebugSection> sections; // ROM, CPU Debug, PPU State — rendered in the left-side menu
    bool showPalette = false;
    std::array<uint16_t, 256> palette{}; // raw SNES BGR555 entries, valid only if showPalette
    std::vector<std::string> instructionLog;
};

class Display final {
public:
    explicit Display(const std::string& title);
    ~Display();
    bool processEvents(DebugAction& action);
    // Starts the ImGui frame; call once per iteration before drawControls/presentWithFrame.
    void beginFrame();
    // Draws the Pause/Resume/Step/Next Frame toolbar pinned at the top of the left menu; returns
    // which button (if any) was clicked.
    DebugAction drawControls(bool paused);
    // Left menu (ROM/CPU/PPU sections + instruction log) — game frame — right menu (palette).
    void presentWithFrame(const uint32_t* pixels, const DebugPanel& panel);
private:
    void drawLeftPanel(const std::vector<DebugSection>& sections, const std::vector<std::string>& instructionLog);
    void drawRightPanel(const DebugPanel& panel);

    SDL_Window*   m_window      = nullptr;
    SDL_Renderer* m_renderer    = nullptr;
    SDL_Texture*  m_frameTex    = nullptr; // 256×224 streaming texture
    int           m_windowWidth = 0;
    int           m_windowHeight = 0;
};
