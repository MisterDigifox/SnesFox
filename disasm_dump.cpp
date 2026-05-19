#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>

#include "disasm_dump.hpp"
#include "opcodes.hpp"

namespace {

struct PointerTable16 {
    uint32_t pc = 0;
    std::vector<uint32_t> targets;
};

struct PointerTable24 {
    uint32_t pc = 0;
    std::vector<uint32_t> targets;
};

struct DataWords16 {
    uint32_t pc = 0;
    std::vector<uint16_t> values;
};

std::string snesHwRegisterName(uint16_t a);

std::string earlyInitLabel(int index) {
    std::ostringstream oss;
    oss << "EarlyInit_" << std::setw(2) << std::setfill('0') << index;
    return oss.str();
}

std::string ptrTableLabel(uint32_t addr) {
    std::ostringstream oss;
    oss << "PtrTable_" << std::uppercase << std::hex
        << std::setw(6) << std::setfill('0')
        << (addr & 0xFFFFFF);
    return oss.str();
}

std::string ptrTable24Label(uint32_t addr) {
    std::ostringstream oss;
    oss << "PtrTable24_" << std::uppercase << std::hex
        << std::setw(6) << std::setfill('0')
        << (addr & 0xFFFFFF);
    return oss.str();
}

std::string dataWords16Label(uint32_t addr) {
    std::ostringstream oss;
    oss << "DataWords_" << std::uppercase << std::hex
        << std::setw(6) << std::setfill('0')
        << (addr & 0xFFFFFF);
    return oss.str();
}

std::string hex8(uint8_t v) {
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(v);
    return oss.str();
}

std::string hex16(uint16_t v) {
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << v;
    return oss.str();
}

std::string hex24(uint32_t v) {
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setw(6) << std::setfill('0')
        << (v & 0xFFFFFF);
    return oss.str();
}

uint16_t read16le(uint8_t lo, uint8_t hi) {
    return static_cast<uint16_t>(lo | (hi << 8));
}

uint32_t read24le(uint8_t b1, uint8_t b2, uint8_t b3) {
    return static_cast<uint32_t>(b1 | (b2 << 8) | (b3 << 16));
}

bool isLoRomArea(uint8_t /*bank*/, uint16_t addr) {
    return addr >= 0x8000;
}

uint32_t loRomToOffset(uint8_t bank, uint16_t addr) {
    return ((bank & 0x7F) << 15) | (addr - 0x8000);
}

uint32_t offsetToLoRom(uint32_t off) {
    uint8_t bank = static_cast<uint8_t>((off >> 15) & 0x7F);
    uint16_t addr = static_cast<uint16_t>(0x8000 | (off & 0x7FFF));
    return (static_cast<uint32_t>(bank) << 16) | addr;
}

bool romRead8(const std::vector<uint8_t>& rom, uint32_t pc, uint8_t& out) {
    const uint8_t bank = static_cast<uint8_t>(pc >> 16);
    const uint16_t addr = static_cast<uint16_t>(pc & 0xFFFF);

    if (!isLoRomArea(bank, addr)) return false;

    const uint32_t off = loRomToOffset(bank, addr);
    if (off >= rom.size()) return false;

    out = rom[off];
    return true;
}

bool romReadBytes(const std::vector<uint8_t>& rom, uint32_t pc, uint8_t* dst, int count) {
    for (int i = 0; i < count; ++i) {
        if (!romRead8(rom, pc + i, dst[i])) return false;
    }
    return true;
}

bool isBranch(uint8_t op) {
    switch (op) {
        case 0x10: case 0x30: case 0x50: case 0x70:
        case 0x90: case 0xB0: case 0xD0: case 0xF0:
        case 0x80: case 0x82:
            return true;
        default:
            return false;
    }
}

bool isCondBranch(uint8_t op) {
    switch (op) {
        case 0x10: case 0x30: case 0x50: case 0x70:
        case 0x90: case 0xB0: case 0xD0: case 0xF0:
            return true;
        default:
            return false;
    }
}

bool isReturn(uint8_t op) {
    return op == 0x60 || op == 0x6B || op == 0x40;
}

bool isStop(uint8_t op) {
    return op == 0x00 || op == 0x02 || op == 0xDB;
}

bool isJsr(uint8_t op) {
    return op == 0x20 || op == 0x22;
}

uint32_t branch8(uint32_t pc, uint8_t rel) {
    const int8_t r = static_cast<int8_t>(rel);
    const uint16_t next = static_cast<uint16_t>((pc & 0xFFFF) + 2);
    return (pc & 0xFF0000) | static_cast<uint16_t>(next + r);
}

uint32_t branch16(uint32_t pc, uint8_t lo, uint8_t hi) {
    const int16_t r = static_cast<int16_t>(read16le(lo, hi));
    const uint16_t next = static_cast<uint16_t>((pc & 0xFFFF) + 3);
    return (pc & 0xFF0000) | static_cast<uint16_t>(next + r);
}

std::string label(uint32_t addr) {
    std::ostringstream oss;
    oss << "L_" << std::uppercase << std::hex << std::setw(6) << std::setfill('0')
        << (addr & 0xFFFFFF);
    return oss.str();
}

std::string funcLabel(uint32_t addr) {
    std::ostringstream oss;
    oss << "Func_" << std::uppercase << std::hex << std::setw(6) << std::setfill('0')
        << (addr & 0xFFFFFF);
    return oss.str();
}

struct DecodedLine {
    uint32_t pc = 0;
    uint8_t size = 1;
    std::array<uint8_t, 4> bytes{};
    std::string name;
    std::string operand;
    std::string comment;
};

uint8_t instrSize(const Op& op, uint8_t p) {
    switch (op.mode) {
        case AddrMode::ImmediateM:
            return (p & 0x20) ? 2 : 3;
        case AddrMode::ImmediateX:
            return (p & 0x10) ? 2 : 3;
        default:
            return op.size ? op.size : 1;
    }
}

bool isClearlyBadFunctionEntryOpcode(uint8_t op) {
    switch (op) {
        case 0x00:
        case 0x02:
        case 0x10:
        case 0x30:
        case 0x50:
        case 0x70:
        case 0x80:
        case 0x82:
        case 0x90:
        case 0xB0:
        case 0xD0:
        case 0xF0:
        case 0x40:
        case 0x60:
        case 0x6B:
        case 0xDB:
            return true;
        default:
            return false;
    }
}

bool isRomPointer16(const std::vector<uint8_t>& rom,
                    uint8_t bank,
                    uint16_t addr,
                    uint32_t& target) {
    if (!isLoRomArea(bank, addr)) return false;

    uint8_t lo = 0;
    uint8_t hi = 0;

    if (!romRead8(rom, (static_cast<uint32_t>(bank) << 16) | addr, lo)) return false;
    if (!romRead8(rom,
                  (static_cast<uint32_t>(bank) << 16) |
                      static_cast<uint16_t>(addr + 1),
                  hi)) return false;

    const uint16_t ptr = read16le(lo, hi);
    if (ptr < 0x8000) return false;

    target = (static_cast<uint32_t>(bank) << 16) | ptr;

    uint8_t dummy = 0;
    return romRead8(rom, target, dummy);
}

bool isRomPointer24AtPc(const std::vector<uint8_t>& rom,
                        uint32_t pc,
                        uint32_t& target) {
    uint8_t b0 = 0, b1 = 0, b2 = 0;
    if (!romRead8(rom, pc, b0)) return false;
    if (!romRead8(rom, pc + 1, b1)) return false;
    if (!romRead8(rom, pc + 2, b2)) return false;

    target = read24le(b0, b1, b2);

    uint8_t dummy = 0;
    return romRead8(rom, target, dummy);
}

template <typename EnqueueFn>
void enqueueJumpTable16SameBank(const std::vector<uint8_t>& rom,
                                uint8_t codeBank,
                                uint16_t tableAddr,
                                uint8_t p,
                                EnqueueFn&& enqueueFn) {
    if (!isLoRomArea(codeBank, tableAddr)) return;

    int good = 0;
    int bad = 0;

    for (int i = 0; i < 32; ++i) {
        const uint16_t entryAddr = static_cast<uint16_t>(tableAddr + i * 2);
        uint32_t target = 0;

        if (isRomPointer16(rom, codeBank, entryAddr, target)) {
            enqueueFn(target, p);
            ++good;
            bad = 0;
        } else {
            ++bad;
            if (good == 0 && bad >= 2) break;
            if (good > 0 && bad >= 3) break;
        }
    }
}

bool readsAsPaddingLike(const std::vector<uint8_t>& rom, uint32_t pc, int count = 8) {
    uint8_t first = 0;
    if (!romRead8(rom, pc, first)) return false;
    if (first != 0x00 && first != 0xFF) return false;

    for (int i = 1; i < count; ++i) {
        uint8_t b = 0;
        if (!romRead8(rom, pc + i, b)) return true;
        if (b != first) return false;
    }
    return true;
}

bool looksLikeFunctionEntryOpcode(uint8_t op) {
    switch (op) {
        case 0x08:
        case 0x0B:
        case 0x48:
        case 0x4B:
        case 0x78:
        case 0x8B:
        case 0xA0:
        case 0xA2:
        case 0xA9:
        case 0xC2:
        case 0xE2:
        case 0x20:
        case 0x22:
        case 0x5C:
            return true;
        default:
            return false;
    }
}

bool isPlausibleFunctionEntry(const std::vector<uint8_t>& rom, uint32_t target) {
    uint8_t op0 = 0;
    if (!romRead8(rom, target, op0)) return false;

    if (isClearlyBadFunctionEntryOpcode(op0)) return false;
    if (!looksLikeFunctionEntryOpcode(op0)) return false;

    uint8_t p = 0x34;
    uint32_t pc = target;
    int good = 0;
    bool sawSetup = false;
    bool sawCallOrMode = false;

    for (int i = 0; i < 4; ++i) {
        uint8_t op = 0;
        if (!romRead8(rom, pc, op)) break;

        if (isClearlyBadFunctionEntryOpcode(op) && i == 0) return false;

        const Op& meta = OPCODES[op];
        if (!meta.valid) return false;

        const uint8_t size = instrSize(meta, p);
        if (size == 0 || size > 4) return false;

        std::array<uint8_t, 4> bytes{};
        if (!romReadBytes(rom, pc, bytes.data(), size)) return false;

        switch (op) {
            case 0x08:
            case 0x0B:
            case 0x48:
            case 0x4B:
            case 0x8B:
            case 0x78:
                sawSetup = true;
                break;

            case 0xC2:
            case 0xE2:
            case 0x20:
            case 0x22:
            case 0x5C:
                sawCallOrMode = true;
                break;

            default:
                break;
        }

        if (op == 0xC2 && size >= 2) p &= static_cast<uint8_t>(~bytes[1]);
        if (op == 0xE2 && size >= 2) p |= bytes[1];

        ++good;

        if ((isReturn(op) || isStop(op)) && good < 2) return false;
        if (i == 0 && isBranch(op)) return false;

        pc += size;
    }

    if (good < 2) return false;
    if (!sawSetup && !sawCallOrMode) return false;

    return true;
}

bool collectDataWords16Block(const std::vector<uint8_t>& rom,
                             uint8_t bank,
                             uint16_t startAddr,
                             std::vector<uint16_t>& outValues,
                             int maxWords = 16) {
    outValues.clear();

    if (!isLoRomArea(bank, startAddr)) return false;

    for (int i = 0; i < maxWords; ++i) {
        uint8_t lo = 0, hi = 0;
        const uint16_t cur = static_cast<uint16_t>(startAddr + i * 2);

        if (!romRead8(rom, (static_cast<uint32_t>(bank) << 16) | cur, lo)) break;
        if (!romRead8(rom,
                      (static_cast<uint32_t>(bank) << 16) | static_cast<uint16_t>(cur + 1),
                      hi)) break;

        const uint16_t value = read16le(lo, hi);

        if (value < 0x8000) break;

        uint8_t dummy = 0;
        const uint32_t target = (static_cast<uint32_t>(bank) << 16) | value;
        if (!romRead8(rom, target, dummy)) break;

        outValues.push_back(value);
    }

    const int n = static_cast<int>(outValues.size());
    // Need at least 4 consecutive in-bank ROM pointers (same rule as before).
    if (n < 4) return false;

    int plausibleFnTargets = 0;
    for (uint16_t value : outValues) {
        const uint32_t target = (static_cast<uint32_t>(bank) << 16) | value;
        if (isPlausibleFunctionEntry(rom, target)) {
            ++plausibleFnTargets;
        }
    }

    // Loose runs (graphics / data pointer tables): many halfwords that happen to land in ROM
    // are common; require either a longer run or several targets that decode like code entry.
    if (n >= 6) return true;
    if (n == 5) return plausibleFnTargets >= 2;
    if (n == 4) return plausibleFnTargets >= 3;

    return false;
}

std::string formatAbsolute16OrLabel(
    uint32_t pc,
    uint16_t value,
    const std::unordered_map<uint32_t, std::string>& labels,
    bool sameBankOnly = true
) {
    const uint32_t full = sameBankOnly ? ((pc & 0xFF0000) | value) : value;
    auto it = labels.find(full);
    if (it != labels.end()) return it->second;
    // MMIO $00:21xx is usually keyed as 0x21xx in labels; PC may be in another LoROM bank.
    if (value >= 0x2100 && value <= 0x213F) {
        auto ih = labels.find(static_cast<uint32_t>(value));
        if (ih != labels.end()) return ih->second;
    }
    return "$" + hex16(value);
}

std::string operandStr(const Op& op,
                       uint32_t pc,
                       const uint8_t* b,
                       uint8_t size,
                       const std::unordered_map<uint32_t, std::string>& labels) {
    switch (op.mode) {
        case AddrMode::Implied:
        case AddrMode::Accumulator:
            return "";

        case AddrMode::Immediate8:
            return "#$" + hex8(b[1]);

        case AddrMode::ImmediateM:
        case AddrMode::ImmediateX:
            if (size == 2) return "#$" + hex8(b[1]);
            return "#$" + hex16(read16le(b[1], b[2]));

        case AddrMode::DirectPage:
            return "$" + hex8(b[1]);
        case AddrMode::DirectPageX:
            return "$" + hex8(b[1]) + ",X";
        case AddrMode::DirectPageY:
            return "$" + hex8(b[1]) + ",Y";
        case AddrMode::DirectIndirect:
            return "($" + hex8(b[1]) + ")";
        case AddrMode::DirectIndirectY:
            return "($" + hex8(b[1]) + "),Y";
        case AddrMode::DirectIndirectLong:
            return "[$" + hex8(b[1]) + "]";
        case AddrMode::DirectIndirectLongY:
            return "[$" + hex8(b[1]) + "],Y";
        case AddrMode::DirectXIndirect:
            return "($" + hex8(b[1]) + ",X)";
        case AddrMode::StackRelative:
            return "$" + hex8(b[1]) + ",S";
        case AddrMode::StackRelativeIndirectY:
            return "($" + hex8(b[1]) + ",S),Y";

        case AddrMode::Absolute:
            return formatAbsolute16OrLabel(pc, read16le(b[1], b[2]), labels);
        case AddrMode::AbsoluteX:
            return formatAbsolute16OrLabel(pc, read16le(b[1], b[2]), labels) + ",X";
        case AddrMode::AbsoluteY:
            return formatAbsolute16OrLabel(pc, read16le(b[1], b[2]), labels) + ",Y";
        case AddrMode::AbsoluteLong:
        case AddrMode::AbsoluteLongX: {
            const uint32_t v = read24le(b[1], b[2], b[3]);
            const uint16_t lo = static_cast<uint16_t>(v & 0xFFFF);
            const uint8_t bankB = static_cast<uint8_t>((v >> 16) & 0xFF);
            if (bankB == 0 && lo >= 0x2100 && lo <= 0x213F) {
                const std::string base = snesHwRegisterName(lo);
                if (!base.empty()) {
                    return op.mode == AddrMode::AbsoluteLongX
                        ? (base + "_with_bank,X")
                        : (base + "_with_bank");
                }
            }
            return "$" + hex24(v) + (op.mode == AddrMode::AbsoluteLongX ? ",X" : "");
        }
        case AddrMode::AbsoluteIndirect:
            return "($" + hex16(read16le(b[1], b[2])) + ")";
        case AddrMode::AbsoluteXIndirect:
            return "($" + hex16(read16le(b[1], b[2])) + ",X)";
        case AddrMode::AbsoluteIndirectLong:
            return "[$" + hex16(read16le(b[1], b[2])) + "]";

        case AddrMode::Relative8: {
            const uint32_t tgt = branch8(pc, b[1]);
            auto it = labels.find(tgt);
            return (it != labels.end()) ? it->second : ("$" + hex24(tgt));
        }

        case AddrMode::Relative16: {
            const uint32_t tgt = branch16(pc, b[1], b[2]);
            auto it = labels.find(tgt);
            return (it != labels.end()) ? it->second : ("$" + hex24(tgt));
        }

        case AddrMode::BlockMove:
            return "$" + hex8(b[1]) + ",$" + hex8(b[2]);

        case AddrMode::Unknown:
        default:
            if (size == 2) return "$" + hex8(b[1]);
            if (size == 3) return "$" + hex16(read16le(b[1], b[2]));
            if (size == 4) return "$" + hex24(read24le(b[1], b[2], b[3]));
            return "";
    }
}

bool isSnesHwRegister(uint32_t addr) {
    const uint16_t a = static_cast<uint16_t>(addr & 0xFFFF);

    // PPU
    if (a >= 0x2100 && a <= 0x213F) return true;

    // APU / CPU I/O / WRAM / joypad / NMI/IRQ/DMA/math
    if (a >= 0x2140 && a <= 0x2183) return true;
    if (a >= 0x4016 && a <= 0x4017) return true;
    if (a >= 0x4200 && a <= 0x421F) return true;
    if (a >= 0x4300 && a <= 0x437F) return true;

    return false;
}

std::string snesHwCategory(uint32_t addr) {
    const uint16_t a = static_cast<uint16_t>(addr & 0xFFFF);

    if (a >= 0x2100 && a <= 0x213F) return "PPU register access";
    if (a >= 0x2140 && a <= 0x217F) return "APU/CPU I/O register access";
    if (a >= 0x2180 && a <= 0x2183) return "WRAM register access";
    if (a >= 0x4016 && a <= 0x4017) return "JOY serial register access";
    if (a >= 0x4200 && a <= 0x421F) return "CPU/JOY/NMI/IRQ register access";
    if (a >= 0x4300 && a <= 0x437F) return "DMA/HDMA register access";

    return "hardware I/O access";
}

std::string snesHwRegisterName(uint16_t a) {
    // PPU
    switch (a) {
        case 0x2100: return "INIDISP";
        case 0x2101: return "OBJSEL";
        case 0x2102: return "OAMADD";
        case 0x2103: return "OAMADDH";
        case 0x2104: return "OAMDATA";
        case 0x2105: return "BGMODE";
        case 0x2106: return "MOSAIC";
        case 0x2107: return "BG1SC";
        case 0x2108: return "BG2SC";
        case 0x2109: return "BG3SC";
        case 0x210A: return "BG4SC";
        case 0x210B: return "BG12NBA";
        case 0x210C: return "BG34NBA";
        case 0x210D: return "BG1HOFS";
        case 0x210E: return "BG1VOFS";
        case 0x210F: return "BG2HOFS";
        case 0x2110: return "BG2VOFS";
        case 0x2111: return "BG3HOFS";
        case 0x2112: return "BG3VOFS";
        case 0x2113: return "BG4HOFS";
        case 0x2114: return "BG4VOFS";
        case 0x2115: return "VMAIN";
        case 0x2116: return "VMADDL";
        case 0x2117: return "VMADDH";
        case 0x2118: return "VMDATAL";
        case 0x2119: return "VMDATAH";
        case 0x211A: return "M7SEL";
        case 0x211B: return "M7A";
        case 0x211C: return "M7B";
        case 0x211D: return "M7C";
        case 0x211E: return "M7D";
        case 0x211F: return "M7X";
        case 0x2120: return "M7Y";
        case 0x2121: return "CGADD";
        case 0x2122: return "CGDATA";
        case 0x2123: return "W12SEL";
        case 0x2124: return "W34SEL";
        case 0x2125: return "WOBJSEL";
        case 0x2126: return "WH0";
        case 0x2127: return "WH1";
        case 0x2128: return "WH2";
        case 0x2129: return "WH3";
        case 0x212A: return "WBGLOG";
        case 0x212B: return "WOBJLOG";
        case 0x212C: return "TM";
        case 0x212D: return "TS";
        case 0x212E: return "TMW";
        case 0x212F: return "TSW";
        case 0x2130: return "CGWSEL";
        case 0x2131: return "CGADSUB";
        case 0x2132: return "COLDATA";
        case 0x2133: return "SETINI";
        case 0x2134: return "MPYL";
        case 0x2135: return "MPYM";
        case 0x2136: return "MPYH";
        case 0x2137: return "SLHV";
        case 0x2138: return "RDOAM";
        case 0x2139: return "RDVRAML";
        case 0x213A: return "RDVRAMH";
        case 0x213B: return "RDCGRAM";
        case 0x213C: return "OPHCT";
        case 0x213D: return "OPVCT";
        case 0x213E: return "STAT77";
        case 0x213F: return "STAT78";
        // APU
        case 0x2140: return "APUIO0";
        case 0x2141: return "APUIO1";
        case 0x2142: return "APUIO2";
        case 0x2143: return "APUIO3";
        // WRAM
        case 0x2180: return "WMDATA";
        case 0x2181: return "WMADDL";
        case 0x2182: return "WMADDM";
        case 0x2183: return "WMADDH";
        // Joypad serial
        case 0x4016: return "JOYA";
        case 0x4017: return "JOYB";
        // CPU/NMI/IRQ/Joypad
        case 0x4200: return "NMITIMEN";
        case 0x4201: return "WRIO";
        case 0x4202: return "WRMPYA";
        case 0x4203: return "WRMPYB";
        case 0x4204: return "WRDIVL";
        case 0x4205: return "WRDIVH";
        case 0x4206: return "WRDIVB";
        case 0x4207: return "HTIMEL";
        case 0x4208: return "HTIMEH";
        case 0x4209: return "VTIMEL";
        case 0x420A: return "VTIMEH";
        case 0x420B: return "MDMAEN";
        case 0x420C: return "HDMAEN";
        case 0x420D: return "MEMSEL";
        case 0x4210: return "RDNMI";
        case 0x4211: return "TIMEUP";
        case 0x4212: return "HVBJOY";
        case 0x4213: return "RDIO";
        case 0x4214: return "RDDIVL";
        case 0x4215: return "RDDIVH";
        case 0x4216: return "RDMPYL";
        case 0x4217: return "RDMPYH";
        case 0x4218: return "JOY1L";
        case 0x4219: return "JOY1H";
        case 0x421A: return "JOY2L";
        case 0x421B: return "JOY2H";
        case 0x421C: return "JOY3L";
        case 0x421D: return "JOY3H";
        case 0x421E: return "JOY4L";
        case 0x421F: return "JOY4H";
        default: break;
    }
    // DMA/HDMA channels 0-7 ($4300-$437F)
    if (a >= 0x4300 && a <= 0x437F) {
        const int ch = (a >> 4) & 0x7;
        const int reg = a & 0xF;
        std::ostringstream oss;
        switch (reg) {
            case 0x0: oss << "DMAP"  << ch; return oss.str();
            case 0x1: oss << "BBAD"  << ch; return oss.str();
            case 0x2: oss << "A1TL"  << ch; return oss.str();
            case 0x3: oss << "A1TH"  << ch; return oss.str();
            case 0x4: oss << "A1B"   << ch; return oss.str();
            case 0x5: oss << "DASL"  << ch; return oss.str();
            case 0x6: oss << "DASH"  << ch; return oss.str();
            case 0x7: oss << "DASB"  << ch; return oss.str();
            case 0x8: oss << "A2AL"  << ch; return oss.str();
            case 0x9: oss << "A2AH"  << ch; return oss.str();
            case 0xA: oss << "NTRL"  << ch; return oss.str();
            default:  oss << "DMAUNUSED" << ch << "_" << hex8(static_cast<uint8_t>(reg)); return oss.str();
        }
    }
    return "";
}

bool extractAbsoluteHwAddr(const Op& meta,
                           uint32_t pc,
                           const std::array<uint8_t, 4>& bytes,
                           uint8_t size,
                           uint32_t& outAddr) {
    switch (meta.mode) {
        case AddrMode::Absolute:
        case AddrMode::AbsoluteX:
        case AddrMode::AbsoluteY:
        case AddrMode::AbsoluteIndirect:
        case AddrMode::AbsoluteXIndirect:
        case AddrMode::AbsoluteIndirectLong: {
            if (size < 3) return false;
            const uint16_t addr16 = read16le(bytes[1], bytes[2]);
            outAddr = (pc & 0xFF0000) | addr16;
            return true;
        }

        case AddrMode::AbsoluteLong:
        case AddrMode::AbsoluteLongX: {
            if (size < 4) return false;
            outAddr = read24le(bytes[1], bytes[2], bytes[3]);
            return true;
        }

        default:
            return false;
    }
}

std::string cgramPaletteHint(uint16_t a) {
    switch (a) {
        case 0x2121:
            return " · CGRAM addr/index (CGADD)";
        case 0x2122:
            return " · palette color → CGRAM (CGDATA)";
        default:
            return "";
    }
}

std::string hardwareCommentFor(const Op& meta,
                               uint32_t pc,
                               const std::array<uint8_t, 4>& bytes,
                               uint8_t size) {
    uint32_t addr = 0;
    if (!extractAbsoluteHwAddr(meta, pc, bytes, size, addr)) return "";

    if (!isSnesHwRegister(addr)) return "";

    const uint16_t lo = static_cast<uint16_t>(addr & 0xFFFF);
    return snesHwCategory(addr) + " ($" + hex16(lo) + ")" + cgramPaletteHint(lo);
}

void emitSnesHeaderSection(std::ofstream& out,
                           const std::vector<uint8_t>& rom,
                           uint32_t baseOff,
                           const std::unordered_map<uint32_t, std::string>& labels) {
    if (baseOff + 64 > rom.size()) return;
    const uint8_t* h = rom.data() + baseOff;

    auto b = [&](int i) { return "$" + hex8(h[i]); };
    auto bpair = [&](int i) { return b(i) + "," + b(i + 1); };

    // Emit a vector entry as .dw LabelName if known, else raw .db bytes
    auto emitVector = [&](int i, const std::string& comment) {
        const uint32_t addr = static_cast<uint32_t>(h[i] | (h[i + 1] << 8));
        auto it = labels.find(addr);
        if (it != labels.end()) {
            out << "    .dw " << it->second << "  ; " << comment << "\n";
        } else {
            out << "    .db " << b(i) << "," << b(i + 1) << "  ; " << comment << "\n";
        }
    };

    out << "; ----- SNES Internal Header ($00:FFC0-$00:FFFF) -----\n";
    out << "SnesHeader:\n";

    // Title: 21 bytes at $FFC0-$FFD4
    out << "SnesHeader_Title:\n";
    out << "    .db ";
    for (int i = 0; i < 21; ++i) {
        out << b(i);
        if (i < 20) out << ",";
    }
    std::string title;
    for (int i = 0; i < 21; ++i) {
        const char c = static_cast<char>(h[i]);
        title += (c >= 0x20 && c < 0x7F) ? c : '.';
    }
    out << "  ; \"" << title << "\"\n";

    out << "SnesHeader_MapMode:\n";
    out << "    .db " << b(21) << "  ; Map Mode\n";
    out << "SnesHeader_ROMType:\n";
    out << "    .db " << b(22) << "  ; ROM Type\n";
    out << "SnesHeader_ROMSize:\n";
    out << "    .db " << b(23) << "  ; ROM Size\n";
    out << "SnesHeader_SRAMSize:\n";
    out << "    .db " << b(24) << "  ; SRAM Size\n";
    out << "SnesHeader_Country:\n";
    out << "    .db " << b(25) << "  ; Country\n";
    out << "SnesHeader_License:\n";
    out << "    .db " << b(26) << "  ; License\n";
    out << "SnesHeader_Version:\n";
    out << "    .db " << b(27) << "  ; Version\n";
    out << "SnesHeader_Complement:\n";
    out << "    .db " << bpair(28) << "  ; Complement checksum\n";
    out << "SnesHeader_Checksum:\n";
    out << "    .db " << bpair(30) << "  ; Checksum\n";

    // Native mode vectors ($FFE0-$FFEF)
    out << "; Native mode vectors ($00:FFE0-$00:FFEF)\n";
    out << "    .db " << b(32) << "," << b(33) << "," << b(34) << "," << b(35) << "  ; unused\n";
    out << "SnesVector_NativeCOP:\n";
    emitVector(36, "Native COP");
    out << "SnesVector_NativeBRK:\n";
    emitVector(38, "Native BRK");
    out << "SnesVector_NativeABORT:\n";
    emitVector(40, "Native ABORT");
    out << "SnesVector_NativeNMI:\n";
    emitVector(42, "Native NMI");
    out << "SnesVector_NativeRESET:\n";
    emitVector(44, "Native RESET (unused)");
    out << "SnesVector_NativeIRQ:\n";
    emitVector(46, "Native IRQ/BRK");

    // Emulation mode vectors ($FFF0-$FFFF)
    out << "; Emulation mode vectors ($00:FFF0-$00:FFFF)\n";
    out << "    .db " << b(48) << "," << b(49) << "," << b(50) << "," << b(51) << "  ; unused\n";
    out << "SnesVector_EmuCOP:\n";
    emitVector(52, "Emu COP");
    out << "SnesVector_EmuBRK:\n";
    emitVector(54, "Emu BRK (unused)");
    out << "SnesVector_EmuABORT:\n";
    emitVector(56, "Emu ABORT");
    out << "SnesVector_EmuNMI:\n";
    emitVector(58, "Emu NMI");
    out << "SnesVector_EmuRESET:\n";
    emitVector(60, "Emu RESET");
    out << "SnesVector_EmuIRQ:\n";
    emitVector(62, "Emu IRQ/BRK");
}

} // namespace

void dumpRomAsAsmFull(const std::vector<uint8_t>& rom,
                      uint16_t resetVector,
                      const std::string& filename,
                      const std::unordered_set<uint32_t>* coverageHits) {
    constexpr uint8_t P0 = 0x34;

    struct Work {
        uint32_t pc;
        uint8_t p;
    };

    struct Key {
        uint32_t pc;
        uint8_t p;

        bool operator<(const Key& o) const {
            return pc != o.pc ? pc < o.pc : p < o.p;
        }
    };

    std::queue<Work> q;
    std::set<Key> visited;
    std::map<uint32_t, DecodedLine> decoded;
    std::unordered_map<uint32_t, std::string> labels;
    std::set<uint32_t> functionEntries;
    std::unordered_map<uint32_t, std::vector<uint32_t>> xrefsTo;
    std::map<uint32_t, PointerTable16> pointerTables16;
    std::map<uint32_t, PointerTable24> pointerTables24;
    std::map<uint32_t, DataWords16> dataWords16Blocks;

    auto enqueueIfValid = [&](uint32_t target, uint8_t p, bool isFunction = false) {
        uint8_t dummy = 0;
        if (!romRead8(rom, target, dummy)) return;

        if (isFunction) {
            functionEntries.insert(target);
            if (!labels.count(target)) {
                labels[target] = funcLabel(target);
            }
        } else if (!labels.count(target)) {
            labels[target] = label(target);
        }

        q.push({target, p});
    };

    auto seedPointerTablesForBank16 = [&](uint8_t bank) {
        const uint32_t startOff = loRomToOffset(bank, 0x8000);
        const uint32_t endOff = std::min<uint32_t>(loRomToOffset(bank, 0xFFFF) + 1,
                                                   static_cast<uint32_t>(rom.size()));

        for (uint32_t off = startOff; off + 7 < endOff; off += 2) {
            const uint16_t addr = static_cast<uint16_t>(0x8000 + (off - startOff));
            if (addr >= 0xFFE0) continue;

            uint32_t t0 = 0, t1 = 0, t2 = 0, t3 = 0;
            if (!isRomPointer16(rom, bank, addr, t0)) continue;
            if (!isRomPointer16(rom, bank, static_cast<uint16_t>(addr + 2), t1)) continue;
            if (!isRomPointer16(rom, bank, static_cast<uint16_t>(addr + 4), t2)) continue;
            if (!isRomPointer16(rom, bank, static_cast<uint16_t>(addr + 6), t3)) continue;

            if (!isPlausibleFunctionEntry(rom, t0)) continue;
            if (!isPlausibleFunctionEntry(rom, t1)) continue;
            if (!isPlausibleFunctionEntry(rom, t2)) continue;
            if (!isPlausibleFunctionEntry(rom, t3)) continue;

            auto closePair = [](uint32_t a, uint32_t b) {
                return a > b ? (a - b) < 4 : (b - a) < 4;
            };
            if (closePair(t0, t1) || closePair(t1, t2) || closePair(t2, t3)) continue;

            std::vector<uint32_t> tableTargets{t0, t1, t2, t3};

            for (int i = 4; i < 12; ++i) {
                uint32_t target = 0;
                const uint16_t entryAddr = static_cast<uint16_t>(addr + i * 2);
                if (!isRomPointer16(rom, bank, entryAddr, target)) break;
                if (!isPlausibleFunctionEntry(rom, target)) break;
                tableTargets.push_back(target);
            }

            const uint32_t tablePc = (static_cast<uint32_t>(bank) << 16) | addr;
            pointerTables16[tablePc] = PointerTable16{tablePc, tableTargets};

            for (uint32_t target : tableTargets) {
                xrefsTo[target].push_back(tablePc);
                enqueueIfValid(target, P0, true);
            }
        }
    };

    auto seedPointerTablesForBank24 = [&](uint8_t bank) {
        const uint32_t startOff = loRomToOffset(bank, 0x8000);
        const uint32_t endOff = std::min<uint32_t>(loRomToOffset(bank, 0xFFFF) + 1,
                                                   static_cast<uint32_t>(rom.size()));

        for (uint32_t off = startOff; off + 11 < endOff; off += 3) {
            const uint16_t addr = static_cast<uint16_t>(0x8000 + (off - startOff));
            if (addr >= 0xFFE0) continue;

            const uint32_t pc0 = (static_cast<uint32_t>(bank) << 16) | addr;

            uint32_t t0 = 0, t1 = 0, t2 = 0, t3 = 0;
            if (!isRomPointer24AtPc(rom, pc0, t0)) continue;
            if (!isRomPointer24AtPc(rom, pc0 + 3, t1)) continue;
            if (!isRomPointer24AtPc(rom, pc0 + 6, t2)) continue;
            if (!isRomPointer24AtPc(rom, pc0 + 9, t3)) continue;

            if (!isPlausibleFunctionEntry(rom, t0)) continue;
            if (!isPlausibleFunctionEntry(rom, t1)) continue;
            if (!isPlausibleFunctionEntry(rom, t2)) continue;
            if (!isPlausibleFunctionEntry(rom, t3)) continue;

            auto closePair = [](uint32_t a, uint32_t b) {
                return a > b ? (a - b) < 4 : (b - a) < 4;
            };
            if (closePair(t0, t1) || closePair(t1, t2) || closePair(t2, t3)) continue;

            std::vector<uint32_t> tableTargets{t0, t1, t2, t3};

            for (int i = 4; i < 10; ++i) {
                uint32_t target = 0;
                const uint32_t entryPc = pc0 + static_cast<uint32_t>(i * 3);
                if (!isRomPointer24AtPc(rom, entryPc, target)) break;
                if (!isPlausibleFunctionEntry(rom, target)) break;
                tableTargets.push_back(target);
            }

            pointerTables24[pc0] = PointerTable24{pc0, tableTargets};

            for (uint32_t target : tableTargets) {
                xrefsTo[target].push_back(pc0);
                enqueueIfValid(target, P0, true);
            }
        }
    };

    const uint32_t romBankCount =
        static_cast<uint32_t>((rom.size() + 0x7FFFu) >> 15);
    for (uint32_t bi = 0; bi < romBankCount; ++bi) {
        seedPointerTablesForBank16(static_cast<uint8_t>(bi));
        seedPointerTablesForBank24(static_cast<uint8_t>(bi));
    }

    auto seedVectorAtOffset = [&](uint32_t off) {
        if (off + 1 >= rom.size()) return;
        const uint16_t vec = read16le(rom[off], rom[off + 1]);
        if (vec < 0x8000) return;
        enqueueIfValid(static_cast<uint32_t>(vec), P0, true);
    };

    std::vector<uint32_t> earlyInitCalls;

    auto collectEarlyInitCallsFromReset = [&](uint32_t startPc, int maxInstructions = 64, int maxCalls = 8) {
        uint32_t pc = startPc;
        uint8_t p = P0;

        for (int i = 0; i < maxInstructions && static_cast<int>(earlyInitCalls.size()) < maxCalls; ++i) {
            uint8_t op = 0;
            if (!romRead8(rom, pc, op)) break;

            const Op& meta = OPCODES[op];
            uint8_t size = instrSize(meta, p);
            if (size == 0 || size > 4) break;

            std::array<uint8_t, 4> bytes{};
            if (!romReadBytes(rom, pc, bytes.data(), size)) break;

            if (op == 0x20 || op == 0x22) {
                const uint32_t tgt = (op == 0x20)
                    ? ((pc & 0xFF0000) | read16le(bytes[1], bytes[2]))
                    : read24le(bytes[1], bytes[2], bytes[3]);

                if (std::find(earlyInitCalls.begin(), earlyInitCalls.end(), tgt) == earlyInitCalls.end()) {
                    earlyInitCalls.push_back(tgt);
                }
            }

            if (op == 0xC2 && size >= 2) p &= static_cast<uint8_t>(~bytes[1]);
            if (op == 0xE2 && size >= 2) p |= bytes[1];

            if (isReturn(op) || isStop(op)) break;
            if (op == 0x4C || op == 0x5C || op == 0x6C || op == 0x7C) break;
            if (!isCondBranch(op) && isBranch(op)) break;

            pc += size;
        }
    };

    const uint32_t entry = 0x008000u | resetVector;
    enqueueIfValid(entry, P0, true);

    uint32_t resetEntry = entry;
    uint32_t nativeNMI = 0;
    uint32_t nativeIRQ = 0;
    uint32_t emuNMI = 0;
    uint32_t emuIRQ = 0;

    auto readVectorPc = [&](uint32_t off, uint32_t& outPc) {
        outPc = 0;
        if (off + 1 >= rom.size()) return;
        const uint16_t vec = read16le(rom[off], rom[off + 1]);
        if (vec == 0x0000) return;
        outPc = static_cast<uint32_t>(vec);
    };

    readVectorPc(0x7FEA, nativeNMI);
    readVectorPc(0x7FEE, nativeIRQ);
    readVectorPc(0x7FFA, emuNMI);
    readVectorPc(0x7FFE, emuIRQ);

    seedVectorAtOffset(0x7FE4);
    seedVectorAtOffset(0x7FE6);
    seedVectorAtOffset(0x7FE8);
    seedVectorAtOffset(0x7FEA);
    seedVectorAtOffset(0x7FEE);

    seedVectorAtOffset(0x7FF4);
    seedVectorAtOffset(0x7FF8);
    seedVectorAtOffset(0x7FFA);
    seedVectorAtOffset(0x7FFC);
    seedVectorAtOffset(0x7FFE);

    while (!q.empty()) {
        const auto [startPc, startP] = q.front();
        q.pop();

        uint32_t pc = startPc;
        uint8_t p = startP;

        while (true) {
            const Key k{pc, p};
            if (visited.count(k)) break;
            visited.insert(k);

            uint8_t op = 0;
            if (!romRead8(rom, pc, op)) break;

            const Op& meta = OPCODES[op];
            DecodedLine line;
            line.pc = pc;
            line.name = meta.valid ? meta.name : ".db";
            line.size = instrSize(meta, p);

            if (line.size == 0 || line.size > 4) line.size = 1;
            if (!romReadBytes(rom, pc, line.bytes.data(), line.size)) break;

            uint8_t nextP = p;
            if (op == 0xC2 && line.size >= 2) nextP &= static_cast<uint8_t>(~line.bytes[1]);
            if (op == 0xE2 && line.size >= 2) nextP |= line.bytes[1];

            const uint8_t b1 = line.bytes[1];
            const uint8_t b2 = line.bytes[2];
            const uint8_t b3 = line.bytes[3];

            auto addXref = [&](uint32_t target) {
                uint8_t dummy = 0;
                if (!romRead8(rom, target, dummy)) return;
                xrefsTo[target].push_back(pc);
            };

            if (isBranch(op)) {
                const uint32_t tgt = (op == 0x82) ? branch16(pc, b1, b2) : branch8(pc, b1);
                if (!labels.count(tgt)) labels[tgt] = label(tgt);
                addXref(tgt);
            } else if (isJsr(op)) {
                const uint32_t tgt = (op == 0x20)
                    ? ((pc & 0xFF0000) | read16le(b1, b2))
                    : read24le(b1, b2, b3);
                functionEntries.insert(tgt);
                labels[tgt] = funcLabel(tgt);
                addXref(tgt);
            } else if (op == 0x4C || op == 0x5C) {
                const uint32_t tgt = (op == 0x4C)
                    ? ((pc & 0xFF0000) | read16le(b1, b2))
                    : read24le(b1, b2, b3);
                if (!labels.count(tgt)) labels[tgt] = label(tgt);
                addXref(tgt);
            } else if (op == 0x6C) {
                const uint16_t ptrLoc = read16le(b1, b2);
                const uint32_t ptrPc = (pc & 0xFF0000u) | ptrLoc;
                uint8_t lo = 0, hi = 0;
                if (romRead8(rom, ptrPc, lo) && romRead8(rom, ptrPc + 1, hi)) {
                    const uint32_t tgt = (pc & 0xFF0000u) | read16le(lo, hi);
                    if (!labels.count(tgt)) labels[tgt] = label(tgt);
                    addXref(tgt);
                }
            } else if (op == 0xDC) {
                const uint16_t ptrLoc = read16le(b1, b2);
                const uint32_t ptrPc = (pc & 0xFF0000u) | ptrLoc;
                uint8_t v0 = 0, v1 = 0, v2 = 0;
                if (romRead8(rom, ptrPc, v0) && romRead8(rom, ptrPc + 1, v1) &&
                    romRead8(rom, ptrPc + 2, v2)) {
                    const uint32_t tgt = read24le(v0, v1, v2);
                    if (!labels.count(tgt)) labels[tgt] = label(tgt);
                    addXref(tgt);
                }
            } else if (op == 0x7C) {
                const uint16_t tableAddr = read16le(b1, b2);
                const uint8_t bank = static_cast<uint8_t>(pc >> 16);

                for (int i = 0; i < 32; ++i) {
                    uint32_t tgt = 0;
                    const uint16_t entryAddr = static_cast<uint16_t>(tableAddr + i * 2);
                    if (!isRomPointer16(rom, bank, entryAddr, tgt)) break;
                    if (!labels.count(tgt)) labels[tgt] = label(tgt);
                    addXref(tgt);
                }
            }

            line.operand = operandStr(meta, pc, line.bytes.data(), line.size, labels);
            line.comment = hardwareCommentFor(meta, pc, line.bytes, line.size);
            decoded[pc] = line;

            if (isBranch(op)) {
                const uint32_t tgt = (op == 0x82) ? branch16(pc, b1, b2) : branch8(pc, b1);
                enqueueIfValid(tgt, nextP);

                if (isCondBranch(op)) {
                    pc += line.size;
                    p = nextP;
                    continue;
                }
                break;
            }

            if (isJsr(op)) {
                const uint32_t tgt = (op == 0x20)
                    ? ((pc & 0xFF0000) | read16le(b1, b2))
                    : read24le(b1, b2, b3);
                enqueueIfValid(tgt, nextP, true);
                pc += line.size;
                p = nextP;
                continue;
            }

            if (op == 0x4C || op == 0x5C) {
                const uint32_t tgt = (op == 0x4C)
                    ? ((pc & 0xFF0000) | read16le(b1, b2))
                    : read24le(b1, b2, b3);
                enqueueIfValid(tgt, nextP);
                break;
            }

            if (op == 0x6C) {
                const uint16_t ptrLoc = read16le(b1, b2);
                const uint32_t ptrPc = (pc & 0xFF0000u) | ptrLoc;
                uint8_t lo = 0, hi = 0;
                if (romRead8(rom, ptrPc, lo) && romRead8(rom, ptrPc + 1, hi)) {
                    const uint32_t tgt = (pc & 0xFF0000u) | read16le(lo, hi);
                    enqueueIfValid(tgt, nextP);
                }
                break;
            }

            if (op == 0xDC) {
                const uint16_t ptrLoc = read16le(b1, b2);
                const uint32_t ptrPc = (pc & 0xFF0000u) | ptrLoc;
                uint8_t v0 = 0, v1 = 0, v2 = 0;
                if (romRead8(rom, ptrPc, v0) && romRead8(rom, ptrPc + 1, v1) &&
                    romRead8(rom, ptrPc + 2, v2)) {
                    const uint32_t tgt = read24le(v0, v1, v2);
                    enqueueIfValid(tgt, nextP);
                }
                break;
            }

            if (op == 0x7C) {
                const uint16_t tableAddr = read16le(b1, b2);
                const uint8_t bank = static_cast<uint8_t>(pc >> 16);

                enqueueJumpTable16SameBank(rom, bank, tableAddr, nextP,
                    [&](uint32_t target, uint8_t newP) {
                        enqueueIfValid(target, newP);
                    });

                break;
            }

            if (isReturn(op) || isStop(op)) break;

            pc += line.size;
            p = nextP;
        }
    }

    collectEarlyInitCallsFromReset(resetEntry);

    for (auto& [target, refs] : xrefsTo) {
        std::sort(refs.begin(), refs.end());
        refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
    }

    auto forceFunctionName = [&](uint32_t addr, const std::string& name) {
        if (!addr) return;
        labels[addr] = name;
        functionEntries.insert(addr);
    };

    forceFunctionName(resetEntry, "ResetHandler");
    forceFunctionName(nativeNMI, "NativeNMIHandler");
    forceFunctionName(nativeIRQ, "NativeIRQHandler");
    forceFunctionName(emuNMI, "EmuNMIHandler");
    forceFunctionName(emuIRQ, "EmuIRQBRKHandler");

    std::vector<std::pair<std::string, uint32_t>> earlyInitRomEqus;

    for (size_t i = 0; i < earlyInitCalls.size(); ++i) {
        const uint32_t addr = earlyInitCalls[i];
        if (!addr) continue;

        if (addr == resetEntry || addr == nativeNMI || addr == nativeIRQ ||
            addr == emuNMI || addr == emuIRQ) {
            continue;
        }

        if (labels.count(addr) && !labels[addr].starts_with("Func_")) {
            continue;
        }

        const std::string initName = earlyInitLabel(static_cast<int>(i + 1));
        forceFunctionName(addr, initName);
        earlyInitRomEqus.emplace_back(initName, addr);
    }

    auto collectDataWordBlocks = [&]() {
        const uint32_t bankCount = static_cast<uint32_t>((rom.size() + 0x7FFFu) >> 15);

        for (uint32_t bank = 0; bank < bankCount; ++bank) {
            const uint32_t bankStartOff = bank << 15;
            const uint32_t bankEndOff =
                std::min<uint32_t>(bankStartOff + 0x8000u, static_cast<uint32_t>(rom.size()));

            uint32_t off = bankStartOff;
            while (off + 7 < bankEndOff) {
                const uint32_t pc = offsetToLoRom(off);

                if (decoded.count(pc) || labels.count(pc) ||
                    pointerTables16.count(pc) || pointerTables24.count(pc)) {
                    ++off;
                    continue;
                }

                const uint8_t bank8 = static_cast<uint8_t>(pc >> 16);
                const uint16_t addr = static_cast<uint16_t>(pc & 0xFFFF);

                std::vector<uint16_t> values;
                if (!collectDataWords16Block(rom, bank8, addr, values, 16)) {
                    ++off;
                    continue;
                }

                dataWords16Blocks[pc] = DataWords16{pc, values};
                off += static_cast<uint32_t>(values.size() * 2);
            }
        }
    };

    collectDataWordBlocks();

    // Add hardware register labels for accessed addresses
    for (const auto& [pc, line] : decoded) {
        const Op& meta = OPCODES[line.bytes[0]];
        uint32_t hwAddr = 0;
        if (!extractAbsoluteHwAddr(meta, pc, line.bytes, line.size, hwAddr)) continue;
        const uint16_t a = static_cast<uint16_t>(hwAddr & 0xFFFF);
        const std::string name = snesHwRegisterName(a);
        if (name.empty()) continue;
        const uint32_t key = static_cast<uint32_t>(a);
        if (!labels.count(key)) labels[key] = name;
    }

    // Recompute operand strings now that hardware register labels are in the map
    for (auto& [pc, line] : decoded) {
        if (!OPCODES[line.bytes[0]].valid) continue;
        const Op& meta = OPCODES[line.bytes[0]];
        line.operand = operandStr(meta, pc, line.bytes.data(), line.size, labels);
    }

    std::ofstream out(filename);
    if (!out) return;

    // Collect labels pointing to non-ROM addresses (RAM, hardware regs) → emit as equ
    std::vector<std::pair<std::string, uint32_t>> equLabels;
    for (const auto& [addr, name] : labels) {
        const uint8_t lbank = static_cast<uint8_t>(addr >> 16);
        const uint16_t addr16 = static_cast<uint16_t>(addr & 0xFFFF);
        if (!isLoRomArea(lbank, addr16)) {
            equLabels.emplace_back(name, addr);
        }
    }
    std::sort(equLabels.begin(), equLabels.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    auto earlyEq = earlyInitRomEqus;
    std::sort(earlyEq.begin(), earlyEq.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    std::vector<std::pair<std::string, uint32_t>> ppuWithBankEqus;
    for (uint32_t lo = 0x2100; lo <= 0x213F; ++lo) {
        const uint16_t a = static_cast<uint16_t>(lo);
        const std::string base = snesHwRegisterName(a);
        if (base.empty()) continue;
        ppuWithBankEqus.emplace_back(base + "_with_bank", static_cast<uint32_t>(a));
    }
    std::sort(ppuWithBankEqus.begin(), ppuWithBankEqus.end(),
              [](const auto& x, const auto& y) { return x.second < y.second; });

    if (!equLabels.empty() || !earlyEq.empty() || !ppuWithBankEqus.empty()) {
        out << "; ===== Equates =====\n\n";
        for (const auto& [name, addr] : equLabels) {
            out << name << " equ $";
            // ROM labels like $08:2308 must keep the bank byte; truncating to $2308 breaks JSL/JML.
            if ((addr & 0xFF0000u) != 0u) {
                out << hex24(addr);
            } else {
                out << hex16(static_cast<uint16_t>(addr & 0xFFFF));
            }
            out << "\n";
        }
        for (const auto& [name, addr] : earlyEq) {
            out << name << " equ $" << hex24(addr) << "\n";
        }
        if (!ppuWithBankEqus.empty()) {
            out << "\n; PPU regs as $00:21xx (absolute-long addressing)\n";
            for (const auto& [name, addr] : ppuWithBankEqus) {
                out << name << " equ $" << hex24(addr) << "\n";
            }
        }
        out << "\n";
    }

    const uint32_t bankCount = static_cast<uint32_t>((rom.size() + 0x7FFFu) >> 15);
    for (uint32_t bank = 0; bank < bankCount; ++bank) {
        const uint32_t bankStartOff = bank << 15;
        const uint32_t bankEndOff =
            std::min<uint32_t>(bankStartOff + 0x8000u, static_cast<uint32_t>(rom.size()));

        out << "; ===== BANK $" << hex8(static_cast<uint8_t>(bank)) << " =====\n\n";

        uint32_t off = bankStartOff;
        while (off < bankEndOff) {
            const uint32_t pc = offsetToLoRom(off);

            // SNES internal header region ($00:FFC0-$00:FFFF)
            if (pc == 0x00FFC0 && !decoded.count(pc) && off + 64 <= rom.size()) {
                emitSnesHeaderSection(out, rom, off, labels);
                off += 64;
                continue;
            }

            auto pt16 = pointerTables16.find(pc);
            if (pt16 != pointerTables16.end()) {
                out << ptrTableLabel(pc) << ":\n";
                out << "    .dw ";

                for (size_t i = 0; i < pt16->second.targets.size(); ++i) {
                    const uint32_t target = pt16->second.targets[i];
                    auto fit = labels.find(target);
                    if (fit != labels.end()) out << fit->second;
                    else out << "$" << hex16(static_cast<uint16_t>(target & 0xFFFF));
                    if (i + 1 < pt16->second.targets.size()) out << ", ";
                }
                out << "\n";

                off += static_cast<uint32_t>(pt16->second.targets.size() * 2);
                continue;
            }

            auto pt24 = pointerTables24.find(pc);
            if (pt24 != pointerTables24.end()) {
                out << ptrTable24Label(pc) << ":\n";
                out << "    .dl ";

                for (size_t i = 0; i < pt24->second.targets.size(); ++i) {
                    const uint32_t target = pt24->second.targets[i];
                    auto fit = labels.find(target);
                    if (fit != labels.end()) out << fit->second;
                    else out << "$" << hex24(target);
                    if (i + 1 < pt24->second.targets.size()) out << ", ";
                }
                out << "\n";

                off += static_cast<uint32_t>(pt24->second.targets.size() * 3);
                continue;
            }

            auto dwb = dataWords16Blocks.find(pc);
            if (dwb != dataWords16Blocks.end()) {
                out << dataWords16Label(pc) << ":\n";
                out << "    .dw ";

                for (size_t i = 0; i < dwb->second.values.size(); ++i) {
                    const uint16_t value = dwb->second.values[i];
                    const uint32_t target = (pc & 0xFF0000) | value;

                    auto fit = labels.find(target);
                    if (fit != labels.end()) out << fit->second;
                    else out << "$" << hex16(value);

                    if (i + 1 < dwb->second.values.size()) {
                        out << ", ";
                    }
                }
                out << "\n";

                off += static_cast<uint32_t>(dwb->second.values.size() * 2);
                continue;
            }

            auto lbl = labels.find(pc);
            if (lbl != labels.end()) {
                const bool isFunc = functionEntries.count(pc) != 0;
                auto xr = xrefsTo.find(pc);

                if (isFunc) {
                    out << "; ==================================================\n";
                    out << "; Function " << lbl->second << "\n";

                    if (xr != xrefsTo.end() && !xr->second.empty()) {
                        bool hasTableSeed = false;
                        for (uint32_t from : xr->second) {
                            if ((from >> 16) == 0x00 && (from & 0xFFFF) >= 0x8000) {
                                hasTableSeed = true;
                                break;
                            }
                        }

                        if (hasTableSeed) {
                            out << "; inferred from pointer table seed\n";
                        }

                        out << "; xrefs: ";
                        for (size_t i = 0; i < xr->second.size(); ++i) {
                            const uint32_t from = xr->second[i];
                            auto from16 = pointerTables16.find(from);
                            auto from24 = pointerTables24.find(from);

                            if (from16 != pointerTables16.end()) {
                                out << ptrTableLabel(from);
                            } else if (from24 != pointerTables24.end()) {
                                out << ptrTable24Label(from);
                            } else {
                                auto fit = labels.find(from);
                                if (fit != labels.end()) out << fit->second;
                                else out << label(from);
                            }

                            if (i + 1 < xr->second.size()) out << ", ";
                        }
                        out << "\n";
                    }

                    out << "; ==================================================\n";
                } else {
                    if (xr != xrefsTo.end() && !xr->second.empty()) {
                        out << "; xrefs: ";
                        for (size_t i = 0; i < xr->second.size(); ++i) {
                            const uint32_t from = xr->second[i];
                            auto from16 = pointerTables16.find(from);
                            auto from24 = pointerTables24.find(from);

                            if (from16 != pointerTables16.end()) {
                                out << ptrTableLabel(from);
                            } else if (from24 != pointerTables24.end()) {
                                out << ptrTable24Label(from);
                            } else {
                                auto fit = labels.find(from);
                                if (fit != labels.end()) out << fit->second;
                                else out << label(from);
                            }

                            if (i + 1 < xr->second.size()) out << ", ";
                        }
                        out << "\n";
                    }
                }

                out << lbl->second << ":\n";
            }

            auto it = decoded.find(pc);
            if (it != decoded.end()) {
                const auto& l = it->second;
            
                if (!l.comment.empty()) {
                    out << "    ; " << l.comment << "\n";
                }
            
                out << "    " << l.name;
                if (!l.operand.empty()) out << " " << l.operand;
                if (coverageHits && coverageHits->count(pc)) out << "  ; cov";
                out << "\n";
                off += l.size;
                continue;
            }

            out << "    .db ";
            int count = 0;
            while (count < 16 && off < bankEndOff) {
                const uint32_t curPc = offsetToLoRom(off);
                if (curPc == 0x00FFC0) break;
                if (count > 0 && (labels.count(curPc) || decoded.count(curPc)
                                  || pointerTables16.count(curPc) || pointerTables24.count(curPc)
                                  || dataWords16Blocks.count(curPc))) {
                    break;
                }

                out << "$" << hex8(rom[off]);
                ++off;
                ++count;

                if (count < 16 && off < bankEndOff) out << ",";
            }
            out << "\n";
        }

        out << "\n";
    }
}
