#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/// Sony SNES S-DSP: 128-byte register file and IO as seen via SPC `$F2`/`$F3`.
/// PCM generation (BRR, Gaussian mix, …) can be layered on top of [[runClocks]] later.
class Sdsp {
public:
    static constexpr int kRegisters = 128;

    /// Global register indices (voices use 16-byte stride: base + voice*0x10 + offset).
    static constexpr int r_mvoll = 0x0C;
    static constexpr int r_mvolr = 0x1C;
    static constexpr int r_evoll = 0x2C;
    static constexpr int r_evolr = 0x3C;
    static constexpr int r_kon    = 0x4C;
    static constexpr int r_koff   = 0x5C;
    static constexpr int r_flg    = 0x6C;
    static constexpr int r_endx   = 0x7C;
    static constexpr int r_dir    = 0x5D;

    void reset();

    [[nodiscard]] uint8_t addressLatch() const { return m_addressLatch; }
    void                   setAddressLatch(uint8_t v) { m_addressLatch = v; }

    /// Read DSP register `(addressLatch & 0x7F)` (SPC load from `$F3`).
    [[nodiscard]] uint8_t peekDataPort() const;

    /// Write DSP register `(addressLatch & 0x7F)` (SPC store to `$F3`).
    void pokeDataPort(uint8_t value);

    /// Last value written through `$F3` (often mirrored at ARAM `$00F3` in emulators).
    [[nodiscard]] uint8_t lastDataPortWrite() const { return m_lastDataPortByte; }

    [[nodiscard]] uint8_t readReg(int addr) const;
    void                    writeReg(int addr, uint8_t value);

    /// Advance emulation by `dspClocks` at 1.024 MHz; hook point for PCM pipeline.
    void runClocks(int dspClocks);

    [[nodiscard]] const std::array<uint8_t, kRegisters>& registers() const { return m_regs; }

private:
    std::array<uint8_t, kRegisters> m_regs{};
    uint8_t                         m_addressLatch{};
    uint8_t                         m_lastDataPortByte{};
};
