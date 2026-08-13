#include "cpu_test.hpp"
#include "bus.hpp"
#include "cpu.hpp"

#include <cstdio>
#include <vector>

// -----------------------------------------------------------------------
// Lightweight assertion-based regression tests for CPU/Bus cycle timing.
// Mirrors tests/ppu_test.cpp's style: drive Bus/CPU purely through their
// public API, no fixture ROM needed.
// -----------------------------------------------------------------------

namespace {

int g_failures = 0;
int g_checks   = 0;

void expectEq(uint64_t actual, uint64_t expected, const char* what) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s (expected %llu, got %llu)\n", what,
            static_cast<unsigned long long>(expected),
            static_cast<unsigned long long>(actual));
    } else {
        std::fprintf(stderr, "PASS: %s\n", what);
    }
}

// Large enough for HeaderParser::detect to safely probe both the LoROM
// ($7FC0) and HiROM ($FFC0) header offsets without reading out of bounds.
std::vector<uint8_t> makeRom() {
    return std::vector<uint8_t>(0x10000, 0x00);
}

// -----------------------------------------------------------------------
// Regression test: $420D MEMSEL bit0 (FastROM) must actually reduce CPU
// cycle cost for code fetched from a FastROM-eligible bank/address (00-3F
// and 80-BF upper half, plus C0-FF). Previously $420D was recognized only
// as a disassembler label and never consulted by Bus/CPU, so FastROM games
// always ran at SlowROM's fixed 8-cycle-per-access rate.
// -----------------------------------------------------------------------
void testFastRomSpeedsUpRomFetch() {
    auto rom = makeRom();
    Bus bus(rom);
    bus.reset();

    // JSL $00:9000 at $00:8000 (LoROM upper half, file offset 0) — base cost 8.
    rom[0] = 0x22;
    rom[1] = 0x00;
    rom[2] = 0x90;
    rom[3] = 0x00;

    CPU cpuSlow;
    cpuSlow.reset(bus, 0x8000);
    cpuSlow.step(bus);
    expectEq(cpuSlow.cycles(), 8, "MEMSEL default (SlowROM): JSL from $00:8000 costs 8 cycles");

    bus.write(0x00, 0x420D, 0x01); // enable FastROM

    CPU cpuFast;
    cpuFast.reset(bus, 0x8000);
    cpuFast.step(bus);
    expectEq(cpuFast.cycles(), 6, "MEMSEL FastROM: JSL from $00:8000 costs 6 cycles");
}

// -----------------------------------------------------------------------
// Regression test: FastROM must NOT speed up WRAM-resident code. Banks
// 00-3F/80-BF address $0000-$1FFF (the WRAM mirror) are always Slow on
// real hardware regardless of MEMSEL.
// -----------------------------------------------------------------------
void testFastRomDoesNotAffectWram() {
    auto rom = makeRom();
    Bus bus(rom);
    bus.reset();
    bus.write(0x00, 0x420D, 0x01); // FastROM enabled

    // JSL $00:9000 written into the WRAM mirror at $00:0100.
    bus.write(0x00, 0x0100, 0x22);
    bus.write(0x00, 0x0101, 0x00);
    bus.write(0x00, 0x0102, 0x90);
    bus.write(0x00, 0x0103, 0x00);

    CPU cpu;
    cpu.reset(bus, 0x0100);
    cpu.step(bus);
    expectEq(cpu.cycles(), 8, "FastROM enabled: JSL from WRAM mirror $00:0100 still costs 8 cycles");
}

} // namespace

int runCpuSelfTests() {
    testFastRomSpeedsUpRomFetch();
    testFastRomDoesNotAffectWram();

    std::fprintf(stderr, "\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
