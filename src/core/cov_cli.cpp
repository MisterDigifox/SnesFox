#include "cov_cli.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "bus.hpp"
#include "cli_common.hpp"
#include "cpu.hpp"
#include "header.hpp"
#include "rom.hpp"
#include "rom_cli_helpers.hpp"

namespace {
constexpr uint64_t COV_MAX_STEPS = 20000000ull;
} // namespace

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

        while ((cpu.cycles() - frameStartCycles) < Bus::kCyclesPerFrame) {
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
