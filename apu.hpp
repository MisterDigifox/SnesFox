#pragma once

#include <array>
#include <cstdint>

#include "sdsp.hpp"
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

    uint8_t spcPeek(uint16_t addr);
    void    spcPoke(uint16_t addr, uint8_t value);

private:
    void runSpc712(uint64_t cpuDelta);
    void runTimers(uint32_t spcCycles);

    Spc700 m_spc{};
    std::array<uint8_t, 65536> m_ram{};
    Sdsp     m_sdsp{};

    // Main CPU writes $2140-$2143 → SPC reads these at $F4-$F7.
    std::array<uint8_t, 4> m_cpuToSpc{};
    // SPC writes $F4-$F7 → main CPU reads these from $2140-$2143.
    std::array<uint8_t, 4> m_spcToCpu{};

    // Fractional SPC scheduling (carry-scaled CPU vs SPC cycle ratio ≈ 12:7).
    int64_t m_spcSched{};

    // SPC timer state for $FA-$FC targets and $FD-$FF 4-bit output counters.
    std::array<uint16_t, 3> m_timerStage{};
    std::array<uint16_t, 3> m_timerPrescale{};
    std::array<uint8_t, 3>  m_timerOut{};
};
