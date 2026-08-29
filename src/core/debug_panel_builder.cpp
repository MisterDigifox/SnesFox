#include "debug_panel_builder.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>

#include "bus.hpp"
#include "cli_common.hpp"
#include "cpu.hpp"
#include "display.hpp"
#include "gsu.hpp"
#include "gsu_disasm.hpp"
#include "ppu.hpp"
#include "sdsp.hpp"

namespace {

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

// Splits a HeaderParser::toLines() block ("=== Title ===" + key/value lines) into a DebugSection.
DebugSection headerLinesToSection(const std::vector<std::string>& headerLines) {
    if (headerLines.empty()) return DebugSection{"ROM", {}};
    std::string title = headerLines.front();
    if (title.size() >= 8 && title.compare(0, 4, "=== ") == 0 && title.compare(title.size() - 4, 4, " ===") == 0) {
        title = title.substr(4, title.size() - 8);
    }
    return DebugSection{title, {headerLines.begin() + 1, headerLines.end()}};
}

} // namespace

DebugPanel makeDebugPanel(
    const std::vector<std::string>& headerLines,
    const CPU& cpu,
    const Ppu& ppu,
    const Bus& bus,
    const std::deque<std::string>& instructionLog,
    bool paused,
    bool debugUi
) {
    DebugPanel panel;
    panel.sections.push_back(headerLinesToSection(headerLines));

    panel.showPalette = debugUi;
    if (debugUi) {
        for (int i = 0; i < 256; ++i) {
            panel.palette[i] = ppu.cgram()[i];
        }
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
    panel.joy1 = bus.joy1();
    panel.joy2 = bus.joy2();
    for (int i = 0; i < 4; ++i) {
        panel.bgTilemapBase[i] = static_cast<uint16_t>((ppu.bgSC(i) >> 2) * 0x400);
        const uint8_t nba = (i >> 1) ? ppu.bgNBA34() : ppu.bgNBA12();
        const int shift = (i & 1) ? 4 : 0;
        panel.bgChrBase[i] = static_cast<uint16_t>(((nba >> shift) & 0x0F) * 0x1000);
    }

    panel.showTiles = debugUi;
    if (debugUi) {
        decodeTileSheet(ppu.vram(), ppu.cgram(), panel.tileSheetArgb);
    }

    panel.instructionLog.assign(instructionLog.begin(), instructionLog.end());

    panel.apuRam = bus.apu().ram();

    {
        const Sdsp& dsp = bus.apu().dsp();
        const auto& regs = dsp.registers();
        panel.dspDir = regs[Sdsp::r_dir];
        panel.dspKon = regs[Sdsp::r_kon];
        panel.dspEndx = regs[Sdsp::r_endx];
        for (int v = 0; v < 8; ++v) {
            panel.dspSrcn[v] = regs[v * 0x10 + 0x04];
            panel.dspEnvx[v] = regs[v * 0x10 + 0x08];
            panel.dspOutx[v] = regs[v * 0x10 + 0x09];
            const Sdsp::VoiceDebugState vs = dsp.voiceDebugState(v);
            panel.dspActive[v] = vs.active;
            panel.dspBrrAddr[v] = vs.brrAddr;

            const uint32_t dirEntry = (static_cast<uint32_t>(panel.dspDir) << 8)
                                     + static_cast<uint32_t>(panel.dspSrcn[v]) * 4;
            panel.dspLoadAddr[v] = static_cast<uint16_t>(panel.apuRam[dirEntry] | (panel.apuRam[dirEntry + 1] << 8));
        }
    }

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
        panel.gsuCfgrIrqDisabled = gsu.cfgrIrq();
        panel.gsuCfgrHighSpeed = gsu.cfgrMs0();
        panel.gsuClsr = gsu.clsr();
        panel.gsuScmrMd = gsu.scmrMd();
        panel.gsuScmrHt = gsu.scmrHt();
        panel.gsuScreenHeightPx = gsu.screenHeight();
        panel.gsuScmrRan = gsu.scmrRan();
        panel.gsuScmrRon = gsu.scmrRon();
        panel.gsuPorTransparent = gsu.porTransparent();
        panel.gsuPorDither = gsu.porDither();
        panel.gsuPorHighNibble = gsu.porHighNibble();
        panel.gsuPorFreezeHigh = gsu.porFreezeHigh();
        panel.gsuPorObj = gsu.porObj();
        panel.gsuBramr = gsu.bramr();
        panel.gsuVcr = gsu.vcr();
        panel.gsuCbr = gsu.cbr();
        for (int i = 0; i < 16; ++i) panel.gsuRegs[i] = gsu.reg(static_cast<uint8_t>(i));

        if (debugUi) {
            decodeGsuRam(gsu, bus.gsuWorkRam(), ppu.cgram(), panel.gsuRamArgb,
                         panel.gsuRamWidthPx, panel.gsuRamHeightPx, panel.gsuRamBpp);
        }

        const size_t logCount = gsu.debugLogCount();
        if (logCount > 0) {
            const GSU::DebugLogEntry& e = gsu.debugLogEntry(logCount - 1);
            panel.gsuPcAddr = static_cast<uint16_t>(e.pc - 1);
            // Operand bytes aren't stored in the log itself (see DebugLogEntry's doc comment) —
            // ROM is immutable, so re-derive them here, once per frame, instead of on every GSU
            // instruction.
            const uint32_t operandBase = (static_cast<uint32_t>(e.pbr) << 16) | e.pc;
            const uint8_t operand1 = bus.gsuReadRom(operandBase);
            const uint8_t operand2 = bus.gsuReadRom(operandBase + 1);
            panel.gsuCurrentInstr = gsuDisassemble(panel.gsuPcAddr, e.opcode, e.alt1, e.alt2,
                                                    operand1, operand2);
        }
    }

    return panel;
}
