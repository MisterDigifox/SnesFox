#include "apu.hpp"

#include <cstdlib>

namespace {

// Official 64-byte IPL ROM (see problemkaputt `fullsnes.txt` — "Boot ROM Disassembly").
constexpr std::array<uint8_t, 64> kIplRom = {
    0xCD, 0xEF, 0xBD, 0xE8, 0x00, 0xC6, 0x1D, 0xD0, 0xFC, 0x8F, 0xAA, 0xF4, 0x8F, 0xBB, 0xF5, 0x78,
    0xCC, 0xF4, 0xD0, 0xFB, 0x2F, 0x19, 0xEB, 0xF4, 0xD0, 0xFC, 0x7E, 0xF4, 0xD0, 0x0B, 0xE4, 0xF5,
    0xCB, 0xF4, 0xD7, 0x00, 0xFC, 0xD0, 0xF3, 0xAB, 0x01, 0x10, 0xEF, 0x7E, 0xF4, 0x10, 0xEB, 0xBA,
    0xF6, 0xDA, 0x00, 0xBA, 0xF4, 0xC4, 0xF4, 0xDD, 0x5D, 0xD0, 0xDB, 0x1F, 0x00, 0x00, 0xC0, 0xFF};

inline bool iplRomOn(uint8_t f1) { return (f1 & 0x80) != 0; }

} // namespace

void APU::reset() {
    m_ram.fill(0);
    m_cpuToSpc.fill(0);
    m_spcToCpu.fill(0);
    m_spcSched = 0;

    // CONTROL ($F1): bit 7 maps IPL ROM at $FFC0-$FFFF; hardware comes out of reset with ROM on.
    m_ram[0x00F1] = 0x80;

    m_spc.reset();

    if (const char* z = std::getenv("SNESFOX_APU_PORTS_ZERO")) {
        if (z[0] == '1' && z[1] == '\0') {
            m_cpuToSpc.fill(0);
            m_spcToCpu.fill(0);
        }
    }
}

uint8_t APU::readPort(uint16_t addr) const {
    const size_t i = static_cast<size_t>(addr - 0x2140);
    if (i >= 4) return 0;
    return m_spcToCpu[i];
}

void APU::writePort(uint16_t addr, uint8_t value) {
    const size_t i = static_cast<size_t>(addr - 0x2140);
    if (i >= 4) return;
    m_cpuToSpc[i] = value;
    // Instant echo: CPU read side tracks last CPU write until the SPC overwrites
    // via $F4-$F7 (upload polls on $2140 otherwise stay on $00 forever).
    m_spcToCpu[i] = value;
}

uint8_t APU::spcPeek(uint16_t addr) const {
    if (addr >= 0xFFC0 && iplRomOn(m_ram[0x00F1])) {
        return kIplRom[addr - 0xFFC0];
    }
    if (addr >= 0x00F4 && addr <= 0x00F7) {
        return m_cpuToSpc[static_cast<size_t>(addr - 0x00F4)];
    }
    // S-DSP not emulated: treat DSP data port as always idle so SPC code that
    // busy-waits on $F3 does not wedge (would block handshakes with the main CPU).
    if (addr == 0x00F3) {
        return 0;
    }
    return m_ram[addr];
}

void APU::spcPoke(uint16_t addr, uint8_t v) {
    if (addr == 0x00F1) {
        m_ram[0x00F1] = v;
        return;
    }
    m_ram[addr] = v;
    if (addr >= 0x00F4 && addr <= 0x00F7) {
        m_spcToCpu[static_cast<size_t>(addr - 0x00F4)] = v;
    }
    // DSP writes complete "immediately" with no S-DSP model; RAM mirror stays 0
    // so any poll of $F3 after a transfer sees an idle port (matches spcPeek).
    if (addr == 0x00F3) {
        m_ram[0x00F3] = 0;
    }
}

void APU::runSpc712(uint64_t cpuDelta) {
    if (cpuDelta == 0 || m_spc.halted()) return;

    m_spcSched += static_cast<int64_t>(cpuDelta) * 7;

    while (m_spcSched >= 0 && !m_spc.halted()) {
        const uint32_t spcCyc = m_spc.step(*this);
        const int64_t debit   = static_cast<int64_t>(spcCyc) * 12;
        m_spcSched -= debit;
    }
}

void APU::step(uint64_t cpuCyclesSinceLast) {
    runSpc712(cpuCyclesSinceLast);
}
