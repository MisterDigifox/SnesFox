#pragma once

#include <cstdint>
#include <string>

class Bus;
class CPU;

// Runs one CPU instruction's worth of peripheral scheduling (stepPeripherals, joypad
// auto-read latch, NMI/IRQ dispatch, WAI sync) — shared by the interactive debug loop and
// the headless `cov`/`snap` CLI subcommands.
void advanceCpuScheduling(Bus& bus, CPU& cpu, bool updateJoyOnNmi, bool suppressJoypad = false);

// Zero-padded uppercase hex formatting for register/address dumps.
std::string hex8(uint8_t value);
std::string hex16(uint16_t value);
std::string hex24(uint32_t value);
