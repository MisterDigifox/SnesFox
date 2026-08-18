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
    NextFrame,
    Reset,
    LoadRom
};

// A titled group of lines rendered as a two-column (label/value) table.
// A line containing " : " is split into label/value columns; any other line
// spans the row as plain text (e.g. compact one-off debug strings).
struct DebugSection {
    std::string title;
    std::vector<std::string> lines;
};

// Full VRAM (0x8000 words) as 8x8 4bpp tiles = 2048 tiles, shown as a 16-wide sheet (128 rows).
constexpr int kTileSheetCols = 16;
constexpr int kTileSheetRows = 128;
constexpr int kTileSheetW = kTileSheetCols * 8; // 128
constexpr int kTileSheetH = kTileSheetRows * 8; // 1024

// SNES CGRAM: 16 palettes × 16 colors each (256 entries total).
struct DebugPanel {
    std::vector<DebugSection> sections; // ROM, CPU Debug, PPU State — rendered in the left-side menu
    bool showPalette = false;
    std::array<uint16_t, 256> palette{}; // raw SNES BGR555 entries, valid only if showPalette
    std::vector<std::string> instructionLog;
    bool showTiles = false;
    std::array<uint32_t, kTileSheetW * kTileSheetH> tileSheetArgb{}; // row-major, resolved via palette 0
    std::array<uint16_t, 4> bgTilemapBase{}; // VRAM word address of BG1-4's tilemap base ((BGxSC>>2)*0x400)
    std::array<uint16_t, 4> bgChrBase{};     // VRAM word address of BG1-4's tileset/CHR base (Ppu::chrBase)
    uint8_t bgMode = 0;                      // current BG mode (0-7), Ppu::bgMode()
};

// Result of clicking a palette swatch and hitting Apply in the editor popup.
struct PaletteEdit {
    bool applied = false;
    int index = 0;
    uint16_t bgr555 = 0;
};

class Display final {
public:
    explicit Display(const std::string& title);
    ~Display();
    bool processEvents(DebugAction& action);
    // Starts the ImGui frame; call once per iteration before drawControls/presentWithFrame.
    void beginFrame();
    // True while an ImGui widget (e.g. the palette editor popup's text fields) has keyboard focus —
    // the emulated joypad should ignore keyboard input for the frame while this is true.
    bool wantsKeyboardCapture() const;
    // Draws the Load/Reset/Pause/Resume/Step/Next Frame toolbar pinned at the top of the left
    // menu; returns which button (if any) was clicked. If the returned action is LoadRom, the
    // selected ROM's path (relative to the current working directory) is in pendingRomLoadPath().
    DebugAction drawControls(bool paused);
    const std::string& pendingRomLoadPath() const { return m_pendingRomLoadPath; }
    // Left menu (ROM/CPU/PPU sections + instruction log) — game frame — right menu (palette).
    // Returns the pending palette edit (if the user hit Apply in the swatch editor popup this frame).
    PaletteEdit presentWithFrame(const uint32_t* pixels, const DebugPanel& panel);
    // BG0-3/OAM visibility toggles from the Tiles Viewer panel (bit0-3 = BG0-3, bit4 = OAM) —
    // apply this to Ppu::setDebugLayerDisable each frame to hide the corresponding layer(s)
    // in the emulated game view.
    uint8_t layerDisableMask() const { return m_layerDisableMask; }
private:
    void drawLeftPanel(const std::vector<DebugSection>& sections, const std::vector<std::string>& instructionLog);
    void drawRightPanel(const DebugPanel& panel);
    void drawBottomPanel(const DebugPanel& panel);
    void drawGameInfoPanel(const DebugPanel& panel);

    SDL_Window*   m_window       = nullptr;
    SDL_Renderer* m_renderer     = nullptr;
    SDL_Texture*  m_frameTex     = nullptr; // 256×224 streaming texture
    SDL_Texture*  m_tileSheetTex = nullptr; // 128×1024 streaming texture for the Tiles Viewer (full VRAM)
    int           m_windowWidth  = 0;
    int           m_windowHeight = 0;

    int m_editingPaletteIndex = -1; // -1 when the palette editor popup is closed
    int m_editR = 0;
    int m_editG = 0;
    int m_editB = 0;
    PaletteEdit m_pendingPaletteEdit;
    std::string m_pendingRomLoadPath; // set when the Load popup's file selection is clicked
    uint8_t m_layerDisableMask = 0; // bit0-3 = BG0-3, bit4 = OAM; toggled by the Tiles Viewer buttons
};
