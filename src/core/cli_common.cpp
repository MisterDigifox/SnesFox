#include "cli_common.hpp"

#include <iomanip>
#include <sstream>

#include "bus.hpp"
#include "cpu.hpp"
#include "input.hpp"

void advanceCpuScheduling(Bus& bus, CPU& cpu, bool updateJoyOnNmi, bool suppressJoypad) {
    const bool nmi = bus.stepPeripherals(cpu.cycles(), cpu.fineCycles());
    // Auto-joypad-read latches every real VBlank on real hardware, independent of whether NMI
    // itself is enabled ($4200 bit7) — so this must not be gated behind `nmi` (which is). See
    // Bus::consumeVblankLatch()'s doc comment: without this split, a ROM running an
    // interrupt-masked polling loop across VBlank (GSU titles routinely do, per this codebase's
    // own GSU debugging history) never gets fresh joypad state until it re-enables NMI.
    if (bus.consumeVblankLatch() && updateJoyOnNmi) {
        bus.setJoy1(sampleJoy1(suppressJoypad));
        bus.setJoy2(sampleJoy2(suppressJoypad));
    }
    if (nmi) {
        cpu.triggerNmi(bus);
    }
    if (bus.takePendingIrq()) {
        cpu.triggerIrq(bus);
    }
    bus.syncWaiAfterVblankEdge(cpu);
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
