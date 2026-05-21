#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

/// Sony SNES S-DSP: 128-byte register file and IO as seen via SPC `$F2`/`$F3`.
/// PCM generation (BRR, Gaussian mix, …) can be layered on top of [[runClocks]] later.
class Sdsp {
public:
    static constexpr int kRegisters = 128;
    static constexpr int kVoices    = 8;

    struct PcmFrame {
        int16_t left{};
        int16_t right{};
    };

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

    /// Advance emulation by `dspClocks` at 1.024 MHz and mix decoded BRR voices.
    void runClocks(int dspClocks, const std::array<uint8_t, 65536>& aram);

    [[nodiscard]] size_t availableSamples() const { return m_pcm.size(); }
    size_t popSamples(PcmFrame* out, size_t maxFrames);

    [[nodiscard]] const std::array<uint8_t, kRegisters>& registers() const { return m_regs; }

private:
    struct Voice {
        bool active = false;
        uint16_t brrAddr = 0;
        uint16_t loopAddr = 0;
        uint16_t pitchCounter = 0;
        uint8_t sampleIndex = 0;
        uint8_t brrHeader = 0;
        int16_t prev1 = 0;
        int16_t prev2 = 0;
        std::array<int16_t, 16> decoded{};
    };

    void keyOn(uint8_t mask, const std::array<uint8_t, 65536>& aram);
    void keyOff(uint8_t mask);
    void decodeBlock(size_t voiceIndex, const std::array<uint8_t, 65536>& aram);
    void advanceVoice(size_t voiceIndex, const std::array<uint8_t, 65536>& aram);
    void mixOneSample(const std::array<uint8_t, 65536>& aram);

    std::array<uint8_t, kRegisters> m_regs{};
    uint8_t                         m_addressLatch{};
    uint8_t                         m_lastDataPortByte{};
    std::array<Voice, kVoices>      m_voices{};
    std::vector<PcmFrame>           m_pcm{};
    int                             m_clockRemainder{};
    uint8_t                         m_pendingKon{};
    uint8_t                         m_pendingKoff{};
};
