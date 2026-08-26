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
    static constexpr int r_efb    = 0x0D;
    static constexpr int r_pmon   = 0x2D;
    static constexpr int r_non    = 0x3D;
    static constexpr int r_eon    = 0x4D;
    static constexpr int r_dir    = 0x5D;
    static constexpr int r_esa    = 0x6D;
    static constexpr int r_edl    = 0x7D;
    /// FIR filter tap N lives at the offset otherwise used by voice N's low nibble.
    static constexpr int r_fir0   = 0x0F;

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
    void runClocks(int dspClocks, std::array<uint8_t, 65536>& aram);

    [[nodiscard]] size_t availableSamples() const { return m_pcm.size(); }
    size_t popSamples(PcmFrame* out, size_t maxFrames);

    [[nodiscard]] const std::array<uint8_t, kRegisters>& registers() const { return m_regs; }

    /// Live per-voice playback state (as opposed to the SRCN register, which can be
    /// changed after key-on without re-triggering — this reflects what's actually sounding).
    struct VoiceDebugState {
        bool active = false;
        uint16_t brrAddr = 0;
        uint16_t loopAddr = 0;
    };
    [[nodiscard]] VoiceDebugState voiceDebugState(int voice) const {
        const Voice& v = m_voices[static_cast<size_t>(voice)];
        return VoiceDebugState{v.active, v.brrAddr, v.loopAddr};
    }

private:
    enum class EnvPhase : uint8_t { Attack, Decay, Sustain, Release };

    struct Voice {
        bool active = false;
        uint16_t brrAddr = 0;
        uint16_t loopAddr = 0;
        uint16_t pitchCounter = 0;
        uint8_t sampleIndex = 0;
        uint8_t brrHeader = 0;
        int16_t prev1 = 0;
        int16_t prev2 = 0;
        int16_t prevBlockLast = 0; // last decoded sample of the block before this one (for interpolation lookback)
        std::array<int16_t, 16> decoded{};

        // ADSR/GAIN envelope state.
        EnvPhase envPhase = EnvPhase::Release;
        int envLevel = 0;   // 0..0x7FF
        int envCounter = 0; // ticks accumulated toward the current rate-table period

        // Real hardware holds a freshly key-on'd voice silent (envelope 0, pitch not
        // added) for 5 sample ticks before it starts actually playing.
        int keyonDelay = 0;
    };

    /// Per-voice register offsets (base + voice*0x10 + offset).
    static constexpr int v_adsr1 = 0x05;
    static constexpr int v_adsr2 = 0x06;
    static constexpr int v_gain  = 0x07;
    static constexpr int v_envx  = 0x08;
    static constexpr int v_outx  = 0x09;

    void keyOn(uint8_t mask, const std::array<uint8_t, 65536>& aram);
    void keyOff(uint8_t mask);
    void decodeBlock(size_t voiceIndex, const std::array<uint8_t, 65536>& aram);
    void advanceVoice(size_t voiceIndex, const std::array<uint8_t, 65536>& aram);
    void updateEnvelope(size_t voiceIndex);
    void mixOneSample(std::array<uint8_t, 65536>& aram);
    void runEcho(std::array<uint8_t, 65536>& aram, int dryEchoL, int dryEchoR);

    std::array<uint8_t, kRegisters> m_regs{};
    uint8_t                         m_addressLatch{};
    uint8_t                         m_lastDataPortByte{};
    std::array<Voice, kVoices>      m_voices{};
    std::vector<PcmFrame>           m_pcm{};
    int                             m_clockRemainder{};
    uint8_t                         m_pendingKon{};
    uint8_t                         m_pendingKoff{};

    // Echo: 8-tap FIR over a small per-channel history ring, plus a delay buffer
    // living in ARAM (ESA/EDL) with feedback (EFB).
    std::array<std::array<int16_t, 8>, 2> m_echoHistory{};
    int                                    m_echoHistPos{};
    uint32_t                               m_echoBufOffset{};
    int                                    m_echoOutL{};
    int                                    m_echoOutR{};
};
