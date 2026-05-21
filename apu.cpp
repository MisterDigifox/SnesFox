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

inline uint16_t timerPeriod(size_t i) { return i == 2 ? 16 : 128; }

inline uint16_t timerTarget(uint8_t target) {
    return target == 0 ? 0x100 : static_cast<uint16_t>(target);
}

} // namespace

void APU::reset() {
    m_ram.fill(0);
    m_sdsp.reset();
    m_cpuToSpc.fill(0);
    m_spcToCpu.fill(0);
    m_timerStage.fill(0);
    m_timerPrescale.fill(0);
    m_timerOut.fill(0);
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
    // Port 0 is the IPL upload acknowledge byte and must stay SPC-driven.
    // Data/control ports are locally readable by CPU-side upload routines that
    // poll $2140 as a 16-bit word while the IPL only updates $F4.
    if (i != 0) {
        m_spcToCpu[i] = value;
    }
}

uint8_t APU::spcPeek(uint16_t addr) {
    // $00F2: DSP register address latch (readable/writable).
    if (addr == 0x00F2) {
        return m_sdsp.addressLatch();
    }
    // $00F3: read selected DSP register (address = `$F2 & 0x7F`).
    if (addr == 0x00F3) {
        return m_sdsp.peekDataPort();
    }
    if (addr >= 0xFFC0 && iplRomOn(m_ram[0x00F1])) {
        return kIplRom[addr - 0xFFC0];
    }
    if (addr >= 0x00F4 && addr <= 0x00F7) {
        return m_cpuToSpc[static_cast<size_t>(addr - 0x00F4)];
    }
    if (addr >= 0x00FD && addr <= 0x00FF) {
        const size_t i = static_cast<size_t>(addr - 0x00FD);
        const uint8_t value = m_timerOut[i] & 0x0F;
        m_timerOut[i] = 0;
        m_ram[addr] = 0;
        return value;
    }
    return m_ram[addr];
}

void APU::spcPoke(uint16_t addr, uint8_t v) {
    if (addr == 0x00F1) {
        const uint8_t old = m_ram[0x00F1];
        m_ram[0x00F1] = v;
        for (size_t i = 0; i < 3; ++i) {
            const uint8_t bit = static_cast<uint8_t>(1u << i);
            if ((v & bit) != 0 && (old & bit) == 0) {
                m_timerStage[i] = 0;
                m_timerPrescale[i] = 0;
                m_timerOut[i] = 0;
                m_ram[0x00FD + i] = 0;
            }
        }
        if ((v & 0x10) != 0) {
            m_cpuToSpc[0] = 0;
            m_cpuToSpc[1] = 0;
        }
        if ((v & 0x20) != 0) {
            m_cpuToSpc[2] = 0;
            m_cpuToSpc[3] = 0;
        }
        return;
    }
    if (addr == 0x00F2) {
        m_sdsp.setAddressLatch(v);
        m_ram[0x00F2] = v;
        return;
    }
    if (addr == 0x00F3) {
        m_sdsp.pokeDataPort(v);
        m_ram[0x00F3] = m_sdsp.lastDataPortWrite();
        return;
    }
    m_ram[addr] = v;
    if (addr >= 0x00F4 && addr <= 0x00F7) {
        m_spcToCpu[static_cast<size_t>(addr - 0x00F4)] = v;
    }
}

void APU::runTimers(uint32_t spcCycles) {
    const uint8_t control = m_ram[0x00F1];
    for (size_t i = 0; i < 3; ++i) {
        if ((control & (1u << i)) == 0) continue;

        m_timerPrescale[i] = static_cast<uint16_t>(m_timerPrescale[i] + spcCycles);
        const uint16_t period = timerPeriod(i);
        while (m_timerPrescale[i] >= period) {
            m_timerPrescale[i] = static_cast<uint16_t>(m_timerPrescale[i] - period);
            ++m_timerStage[i];
            if (m_timerStage[i] >= timerTarget(m_ram[0x00FA + i])) {
                m_timerStage[i] = 0;
                m_timerOut[i] = static_cast<uint8_t>((m_timerOut[i] + 1u) & 0x0F);
                m_ram[0x00FD + i] = m_timerOut[i];
            }
        }
    }
}

void APU::runSpc712(uint64_t cpuDelta) {
    if (cpuDelta == 0 || m_spc.halted()) return;

    m_spcSched += static_cast<int64_t>(cpuDelta) * 7;

    while (m_spcSched >= 0 && !m_spc.halted()) {
        const uint32_t spcCyc = m_spc.step(*this);
        runTimers(spcCyc);
        m_sdsp.runClocks(static_cast<int>(spcCyc));
        const int64_t debit = static_cast<int64_t>(spcCyc) * 12;
        m_spcSched -= debit;
    }
}

void APU::step(uint64_t cpuCyclesSinceLast) {
    runSpc712(cpuCyclesSinceLast);
}
