#include "display.hpp"
#include <algorithm>
#include <stdexcept>

namespace {
constexpr int WINDOW_HEIGHT = 768;

// SNES frame: 256×224 scaled 3× → 768×672, centred vertically
constexpr int GAME_SCALE  = 3;
constexpr int GAME_DST_W  = 256 * GAME_SCALE;                    // 768
constexpr int GAME_DST_H  = 224 * GAME_SCALE;                    // 672
constexpr int GAME_DST_X  = 0;
constexpr int GAME_DST_Y  = (WINDOW_HEIGHT - GAME_DST_H) / 2;   // 48

// Debug text panel to the right of the game frame
constexpr int TEXT_PANEL_X         = GAME_DST_W + 4;             // 772
constexpr int TEXT_PANEL_RIGHT_PAD = 12;

constexpr SDL_Color TEXT_COLOR{255, 255, 255, 255};
constexpr SDL_Color BG_COLOR{0, 0, 0, 255};
constexpr int LINE_HEIGHT = 18;
constexpr int LEFT_MARGIN = 12;
constexpr int TOP_MARGIN  = 12;

// Representative longest lines from main.cpp debug UI (disasm log, PPU dump, ROM header).
int maxProbeLinePixels(TTF_Font* font) {
    static const char* const kProbes[] = {
        "> $FF:FFFF  FF FF FF    LDA [$FFFFFF],Y",
        "> $FF:FFFF  FF FF FF    JSL $FFFFFF",
        "CHR@FFFF: FFFF FFFF FFFF FFFF",
        "[12]FFFF [13]FFFF [14]FFFF [15]FFFF ",
        "Title      : AAAAAAAAAAAAAAAAAAAAA",
        "Instruction  : LDA [$FFFFFF],Y",
        "Map Mode   : HiROM / SRAM / EEPROM (0xff)",
        "Special Chip  : Super FX GSU-1",
    };
    int maxPx = 0;
    for (const char* p : kProbes) {
        int w = 0;
        int h = 0;
        if (TTF_SizeUTF8(font, p, &w, &h) == 0) {
            maxPx = std::max(maxPx, w);
        }
    }
    return maxPx;
}
}

Display::Display(const std::string& title) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
    if (TTF_Init() != 0) {
        SDL_Quit();
        throw std::runtime_error(std::string("TTF_Init failed: ") + TTF_GetError());
    }
    m_font = TTF_OpenFont("/System/Library/Fonts/Menlo.ttc", 16);
    if (!m_font) m_font = TTF_OpenFont("/System/Library/Fonts/Supplemental/Courier New.ttf", 16);
    if (!m_font) {
        TTF_Quit();
        SDL_Quit();
        throw std::runtime_error(std::string("TTF_OpenFont failed: ") + TTF_GetError());
    }

    int probeW = maxProbeLinePixels(m_font);
    if (probeW <= 0) {
        probeW = 560;
    }
    const int windowWidth = TEXT_PANEL_X + probeW + TEXT_PANEL_RIGHT_PAD;

    m_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                windowWidth, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!m_window) {
        TTF_CloseFont(m_font);
        m_font = nullptr;
        TTF_Quit();
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }
    m_renderer = SDL_CreateRenderer(m_window, -1,
                                    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        TTF_CloseFont(m_font);
        m_font = nullptr;
        TTF_Quit();
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
    }
}

Display::~Display() {
    if (m_frameTex)  SDL_DestroyTexture(m_frameTex);
    if (m_font)      TTF_CloseFont(m_font);
    if (m_renderer)  SDL_DestroyRenderer(m_renderer);
    if (m_window)    SDL_DestroyWindow(m_window);
    TTF_Quit();
    SDL_Quit();
}

bool Display::processEvents(DebugAction& action) {
    action = DebugAction::None;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) return false;
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
            switch (event.key.keysym.sym) {
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    action = DebugAction::TogglePause;
                    break;
                case SDLK_SPACE:
                    action = DebugAction::StepOne;
                    break;
            }
        }
    }
    return true;
}

void Display::clear() {
    SDL_SetRenderDrawColor(m_renderer, BG_COLOR.r, BG_COLOR.g, BG_COLOR.b, BG_COLOR.a);
    SDL_RenderClear(m_renderer);
}

void Display::renderLines(const std::vector<std::string>& lines, int xOffset) {
    int y = TOP_MARGIN;
    for (const auto& line : lines) {
        SDL_Surface* surface = TTF_RenderUTF8_Blended(m_font, line.c_str(), TEXT_COLOR);
        if (!surface) continue;
        SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
        if (!texture) { SDL_FreeSurface(surface); continue; }
        SDL_Rect dst{xOffset, y, surface->w, surface->h};
        SDL_RenderCopy(m_renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
        SDL_FreeSurface(surface);
        y += LINE_HEIGHT;
        if (y > WINDOW_HEIGHT - LINE_HEIGHT) break;
    }
}

void Display::present(const std::vector<std::string>& lines) {
    renderLines(lines, LEFT_MARGIN);
    SDL_RenderPresent(m_renderer);
}

void Display::presentWithFrame(const uint32_t* pixels,
                                const std::vector<std::string>& lines) {
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
        const SDL_Rect dst{GAME_DST_X, GAME_DST_Y, GAME_DST_W, GAME_DST_H};
        SDL_RenderCopy(m_renderer, m_frameTex, nullptr, &dst);
    }

    renderLines(lines, TEXT_PANEL_X);
    SDL_RenderPresent(m_renderer);
}

void Display::delay(unsigned ms) {
    SDL_Delay(ms);
}
