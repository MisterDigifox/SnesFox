#include "sdsp.hpp"

#include <cmath>

namespace {

constexpr int kClocksPerSample = 32; // 1.024 MHz DSP clock / 32 = 32 kHz PCM.
constexpr size_t kMaxBufferedFrames = 32768;

int16_t clamp16(int value) {
    if (value < -32768) return -32768;
    if (value > 32767) return 32767;
    return static_cast<int16_t>(value);
}

int8_t signedReg(uint8_t value) {
    return static_cast<int8_t>(value);
}

uint16_t read16(const std::array<uint8_t, 65536>& aram, uint16_t addr) {
    return static_cast<uint16_t>(aram[addr] | (static_cast<uint16_t>(aram[static_cast<uint16_t>(addr + 1)]) << 8));
}

void write16(std::array<uint8_t, 65536>& aram, uint16_t addr, int16_t value) {
    aram[addr] = static_cast<uint8_t>(value & 0xFF);
    aram[static_cast<uint16_t>(addr + 1)] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

int voiceReg(size_t voice, int offset) {
    return static_cast<int>(voice * 0x10 + static_cast<size_t>(offset));
}

int8_t clamp8(int value) {
    if (value < -128) return -128;
    if (value > 127) return 127;
    return static_cast<int8_t>(value);
}

constexpr double kPi = 3.14159265358979323846;

const std::array<int16_t, 512>& gaussianTable() {
    static const std::array<int16_t, 512> table = [] {
        std::array<double, 512> raw{};
        for (int n = 0; n < 512; ++n) {
            const double k = 0.5 + n;
            const double s = std::sin(kPi * k * 1.280 / 1024.0);
            const double t = (std::cos(kPi * k * 2.000 / 1023.0) - 1.0) * 0.50;
            const double u = (std::cos(kPi * k * 4.000 / 1023.0) - 1.0) * 0.08;
            raw[static_cast<size_t>(511 - n)] = s * (t + u + 1.0) / k;
        }
        std::array<int16_t, 512> out{};
        for (int phase = 0; phase < 128; ++phase) {
            const size_t a = static_cast<size_t>(phase);
            const size_t b = static_cast<size_t>(phase + 256);
            const size_t c = static_cast<size_t>(511 - phase);
            const size_t d = static_cast<size_t>(255 - phase);
            const double scale = 2048.0 / (raw[a] + raw[b] + raw[c] + raw[d]);
            out[a] = static_cast<int16_t>(raw[a] * scale + 0.5);
            out[b] = static_cast<int16_t>(raw[b] * scale + 0.5);
            out[c] = static_cast<int16_t>(raw[c] * scale + 0.5);
            out[d] = static_cast<int16_t>(raw[d] * scale + 0.5);
        }
        return out;
    }();
    return table;
}

int gaussianInterpolate(int p0, int p1, int p2, int p3, int frac) {
    const std::array<int16_t, 512>& table = gaussianTable();
    const int off = (frac >> 4) & 0xFF;
    const int16_t* forward = table.data() + 255 - off;
    const int16_t* reverse = table.data() + off;
    int32_t output = (forward[0] * p0) >> 11;
    output += (forward[256] * p1) >> 11;
    output += (reverse[256] * p2) >> 11;
    output = static_cast<int16_t>(output);
    output += (reverse[0] * p3) >> 11;
    return clamp16(output) & ~1;
}

// S-DSP envelope rate table: index (0-31, from AR/DR/SR/GAIN-rate fields) -> period
// in samples between envelope steps. 0 means "never fires" (frozen level).
constexpr int kRateTable[32] = {
    0, 2048, 1536, 1280, 1024, 768, 640, 512,
    384, 320, 256, 192, 160, 128, 96, 80,
    64, 48, 40, 32, 24, 20, 16, 12,
    10, 8, 6, 5, 4, 3, 2, 1
};

} // namespace

void Sdsp::reset() {
    m_regs.fill(0);
    // Hardware power-on/reset state: soft-reset + mute + echo-writes-disabled all set.
    // Without this, ESA/EDL default to 0 and the DSP starts scribbling zero-feedback
    // echo writes into ARAM $0000 every tick before the driver has a chance to
    // configure things — stomping whatever boot-handshake state lives there.
    m_regs[static_cast<size_t>(r_flg)] = 0xE0;
    m_addressLatch       = 0;
    m_lastDataPortByte   = 0;
    m_voices.fill(Voice{});
    m_pcm.clear();
    m_clockRemainder = 0;
    m_pendingKon = 0;
    m_pendingKoff = 0;
    m_echoHistory = {};
    m_echoHistPos = 0;
    m_echoBufOffset = 0;
    m_echoOutL = 0;
    m_echoOutR = 0;
}

uint8_t Sdsp::peekDataPort() const {
    return readReg(static_cast<int>(m_addressLatch) & 0x7F);
}

void Sdsp::pokeDataPort(uint8_t value) {
    writeReg(static_cast<int>(m_addressLatch) & 0x7F, value);
    m_lastDataPortByte = value;
}

uint8_t Sdsp::readReg(int addr) const {
    const int a = addr & 0x7F;
    return m_regs[static_cast<size_t>(a)];
}

void Sdsp::writeReg(int addr, uint8_t value) {
    const int a = addr & 0x7F;
    // ENDX: always clears on write (hardware), regardless of data.
    if (a == r_endx) {
        m_regs[static_cast<size_t>(r_endx)] = 0;
        return;
    }
    if (a == r_kon) {
        m_pendingKon = static_cast<uint8_t>(m_pendingKon | value);
    } else if (a == r_koff) {
        m_pendingKoff = static_cast<uint8_t>(m_pendingKoff | value);
    }
    m_regs[static_cast<size_t>(a)] = value;
}

size_t Sdsp::popSamples(PcmFrame* out, size_t maxFrames) {
    const size_t n = maxFrames < m_pcm.size() ? maxFrames : m_pcm.size();
    for (size_t i = 0; i < n; ++i) {
        out[i] = m_pcm[i];
    }
    m_pcm.erase(m_pcm.begin(), m_pcm.begin() + static_cast<std::ptrdiff_t>(n));
    return n;
}

void Sdsp::keyOn(uint8_t mask, const std::array<uint8_t, 65536>& aram) {
    const uint16_t dirBase = static_cast<uint16_t>(m_regs[static_cast<size_t>(r_dir)] << 8);
    for (size_t i = 0; i < kVoices; ++i) {
        if ((mask & (1u << i)) == 0) continue;

        const uint8_t srcn = m_regs[static_cast<size_t>(voiceReg(i, 0x04))];
        const uint16_t entry = static_cast<uint16_t>(dirBase + static_cast<uint16_t>(srcn) * 4u);

        Voice& v = m_voices[i];
        v.active = true;
        v.brrAddr = read16(aram, entry);
        v.loopAddr = read16(aram, static_cast<uint16_t>(entry + 2));
        v.pitchCounter = 0;
        v.sampleIndex = 0;
        v.prev1 = 0;
        v.prev2 = 0;
        v.envPhase = EnvPhase::Attack;
        v.envLevel = 0;
        v.envCounter = 0;
        v.keyonDelay = 5;
        m_regs[static_cast<size_t>(r_endx)] &= static_cast<uint8_t>(~(1u << i));
        decodeBlock(i, aram);
    }
}

void Sdsp::keyOff(uint8_t mask) {
    for (size_t i = 0; i < kVoices; ++i) {
        if ((mask & (1u << i)) != 0) {
            // Release is unconditional (independent of ADSR/GAIN mode): the voice keeps
            // playing while its envelope fades out, rather than cutting off instantly.
            m_voices[i].envPhase = EnvPhase::Release;
        }
    }
}

void Sdsp::updateEnvelope(size_t voiceIndex) {
    Voice& v = m_voices[voiceIndex];

    if (v.envPhase == EnvPhase::Release) {
        v.envLevel -= 8;
        if (v.envLevel <= 0) {
            v.envLevel = 0;
            v.active = false;
        }
        return;
    }

    const uint8_t adsr1 = m_regs[static_cast<size_t>(voiceReg(voiceIndex, v_adsr1))];
    const uint8_t adsr2 = m_regs[static_cast<size_t>(voiceReg(voiceIndex, v_adsr2))];
    const uint8_t gain  = m_regs[static_cast<size_t>(voiceReg(voiceIndex, v_gain))];
    const bool adsrEnabled = (adsr1 & 0x80) != 0;

    // GAIN direct mode snaps to the target level immediately every tick — no rate table.
    if (!adsrEnabled && (gain & 0x80) == 0) {
        v.envLevel = (gain & 0x7F) << 4;
        return;
    }

    int rateIndex = 0;
    enum class Shape { LinearIncrease, LinearDecrease, BentLineIncrease, ExponentialDecrease } shape{};
    bool fastAttack = false;

    if (adsrEnabled) {
        if (v.envPhase == EnvPhase::Attack) {
            const int ar = adsr1 & 0x0F;
            rateIndex = ar * 2 + 1;
            shape = Shape::LinearIncrease;
            fastAttack = (ar == 15);
        } else if (v.envPhase == EnvPhase::Decay) {
            const int dr = (adsr1 >> 4) & 0x07;
            rateIndex = dr * 2 + 16;
            shape = Shape::ExponentialDecrease;
        } else { // Sustain
            rateIndex = adsr2 & 0x1F;
            shape = Shape::ExponentialDecrease;
        }
    } else {
        rateIndex = gain & 0x1F;
        switch ((gain >> 5) & 0x03) {
            case 0: shape = Shape::LinearDecrease; break;
            case 1: shape = Shape::ExponentialDecrease; break;
            case 2: shape = Shape::LinearIncrease; break;
            default: shape = Shape::BentLineIncrease; break;
        }
    }

    const int period = kRateTable[rateIndex & 0x1F];
    if (period == 0) return; // rate 0 => frozen envelope

    ++v.envCounter;
    if (v.envCounter < period) return;
    v.envCounter = 0;

    switch (shape) {
        case Shape::LinearIncrease:
            v.envLevel += fastAttack ? 1024 : 32;
            if (v.envLevel > 0x7FF) v.envLevel = 0x7FF;
            if (adsrEnabled && v.envPhase == EnvPhase::Attack && v.envLevel >= 0x7FF) {
                v.envPhase = EnvPhase::Decay;
            }
            break;
        case Shape::LinearDecrease:
            v.envLevel -= 32;
            if (v.envLevel < 0) v.envLevel = 0;
            break;
        case Shape::BentLineIncrease:
            v.envLevel += (v.envLevel < 0x600) ? 32 : 8;
            if (v.envLevel > 0x7FF) v.envLevel = 0x7FF;
            break;
        case Shape::ExponentialDecrease:
            --v.envLevel;
            v.envLevel -= v.envLevel >> 8;
            if (v.envLevel < 0) v.envLevel = 0;
            if (adsrEnabled && v.envPhase == EnvPhase::Decay) {
                const int sl = (adsr2 >> 5) & 0x07;
                const int sustainLevel = (sl + 1) * 0x100 - 1;
                if (v.envLevel <= sustainLevel) {
                    v.envPhase = EnvPhase::Sustain;
                }
            }
            break;
    }
}

void Sdsp::decodeBlock(size_t voiceIndex, const std::array<uint8_t, 65536>& aram) {
    Voice& v = m_voices[voiceIndex];
    v.prevBlockLast = v.decoded[15];
    v.brrHeader = aram[v.brrAddr];
    const int range = (v.brrHeader >> 4) & 0x0F;
    const int filter = (v.brrHeader >> 2) & 0x03;

    for (int i = 0; i < 16; ++i) {
        const uint8_t packed = aram[static_cast<uint16_t>(v.brrAddr + 1 + i / 2)];
        int nibble = (i & 1) == 0 ? (packed >> 4) : (packed & 0x0F);
        if (nibble >= 8) nibble -= 16;

        // Hardware decodes in a half-scale working domain (this shift-by-1 discards
        // the LSB, restored by the final <<1 below), not a direct nibble<<range.
        int sample = 0;
        if (range <= 12) {
            sample = (nibble << range) >> 1;
        } else {
            sample = nibble < 0 ? -2048 : 0;
        }

        // p1 = previous decoded (already at full scale); p2 = the one before that,
        // halved back into this half-scale domain for the filter math.
        const int p1 = v.prev1;
        const int p2 = v.prev2 >> 1;

        switch (filter) {
            case 1:
                sample += p1 >> 1;
                sample += (-p1) >> 5;
                break;
            case 2:
                sample += p1;
                sample -= p2;
                sample += p2 >> 4;
                sample += (p1 * -3) >> 6;
                break;
            case 3:
                sample += p1;
                sample -= p2;
                sample += (p1 * -13) >> 7;
                sample += (p2 * 3) >> 4;
                break;
            default:
                break;
        }

        // Hardware clamps this half-scale accumulator to 16 bits, THEN doubles it —
        // values beyond that wrap via the 16-bit cast rather than saturating again.
        const int16_t decoded = static_cast<int16_t>(clamp16(sample) << 1);
        v.decoded[static_cast<size_t>(i)] = decoded;
        v.prev2 = v.prev1;
        v.prev1 = decoded;
    }
}

void Sdsp::advanceVoice(size_t voiceIndex, const std::array<uint8_t, 65536>& aram) {
    Voice& v = m_voices[voiceIndex];
    if (!v.active) return;

    const uint16_t pitch =
        static_cast<uint16_t>((m_regs[static_cast<size_t>(voiceReg(voiceIndex, 0x02))]
            | ((m_regs[static_cast<size_t>(voiceReg(voiceIndex, 0x03))] & 0x3F) << 8)) & 0x3FFF);
    v.pitchCounter = static_cast<uint16_t>(v.pitchCounter + pitch);

    while (v.pitchCounter >= 0x1000 && v.active) {
        v.pitchCounter = static_cast<uint16_t>(v.pitchCounter - 0x1000);
        ++v.sampleIndex;

        if (v.sampleIndex < 16) continue;

        const bool end = (v.brrHeader & 0x01) != 0;
        const bool loop = (v.brrHeader & 0x02) != 0;

        if (end) {
            m_regs[static_cast<size_t>(r_endx)] |= static_cast<uint8_t>(1u << voiceIndex);
            if (!loop) {
                v.active = false;
                break;
            }
            v.brrAddr = v.loopAddr;
        } else {
            v.brrAddr = static_cast<uint16_t>(v.brrAddr + 9);
        }

        v.sampleIndex = 0;
        decodeBlock(voiceIndex, aram);
    }
}

void Sdsp::mixOneSample(std::array<uint8_t, 65536>& aram) {
    if ((m_regs[static_cast<size_t>(r_flg)] & 0x80) != 0) {
        keyOff(0xFF);
    }

    int left = 0;
    int right = 0;
    int dryEchoL = 0;
    int dryEchoR = 0;
    const uint8_t eon = m_regs[static_cast<size_t>(r_eon)];
    for (size_t i = 0; i < kVoices; ++i) {
        Voice& v = m_voices[i];
        if (!v.active) continue;

        if (v.keyonDelay > 0) {
            --v.keyonDelay; // silent warm-up: no envelope, no pitch/position advance yet
            continue;
        }

        // Real hardware's 4-tap Gaussian interpolation (see gaussianTable/gaussianInterpolate
        // above) between the two samples straddling the fractional position left over in the
        // pitch counter, with one sample of lookback/lookahead on each side. p2/p3 fall back to
        // repeating the last available sample at a block boundary rather than reading ahead into
        // the not-yet-decoded next block — a small residual approximation distinct from the
        // interpolation curve itself.
        const int idx = v.sampleIndex;
        const int p1 = v.decoded[idx];
        const int p0 = (idx >= 1) ? v.decoded[idx - 1] : v.prevBlockLast;
        const int p2 = (idx < 15) ? v.decoded[idx + 1] : p1;
        const int p3 = (idx < 14) ? v.decoded[idx + 2] : p2;
        const int sample = gaussianInterpolate(p0, p1, p2, p3, v.pitchCounter);

        updateEnvelope(i);
        const int enveloped = (sample * v.envLevel) >> 11;

        m_regs[static_cast<size_t>(voiceReg(i, v_envx))] = static_cast<uint8_t>((v.envLevel >> 4) & 0x7F);
        m_regs[static_cast<size_t>(voiceReg(i, v_outx))] = static_cast<uint8_t>(clamp8(enveloped >> 8));

        const int vl = signedReg(m_regs[static_cast<size_t>(voiceReg(i, 0x00))]);
        const int vr = signedReg(m_regs[static_cast<size_t>(voiceReg(i, 0x01))]);
        const int chL = (enveloped * vl) / 128;
        const int chR = (enveloped * vr) / 128;
        left += chL;
        right += chR;
        if ((eon & (1u << i)) != 0) {
            dryEchoL += chL;
            dryEchoR += chR;
        }

        advanceVoice(i, aram);
    }

    const int ml = signedReg(m_regs[static_cast<size_t>(r_mvoll)]);
    const int mr = signedReg(m_regs[static_cast<size_t>(r_mvolr)]);
    if ((m_regs[static_cast<size_t>(r_flg)] & 0x40) != 0) {
        left = 0;
        right = 0;
    } else {
        left = (left * ml) / 128;
        right = (right * mr) / 128;
    }

    runEcho(aram, dryEchoL, dryEchoR);
    left += m_echoOutL;
    right += m_echoOutR;

    if (m_pcm.size() >= kMaxBufferedFrames) {
        m_pcm.erase(m_pcm.begin(), m_pcm.begin() + static_cast<std::ptrdiff_t>(kMaxBufferedFrames / 2));
    }
    m_pcm.push_back(PcmFrame{clamp16(left), clamp16(right)});
}

// 8-tap FIR echo: reads the delay line from ARAM (ESA/EDL), filters it through the
// per-channel history ring using the FIR coefficient registers, mixes the filtered
// result (scaled by EVOLL/EVOLR) into this tick's output, then writes the echo-enabled
// voices' dry sum plus feedback back into the delay line for future ticks.
void Sdsp::runEcho(std::array<uint8_t, 65536>& aram, int dryEchoL, int dryEchoR) {
    const uint32_t length = static_cast<uint32_t>(m_regs[static_cast<size_t>(r_edl)] & 0x0F) << 11;
    const uint16_t base = static_cast<uint16_t>(m_regs[static_cast<size_t>(r_esa)] << 8);
    const uint16_t addr = static_cast<uint16_t>(base + m_echoBufOffset);

    m_echoHistPos = (m_echoHistPos + 1) & 7;
    for (int ch = 0; ch < 2; ++ch) {
        const uint16_t sampleAddr = static_cast<uint16_t>(addr + ch * 2);
        const int16_t raw = static_cast<int16_t>(read16(aram, sampleAddr));
        m_echoHistory[static_cast<size_t>(ch)][static_cast<size_t>(m_echoHistPos)] =
            static_cast<int16_t>(raw >> 1);

        int fir = 0;
        for (int tap = 0; tap < 8; ++tap) {
            const int8_t coeff = signedReg(m_regs[static_cast<size_t>(voiceReg(static_cast<size_t>(tap), r_fir0))]);
            const int histSlot = (m_echoHistPos - tap) & 7;
            fir += (m_echoHistory[static_cast<size_t>(ch)][static_cast<size_t>(histSlot)] * coeff) >> 6;
        }

        const int evol = signedReg(m_regs[static_cast<size_t>(ch == 0 ? r_evoll : r_evolr)]);
        const int mixed = (clamp16(fir) * evol) / 128;
        if (ch == 0) m_echoOutL = mixed; else m_echoOutR = mixed;

        const int feedback = signedReg(m_regs[static_cast<size_t>(r_efb)]);
        const int dry = (ch == 0) ? dryEchoL : dryEchoR;
        const int16_t writeBack = clamp16(dry + ((clamp16(fir) * feedback) >> 7));
        if ((m_regs[static_cast<size_t>(r_flg)] & 0x20) == 0) {
            write16(aram, sampleAddr, writeBack);
        }
    }

    if (length == 0) {
        m_echoBufOffset = 0;
    } else {
        m_echoBufOffset += 4;
        if (m_echoBufOffset >= length) m_echoBufOffset = 0;
    }
}

void Sdsp::runClocks(int dspClocks, std::array<uint8_t, 65536>& aram) {
    if (dspClocks <= 0) return;

    if (m_pendingKoff != 0) {
        keyOff(m_pendingKoff);
        m_pendingKoff = 0;
    }
    if (m_pendingKon != 0) {
        keyOn(m_pendingKon, aram);
        m_pendingKon = 0;
    }

    m_clockRemainder += dspClocks;
    while (m_clockRemainder >= kClocksPerSample) {
        m_clockRemainder -= kClocksPerSample;
        mixOneSample(aram);
    }
}
