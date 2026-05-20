#pragma once

#include <array>
#include <cstdint>

enum class AddrMode : uint8_t {
    Unknown,
    Implied,
    Accumulator,

    Immediate8,   // toujours 8-bit
    ImmediateM,   // 8 ou 16 selon flag M
    ImmediateX,   // 8 ou 16 selon flag X

    DirectPage,
    DirectPageX,
    DirectPageY,
    DirectIndirect,
    DirectIndirectY,
    DirectIndirectLong,
    DirectIndirectLongY,
    DirectXIndirect,
    StackRelative,
    StackRelativeIndirectY,
    Absolute,
    AbsoluteX,
    AbsoluteY,
    AbsoluteLong,
    AbsoluteLongX,
    AbsoluteIndirect,
    AbsoluteXIndirect,
    AbsoluteIndirectLong,
    Relative8,
    Relative16,
    BlockMove
};

struct CpuOpcode {
    const char* name;
    uint8_t size;
    AddrMode mode;
    bool valid;
    uint16_t cyclesNumber;
};

extern const std::array<CpuOpcode, 256> cpuOpcodesTable;

void printMissingCpuOpcodes(const std::array<CpuOpcode, 256>& ops);
