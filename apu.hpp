#pragma once

#include <array>
#include <cstdint>

#include "spc700.hpp"

// SPC700/APU: 64 KiB ARAM, IPL ROM at $FFC0-$FFFF (until disabled via $F1 bit 7),
// four CPU↔SPC latches ($2140-$2143 ↔ $00F4-$00F7), and S-DSP register file access via
// $00F2 (address) / $00F3 (data read-write for the latched address).
class APU {
public:
    void reset();

    uint8_t readPort(uint16_t addr) const;
    void writePort(uint16_t addr, uint8_t value);

    // Advance the SPC700 in lock-step with SNES CPU cycles consumed since last call.
    void step(uint64_t cpuCyclesSinceLast);

    uint8_t spcPeek(uint16_t addr) const;
    void    spcPoke(uint16_t addr, uint8_t value);

private:
    void runSpc712(uint64_t cpuDelta);

    Spc700 m_spc{};
    std::array<uint8_t, 65536> m_ram{};
    /// S-DSP register file (128 × 8-bit); indexed by `$F2 & 0x7F` for `$F3` transfers.
    std::array<uint8_t, 128> m_dspRegs{};

    // Main CPU writes $2140-$2143 → SPC reads these at $F4-$F7.
    std::array<uint8_t, 4> m_cpuToSpc{};
    // SPC writes $F4-$F7 → main CPU reads these from $2140-$2143.
    std::array<uint8_t, 4> m_spcToCpu{};

    // Fractional SPC scheduling (carry-scaled CPU vs SPC cycle ratio ≈ 12:7).
    int64_t m_spcSched{};
};
