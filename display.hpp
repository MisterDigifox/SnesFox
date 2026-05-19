#pragma once
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
    // Game frame (256×224 ARGB) on the left, debug text on the right
    void presentWithFrame(const uint32_t* pixels,
                          const std::vector<std::string>& lines);
    void delay(unsigned ms);
private:
    void renderLines(const std::vector<std::string>& lines, int xOffset);

    SDL_Window*   m_window      = nullptr;
    SDL_Renderer* m_renderer    = nullptr;
    TTF_Font*     m_font        = nullptr;
    SDL_Texture*  m_frameTex    = nullptr; // 256×224 streaming texture
};
