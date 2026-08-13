#include "display.hpp"
#include <algorithm>
#include <stdexcept>

namespace {
// Layout uses (2/3) × the original design so window dimensions are ÷1.5 (~3× framebuffer → ~2×).
constexpr int scale23(int v) { return (v * 2 + 1) / 3; }

// Baseline window height keeps scaled SNES framebuffer centred with proportional inset (~32 px).
constexpr int MIN_WINDOW_HEIGHT = scale23(768);

constexpr int GAME_SCALE  = 2;
constexpr int GAME_DST_W  = 256 * GAME_SCALE;                    // 512
constexpr int GAME_DST_H  = 224 * GAME_SCALE;                    // 448
constexpr int GAME_DST_X  = 0;

constexpr int FONT_PT = scale23(16); // rounded from previous 16px

// Debug text panel to the right of the game frame
constexpr int TEXT_PANEL_X         = GAME_DST_W + scale23(4);
constexpr int TEXT_PANEL_RIGHT_PAD = scale23(12);

constexpr SDL_Color TEXT_COLOR{222, 222, 222, 255};
constexpr SDL_Color BG_COLOR{0, 0, 0, 255};
constexpr SDL_Color PANEL_BG_COLOR{16, 17, 20, 255};
constexpr SDL_Color DIVIDER_COLOR{58, 60, 66, 255};
constexpr SDL_Color HEADER_COLOR{255, 178, 64, 255};
constexpr SDL_Color LABEL_COLOR{140, 142, 150, 255};
constexpr SDL_Color VALUE_COLOR{118, 214, 196, 255};
constexpr int LINE_HEIGHT = scale23(18);
constexpr int LEFT_MARGIN = scale23(12);
constexpr int TOP_MARGIN  = scale23(12);
constexpr int BOTTOM_PAD  = scale23(12);

bool isSectionHeader(const std::string& line) {
    return line.size() >= 8 && line.compare(0, 4, "=== ") == 0
        && line.compare(line.size() - 4, 4, " ===") == 0;
}

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
    m_font = TTF_OpenFont("/System/Library/Fonts/Menlo.ttc", FONT_PT);
    if (!m_font) m_font = TTF_OpenFont("/System/Library/Fonts/Supplemental/Courier New.ttf", FONT_PT);
    if (!m_font) {
        TTF_Quit();
        SDL_Quit();
        throw std::runtime_error(std::string("TTF_OpenFont failed: ") + TTF_GetError());
    }

    int probeW = maxProbeLinePixels(m_font);
    if (probeW <= 0) {
        probeW = scale23(560);
    }
    m_windowWidth = TEXT_PANEL_X + probeW + TEXT_PANEL_RIGHT_PAD;
    m_windowHeight = MIN_WINDOW_HEIGHT;

    m_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                m_windowWidth, m_windowHeight, SDL_WINDOW_SHOWN);
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

void Display::applyPanelWindowHeight(std::size_t lineCount) {
    const int textExtent = TOP_MARGIN + static_cast<int>(lineCount) * LINE_HEIGHT + BOTTOM_PAD;
    const int requiredH = std::max(MIN_WINDOW_HEIGHT, textExtent);
    if (requiredH != m_windowHeight) {
        SDL_SetWindowSize(m_window, m_windowWidth, requiredH);
        m_windowHeight = requiredH;
    }
}

void Display::setFixedPanelLineCount(std::size_t lineCount) {
    m_fixedPanelLineCount = lineCount;
    applyPanelWindowHeight(lineCount);
}

void Display::renderLines(const std::vector<std::string>& lines, int xOffset) {
    int y = TOP_MARGIN;
    for (const auto& line : lines) {
        if (line.empty()) {
            y += LINE_HEIGHT;
            continue;
        }

        auto drawText = [&](const std::string& text, int x, SDL_Color color) -> int {
            if (text.empty()) return 0;
            SDL_Surface* surface = TTF_RenderUTF8_Blended(m_font, text.c_str(), color);
            if (!surface) return 0;
            const int w = surface->w;
            SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
            if (texture) {
                SDL_Rect dst{x, y, surface->w, surface->h};
                SDL_RenderCopy(m_renderer, texture, nullptr, &dst);
                SDL_DestroyTexture(texture);
            }
            SDL_FreeSurface(surface);
            return w;
        };

        if (isSectionHeader(line)) {
            TTF_SetFontStyle(m_font, TTF_STYLE_BOLD);
            drawText(line, xOffset, HEADER_COLOR);
            TTF_SetFontStyle(m_font, TTF_STYLE_NORMAL);
        } else {
            const auto sep = line.find(" : ");
            if (sep != std::string::npos) {
                const std::string label = line.substr(0, sep + 1);
                const std::string value = line.substr(sep + 2);
                const int labelW = drawText(label, xOffset, LABEL_COLOR);
                drawText(value, xOffset + labelW + scale23(4), VALUE_COLOR);
            } else {
                drawText(line, xOffset, TEXT_COLOR);
            }
        }

        y += LINE_HEIGHT;
    }
}

void Display::present(const std::vector<std::string>& lines) {
    applyPanelWindowHeight(lines.size());
    renderLines(lines, LEFT_MARGIN);
    SDL_RenderPresent(m_renderer);
}

void Display::presentWithFrame(const uint32_t* pixels,
                               const std::vector<std::string>& lines) {
    const std::size_t layoutLines =
        m_fixedPanelLineCount > 0 ? m_fixedPanelLineCount : lines.size();
    applyPanelWindowHeight(layoutLines);

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

    const SDL_Rect panelRect{GAME_DST_W, 0, m_windowWidth - GAME_DST_W, m_windowHeight};
    SDL_SetRenderDrawColor(m_renderer, PANEL_BG_COLOR.r, PANEL_BG_COLOR.g, PANEL_BG_COLOR.b, PANEL_BG_COLOR.a);
    SDL_RenderFillRect(m_renderer, &panelRect);
    SDL_SetRenderDrawColor(m_renderer, DIVIDER_COLOR.r, DIVIDER_COLOR.g, DIVIDER_COLOR.b, DIVIDER_COLOR.a);
    SDL_RenderDrawLine(m_renderer, GAME_DST_W, 0, GAME_DST_W, m_windowHeight);

    if (m_frameTex && pixels) {
        SDL_UpdateTexture(m_frameTex, nullptr, pixels, 256 * static_cast<int>(sizeof(uint32_t)));
        const int gameDstY = std::max(0, (m_windowHeight - GAME_DST_H) / 2);
        const SDL_Rect dst{GAME_DST_X, gameDstY, GAME_DST_W, GAME_DST_H};
        SDL_RenderCopy(m_renderer, m_frameTex, nullptr, &dst);
    }

    renderLines(lines, TEXT_PANEL_X);
    SDL_RenderPresent(m_renderer);
}

void Display::delay(unsigned ms) {
    SDL_Delay(ms);
}
