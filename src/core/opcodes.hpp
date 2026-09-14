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
    BlockMove,

    // SPC700/APU CpuOpcode table (reuse struct; disasm tooling).
    SpcIndirectX,     // operand (X): byte 0 extras
    SpcIndirectXInc,  // (X)+ loads/stores
    SpcDpRel8,        // dp + rel offset (BBC/BBS/CBNE/DBNZ dp)
    SpcDpImm8,        // dp + immediate (three-byte form)
    SpcDpPair,        // two direct-page operands (MOV dp(dd), dp(ss), ADC dp pair, …)

    // Super FX / GSU (baseline mnemonics; ALT1–ALT3 change meanings of some opcode bytes).
    GsuImmediateWord, // IWT Rn, #$imm16 — 16-bit literal after opcode (GSU endianness LE in ROM dumps)
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
