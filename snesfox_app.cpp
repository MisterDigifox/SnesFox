#include "snesfox_app.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include <SDL2/SDL.h>

#include "bus.hpp"
#include "cpu.hpp"
#include "display.hpp"
#include "gsu_disasm.hpp"
#include "disasm_dump.hpp"
#include "header.hpp"
#include "opcodes.hpp"
#include "reasm.hpp"
#include "rom.hpp"
#include "tests/cpu_test.hpp"
#include "tests/ppu_test.hpp"
#include "tests/sdsp_test.hpp"

namespace {

constexpr uint64_t CYCLES_PER_FRAME = Bus::kCyclesPerFrame;
constexpr int AUDIO_SAMPLE_RATE = 32000;
constexpr int AUDIO_CHANNELS = 2;
constexpr int AUDIO_QUEUE_LOW_FRAMES = AUDIO_SAMPLE_RATE / 30;
constexpr int AUDIO_QUEUE_MAX_FRAMES = AUDIO_SAMPLE_RATE / 4;

uint16_t sampleJoy1(bool suppress) {
    if (suppress) return 0;
    SDL_PumpEvents();
    const uint8_t* k = SDL_GetKeyboardState(nullptr);
    uint16_t joy = 0;
    if (k[SDL_SCANCODE_Z]) joy |= 0x8000; // B
    if (k[SDL_SCANCODE_A]) joy |= 0x4000; // Y
    if (k[SDL_SCANCODE_RSHIFT]) joy |= 0x2000; // Select
    if (k[SDL_SCANCODE_RETURN]) joy |= 0x1000; // Start
    if (k[SDL_SCANCODE_UP]) joy |= 0x0800; // Up
    if (k[SDL_SCANCODE_DOWN]) joy |= 0x0400; // Down
    if (k[SDL_SCANCODE_LEFT]) joy |= 0x0200; // Left
    if (k[SDL_SCANCODE_RIGHT]) joy |= 0x0100; // Right
    if (k[SDL_SCANCODE_X]) joy |= 0x0080; // A
    if (k[SDL_SCANCODE_S]) joy |= 0x0040; // X
    if (k[SDL_SCANCODE_Q]) joy |= 0x0020; // L
    if (k[SDL_SCANCODE_W]) joy |= 0x0010; // R
    return joy;
}

uint16_t sampleJoy2(bool suppress) {
    if (suppress) return 0;
    SDL_PumpEvents();
    const uint8_t* k = SDL_GetKeyboardState(nullptr);
    uint16_t joy = 0;
    if (k[SDL_SCANCODE_2]) joy |= 0x8000; // B
    if (k[SDL_SCANCODE_4]) joy |= 0x4000; // Y
    if (k[SDL_SCANCODE_RSHIFT]) joy |= 0x2000; // Select
    if (k[SDL_SCANCODE_RETURN]) joy |= 0x1000; // Start
    if (k[SDL_SCANCODE_7]) joy |= 0x0800; // Up
    if (k[SDL_SCANCODE_8]) joy |= 0x0400; // Down
    if (k[SDL_SCANCODE_9]) joy |= 0x0200; // Left
    if (k[SDL_SCANCODE_0]) joy |= 0x0100; // Right
    if (k[SDL_SCANCODE_1]) joy |= 0x0080; // A
    if (k[SDL_SCANCODE_3]) joy |= 0x0040; // X
    if (k[SDL_SCANCODE_5]) joy |= 0x0020; // L
    if (k[SDL_SCANCODE_6]) joy |= 0x0010; // R
    return joy;
}

constexpr int LOG_SIZE = 4;

constexpr uint64_t DEFAULT_COV_FRAMES = 600;
constexpr uint64_t COV_MAX_STEPS = 20000000ull;

inline void advanceCpuScheduling(Bus& bus, CPU& cpu, bool updateJoyOnNmi, bool suppressJoypad = false) {
    const bool nmi = bus.stepPeripherals(cpu.cycles(), cpu.fineCycles());
    if (nmi) {
        if (updateJoyOnNmi) {
            bus.setJoy1(sampleJoy1(suppressJoypad));
            bus.setJoy2(sampleJoy2(suppressJoypad));
        }
        cpu.triggerNmi(bus);
    }
    if (bus.takePendingIrq()) {
        cpu.triggerIrq(bus);
    }
    bus.syncWaiAfterVblankEdge(cpu);
}

class AudioOutput {
public:
    AudioOutput() {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            std::cerr << "SDL audio disabled: " << SDL_GetError() << "\n";
            return;
        }

        SDL_AudioSpec want{};
        want.freq = AUDIO_SAMPLE_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = AUDIO_CHANNELS;
        want.samples = 1024;

        SDL_AudioSpec have{};
        m_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (m_device == 0) {
            std::cerr << "SDL audio disabled: " << SDL_GetError() << "\n";
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return;
        }
        if (have.freq != want.freq || have.format != want.format || have.channels != want.channels) {
            std::cerr << "SDL audio disabled: unsupported device format\n";
            SDL_CloseAudioDevice(m_device);
            m_device = 0;
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return;
        }

        SDL_PauseAudioDevice(m_device, 0);
    }

    ~AudioOutput() {
        if (m_device != 0) {
            SDL_CloseAudioDevice(m_device);
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
    }

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    void setPaused(bool paused) {
        if (m_device == 0) return;
        SDL_PauseAudioDevice(m_device, paused ? 1 : 0);
        if (paused) {
            SDL_ClearQueuedAudio(m_device);
        }
    }

    void clearQueue() {
        if (m_device == 0) return;
        SDL_ClearQueuedAudio(m_device);
    }

    void pump(APU& apu) {
        if (m_device == 0) return;

        const uint32_t queuedBytes = SDL_GetQueuedAudioSize(m_device);
        const uint32_t frameBytes = static_cast<uint32_t>(sizeof(Sdsp::PcmFrame));
        const uint32_t queuedFrames = queuedBytes / frameBytes;
        if (queuedFrames > AUDIO_QUEUE_MAX_FRAMES) {
            SDL_ClearQueuedAudio(m_device);
        }
        if (queuedFrames >= AUDIO_QUEUE_LOW_FRAMES) return;

        std::array<Sdsp::PcmFrame, 2048> frames{};
        const size_t n = apu.popAudioSamples(frames.data(), frames.size());
        if (n == 0) return;

        const uint32_t bytes = static_cast<uint32_t>(n * sizeof(Sdsp::PcmFrame));
        if (SDL_QueueAudio(m_device, frames.data(), bytes) != 0) {
            std::cerr << "SDL_QueueAudio failed: " << SDL_GetError() << "\n";
        }
    }

private:
    SDL_AudioDeviceID m_device = 0;
};

std::string trimCopy(std::string s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

bool parseHex24Line(const std::string& t, uint32_t& out) {
    std::string h = trimCopy(t);
    if (h.empty()) return false;
    if (h.size() >= 2 && h[0] == '$') h = h.substr(1);
    if (h.size() > 6) return false;
    uint32_t v = 0;
    for (char c : h) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= static_cast<uint32_t>(c - '0');
        else if (c >= 'A' && c <= 'F')
            v |= static_cast<uint32_t>(10 + c - 'A');
        else if (c >= 'a' && c <= 'f')
            v |= static_cast<uint32_t>(10 + c - 'a');
        else
            return false;
    }
    out = v & 0xFFFFFFu;
    return true;
}

std::unordered_set<uint32_t> loadCoverageFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open coverage file: " + path);
    }

    std::unordered_set<uint32_t> pcs;
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trimCopy(line);
        if (t.empty() || t[0] == '#') continue;

        uint32_t pc = 0;
        if (!parseHex24Line(t, pc)) {
            throw std::runtime_error("invalid coverage line (expected hex PC): " + path);
        }
        pcs.insert(pc);
    }

    return pcs;
}

std::string hex8(uint8_t value) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(value);
    return oss.str();
}

std::string hex16(uint16_t value) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << value;
    return oss.str();
}

std::string hex24(uint32_t value) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(6) << std::setfill('0')
        << (value & 0xFFFFFF);
    return oss.str();
}

uint32_t bgr555ToArgb(uint16_t c) {
    const uint32_t r = static_cast<uint32_t>((c & 0x1F) * 255 / 31);
    const uint32_t g = static_cast<uint32_t>(((c >> 5) & 0x1F) * 255 / 31);
    const uint32_t b = static_cast<uint32_t>(((c >> 10) & 0x1F) * 255 / 31);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

// Decodes the entire VRAM (0x8000 words = 2048 8x8 4bpp tiles) into a 128-wide ARGB8888 sheet
// (16 tiles per row, 128 rows), resolved through palette 0.
// A 4bpp tile is 32 bytes / 16 words: 8 words of planes 0/1, then 8 words of planes 2/3.
void decodeTileSheet(const uint16_t* vram, const uint16_t* cgram,
                     std::array<uint32_t, kTileSheetW * kTileSheetH>& outArgb) {
    for (int tileIndex = 0; tileIndex < kTileSheetCols * kTileSheetRows; ++tileIndex) {
        const int tileCol = tileIndex % kTileSheetCols;
        const int tileRow = tileIndex / kTileSheetCols;
        const uint16_t tileWordBase = static_cast<uint16_t>(tileIndex * 16);
        for (int py = 0; py < 8; ++py) {
            const uint16_t w01 = vram[(tileWordBase + py) & 0x7FFF];
            const uint16_t w23 = vram[(tileWordBase + 8 + py) & 0x7FFF];
            const uint8_t p0 = static_cast<uint8_t>(w01 & 0xFF);
            const uint8_t p1 = static_cast<uint8_t>((w01 >> 8) & 0xFF);
            const uint8_t p2 = static_cast<uint8_t>(w23 & 0xFF);
            const uint8_t p3 = static_cast<uint8_t>((w23 >> 8) & 0xFF);
            for (int px = 0; px < 8; ++px) {
                const int bit = 7 - px;
                const int colorIndex = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1)
                                      | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3);
                const int outX = tileCol * 8 + px;
                const int outY = tileRow * 8 + py;
                outArgb[static_cast<size_t>(outY * kTileSheetW + outX)] = bgr555ToArgb(cgram[colorIndex]);
            }
        }
    }
}

// Decodes the GSU's current bitplane framebuffer (wherever SCBR/RAMBR currently points in the
// $70/$71 work RAM) into an ARGB8888 image, cropped to the active screen mode's actual size.
// Shares the exact tile-address (`cn`) and bit-plane-interleave formulas as GSU::rpix/
// flushPixelCache (gsu.cpp) — this is a read-only re-derivation of the same hardware math,
// not a new format.
void decodeGsuRam(const GSU& gsu, const std::vector<uint8_t>& gsuRam, const uint16_t* cgram,
                  std::array<uint32_t, kGsuRamMaxW * kGsuRamMaxH>& outArgb,
                  uint16_t& outWidth, uint16_t& outHeight, uint8_t& outBpp) {
    const uint8_t scmrRaw = gsu.scmr();
    const uint8_t scmrHt = static_cast<uint8_t>((((scmrRaw >> 5) & 1) << 1) | ((scmrRaw >> 2) & 1));
    const uint8_t scmrMd = static_cast<uint8_t>(scmrRaw & 0x03);
    const bool porObj = gsu.porObj();
    const uint8_t ht = porObj ? 3u : scmrHt;
    // The cn/address formula itself has no width limit — "128 wide" is just the conventional
    // non-OBJ screen width most games stay within, not something the hardware enforces. Games
    // that plot past x=127 (real screen coordinates, not GSU-buffer-relative) are still valid;
    // always show the full 256 so the viewer doesn't silently crop real content (confirmed via
    // Star3D's own ground-truth vertex coordinates, which range up to x=172).
    const uint16_t width = kGsuRamMaxW;
    const uint16_t height = gsu.screenHeight();
    const uint32_t bpp = 2u << (scmrMd - (scmrMd >> 1));
    const uint32_t bankOffset = gsu.rambr() ? 0x10000u : 0u;
    const uint32_t scbrBase = static_cast<uint32_t>(gsu.scbr()) << 10;

    outWidth = width;
    outHeight = height;
    outBpp = static_cast<uint8_t>(bpp);
    outArgb.fill(0xFF000000u);

    auto readRam = [&](uint32_t addr) -> uint8_t {
        return gsuRam[bankOffset + (addr & 0xFFFF)];
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint32_t cn = 0;
            switch (ht) {
            case 0: cn = static_cast<uint32_t>(((x & 0xF8) << 1) + ((y & 0xF8) >> 3)); break;
            case 1: cn = static_cast<uint32_t>(((x & 0xF8) << 1) + ((x & 0xF8) >> 1) + ((y & 0xF8) >> 3)); break;
            case 2: cn = static_cast<uint32_t>(((x & 0xF8) << 1) + (x & 0xF8) + ((y & 0xF8) >> 3)); break;
            default: cn = static_cast<uint32_t>(((y & 0x80) << 2) + ((x & 0x80) << 1) + ((y & 0x78) << 1) + ((x & 0x78) >> 3)); break;
            }
            const uint32_t addrBase = scbrBase + cn * (bpp << 3) + static_cast<uint32_t>(y & 7) * 2;
            const uint8_t shift = static_cast<uint8_t>((x & 7) ^ 7);
            uint32_t colorIndex = 0;
            for (uint32_t n = 0; n < bpp; ++n) {
                const uint32_t byte = ((n >> 1) << 4) + (n & 1);
                colorIndex |= ((readRam(addrBase + byte) >> shift) & 1u) << n;
            }
            outArgb[static_cast<size_t>(y * kGsuRamMaxW + x)] = bgr555ToArgb(cgram[colorIndex]);
        }
    }
}

uint16_t readResetVector(const std::vector<uint8_t>& rom, bool isLoRom) {
    const size_t addr = isLoRom ? 0x7FFC : 0xFFFC;
    if (rom.size() <= addr + 1) return 0x0000;
    return static_cast<uint16_t>(rom[addr] | (rom[addr + 1] << 8));
}

std::string formatDisasmLine(uint32_t pc24, const CPU& cpu, bool isCurrent = false) {
    std::ostringstream oss;
    oss << (isCurrent ? "> " : "  ");
    oss << "$" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << ((pc24 >> 16) & 0xFF) << ":" << std::setw(4) << (pc24 & 0xFFFF) << "  ";
    oss << std::left << std::setw(10) << std::setfill(' ') << cpu.bytes();
    oss << " " << cpu.instruction();
    return oss.str();
}

// Splits a HeaderParser::toLines() block ("=== Title ===" + key/value lines) into a DebugSection.
DebugSection headerLinesToSection(const std::vector<std::string>& headerLines) {
    if (headerLines.empty()) return DebugSection{"ROM", {}};
    std::string title = headerLines.front();
    if (title.size() >= 8 && title.compare(0, 4, "=== ") == 0 && title.compare(title.size() - 4, 4, " ===") == 0) {
        title = title.substr(4, title.size() - 8);
    }
    return DebugSection{title, {headerLines.begin() + 1, headerLines.end()}};
}

DebugPanel makeDebugPanel(
    const std::vector<std::string>& headerLines,
    const CPU& cpu,
    const Ppu& ppu,
    const Bus& bus,
    const std::deque<std::string>& instructionLog,
    bool paused
) {
    DebugPanel panel;
    panel.sections.push_back(headerLinesToSection(headerLines));

    panel.showPalette = true;
    for (int i = 0; i < 256; ++i) {
        panel.palette[i] = ppu.cgram()[i];
    }

    panel.sections.push_back(DebugSection{"CPU Debug", {
        std::string("State : ") + (paused ? "PAUSED" : "RUNNING"),
        "Reset Vector : " + hex16(cpu.resetVector()),
        "Bank : " + hex8(cpu.bank()),
        "PC : " + hex16(cpu.pc()),
        "PC24 : " + hex24(cpu.pc24()),
        "Opcode : " + hex8(cpu.opcode()),
        "Instruction : " + cpu.instruction(),
        "P : " + hex8(cpu.p()),
        std::string("M/X : ") + (cpu.flagM() ? "M=8" : "M=16") + "  " + (cpu.flagX() ? "X=8" : "X=16"),
        "A : " + hex16(cpu.a()),
        "X : " + hex16(cpu.x()),
        "Y : " + hex16(cpu.y()),
        "SP : " + hex16(cpu.sp()),
        "Cycles : " + std::to_string(cpu.cycles())
    }});

    DebugSection ppuSection{"PPU State", {}};
    {
        const uint16_t chrBase = static_cast<uint16_t>((ppu.bgNBA12() & 0x0F) * 0x1000);
        const uint16_t tmBase = static_cast<uint16_t>((ppu.bgSC(0) >> 2) * 0x400);
        const uint16_t* vr = ppu.vram();
        {
            std::ostringstream oss;
            oss << "VWr:" << std::dec << ppu.vramWrites() << " FB:" << (ppu.forcedBlank() ? "ON" : "off")
                << " M:" << static_cast<int>(ppu.bgMode()) << " TM:" << std::uppercase << std::hex
                << std::setw(2) << std::setfill('0') << static_cast<int>(ppu.tm());
            ppuSection.lines.push_back(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "SC:" << std::uppercase << std::hex << std::setfill('0') << std::setw(2)
                << static_cast<int>(ppu.bgSC(0)) << " NBA:" << std::setw(2)
                << static_cast<int>(ppu.bgNBA12());
            if (ppu.bgMode() == 7) {
                oss << " M7HOFS/V:" << std::dec << ppu.mode7HOFS() << " " << ppu.mode7VOFS();
            } else {
                oss << " H:" << std::dec << ppu.bgHOFS(0) << " V:" << ppu.bgVOFS(0);
            }
            ppuSection.lines.push_back(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "VMADD:" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
                << ppu.vramAddr();
            ppuSection.lines.push_back(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "CHR@" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << chrBase << ":";
            for (int i = 0; i < 4; ++i)
                oss << " " << std::setw(4) << vr[(chrBase + i) & 0x7FFF];
            ppuSection.lines.push_back(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "TM@" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << tmBase << ":";
            for (int i = 0; i < 4; ++i)
                oss << " " << std::setw(4) << vr[(tmBase + i) & 0x7FFF];
            ppuSection.lines.push_back(oss.str());
        }
    }
    panel.sections.push_back(std::move(ppuSection));

    panel.bgMode = ppu.bgMode();
    for (int i = 0; i < 4; ++i) {
        panel.bgTilemapBase[i] = static_cast<uint16_t>((ppu.bgSC(i) >> 2) * 0x400);
        const uint8_t nba = (i >> 1) ? ppu.bgNBA34() : ppu.bgNBA12();
        const int shift = (i & 1) ? 4 : 0;
        panel.bgChrBase[i] = static_cast<uint16_t>(((nba >> shift) & 0x0F) * 0x1000);
    }

    panel.showTiles = true;
    decodeTileSheet(ppu.vram(), ppu.cgram(), panel.tileSheetArgb);

    panel.instructionLog.assign(instructionLog.begin(), instructionLog.end());

    panel.hasGsu = bus.hasSuperFx();
    if (panel.hasGsu) {
        const GSU& gsu = bus.gsu();
        panel.gsuRunning = gsu.running();
        panel.gsuPbr = gsu.pbr();
        panel.gsuSfr = gsu.sfr();
        panel.gsuLaunches = gsu.launchCount();
        panel.gsuStops = gsu.stopCount();
        panel.gsuPlotCount = gsu.plotCount();
        panel.gsuScbr = gsu.scbr();
        panel.gsuScmr = gsu.scmr();
        panel.gsuRombr = gsu.rombr();
        panel.gsuRambr = gsu.rambr();
        for (int i = 0; i < 16; ++i) panel.gsuRegs[i] = gsu.reg(static_cast<uint8_t>(i));

        decodeGsuRam(gsu, bus.gsuWorkRam(), ppu.cgram(), panel.gsuRamArgb,
                     panel.gsuRamWidthPx, panel.gsuRamHeightPx, panel.gsuRamBpp);

        const size_t logCount = gsu.debugLogCount();
        panel.gsuLog.clear();
        panel.gsuLog.reserve(logCount);
        for (size_t i = 0; i < logCount; ++i) {
            const GSU::DebugLogEntry& e = gsu.debugLogEntry(i);
            const uint16_t opcodeAddr = static_cast<uint16_t>(e.pc - 1);
            const std::string instr = gsuDisassemble(opcodeAddr, e.opcode, e.alt1, e.alt2,
                                                       e.operand1, e.operand2);
            std::ostringstream oss;
            oss << "$" << hex8(e.pbr) << ":" << std::uppercase << std::hex << std::setw(4)
                << std::setfill('0') << opcodeAddr << "  " << instr;
            panel.gsuLog.push_back(oss.str());
            if (i + 1 == logCount) {
                panel.gsuPcAddr = opcodeAddr;
                panel.gsuCurrentInstr = instr;
            }
        }
    }

    return panel;
}

void printRomInfo(const Rom& rom, const std::vector<uint8_t>& data) {
    std::cout << "=== Rom Header ===\n";
    std::cout << "File size      : " << rom.fileSize() << " bytes\n";
    std::cout << "ROM size       : " << rom.size() << " bytes\n";
    std::cout << "Copier header  : " << (rom.hasHeader() ? "Yes (512 bytes)" : "No") << "\n";
    std::cout << "Data offset    : " << rom.offset() << "\n";
    HeaderParser::print(data);
}

int runHeader(const std::string& romPath) {
    Rom rom(romPath);
    const auto& data = rom.data();
    if (data.size() < 0x10000) {
        throw std::runtime_error("ROM: unexpected size");
    }
    printRomInfo(rom, data);
    return 0;
}

int runDisasm(
    const std::string& romPath,
    const std::string& asmPath,
    const std::optional<std::string>& coveragePath
) {
    Rom rom(romPath);
    const auto& data = rom.data();
    if (data.size() < 0x10000) {
        throw std::runtime_error("ROM: unexpected size");
    }

    printRomInfo(rom, data);

    const RomMapping mapping = HeaderParser::detect(data);
    const bool isLoRom = (mapping == RomMapping::LoROM);
    const uint16_t resetVector = readResetVector(data, isLoRom);

    const std::unordered_set<uint32_t>* covPtr = nullptr;
    std::unordered_set<uint32_t> covStorage;
    if (coveragePath) {
        covStorage = loadCoverageFile(*coveragePath);
        covPtr = &covStorage;
        std::cout << "Coverage PCs    : " << covStorage.size() << " (from " << *coveragePath << ")\n";
    }

    dumpRomAsAsmFull(data, resetVector, asmPath, covPtr);
    std::cout << "=== Disassembler ===\n";
    std::cout << "Disassembly written to: " << asmPath << "\n";
    return 0;
}

int runReasm(const std::string& asmPath, const std::string& outRomPath) {
    std::string error;
    if (!reassembleDumpAsmToRomFile(asmPath, outRomPath, error)) {
        throw std::runtime_error("Reassembly failed: " + error);
    }

    std::cout << "=== Reassembler ===\n";
    std::cout << "ROM rebuilt successfully: " << outRomPath << "\n";
    return 0;
}

int runPpuSnap(const std::string& romPath, uint64_t frames) {
    Rom rom(romPath);
    const auto& data = rom.data();
    if (data.size() < 0x10000) {
        throw std::runtime_error("ROM: unexpected size");
    }

    const RomMapping mapping = HeaderParser::detect(data);
    const bool isLoRom = (mapping == RomMapping::LoROM);
    const uint16_t resetVector =
        static_cast<uint16_t>(data[(isLoRom ? 0x7FFC : 0xFFFC)]
                              | (data[(isLoRom ? 0x7FFD : 0xFFFD)] << 8));

    Bus bus(data);
    bus.reset();
    CPU cpu;
    cpu.reset(bus, resetVector);

    uint64_t audioTotal = 0;
    uint64_t audioClipped = 0;
    int16_t audioPeak = 0;
    double audioSumSq = 0.0;
    std::array<Sdsp::PcmFrame, 4096> audioBuf{};

    for (uint64_t f = 0; f < frames; ++f) {
        const uint64_t frameStartCycles = cpu.cycles();

        while ((cpu.cycles() - frameStartCycles) < CYCLES_PER_FRAME) {
            cpu.step(bus);
            advanceCpuScheduling(bus, cpu, false);
        }

        size_t n;
        while ((n = bus.apu().popAudioSamples(audioBuf.data(), audioBuf.size())) > 0) {
            for (size_t i = 0; i < n; ++i) {
                for (int16_t s : {audioBuf[i].left, audioBuf[i].right}) {
                    ++audioTotal;
                    if (s == 32767 || s == -32768) ++audioClipped;
                    if (std::abs(static_cast<int>(s)) > std::abs(static_cast<int>(audioPeak))) audioPeak = s;
                    audioSumSq += static_cast<double>(s) * static_cast<double>(s);
                }
            }
        }
    }
    if (audioTotal > 0) {
        std::cerr << "--- Audio stats over " << std::dec << frames << " frame(s)\n"
                  << "  samples=" << audioTotal << " clipped(=+-32767/32768)=" << audioClipped
                  << " (" << (100.0 * static_cast<double>(audioClipped) / static_cast<double>(audioTotal)) << "%)"
                  << " peak=" << audioPeak
                  << " rms=" << std::sqrt(audioSumSq / static_cast<double>(audioTotal)) << "\n";
    }

    if (const char* dumpPath = std::getenv("SNESFOX_GSU_RAM_DUMP")) {
        std::ofstream f(dumpPath, std::ios::binary);
        const auto& ram = bus.gsuWorkRam();
        f.write(reinterpret_cast<const char*>(ram.data()), static_cast<std::streamsize>(ram.size()));
    }
    if (const char* decodedPath = std::getenv("SNESFOX_GSU_RAM_DECODED_DUMP")) {
        // Exercises the REAL decodeGsuRam (not a reimplementation) and dumps its actual output,
        // for pixel-for-pixel comparison against an independent reference decode.
        DebugPanel tmpPanel;
        decodeGsuRam(bus.gsu(), bus.gsuWorkRam(), bus.ppu().cgram(), tmpPanel.gsuRamArgb,
                     tmpPanel.gsuRamWidthPx, tmpPanel.gsuRamHeightPx, tmpPanel.gsuRamBpp);
        std::ofstream f(decodedPath, std::ios::binary);
        uint16_t hdr[3] = {tmpPanel.gsuRamWidthPx, tmpPanel.gsuRamHeightPx,
                           static_cast<uint16_t>(tmpPanel.gsuRamBpp)};
        f.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
        f.write(reinterpret_cast<const char*>(tmpPanel.gsuRamArgb.data()),
                static_cast<std::streamsize>(tmpPanel.gsuRamArgb.size() * sizeof(uint32_t)));
    }

    const Ppu& p = bus.ppu();
    const uint16_t* v = p.vram();
    const uint8_t nba34 = p.bgNBA34();
    const uint16_t chrW = static_cast<uint16_t>((nba34 & 0x0F) * 0x1000u);
    const uint16_t tmWords = static_cast<uint16_t>((p.bgSC(2) >> 2) * 0x400u);

    auto hexW = [](uint16_t x) -> std::string {
        std::ostringstream o;
        o << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << x;
        return o.str();
    };

    std::cerr << "--- PPU snap after " << std::dec << frames << " frame(s)\n";
    std::cerr << "  forcedBlank=" << (p.forcedBlank() ? 1 : 0) << "  bgMode=" << static_cast<int>(p.bgMode())
              << "  bg3PrioBit=" << (p.bg3Priority() ? 1 : 0) << "  tm=$" << hex8(p.tm()) << " (BG1234|OBJ="
              << ((p.tm() >> 0) & 1) << ((p.tm() >> 1) & 1) << ((p.tm() >> 2) & 1) << ((p.tm() >> 3) & 1) << "|"
              << ((p.tm() >> 4) & 1) << ")\n";

    std::cerr << "  BG34NBA=$" << hex8(nba34) << " -> BG3 CHR word base $" << hexW(chrW) << '\n';

    unsigned tmPri13 = 0;
    unsigned tmNonzero = 0;
    unsigned chrNz = 0;
    const int kPeek = 32;
    std::cerr << "  BG3 tilemap @$" << hexW(tmWords) << " words[0.." << (kPeek - 1) << "]:\n   ";
    for (int i = 0; i < kPeek; ++i) {
        const uint16_t w = v[(tmWords + static_cast<uint16_t>(i)) & 0x7FFF];
        std::cerr << hexW(w) << (i + 1 == kPeek ? '\n' : ' ');
        if (w != 0) ++tmNonzero;
        if (w & 0x2000) ++tmPri13;
    }

    std::cerr << "  BG3 CHR @$" << hexW(chrW) << " first " << kPeek << " words:\n   ";
    for (int i = 0; i < kPeek; ++i) {
        const uint16_t w = v[(chrW + static_cast<uint16_t>(i)) & 0x7FFF];
        std::cerr << hexW(w) << (i + 1 == kPeek ? '\n' : ' ');
        if (w != 0) ++chrNz;
    }

    const uint32_t* fb = p.framebuffer();
    unsigned nonBlack = 0;
    unsigned uniq = 0;
    uint32_t seen[384]{};
    for (size_t i = 0; i < 256u * 224u; ++i) {
        const uint32_t c = fb[i];
        if ((c >> 24) != 0xFF) continue;
        if (c != 0xFF000000u) {
            ++nonBlack;
            size_t si = uniq;
            bool dup = false;
            while (si-- > 0) {
                if (seen[si] == c) {
                    dup = true;
                    break;
                }
            }
            if (!dup && uniq < sizeof(seen) / sizeof(seen[0])) seen[uniq++] = c;
        }
    }

    std::cerr << "  framebuffer: non-black opaque pix=" << nonBlack << " uniqColors~=" << uniq << '\n';

    if (bus.hasSuperFx()) {
        const GSU& g = bus.gsu();
        std::cerr << "  GSU: launches=" << g.launchCount() << " stops=" << g.stopCount()
                  << " running=" << (g.running() ? 1 : 0) << " plotCount=" << g.plotCount()
                  << " lastSessionPlots=" << g.lastSessionPlots()
                  << " lastSessionCycles=" << g.lastSessionCycles()
                  << " pc=$" << hex8(g.pbr()) << ":" << hexW(g.pc())
                  << " scbr=$" << hex8(g.scbr()) << " scmr=$" << hex8(g.scmr())
                  << " rombr=$" << hex8(g.rombr()) << " rambr=" << (g.rambr() ? 1 : 0) << '\n';
    }

    {
        std::ofstream ppm("/tmp/snap.ppm", std::ios::binary);
        ppm << "P6\n256 224\n255\n";
        for (size_t i = 0; i < 256u * 224u; ++i) {
            const uint32_t c = fb[i];
            const char rgb[3] = {
                static_cast<char>((c >> 16) & 0xFF),
                static_cast<char>((c >> 8) & 0xFF),
                static_cast<char>(c & 0xFF)
            };
            ppm.write(rgb, 3);
        }
    }

    std::cerr << "  heuristic: nonzero map words=" << tmNonzero << "  tile-priority(bit13) entries=" << tmPri13
              << "  nonzero CHR peek words=" << chrNz << '\n';

    // --- TEMP sprite diag ---
    std::cerr << "  OBSEL=$" << hex8(p.obsel()) << "\n";
    {
        const uint16_t nameBase = static_cast<uint16_t>((p.obsel() & 0x07) << 12);
        unsigned nz = 0;
        for (int i = 0; i < 1024; ++i) if (v[(nameBase + i) & 0x7FFF]) ++nz;
        std::cerr << "  OBJ CHR@$" << hexW(nameBase) << " nonzero(of 1024 peeked, 64 tiles)=" << nz << "\n";
        std::cerr << "  tile18 (word off 288..303): ";
        for (int i = 288; i < 304; ++i) std::cerr << hexW(v[(nameBase + i) & 0x7FFF]) << ' ';
        std::cerr << "\n";
        std::cerr << "  VRAM@$0400 (DMA target) words[0..31]: ";
        for (int i = 0; i < 32; ++i) std::cerr << hexW(v[(0x0400 + i) & 0x7FFF]) << ' ';
        std::cerr << "\n";
    }
    {
        const uint16_t* cg = p.cgram();
        std::cerr << "  CGRAM[128..143] (OBJ pal0): ";
        for (int i = 128; i < 144; ++i) std::cerr << hexW(cg[i]) << ' ';
        std::cerr << "\n  fb(48,100)=" << std::hex << fb[100*256+48]
                  << "  fb(8,10)=" << fb[10*256+8]
                  << "  fb(220,100)=" << fb[100*256+220] << std::dec << "\n";
        std::cerr << "  fb row100 x=95..120: ";
        for (int x = 95; x <= 120; ++x) std::cerr << std::hex << fb[100*256+x] << std::dec << ' ';
        std::cerr << "\n";
    }
    for (int bg = 0; bg < 2; ++bg) {
        const uint16_t tmBase = static_cast<uint16_t>((p.bgSC(bg) >> 2) * 0x400u);
        unsigned pri = 0, nz2 = 0;
        for (int i = 0; i < 1024; ++i) {
            const uint16_t w = v[(tmBase + static_cast<uint16_t>(i)) & 0x7FFF];
            if (w) ++nz2;
            if (w & 0x2000) ++pri;
        }
        std::cerr << "  BG" << (bg+1) << " tilemap@$" << hexW(tmBase) << " nonzero=" << nz2
                  << "/1024  priHigh=" << pri << "/1024  sample[0..7]: ";
        for (int i = 0; i < 8; ++i) std::cerr << hexW(v[(tmBase + static_cast<uint16_t>(i)) & 0x7FFF]) << ' ';
        std::cerr << "  around(32,88)[tile col4 row11]: ";
        const int tc = 32/8, tr = 88/8;
        const uint16_t mapWord = v[(tmBase + static_cast<uint16_t>(tr*32+tc)) & 0x7FFF];
        std::cerr << hexW(mapWord) << '\n';

        const uint16_t chrBaseBg = p.chrBase(bg);
        unsigned chrNzBg = 0;
        for (int i = 0; i < 4096; ++i) if (v[(chrBaseBg + static_cast<uint16_t>(i)) & 0x7FFF]) ++chrNzBg;
        std::cerr << "  BG" << (bg+1) << " CHR@$" << hexW(chrBaseBg) << " nonzero=" << chrNzBg
                  << "/4096 first16: ";
        for (int i = 0; i < 16; ++i) std::cerr << hexW(v[(chrBaseBg + static_cast<uint16_t>(i)) & 0x7FFF]) << ' ';
        std::cerr << '\n';

        // What does the SPECIFIC tile referenced by that sampled tilemap entry actually look
        // like at each BG's own CHR base? (bg0=BG1 $210B lo, bg1=BG2 $210B hi, bg2=BG3 $210C lo)
        const unsigned tileIdx = mapWord & 0x3FF;
        std::cerr << "  tile#" << tileIdx << " (from BG" << (bg+1) << " map) 4bpp data at each CHR base:\n";
        for (int cb = 0; cb < 3; ++cb) {
            const uint16_t base = p.chrBase(cb);
            const uint16_t off = static_cast<uint16_t>(base + tileIdx * 16u);
            std::cerr << "    @BG" << (cb+1) << "base($" << hexW(base) << ")+tile*16=$" << hexW(off) << ": ";
            for (int i = 0; i < 8; ++i) std::cerr << hexW(v[(off + static_cast<uint16_t>(i)) & 0x7FFF]) << ' ';
            std::cerr << '\n';
        }
    }
    const uint8_t* oam = p.oam();
    for (int i = 0; i < 8; ++i) {
        const uint8_t* s = oam + i * 4;
        const uint8_t extra = oam[512 + (i >> 2)];
        const uint8_t eBits = (extra >> ((i & 3) << 1)) & 0x03;
        std::cerr << "  spr[" << i << "] x=" << (int)s[0] << "(+"<<(int)(eBits&1)<<") y=" << (int)s[1]
                  << " tile=$" << hex8(s[2]) << " attr=$" << hex8(s[3]) << " large=" << (int)((eBits>>1)&1) << "\n";
    }
    // --- end TEMP sprite diag ---

    return 0;
}

int runCov(const std::string& romPath, const std::string& covPath, uint64_t frames) {
    Rom rom(romPath);
    const auto& data = rom.data();
    if (data.size() < 0x10000) {
        throw std::runtime_error("ROM: unexpected size");
    }

    printRomInfo(rom, data);

    const RomMapping mapping = HeaderParser::detect(data);
    const bool isLoRom = (mapping == RomMapping::LoROM);
    const uint16_t resetVector = readResetVector(data, isLoRom);

    Bus bus(data);
    bus.reset();

    CPU cpu;
    cpu.reset(bus, resetVector);

    std::unordered_set<uint32_t> hit;
    uint64_t steps = 0;
    bool stopped = false;

    for (uint64_t f = 0; f < frames && !stopped; ++f) {
        const uint64_t frameStartCycles = cpu.cycles();

        while ((cpu.cycles() - frameStartCycles) < CYCLES_PER_FRAME) {
            hit.insert(cpu.pc24());

            cpu.step(bus);
            advanceCpuScheduling(bus, cpu, false);

            ++steps;
            if (steps >= COV_MAX_STEPS) {
                stopped = true;
                break;
            }
        }
    }

    std::vector<uint32_t> sorted(hit.begin(), hit.end());
    std::sort(sorted.begin(), sorted.end());

    std::ofstream out(covPath);
    if (!out) {
        throw std::runtime_error("cannot write coverage file: " + covPath);
    }

    out << "# snesfox-cov-v1 frames=" << frames << " steps=" << steps << " unique=" << sorted.size() << "\n";

    for (uint32_t pc : sorted) {
        out << std::uppercase << std::hex << std::setw(6) << std::setfill('0') << (pc & 0xFFFFFFu) << "\n";
    }

    std::cout << std::dec << std::nouppercase << std::setfill(' ');
    std::cout << "=== Coverage ===\n";
    std::cout << "Frames (target) : " << frames << "\n";
    std::cout << "Steps executed  : " << steps << "\n";
    std::cout << "Unique PCs      : " << sorted.size() << "\n";
    std::cout << "Written         : " << covPath << "\n";
    return 0;
}

int runEmu(const std::string& initialRomPath) {
    printMissingCpuOpcodes(cpuOpcodesTable);

    // Display/audio persist across a ROM swap (the "Load" button) — only the ROM-derived
    // state below gets torn down and rebuilt, same as this function's original one-shot
    // setup used to do exactly once.
    Display display("snesfox");
    AudioOutput audio;

    std::string romPath = initialRomPath;
    bool loadRequested = true;

    while (loadRequested) {
        loadRequested = false;

        Rom rom(romPath);
        const auto& data = rom.data();
        if (data.size() < 0x10000) {
            throw std::runtime_error("ROM: unexpected size");
        }

        printRomInfo(rom, data);

        const auto headerLines = HeaderParser::toLines(data);
        const RomMapping mapping = HeaderParser::detect(data);
        const bool isLoRom = (mapping == RomMapping::LoROM);
        const uint16_t resetVector = readResetVector(data, isLoRom);

        std::string savePath = romPath;
        const auto dot = savePath.rfind('.');
        if (dot != std::string::npos)
            savePath.replace(dot, std::string::npos, ".sav");
        else
            savePath += ".sav";

        Bus bus(data, savePath);
        bus.reset();

        CPU cpu;
        cpu.reset(bus, resetVector);

        std::deque<std::string> instructionLog;
        bool paused = false;
        bool stepOnce = false;
        bool nextFrameOnce = false;
        audio.clearQueue();

        auto logInstruction = [&](uint32_t pcBefore) {
            if (!instructionLog.empty() && instructionLog.front().rfind("> ", 0) == 0) {
                instructionLog.front().replace(0, 2, "  ");
            }
            instructionLog.push_front(formatDisasmLine(pcBefore, cpu, true));
            if (instructionLog.size() > LOG_SIZE) {
                instructionLog.pop_back();
            }
        };

        // Pace the loop to the SNES's real NTSC refresh rate rather than running
        // as fast as the host CPU allows (SDL_RENDERER_PRESENTVSYNC alone isn't
        // reliable pacing — it tracks the display's refresh rate, not 60.0988Hz,
        // and some platforms/drivers ignore it entirely).
        constexpr double kTargetFps = 60.0988;
        const uint64_t perfFreq = SDL_GetPerformanceFrequency();
        uint64_t frameStartPerf = SDL_GetPerformanceCounter();

        bool running = true;
        while (running) {
            DebugAction action = DebugAction::None;
            running = display.processEvents(action);

            display.beginFrame();
            const DebugAction uiAction = display.drawControls(paused);
            if (uiAction != DebugAction::None) {
                action = uiAction;
            }
            const bool suppressJoypad = display.wantsKeyboardCapture();

            if (action == DebugAction::LoadRom) {
                romPath = display.pendingRomLoadPath();
                loadRequested = true;
                running = false;
                // Fall through to finish this frame normally (still need the matching
                // presentWithFrame()/ImGui::Render() for the NewFrame() already started
                // above via beginFrame() — skipping it here trips ImGui's own assertion
                // on the next iteration's NewFrame() call after the ROM is swapped in).
            }
            if (action == DebugAction::TogglePause) {
                paused = !paused;
                audio.setPaused(paused);
            }
            if (action == DebugAction::Reset) {
                bus.reset();
                cpu.reset(bus, resetVector);
                instructionLog.clear();
                audio.clearQueue();
            }
            if (action == DebugAction::StepOne && paused) {
                stepOnce = true;
            }
            if (action == DebugAction::NextFrame && paused) {
                nextFrameOnce = true;
            }

            if (!paused) {
                const uint64_t frameStartCycles = cpu.cycles();

                while ((cpu.cycles() - frameStartCycles) < CYCLES_PER_FRAME) {
                    cpu.step(bus);
                    advanceCpuScheduling(bus, cpu, true, suppressJoypad);
                }
                audio.pump(bus.apu());
            } else if (stepOnce) {
                stepOnce = false;

                const uint32_t pcBefore = cpu.pc24();
                cpu.step(bus);
                advanceCpuScheduling(bus, cpu, true, suppressJoypad);
                logInstruction(pcBefore);
            } else if (nextFrameOnce) {
                nextFrameOnce = false;

                const uint64_t frameStartCycles = cpu.cycles();
                while ((cpu.cycles() - frameStartCycles) < CYCLES_PER_FRAME) {
                    const uint32_t pcBefore = cpu.pc24();
                    cpu.step(bus);
                    advanceCpuScheduling(bus, cpu, true, suppressJoypad);
                    logInstruction(pcBefore);
                }
            }

            const auto panel = makeDebugPanel(headerLines, cpu, bus.ppu(), bus, instructionLog, paused);
            const PaletteEdit paletteEdit = display.presentWithFrame(bus.ppu().framebuffer(), panel);
            if (paletteEdit.applied) {
                bus.ppu().setCgramEntry(paletteEdit.index, paletteEdit.bgr555);
            }
            bus.ppu().setDebugLayerDisable(display.layerDisableMask());

            const uint64_t frameEndPerf = SDL_GetPerformanceCounter();
            const double elapsedMs = static_cast<double>(frameEndPerf - frameStartPerf) * 1000.0 / static_cast<double>(perfFreq);
            constexpr double kTargetFrameMs = 1000.0 / kTargetFps;
            if (elapsedMs < kTargetFrameMs) {
                SDL_Delay(static_cast<uint32_t>(kTargetFrameMs - elapsedMs));
            }
            frameStartPerf = SDL_GetPerformanceCounter();
        }
    }

    return 0;
}

void printUsage() {
    std::cerr << "Usage:\n";
    std::cerr << "  ./snesfox selftest                  # PPU register regression tests (no ROM)\n";
    std::cerr << "  ./snesfox emu <rom.sfc>\n";
    std::cerr << "  ./snesfox snap <rom.sfc> [frames]   # dump PPU/VRAM heuristics (no SDL)\n";
    std::cerr << "  ./snesfox header <rom.sfc>\n";
    std::cerr << "  ./snesfox cov <rom.sfc> <coverage.out> [frames]\n";
    std::cerr << "  ./snesfox disasm <rom.sfc> [output.asm [coverage.out]]\n";
    std::cerr << "  ./snesfox reasm <input.asm> [output.sfc]\n";
}

} // namespace

int SnesFoxApp::run(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "selftest") {
        const int ppuResult = runPpuSelfTests();
        const int cpuResult = runCpuSelfTests();
        const int sdspResult = runSdspSelfTests();
        return (ppuResult == 0 && cpuResult == 0 && sdspResult == 0) ? 0 : 1;
    }

    if (argc < 3) {
        printUsage();
        return 1;
    }

    const std::string mode = argv[1];

    if (mode == "emu") {
        return runEmu(argv[2]);
    }

    if (mode == "snap") {
        uint64_t frames = 120;
        if (argc >= 4) {
            frames = std::stoull(argv[3]);
            if (frames == 0) {
                throw std::runtime_error("snap frames must be >= 1");
            }
        }
        return runPpuSnap(argv[2], frames);
    }

    if (mode == "header") {
        return runHeader(argv[2]);
    }

    if (mode == "cov") {
        if (argc < 4) {
            printUsage();
            return 1;
        }
        const std::string covOut = argv[3];
        uint64_t frames = DEFAULT_COV_FRAMES;
        if (argc >= 5) {
            frames = std::stoull(argv[4]);
            if (frames == 0) {
                throw std::runtime_error("cov frames must be >= 1");
            }
        }
        return runCov(argv[2], covOut, frames);
    }

    if (mode == "disasm") {
        const std::string asmPath = (argc >= 4) ? argv[3] : "output.asm";
        std::optional<std::string> covPath;
        if (argc >= 5) {
            covPath = argv[4];
        }
        return runDisasm(argv[2], asmPath, covPath);
    }

    if (mode == "reasm") {
        const std::string outRomPath = (argc >= 4) ? argv[3] : "output.sfc";
        return runReasm(argv[2], outRomPath);
    }

    printUsage();
    return 1;
}
