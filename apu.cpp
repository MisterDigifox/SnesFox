#include "apu.hpp"

void APU::reset() {
    m_cpuToApu.fill(0);

    // Standard IPL boot handshake: LDX $2140 with X16 reads $2141:$2140 → $BBAA
    m_apuToCpu[0] = 0xAA;
    m_apuToCpu[1] = 0xBB;
    m_apuToCpu[2] = 0x00;
    m_apuToCpu[3] = 0x00;
}

uint8_t APU::readPort(uint16_t addr) const {
    return m_apuToCpu[addr - 0x2140];
}

void APU::writePort(uint16_t addr, uint8_t value) {
    const size_t i = static_cast<size_t>(addr - 0x2140);
    if (i >= 4) return;

    m_cpuToApu[i] = value;
    // Instant echo stub: DSP/SPC uploads in PVSNesLib poll until echoed values appear.
    // Mirroring writes is inaccurate for timing but unlocks crt0 + idle sound driver calls.
    m_apuToCpu[i] = value;
}

void APU::step() {}
