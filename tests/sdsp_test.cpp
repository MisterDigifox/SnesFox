#include "sdsp_test.hpp"
#include "sdsp.hpp"

#include <array>
#include <cstdio>

// -----------------------------------------------------------------------
// Lightweight assertion-based regression tests for Sdsp's ADSR/GAIN envelope
// generator and BRR resampling — drives Sdsp purely through its public
// register/clock API, no ARAM/CPU wiring beyond a hand-built BRR sample.
// -----------------------------------------------------------------------

namespace {

int g_failures = 0;
int g_checks   = 0;

void expectTrue(bool cond, const char* what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    } else {
        std::fprintf(stderr, "PASS: %s\n", what);
    }
}

void expectEq(int actual, int expected, const char* what) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s (expected %d, got %d)\n", what, expected, actual);
    } else {
        std::fprintf(stderr, "PASS: %s\n", what);
    }
}

// A one-block, self-looping BRR sample: header selects range=0/filter=0/loop/end,
// so every decoded sample is a constant 4 (no shift, no prediction, loops forever).
std::array<uint8_t, 65536> makeAram() {
    std::array<uint8_t, 65536> aram{};
    aram[0x0200] = 0x00; aram[0x0201] = 0x03; // DIR entry 0: BRR start = $0300
    aram[0x0202] = 0x00; aram[0x0203] = 0x03; // loop point = $0300 (self-loop)
    aram[0x0300] = 0x03;                      // header: range=0 filter=0 loop=1 end=1
    for (int i = 0; i < 8; ++i) aram[0x0301 + i] = 0x44; // both nibbles = 4
    return aram;
}

void setVoice0(Sdsp& dsp, uint8_t volL, uint8_t volR, uint8_t adsr1, uint8_t adsr2, uint8_t gain) {
    dsp.writeReg(0x6C, 0x00); // FLG: clear reset/mute/echo-write-disable (driver init)
    dsp.writeReg(0x5D, 0x02); // DIR base = $0200
    dsp.writeReg(0x00, volL);
    dsp.writeReg(0x01, volR);
    dsp.writeReg(0x02, 0x00); // pitch low
    dsp.writeReg(0x03, 0x10); // pitch high -> pitch = 0x1000 (unity rate)
    dsp.writeReg(0x04, 0x00); // SRCN = 0
    dsp.writeReg(0x05, adsr1);
    dsp.writeReg(0x06, adsr2);
    dsp.writeReg(0x07, gain);
    dsp.writeReg(0x0C, 0x7F); // MVOLL
    dsp.writeReg(0x1C, 0x7F); // MVOLR
}

int envx0(const Sdsp& dsp) { return dsp.registers()[0x08]; }

// Real hardware holds a freshly key-on'd voice silent for 5 sample ticks (no
// envelope, no pitch/position advance) before it starts actually playing.
// Burn through that warm-up (and its silent frames) so tests can reason about
// "the first real tick" without hardcoding the delay into every assertion.
constexpr int kKeyonDelayTicks = 5;
void skipKeyonDelay(Sdsp& dsp, std::array<uint8_t, 65536>& aram) {
    dsp.runClocks(32 * kKeyonDelayTicks, aram);
    std::array<Sdsp::PcmFrame, kKeyonDelayTicks> discard{};
    dsp.popSamples(discard.data(), discard.size());
}

// -----------------------------------------------------------------------
// Regression test: ADSR attack must ramp ENVX up gradually, not jump straight
// to full level on the very first sample — this was previously entirely
// unimplemented, so every note played at instant full volume.
// -----------------------------------------------------------------------
void testAdsrAttackRampsGradually() {
    auto aram = makeAram();
    Sdsp dsp;
    dsp.reset();
    // ADSR enabled (bit7), AR=8 -> rate index 17 -> period 48 samples/step.
    setVoice0(dsp, 127, 127, 0x88, 0x00, 0x00);
    dsp.writeReg(0x4C, 0x01); // KON voice 0
    skipKeyonDelay(dsp, aram);

    dsp.runClocks(32 * 47, aram); // 47 sample ticks: still short of the 48-tick period
    expectEq(envx0(dsp), 0, "ADSR attack: ENVX still 0 after 47/48 ticks (not instant)");

    dsp.runClocks(32, aram); // 48th tick: first +32 step fires
    expectEq(envx0(dsp), 32 >> 4, "ADSR attack: ENVX reaches its first +32 step at tick 48");
}

// -----------------------------------------------------------------------
// Regression test: key-off must fade the envelope out (-8/sample), not cut
// the voice to silence on the next sample.
// -----------------------------------------------------------------------
void testKeyOffFadesOutGradually() {
    auto aram = makeAram();
    Sdsp dsp;
    dsp.reset();
    // GAIN direct mode, snaps ENVX to 0x7F0 immediately.
    setVoice0(dsp, 127, 127, 0x00, 0x00, 0x7F);
    dsp.writeReg(0x4C, 0x01); // KON voice 0
    skipKeyonDelay(dsp, aram);

    dsp.runClocks(32, aram);
    expectEq(envx0(dsp), 0x7F0 >> 4, "GAIN direct: ENVX snaps to target on first tick");

    dsp.writeReg(0x5C, 0x01); // KOFF voice 0
    dsp.runClocks(32, aram);
    expectEq(envx0(dsp), (0x7F0 - 8) >> 4, "key-off: ENVX drops by exactly 8 on the next tick, not to 0");

    expectTrue(dsp.availableSamples() >= 2, "key-off: voice is still producing audio frames while releasing");
}

// -----------------------------------------------------------------------
// Regression test: BRR resampling must interpolate between adjacent decoded
// samples using the pitch counter's fractional part, not just nearest-
// neighbor — otherwise off-unity pitches alias/distort.
// -----------------------------------------------------------------------
void testPitchInterpolatesBetweenSamples() {
    std::array<uint8_t, 65536> aram{};
    aram[0x0200] = 0x00; aram[0x0201] = 0x03;
    aram[0x0202] = 0x00; aram[0x0203] = 0x03;
    aram[0x0300] = 0x83; // range=8 filter=0 loop=1 end=1
    // Nibble sequence 0,6,0,6,... with range=8 -> decoded samples alternate 0, 1536, 0, 1536, ...
    for (int i = 0; i < 8; ++i) aram[0x0301 + i] = 0x06;

    Sdsp dsp;
    dsp.reset();
    setVoice0(dsp, 127, 127, 0x00, 0x00, 0x7F); // GAIN direct max, so envelope doesn't mask this
    dsp.writeReg(0x02, 0x00);
    dsp.writeReg(0x03, 0x08); // pitch = 0x0800 -> half rate: advance 0.5 sample/tick
    dsp.writeReg(0x4C, 0x01); // KON
    skipKeyonDelay(dsp, aram);

    dsp.runClocks(32, aram); // tick 0: pitchCounter starts at 0 -> exactly on sample 0 (value 0)
    Sdsp::PcmFrame f0{};
    dsp.popSamples(&f0, 1);
    expectEq(f0.left, 0, "half-rate playback: first tick lands exactly on sample 0 (value 0)");

    dsp.runClocks(32, aram); // tick 1: pitchCounter now at 0x800 -> halfway between sample 0 (0) and sample 1 (1536)
    Sdsp::PcmFrame f1{};
    dsp.popSamples(&f1, 1);
    // Interpolated between decoded 0 and 1536 (roughly their midpoint), scaled down by envelope
    // (~0.99) and two /128 volume stages (voice + master) to a mid-range but clearly nonzero
    // value. Pure nearest-neighbor (the old behavior) would still report 0 here, same as f0.
    expectTrue(f1.left > 0 && f1.left < 1200,
        "half-rate playback: second tick is interpolated between samples 0 and 1536, not a hard jump");
}

// -----------------------------------------------------------------------
// Regression test: BRR filter prediction overshoot must wrap (matching real
// hardware's half-scale-then-double behavior), not hard-saturate at +32767.
// nibble0=+7 range=12 (no filter) decodes to a large prev1; nibble1=0 with
// filter 2's strong positive feedback then overshoots the 16-bit range —
// hardware wraps this to a negative value post-double, a naive full-scale
// saturating clamp instead pins it at +32767 (audible as harsh clipping).
// -----------------------------------------------------------------------
void testBrrFilterOverflowWrapsLikeHardware() {
    std::array<uint8_t, 65536> aram{};
    aram[0x0200] = 0x00; aram[0x0201] = 0x03;
    aram[0x0202] = 0x00; aram[0x0203] = 0x03;
    aram[0x0300] = 0xCB; // range=12 filter=2 loop=1 end=1
    aram[0x0301] = 0x70; // nibble0=7 (high), nibble1=0 (low)
    for (int i = 1; i < 8; ++i) aram[0x0301 + i] = 0x00;

    Sdsp dsp;
    dsp.reset();
    setVoice0(dsp, 127, 127, 0x00, 0x00, 0x7F); // GAIN direct max
    dsp.writeReg(0x4C, 0x01); // KON
    skipKeyonDelay(dsp, aram);

    dsp.runClocks(32, aram); // tick 0: decoded[0] from nibble0 (no filter yet)
    Sdsp::PcmFrame f0{};
    dsp.popSamples(&f0, 1);
    expectTrue(f0.left > 0, "BRR overflow setup: first sample (range=12, nibble=7) is a large positive value");

    dsp.runClocks(32, aram); // tick 1: filter 2 prediction from that large prev1 overshoots 16-bit range
    Sdsp::PcmFrame f1{};
    dsp.popSamples(&f1, 1);
    expectTrue(f1.left < 0,
        "BRR filter overflow wraps to negative (hardware behavior), not pinned at +32767 (naive saturation)");
}

} // namespace

int runSdspSelfTests() {
    testAdsrAttackRampsGradually();
    testKeyOffFadesOutGradually();
    testPitchInterpolatesBetweenSamples();
    testBrrFilterOverflowWrapsLikeHardware();

    std::fprintf(stderr, "\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
