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

// Normal: bare game-only window (no toolbar/panels). Debug: full debug UI (side panels,
// toolbar, Load button). Distinct keyboard behavior (e.g. Escape) depends on this.
enum class EmulatorState {
    EmulatorStateNormal,
    EmulatorStateDebug
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

// GSU bitplane framebuffer as decoded from RAM ($70/$71): largest possible screen is
// 256x256 (SCMR HT=3 / OBJ mode); smaller modes (128x128/160/192) are decoded into the
// top-left corner of this same buffer and cropped for display.
constexpr int kGsuRamMaxW = 256;
constexpr int kGsuRamMaxH = 256;

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
    uint16_t joy1 = 0;                       // last auto-joypad-read result for player 1 ($4218/$4219)
    uint16_t joy2 = 0;                       // last auto-joypad-read result for player 2 ($421A/$421B)

    std::array<uint8_t, 65536> apuRam{}; // full 64 KiB ARAM snapshot, shown as a hex dump

    // S-DSP voices: which BRR sample (ARAM address) each of the 8 channels is currently playing.
    uint8_t dspDir = 0;   // $5D raw register (sample directory page; table lives at dspDir<<8)
    uint8_t dspKon = 0;   // $4C raw register (voices key-on'd on the most recent write)
    uint8_t dspEndx = 0;  // $7C raw register (voices that hit sample end/loop)
    std::array<uint8_t, 8> dspSrcn{};    // per-voice source number register
    std::array<uint8_t, 8> dspEnvx{};    // per-voice current envelope level
    std::array<uint8_t, 8> dspOutx{};    // per-voice current sample output
    std::array<bool, 8> dspActive{};     // per-voice live "currently sounding" state
    std::array<uint16_t, 8> dspBrrAddr{}; // per-voice ARAM address of the BRR data actually playing
    std::array<uint16_t, 8> dspLoadAddr{}; // per-voice fixed sample start addr from the DIR table (SRCN lookup)

    // GSU Debugger panel — only meaningful when hasGsu is true.
    bool hasGsu = false;
    bool gsuRunning = false;
    uint8_t gsuPbr = 0;
    uint16_t gsuPcAddr = 0;      // address of the currently-executing opcode (already -1 adjusted)
    std::string gsuCurrentInstr;
    std::array<uint16_t, 16> gsuRegs{};
    uint16_t gsuSfr = 0;
    uint32_t gsuLaunches = 0;
    uint32_t gsuStops = 0;
    uint64_t gsuPlotCount = 0;
    uint8_t gsuScbr = 0, gsuScmr = 0, gsuRombr = 0;
    bool gsuRambr = false;

    // CFGR ($3037), CLSR ($3039), SCMR ($303A) bitfields, and the Plot Option Register
    // (CMODE) — the latter is not CPU-addressable MMIO, only set by the GSU's own CMODE opcode.
    bool gsuCfgrIrqDisabled = false;   // $3037.7
    bool gsuCfgrHighSpeed = false;     // $3037.5
    bool gsuClsr = false;              // $3039.0
    uint8_t gsuScmrMd = 0;             // $303A.0-1 raw color-depth code
    uint8_t gsuScmrHt = 0;             // $303A.2+5 raw screen-height code
    uint16_t gsuScreenHeightPx = 0;    // decoded screen height in pixels (128/160/192/256)
    bool gsuScmrRan = false;           // $303A.3 GSU RAM access enabled
    bool gsuScmrRon = false;           // $303A.4 GSU ROM access enabled
    bool gsuPorTransparent = false;
    bool gsuPorDither = false;
    bool gsuPorHighNibble = false;
    bool gsuPorFreezeHigh = false;
    bool gsuPorObj = false;
    bool gsuBramr = false;    // $3033 backup RAM enable (write-only on real hardware)
    uint8_t gsuVcr = 0;       // $303B version code register (read-only)
    uint16_t gsuCbr = 0;      // $303E/$303F cache base register

    // GSU RAM Viewer — decoded bitplane framebuffer at SCBR/RAMBR, cropped to gsuRamWidth/HeightPx.
    std::array<uint32_t, kGsuRamMaxW * kGsuRamMaxH> gsuRamArgb{};
    uint16_t gsuRamWidthPx = 0;
    uint16_t gsuRamHeightPx = 0;
    uint8_t gsuRamBpp = 0;
};

// Result of clicking a palette swatch and hitting Apply in the editor popup.
struct PaletteEdit {
    bool applied = false;
    int index = 0;
    uint16_t bgr555 = 0;
};

class Display final {
public:
    // debugUi=false opens a bare window showing only the scaled game framebuffer — no
    // toolbar, side panels, or Load button; drawControls()/presentWithFrame() become no-ops
    // for everything but the game frame itself and window-close/pause/step keyboard input.
    Display(const std::string& title, bool debugUi);
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
    // Toggled by the "Pause"/"Resume" button in the S-DSP Voices section — apply this to
    // Bus::setApuPaused each frame to freeze/resume just the SPC700/S-DSP.
    bool apuPaused() const { return m_apuPaused; }
    // One-shot: true only for the frame the "Step"/"Next Frame" button (shown while APU-paused)
    // was clicked — advance the APU by a small/one-frame cycle count, then let it refreeze.
    bool apuStepRequested() const { return m_apuStepRequested; }
    bool apuNextFrameRequested() const { return m_apuNextFrameRequested; }
private:
    void drawLeftPanel(const std::vector<DebugSection>& sections, const std::vector<std::string>& instructionLog);
    void drawRightPanel(const DebugPanel& panel);
    void drawBottomPanel(const DebugPanel& panel);
    void drawGameInfoPanel(const DebugPanel& panel);
    void drawGsuDebugPanel(const DebugPanel& panel);
    // Re-presents whatever's already in m_frameTex, rescaled to the window's *current* size.
    // Installed as an SDL event watch so it also runs synchronously from inside SDL's Cocoa
    // live-resize tracking loop, where processEvents()'s ordinary SDL_PollEvent doesn't return
    // to the main loop until the mouse button is released — see the .cpp for detail.
    void redrawDuringResize();
    static int sdlEventWatch(void* userdata, SDL_Event* event);

    SDL_Window*   m_window       = nullptr;
    SDL_Renderer* m_renderer     = nullptr;
    SDL_Texture*  m_frameTex     = nullptr; // 256×224 streaming texture
    SDL_Texture*  m_tileSheetTex = nullptr; // 128×1024 streaming texture for the Tiles Viewer (full VRAM)
    SDL_Texture*  m_gsuRamTex    = nullptr; // 256×256 streaming texture for the GSU RAM Viewer
    int           m_windowWidth  = 0;
    int           m_windowHeight = 0;
    EmulatorState m_state        = EmulatorState::EmulatorStateDebug;
    bool          m_fullscreen   = false; // toggled by F11: hides all panels/toolbar, shows only
                                           // the scaled game frame pillarboxed with checker bars

    int m_editingPaletteIndex = -1; // -1 when the palette editor popup is closed
    int m_editR = 0;
    int m_editG = 0;
    int m_editB = 0;
    PaletteEdit m_pendingPaletteEdit;
    std::string m_pendingRomLoadPath; // set when the Load popup's file selection is clicked
    uint8_t m_layerDisableMask = 0; // bit0-3 = BG0-3, bit4 = OAM; toggled by the Tiles Viewer buttons
    bool m_apuPaused = false; // toggled by the S-DSP Voices section's Pause/Resume button
    bool m_apuStepRequested = false;      // reset each frame in drawRightPanel, set by "Step"
    bool m_apuNextFrameRequested = false; // reset each frame in drawRightPanel, set by "Next Frame"
    bool m_hasFrameContent = false; // true once presentWithFrame has uploaded a real frame at least once
};
