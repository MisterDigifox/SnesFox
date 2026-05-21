#include "sdsp.hpp"

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

int voiceReg(size_t voice, int offset) {
    return static_cast<int>(voice * 0x10 + static_cast<size_t>(offset));
}

} // namespace

void Sdsp::reset() {
    m_regs.fill(0);
    m_addressLatch       = 0;
    m_lastDataPortByte   = 0;
    m_voices.fill(Voice{});
    m_pcm.clear();
    m_clockRemainder = 0;
    m_pendingKon = 0;
    m_pendingKoff = 0;
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
        m_regs[static_cast<size_t>(r_endx)] &= static_cast<uint8_t>(~(1u << i));
        decodeBlock(i, aram);
    }
}

void Sdsp::keyOff(uint8_t mask) {
    for (size_t i = 0; i < kVoices; ++i) {
        if ((mask & (1u << i)) != 0) {
            m_voices[i].active = false;
        }
    }
}

void Sdsp::decodeBlock(size_t voiceIndex, const std::array<uint8_t, 65536>& aram) {
    Voice& v = m_voices[voiceIndex];
    v.brrHeader = aram[v.brrAddr];
    const int range = (v.brrHeader >> 4) & 0x0F;
    const int filter = (v.brrHeader >> 2) & 0x03;

    for (int i = 0; i < 16; ++i) {
        const uint8_t packed = aram[static_cast<uint16_t>(v.brrAddr + 1 + i / 2)];
        int nibble = (i & 1) == 0 ? (packed >> 4) : (packed & 0x0F);
        if (nibble >= 8) nibble -= 16;

        int sample = 0;
        if (range <= 12) {
            sample = nibble << range;
        } else {
            sample = nibble < 0 ? -2048 : 0;
        }

        switch (filter) {
            case 1:
                sample += (v.prev1 * 15) / 16;
                break;
            case 2:
                sample += (v.prev1 * 61) / 32 - (v.prev2 * 15) / 16;
                break;
            case 3:
                sample += (v.prev1 * 115) / 64 - (v.prev2 * 13) / 16;
                break;
            default:
                break;
        }

        const int16_t decoded = clamp16(sample);
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

void Sdsp::mixOneSample(const std::array<uint8_t, 65536>& aram) {
    if ((m_regs[static_cast<size_t>(r_flg)] & 0x80) != 0) {
        keyOff(0xFF);
    }

    int left = 0;
    int right = 0;
    for (size_t i = 0; i < kVoices; ++i) {
        Voice& v = m_voices[i];
        if (!v.active) continue;

        const int sample = v.decoded[v.sampleIndex];
        const int vl = signedReg(m_regs[static_cast<size_t>(voiceReg(i, 0x00))]);
        const int vr = signedReg(m_regs[static_cast<size_t>(voiceReg(i, 0x01))]);
        left += (sample * vl) / 128;
        right += (sample * vr) / 128;

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

    if (m_pcm.size() >= kMaxBufferedFrames) {
        m_pcm.erase(m_pcm.begin(), m_pcm.begin() + static_cast<std::ptrdiff_t>(kMaxBufferedFrames / 2));
    }
    m_pcm.push_back(PcmFrame{clamp16(left), clamp16(right)});
}

void Sdsp::runClocks(int dspClocks, const std::array<uint8_t, 65536>& aram) {
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
