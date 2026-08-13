#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

enum class DebugAction {
    None,
    TogglePause,
    StepOne
};

class Display final {
public:
    explicit Display(const std::string& title);
    ~Display();
    bool processEvents(DebugAction& action);
    void clear();
    void present(const std::vector<std::string>& lines);
    // Fixed vertical space for the debug column (EMU: paused layout line total from ROM header).
    void setFixedPanelLineCount(std::size_t lineCount);
    // Game frame (256×224 ARGB) on the left, debug text on the right
    void presentWithFrame(const uint32_t* pixels,
                          const std::vector<std::string>& lines,
                          bool paused);
    void delay(unsigned ms);
private:
    void renderLines(const std::vector<std::string>& lines, int xOffset, int startY);
    void applyPanelWindowHeight(std::size_t lineCount);
    void drawPauseButton(bool paused);
    void drawStepButton();

    SDL_Window*   m_window      = nullptr;
    SDL_Renderer* m_renderer    = nullptr;
    TTF_Font*     m_font        = nullptr;
    SDL_Texture*  m_frameTex    = nullptr; // 256×224 streaming texture
    int           m_windowWidth = 0;
    int           m_windowHeight = 0; // last size sent to SDL
    std::size_t   m_fixedPanelLineCount = 0;
    SDL_Rect      m_pauseButtonRect{0, 0, 0, 0}; // hit-test rect, set by drawPauseButton
    SDL_Rect      m_stepButtonRect{0, 0, 0, 0};  // hit-test rect, only live while paused
};
