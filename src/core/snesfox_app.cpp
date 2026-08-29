#include "snesfox_app.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>

#include "cov_cli.hpp"
#include "disasm_cli.hpp"
#include "emu_cli.hpp"
#include "header_cli.hpp"
#include "ppu_snap_cli.hpp"
#include "tests/cpu_test.hpp"
#include "tests/ppu_test.hpp"
#include "tests/sdsp_test.hpp"

namespace {

constexpr uint64_t DEFAULT_COV_FRAMES = 600;

void printUsage() {
    std::cerr << "Usage:\n";
    std::cerr << "  ./snesfox selftest                  # PPU register regression tests (no ROM)\n";
    std::cerr << "  ./snesfox [rom.sfc]                 # bare game-only window, no toolbar/panels; omit rom.sfc for an empty screen with no way to load one\n";
    std::cerr << "  ./snesfox --debug [rom.sfc] [--log-cpu]  # full debug UI (toolbar, panels, Load button); --log-cpu dumps every executed CPU instruction to cpu.asm\n";
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

    static const std::unordered_set<std::string> kSubcommands = {
        "snap", "header", "cov", "disasm", "reasm"
    };

    const bool debugUi = argc >= 2 && std::string(argv[1]) == "--debug";

    // `./snesfox --debug [rom.sfc] [--log-cpu]` opens the full debug UI (side
    // panels, toolbar, Load button). Anything else that isn't a recognized subcommand opens
    // a bare game-only window instead: `./snesfox` alone shows an empty screen with no way
    // to load a ROM (no toolbar to click Load from — use --debug for that), while
    // `./snesfox game.sfc` loads straight into it.
    if (debugUi || argc < 2 || kSubcommands.count(argv[1]) == 0) {
        const std::string romPath = debugUi ? ((argc >= 3) ? argv[2] : "")
                                             : ((argc >= 2) ? argv[1] : "");
        const int flagsStart = debugUi ? ((argc >= 3) ? 3 : 2) : 2;
        bool writeTrace = false;
        for (int i = flagsStart; i < argc; ++i) {
            if (std::string(argv[i]) == "--log-cpu") writeTrace = true;
        }
        return runEmu(romPath, writeTrace, debugUi);
    }

    const std::string mode = argv[1];

    if (argc < 3) {
        printUsage();
        return 1;
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
