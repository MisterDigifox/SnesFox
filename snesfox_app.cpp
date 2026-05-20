#include "snesfox_app.hpp"

#include <algorithm>
#include <cctype>
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
#include "disasm_dump.hpp"
#include "header.hpp"
#include "opcodes.hpp"
#include "reasm.hpp"
#include "rom.hpp"

namespace {

constexpr uint64_t CYCLES_PER_FRAME = Bus::kCyclesPerFrame;

uint16_t sampleJoy1() {
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
constexpr int LOG_SIZE = 4;

// Total lines fed to Display in pause mode — must stay in sync with makeDebugLines(..., paused=true).
inline std::size_t pausedEmuPanelLineCount(std::size_t romHeaderLines) {
    constexpr std::size_t kTrailingPausedStructureLines =
        15   // CPU Debug (=== CPU through Cycles)
        + 2  // spacer + === PPU State ===
        + 5  // VRam summary, SC/HOFS/VOFS/VMADDR, CHR@, TM@
        + 5  // PAL0 header + 4 palette rows
        + 2  // spacer + === Instruction Log ===
        + static_cast<std::size_t>(LOG_SIZE);
    constexpr std::size_t kSpacerAfterRomHeader = 1;
    return romHeaderLines + kSpacerAfterRomHeader + kTrailingPausedStructureLines;
}

constexpr uint64_t DEFAULT_COV_FRAMES = 600;
constexpr uint64_t COV_MAX_STEPS = 20000000ull;

inline void advanceCpuScheduling(Bus& bus, CPU& cpu, bool updateJoyOnNmi) {
    const bool nmi = bus.stepPeripherals(cpu.cycles());
    if (nmi) {
        if (updateJoyOnNmi) {
            bus.setJoy1(sampleJoy1());
        }
        cpu.triggerNmi(bus);
    }
    if (bus.takePendingIrq()) {
        cpu.triggerIrq(bus);
    }
    bus.syncWaiAfterVblankEdge(cpu);
}

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

std::vector<std::string> makeDebugLines(
    const std::vector<std::string>& headerLines,
    const CPU& cpu,
    const Ppu& ppu,
    const std::deque<std::string>& instructionLog,
    bool paused
) {
    std::vector<std::string> lines = headerLines;
    lines.push_back("");
    if (!paused) {
        lines.push_back("=== CPU (running) ===");
        lines.push_back(std::string("PC ") + hex24(cpu.pc24()) + "  " + cpu.instruction());
        lines.push_back("cycles " + std::to_string(cpu.cycles()));
        lines.push_back("Enter: pause · Space: step (when paused)");
        return lines;
    }

    lines.push_back("=== CPU Debug ===");
    lines.push_back(std::string("State        : ") + (paused ? "PAUSED" : "RUNNING"));
    lines.push_back("Reset Vector : " + hex16(cpu.resetVector()));
    lines.push_back("Bank         : " + hex8(cpu.bank()));
    lines.push_back("PC           : " + hex16(cpu.pc()));
    lines.push_back("PC24         : " + hex24(cpu.pc24()));
    lines.push_back("Opcode       : " + hex8(cpu.opcode()));
    lines.push_back("Instruction  : " + cpu.instruction());
    lines.push_back("P            : " + hex8(cpu.p()));
    lines.push_back(std::string("M/X          : ") + (cpu.flagM() ? "M=8" : "M=16") + "  "
                    + (cpu.flagX() ? "X=8" : "X=16"));
    lines.push_back("A            : " + hex16(cpu.a()));
    lines.push_back("X            : " + hex16(cpu.x()));
    lines.push_back("Y            : " + hex16(cpu.y()));
    lines.push_back("SP           : " + hex16(cpu.sp()));
    lines.push_back("Cycles       : " + std::to_string(cpu.cycles()));

    lines.push_back("");
    lines.push_back("=== PPU State ===");
    {
        const uint16_t chrBase = static_cast<uint16_t>((ppu.bgNBA12() & 0x0F) * 0x1000);
        const uint16_t tmBase = static_cast<uint16_t>((ppu.bgSC(0) >> 2) * 0x400);
        const uint16_t* vr = ppu.vram();
        {
            std::ostringstream oss;
            oss << "VWr:" << std::dec << ppu.vramWrites() << " FB:" << (ppu.forcedBlank() ? "ON" : "off")
                << " M:" << static_cast<int>(ppu.bgMode()) << " TM:" << std::uppercase << std::hex
                << std::setw(2) << std::setfill('0') << static_cast<int>(ppu.tm());
            lines.push_back(oss.str());
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
            lines.push_back(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "VMADD:" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
                << ppu.vramAddr();
            lines.push_back(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "CHR@" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << chrBase << ":";
            for (int i = 0; i < 4; ++i)
                oss << " " << std::setw(4) << vr[(chrBase + i) & 0x7FFF];
            lines.push_back(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "TM@" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << tmBase << ":";
            for (int i = 0; i < 4; ++i)
                oss << " " << std::setw(4) << vr[(tmBase + i) & 0x7FFF];
            lines.push_back(oss.str());
        }
    }
    lines.push_back("PAL0(idx:BGR):");
    for (int i = 0; i < 16; i += 4) {
        std::ostringstream oss;
        for (int j = 0; j < 4; ++j) {
            oss << "[" << std::dec << (i + j) << "]" << std::uppercase << std::hex << std::setw(4)
                << std::setfill('0') << ppu.cgram()[i + j] << " ";
        }
        lines.push_back(oss.str());
    }

    lines.push_back("");
    lines.push_back("=== Instruction Log ===");
    for (const auto& line : instructionLog) {
        lines.push_back(line);
    }
    return lines;
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

    for (uint64_t f = 0; f < frames; ++f) {
        const uint64_t frameStartCycles = cpu.cycles();

        while ((cpu.cycles() - frameStartCycles) < CYCLES_PER_FRAME) {
            cpu.step(bus);
            advanceCpuScheduling(bus, cpu, false);
        }
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

    std::cerr << "  heuristic: nonzero map words=" << tmNonzero << "  tile-priority(bit13) entries=" << tmPri13
              << "  nonzero CHR peek words=" << chrNz << '\n';

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

int runEmu(const std::string& romPath) {
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

    printMissingCpuOpcodes(cpuOpcodesTable);

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

    Display display("snesfox");
    display.setFixedPanelLineCount(pausedEmuPanelLineCount(headerLines.size()));

    bool running = true;
    while (running) {
        DebugAction action = DebugAction::None;
        running = display.processEvents(action);

        if (action == DebugAction::TogglePause) {
            paused = !paused;
        }
        if (action == DebugAction::StepOne && paused) {
            stepOnce = true;
        }

        if (!paused) {
            const uint64_t frameStartCycles = cpu.cycles();

            while ((cpu.cycles() - frameStartCycles) < CYCLES_PER_FRAME) {
                cpu.step(bus);
                advanceCpuScheduling(bus, cpu, true);
            }
        } else if (stepOnce) {
            stepOnce = false;

            const uint32_t pcBefore = cpu.pc24();
            cpu.step(bus);
            advanceCpuScheduling(bus, cpu, true);

            if (!instructionLog.empty() && instructionLog.front().rfind("> ", 0) == 0) {
                instructionLog.front().replace(0, 2, "  ");
            }

            instructionLog.push_front(formatDisasmLine(pcBefore, cpu, true));
            if (instructionLog.size() > LOG_SIZE) {
                instructionLog.pop_back();
            }
        }

        const auto lines = makeDebugLines(headerLines, cpu, bus.ppu(), instructionLog, paused);
        display.presentWithFrame(bus.ppu().framebuffer(), lines);
    }

    return 0;
}

void printUsage() {
    std::cerr << "Usage:\n";
    std::cerr << "  ./snesfox emu <rom.sfc>\n";
    std::cerr << "  ./snesfox snap <rom.sfc> [frames]   # dump PPU/VRAM heuristics (no SDL)\n";
    std::cerr << "  ./snesfox header <rom.sfc>\n";
    std::cerr << "  ./snesfox cov <rom.sfc> <coverage.out> [frames]\n";
    std::cerr << "  ./snesfox disasm <rom.sfc> [output.asm [coverage.out]]\n";
    std::cerr << "  ./snesfox reasm <input.asm> [output.sfc]\n";
}

} // namespace

int SnesFoxApp::run(int argc, char** argv) {
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
