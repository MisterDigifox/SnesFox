#include "opcodes.hpp"

#include <array>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {

CpuOpcode makeUnknown() {
    return {"???", 1, AddrMode::Unknown, false};
}

void setOp(std::array<CpuOpcode, 256>& ops, uint8_t opcode, const char* name, uint8_t size, AddrMode mode, uint16_t cyclesNumber) {
    if (ops[opcode].valid) {
        throw std::runtime_error("Duplicate opcode in table");
    }
    ops[opcode] = {name, size, mode, true, cyclesNumber};
}

std::array<CpuOpcode, 256> buildCpuOpcodeTable() {
    std::array<CpuOpcode, 256> ops{};
    ops.fill(makeUnknown());

    setOp(ops, 0x00, "BRK", 2, AddrMode::Immediate8, 7);
    setOp(ops, 0x01, "ORA", 2, AddrMode::DirectXIndirect, 6);
    setOp(ops, 0x02, "COP", 2, AddrMode::Immediate8, 7);
    setOp(ops, 0x03, "ORA", 2, AddrMode::StackRelative, 4);
    setOp(ops, 0x04, "TSB", 2, AddrMode::DirectPage, 5);
    setOp(ops, 0x05, "ORA", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0x06, "ASL", 2, AddrMode::DirectPage, 5);
    setOp(ops, 0x07, "ORA", 2, AddrMode::DirectIndirectLong, 6);
    setOp(ops, 0x08, "PHP", 1, AddrMode::Implied, 3);
    setOp(ops, 0x09, "ORA", 2, AddrMode::ImmediateM, 2);
    setOp(ops, 0x0A, "ASL", 1, AddrMode::Accumulator, 2);
    setOp(ops, 0x0B, "PHD", 1, AddrMode::Implied, 4);
    setOp(ops, 0x0C, "TSB", 3, AddrMode::Absolute, 6);
    setOp(ops, 0x0D, "ORA", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x0E, "ASL", 3, AddrMode::Absolute, 6);
    setOp(ops, 0x0F, "ORA", 4, AddrMode::AbsoluteLong, 5);

    setOp(ops, 0x10, "BPL", 2, AddrMode::Relative8, 2);
    setOp(ops, 0x11, "ORA", 2, AddrMode::DirectIndirectY, 5);
    setOp(ops, 0x12, "ORA", 2, AddrMode::DirectIndirect, 5);
    setOp(ops, 0x13, "ORA", 2, AddrMode::StackRelativeIndirectY, 7);
    setOp(ops, 0x14, "TRB", 2, AddrMode::DirectPage, 5);
    setOp(ops, 0x15, "ORA", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0x16, "ASL", 2, AddrMode::DirectPageX, 6);
    setOp(ops, 0x17, "ORA", 2, AddrMode::DirectIndirectLongY, 6);
    setOp(ops, 0x18, "CLC", 1, AddrMode::Implied, 2);
    setOp(ops, 0x19, "ORA", 3, AddrMode::AbsoluteY, 4);
    setOp(ops, 0x1A, "INC", 1, AddrMode::Accumulator, 2);
    setOp(ops, 0x1B, "TCS", 1, AddrMode::Implied, 2);
    setOp(ops, 0x1C, "TRB", 3, AddrMode::Absolute, 6);
    setOp(ops, 0x1D, "ORA", 3, AddrMode::AbsoluteX, 4);
    setOp(ops, 0x1E, "ASL", 3, AddrMode::AbsoluteX, 7);
    setOp(ops, 0x1F, "ORA", 4, AddrMode::AbsoluteLongX, 5);

    setOp(ops, 0x20, "JSR", 3, AddrMode::Absolute, 6);
    setOp(ops, 0x21, "AND", 2, AddrMode::DirectXIndirect, 6);
    setOp(ops, 0x22, "JSL", 4, AddrMode::AbsoluteLong, 8);
    setOp(ops, 0x23, "AND", 2, AddrMode::StackRelative, 4);
    setOp(ops, 0x24, "BIT", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0x25, "AND", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0x26, "ROL", 2, AddrMode::DirectPage, 5);
    setOp(ops, 0x27, "AND", 2, AddrMode::DirectIndirectLong, 6);
    setOp(ops, 0x28, "PLP", 1, AddrMode::Implied, 4);
    setOp(ops, 0x29, "AND", 2, AddrMode::ImmediateM, 2);
    setOp(ops, 0x2A, "ROL", 1, AddrMode::Accumulator, 2);
    setOp(ops, 0x2B, "PLD", 1, AddrMode::Implied, 5);
    setOp(ops, 0x2C, "BIT", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x2D, "AND", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x2E, "ROL", 3, AddrMode::Absolute, 6);
    setOp(ops, 0x2F, "AND", 4, AddrMode::AbsoluteLong, 5);

    setOp(ops, 0x30, "BMI", 2, AddrMode::Relative8, 2);
    setOp(ops, 0x31, "AND", 2, AddrMode::DirectIndirectY, 5);
    setOp(ops, 0x32, "AND", 2, AddrMode::DirectIndirect, 5);
    setOp(ops, 0x33, "AND", 2, AddrMode::StackRelativeIndirectY, 7);
    setOp(ops, 0x34, "BIT", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0x35, "AND", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0x36, "ROL", 2, AddrMode::DirectPageX, 6);
    setOp(ops, 0x37, "AND", 2, AddrMode::DirectIndirectLongY, 6);
    setOp(ops, 0x38, "SEC", 1, AddrMode::Implied, 2);
    setOp(ops, 0x39, "AND", 3, AddrMode::AbsoluteY, 4);
    setOp(ops, 0x3A, "DEC", 1, AddrMode::Accumulator, 2);
    setOp(ops, 0x3B, "TSC", 1, AddrMode::Implied, 2);
    setOp(ops, 0x3C, "BIT", 3, AddrMode::AbsoluteX, 4);
    setOp(ops, 0x3D, "AND", 3, AddrMode::AbsoluteX, 4);
    setOp(ops, 0x3E, "ROL", 3, AddrMode::AbsoluteX, 7);
    setOp(ops, 0x3F, "AND", 4, AddrMode::AbsoluteLongX, 5);

    setOp(ops, 0x40, "RTI", 1, AddrMode::Implied, 6);
    setOp(ops, 0x41, "EOR", 2, AddrMode::DirectXIndirect, 6);
    setOp(ops, 0x42, "WDM", 2, AddrMode::Immediate8, 2);
    setOp(ops, 0x43, "EOR", 2, AddrMode::StackRelative, 4);
    setOp(ops, 0x44, "MVP", 3, AddrMode::BlockMove, 7);
    setOp(ops, 0x45, "EOR", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0x46, "LSR", 2, AddrMode::DirectPage, 5);
    setOp(ops, 0x47, "EOR", 2, AddrMode::DirectIndirectLong, 6);
    setOp(ops, 0x48, "PHA", 1, AddrMode::Implied, 3);
    setOp(ops, 0x49, "EOR", 2, AddrMode::ImmediateM, 2);
    setOp(ops, 0x4A, "LSR", 1, AddrMode::Accumulator, 2);
    setOp(ops, 0x4B, "PHK", 1, AddrMode::Implied, 3);
    setOp(ops, 0x4C, "JMP", 3, AddrMode::Absolute, 3);
    setOp(ops, 0x4D, "EOR", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x4E, "LSR", 3, AddrMode::Absolute, 6);
    setOp(ops, 0x4F, "EOR", 4, AddrMode::AbsoluteLong, 5);

    setOp(ops, 0x50, "BVC", 2, AddrMode::Relative8, 2);
    setOp(ops, 0x51, "EOR", 2, AddrMode::DirectIndirectY, 5);
    setOp(ops, 0x52, "EOR", 2, AddrMode::DirectIndirect, 5);
    setOp(ops, 0x53, "EOR", 2, AddrMode::StackRelativeIndirectY, 7);
    setOp(ops, 0x54, "MVN", 3, AddrMode::BlockMove, 7);
    setOp(ops, 0x55, "EOR", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0x56, "LSR", 2, AddrMode::DirectPageX, 6);
    setOp(ops, 0x57, "EOR", 2, AddrMode::DirectIndirectLongY, 6);
    setOp(ops, 0x58, "CLI", 1, AddrMode::Implied, 2);
    setOp(ops, 0x59, "EOR", 3, AddrMode::AbsoluteY, 4);
    setOp(ops, 0x5A, "PHY", 1, AddrMode::Implied, 3);
    setOp(ops, 0x5B, "TCD", 1, AddrMode::Implied, 2);
    setOp(ops, 0x5C, "JML", 4, AddrMode::AbsoluteLong, 4);
    setOp(ops, 0x5D, "EOR", 3, AddrMode::AbsoluteX, 4);
    setOp(ops, 0x5E, "LSR", 3, AddrMode::AbsoluteX, 7);
    setOp(ops, 0x5F, "EOR", 4, AddrMode::AbsoluteLongX, 5);

    setOp(ops, 0x60, "RTS", 1, AddrMode::Implied, 6);
    setOp(ops, 0x61, "ADC", 2, AddrMode::DirectXIndirect, 6);
    setOp(ops, 0x62, "PER", 3, AddrMode::Relative16, 6);
    setOp(ops, 0x63, "ADC", 2, AddrMode::StackRelative, 4);
    setOp(ops, 0x64, "STZ", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0x65, "ADC", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0x66, "ROR", 2, AddrMode::DirectPage, 5);
    setOp(ops, 0x67, "ADC", 2, AddrMode::DirectIndirectLong, 6);
    setOp(ops, 0x68, "PLA", 1, AddrMode::Implied, 4);
    setOp(ops, 0x69, "ADC", 2, AddrMode::ImmediateM, 2);
    setOp(ops, 0x6A, "ROR", 1, AddrMode::Accumulator, 2);
    setOp(ops, 0x6B, "RTL", 1, AddrMode::Implied, 6);
    setOp(ops, 0x6C, "JMP", 3, AddrMode::AbsoluteIndirect, 5);
    setOp(ops, 0x6D, "ADC", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x6E, "ROR", 3, AddrMode::Absolute, 6);
    setOp(ops, 0x6F, "ADC", 4, AddrMode::AbsoluteLong, 5);

    setOp(ops, 0x70, "BVS", 2, AddrMode::Relative8, 2);
    setOp(ops, 0x71, "ADC", 2, AddrMode::DirectIndirectY, 5);
    setOp(ops, 0x72, "ADC", 2, AddrMode::DirectIndirect, 5);
    setOp(ops, 0x73, "ADC", 2, AddrMode::StackRelativeIndirectY, 7);
    setOp(ops, 0x74, "STZ", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0x75, "ADC", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0x76, "ROR", 2, AddrMode::DirectPageX, 6);
    setOp(ops, 0x77, "ADC", 2, AddrMode::DirectIndirectLongY, 6);
    setOp(ops, 0x78, "SEI", 1, AddrMode::Implied, 2);
    setOp(ops, 0x79, "ADC", 3, AddrMode::AbsoluteY, 4);
    setOp(ops, 0x7A, "PLY", 1, AddrMode::Implied, 4);
    setOp(ops, 0x7B, "TDC", 1, AddrMode::Implied, 2);
    setOp(ops, 0x7C, "JMP", 3, AddrMode::AbsoluteXIndirect, 6);
    setOp(ops, 0x7D, "ADC", 3, AddrMode::AbsoluteX, 4);
    setOp(ops, 0x7E, "ROR", 3, AddrMode::AbsoluteX, 7);
    setOp(ops, 0x7F, "ADC", 4, AddrMode::AbsoluteLongX, 5);

    setOp(ops, 0x80, "BRA", 2, AddrMode::Relative8, 3);
    setOp(ops, 0x81, "STA", 2, AddrMode::DirectXIndirect, 6);
    setOp(ops, 0x82, "BRL", 3, AddrMode::Relative16, 4);
    setOp(ops, 0x83, "STA", 2, AddrMode::StackRelative, 4);
    setOp(ops, 0x84, "STY", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0x85, "STA", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0x86, "STX", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0x87, "STA", 2, AddrMode::DirectIndirectLong, 6);
    setOp(ops, 0x88, "DEY", 1, AddrMode::Implied, 2);
    setOp(ops, 0x89, "BIT", 2, AddrMode::ImmediateM, 2);
    setOp(ops, 0x8A, "TXA", 1, AddrMode::Implied, 2);
    setOp(ops, 0x8B, "PHB", 1, AddrMode::Implied, 3);
    setOp(ops, 0x8C, "STY", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x8D, "STA", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x8E, "STX", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x8F, "STA", 4, AddrMode::AbsoluteLong, 5);

    setOp(ops, 0x90, "BCC", 2, AddrMode::Relative8, 2);
    setOp(ops, 0x91, "STA", 2, AddrMode::DirectIndirectY, 6);
    setOp(ops, 0x92, "STA", 2, AddrMode::DirectIndirect, 5);
    setOp(ops, 0x93, "STA", 2, AddrMode::StackRelativeIndirectY, 7);
    setOp(ops, 0x94, "STY", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0x95, "STA", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0x96, "STX", 2, AddrMode::DirectPageY, 4);
    setOp(ops, 0x97, "STA", 2, AddrMode::DirectIndirectLongY, 6);
    setOp(ops, 0x98, "TYA", 1, AddrMode::Implied, 2);
    setOp(ops, 0x99, "STA", 3, AddrMode::AbsoluteY, 5);
    setOp(ops, 0x9A, "TXS", 1, AddrMode::Implied, 2);
    setOp(ops, 0x9B, "TXY", 1, AddrMode::Implied, 2);
    setOp(ops, 0x9C, "STZ", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x9D, "STA", 3, AddrMode::AbsoluteX, 5);
    setOp(ops, 0x9E, "STZ", 3, AddrMode::AbsoluteX, 5);
    setOp(ops, 0x9F, "STA", 4, AddrMode::AbsoluteLongX, 5);

    setOp(ops, 0xA0, "LDY", 2, AddrMode::ImmediateX, 2);
    setOp(ops, 0xA1, "LDA", 2, AddrMode::DirectXIndirect, 6);
    setOp(ops, 0xA2, "LDX", 2, AddrMode::ImmediateX, 2);
    setOp(ops, 0xA3, "LDA", 2, AddrMode::StackRelative, 4);
    setOp(ops, 0xA4, "LDY", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0xA5, "LDA", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0xA6, "LDX", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0xA7, "LDA", 2, AddrMode::DirectIndirectLong, 6);
    setOp(ops, 0xA8, "TAY", 1, AddrMode::Implied, 2);
    setOp(ops, 0xA9, "LDA", 2, AddrMode::ImmediateM, 2);
    setOp(ops, 0xAA, "TAX", 1, AddrMode::Implied, 2);
    setOp(ops, 0xAB, "PLB", 1, AddrMode::Implied, 4);
    setOp(ops, 0xAC, "LDY", 3, AddrMode::Absolute, 4);
    setOp(ops, 0xAD, "LDA", 3, AddrMode::Absolute, 4);
    setOp(ops, 0xAE, "LDX", 3, AddrMode::Absolute, 4);
    setOp(ops, 0xAF, "LDA", 4, AddrMode::AbsoluteLong, 5);

    setOp(ops, 0xB0, "BCS", 2, AddrMode::Relative8, 2);
    setOp(ops, 0xB1, "LDA", 2, AddrMode::DirectIndirectY, 5);
    setOp(ops, 0xB2, "LDA", 2, AddrMode::DirectIndirect, 5);
    setOp(ops, 0xB3, "LDA", 2, AddrMode::StackRelativeIndirectY, 7);
    setOp(ops, 0xB4, "LDY", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0xB5, "LDA", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0xB6, "LDX", 2, AddrMode::DirectPageY, 4);
    setOp(ops, 0xB7, "LDA", 2, AddrMode::DirectIndirectLongY, 6);
    setOp(ops, 0xB8, "CLV", 1, AddrMode::Implied, 2);
    setOp(ops, 0xB9, "LDA", 3, AddrMode::AbsoluteY, 4);
    setOp(ops, 0xBA, "TSX", 1, AddrMode::Implied, 2);
    setOp(ops, 0xBB, "TYX", 1, AddrMode::Implied, 2);
    setOp(ops, 0xBC, "LDY", 3, AddrMode::AbsoluteX, 4);
    setOp(ops, 0xBD, "LDA", 3, AddrMode::AbsoluteX, 4);
    setOp(ops, 0xBE, "LDX", 3, AddrMode::AbsoluteY, 4);
    setOp(ops, 0xBF, "LDA", 4, AddrMode::AbsoluteLongX, 5);

    setOp(ops, 0xC0, "CPY", 2, AddrMode::ImmediateX, 2);
    setOp(ops, 0xC1, "CMP", 2, AddrMode::DirectXIndirect, 6);
    setOp(ops, 0xC2, "REP", 2, AddrMode::Immediate8, 3);
    setOp(ops, 0xC3, "CMP", 2, AddrMode::StackRelative, 4);
    setOp(ops, 0xC4, "CPY", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0xC5, "CMP", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0xC6, "DEC", 2, AddrMode::DirectPage, 5);
    setOp(ops, 0xC7, "CMP", 2, AddrMode::DirectIndirectLong, 6);
    setOp(ops, 0xC8, "INY", 1, AddrMode::Implied, 2);
    setOp(ops, 0xC9, "CMP", 2, AddrMode::ImmediateM, 2);
    setOp(ops, 0xCA, "DEX", 1, AddrMode::Implied, 2);
    setOp(ops, 0xCB, "WAI", 1, AddrMode::Implied, 3);
    setOp(ops, 0xCC, "CPY", 3, AddrMode::Absolute, 4);
    setOp(ops, 0xCD, "CMP", 3, AddrMode::Absolute, 4);
    setOp(ops, 0xCE, "DEC", 3, AddrMode::Absolute, 6);
    setOp(ops, 0xCF, "CMP", 4, AddrMode::AbsoluteLong, 5);

    setOp(ops, 0xD0, "BNE", 2, AddrMode::Relative8, 2);
    setOp(ops, 0xD1, "CMP", 2, AddrMode::DirectIndirectY, 5);
    setOp(ops, 0xD2, "CMP", 2, AddrMode::DirectIndirect, 5);
    setOp(ops, 0xD3, "CMP", 2, AddrMode::StackRelativeIndirectY, 7);
    setOp(ops, 0xD4, "PEI", 2, AddrMode::DirectIndirect, 6);
    setOp(ops, 0xD5, "CMP", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0xD6, "DEC", 2, AddrMode::DirectPageX, 6);
    setOp(ops, 0xD7, "CMP", 2, AddrMode::DirectIndirectLongY, 6);
    setOp(ops, 0xD8, "CLD", 1, AddrMode::Implied, 2);
    setOp(ops, 0xD9, "CMP", 3, AddrMode::AbsoluteY, 4);
    setOp(ops, 0xDA, "PHX", 1, AddrMode::Implied, 3);
    setOp(ops, 0xDB, "STP", 1, AddrMode::Implied, 3);
    setOp(ops, 0xDC, "JMP", 3, AddrMode::AbsoluteIndirectLong, 6);
    setOp(ops, 0xDD, "CMP", 3, AddrMode::AbsoluteX, 4);
    setOp(ops, 0xDE, "DEC", 3, AddrMode::AbsoluteX, 7);
    setOp(ops, 0xDF, "CMP", 4, AddrMode::AbsoluteLongX, 5);

    setOp(ops, 0xE0, "CPX", 2, AddrMode::ImmediateX, 2);
    setOp(ops, 0xE1, "SBC", 2, AddrMode::DirectXIndirect, 6);
    setOp(ops, 0xE2, "SEP", 2, AddrMode::Immediate8, 3);
    setOp(ops, 0xE3, "SBC", 2, AddrMode::StackRelative, 4);
    setOp(ops, 0xE4, "CPX", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0xE5, "SBC", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0xE6, "INC", 2, AddrMode::DirectPage, 5);
    setOp(ops, 0xE7, "SBC", 2, AddrMode::DirectIndirectLong, 6);
    setOp(ops, 0xE8, "INX", 1, AddrMode::Implied, 2);
    setOp(ops, 0xE9, "SBC", 2, AddrMode::ImmediateM, 2);
    setOp(ops, 0xEA, "NOP", 1, AddrMode::Implied, 2);
    setOp(ops, 0xEB, "XBA", 1, AddrMode::Implied, 3);
    setOp(ops, 0xEC, "CPX", 3, AddrMode::Absolute, 4);
    setOp(ops, 0xED, "SBC", 3, AddrMode::Absolute, 4);
    setOp(ops, 0xEE, "INC", 3, AddrMode::Absolute, 6);
    setOp(ops, 0xEF, "SBC", 4, AddrMode::AbsoluteLong, 5);

    setOp(ops, 0xF0, "BEQ", 2, AddrMode::Relative8, 2);
    setOp(ops, 0xF1, "SBC", 2, AddrMode::DirectIndirectY, 5);
    setOp(ops, 0xF2, "SBC", 2, AddrMode::DirectIndirect, 5);
    setOp(ops, 0xF3, "SBC", 2, AddrMode::StackRelativeIndirectY, 7);
    setOp(ops, 0xF4, "PEA", 3, AddrMode::Absolute, 5);
    setOp(ops, 0xF5, "SBC", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0xF6, "INC", 2, AddrMode::DirectPageX, 6);
    setOp(ops, 0xF7, "SBC", 2, AddrMode::DirectIndirectLongY, 6);
    setOp(ops, 0xF8, "SED", 1, AddrMode::Implied, 2);
    setOp(ops, 0xF9, "SBC", 3, AddrMode::AbsoluteY, 4);
    setOp(ops, 0xFA, "PLX", 1, AddrMode::Implied, 4);
    setOp(ops, 0xFB, "XCE", 1, AddrMode::Implied, 2);
    setOp(ops, 0xFC, "JSR", 3, AddrMode::AbsoluteXIndirect, 8);
    setOp(ops, 0xFD, "SBC", 3, AddrMode::AbsoluteX, 4);
    setOp(ops, 0xFE, "INC", 3, AddrMode::AbsoluteX, 7);
    setOp(ops, 0xFF, "SBC", 4, AddrMode::AbsoluteLongX, 5);

    return ops;
}

std::array<CpuOpcode, 256> buildApuOpcodeTable() {
    std::array<CpuOpcode, 256> ops{};
    ops.fill(makeUnknown());
    setOp(ops, 0x00, "NOP", 1, AddrMode::Implied, 2);
    setOp(ops, 0x01, "TCALL0", 1, AddrMode::Implied, 8);
    setOp(ops, 0x02, "SET1", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0x03, "BBS", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0x04, "OR", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0x05, "OR", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x06, "OR", 1, AddrMode::SpcIndirectX, 3);
    setOp(ops, 0x07, "OR", 2, AddrMode::DirectXIndirect, 6);
    setOp(ops, 0x08, "OR", 2, AddrMode::Immediate8, 2);
    setOp(ops, 0x09, "OR", 3, AddrMode::SpcDpPair, 6);
    setOp(ops, 0x0A, "OR1", 3, AddrMode::Absolute, 5);
    setOp(ops, 0x0B, "ASL", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0x0C, "ASL", 3, AddrMode::Absolute, 5);
    setOp(ops, 0x0D, "PUSH", 1, AddrMode::Implied, 4);
    setOp(ops, 0x0E, "TSET1", 3, AddrMode::Absolute, 6);
    setOp(ops, 0x0F, "BRK", 1, AddrMode::Implied, 8);
    setOp(ops, 0x10, "BPL", 2, AddrMode::Relative8, 2);
    setOp(ops, 0x11, "TCALL1", 1, AddrMode::Implied, 8);
    setOp(ops, 0x12, "CLR1", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0x13, "BBC", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0x14, "OR", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0x15, "OR", 3, AddrMode::AbsoluteX, 5);
    setOp(ops, 0x16, "OR", 3, AddrMode::AbsoluteY, 5);
    setOp(ops, 0x17, "OR", 2, AddrMode::DirectIndirectY, 6);
    setOp(ops, 0x18, "OR", 3, AddrMode::SpcDpImm8, 5);
    setOp(ops, 0x19, "OR", 1, AddrMode::Implied, 5);
    setOp(ops, 0x1A, "DECW", 2, AddrMode::DirectPage, 6);
    setOp(ops, 0x1B, "ASL", 2, AddrMode::DirectPageX, 5);
    setOp(ops, 0x1C, "ASL", 1, AddrMode::Implied, 2);
    setOp(ops, 0x1D, "DEC", 1, AddrMode::Implied, 2);
    setOp(ops, 0x1E, "CMP", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x1F, "JMP", 3, AddrMode::AbsoluteXIndirect, 6);
    setOp(ops, 0x20, "CLRP", 1, AddrMode::Implied, 2);
    setOp(ops, 0x21, "TCALL2", 1, AddrMode::Implied, 8);
    setOp(ops, 0x22, "SET1", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0x23, "BBS", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0x24, "AND", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0x25, "AND", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x26, "AND", 1, AddrMode::SpcIndirectX, 3);
    setOp(ops, 0x27, "AND", 2, AddrMode::DirectXIndirect, 6);
    setOp(ops, 0x28, "AND", 2, AddrMode::Immediate8, 2);
    setOp(ops, 0x29, "AND", 3, AddrMode::SpcDpPair, 6);
    setOp(ops, 0x2A, "OR1", 3, AddrMode::Absolute, 5);
    setOp(ops, 0x2B, "ROL", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0x2C, "ROL", 3, AddrMode::Absolute, 5);
    setOp(ops, 0x2D, "PUSH", 1, AddrMode::Implied, 4);
    setOp(ops, 0x2E, "CBNE", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0x2F, "BRA", 2, AddrMode::Relative8, 4);
    setOp(ops, 0x30, "BMI", 2, AddrMode::Relative8, 2);
    setOp(ops, 0x31, "TCALL3", 1, AddrMode::Implied, 8);
    setOp(ops, 0x32, "CLR1", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0x33, "BBC", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0x34, "AND", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0x35, "AND", 3, AddrMode::AbsoluteX, 5);
    setOp(ops, 0x36, "AND", 3, AddrMode::AbsoluteY, 5);
    setOp(ops, 0x37, "AND", 2, AddrMode::DirectIndirectY, 6);
    setOp(ops, 0x38, "AND", 3, AddrMode::SpcDpImm8, 5);
    setOp(ops, 0x39, "AND", 1, AddrMode::Implied, 5);
    setOp(ops, 0x3A, "INCW", 2, AddrMode::DirectPage, 6);
    setOp(ops, 0x3B, "ROL", 2, AddrMode::DirectPageX, 5);
    setOp(ops, 0x3C, "ROL", 1, AddrMode::Implied, 2);
    setOp(ops, 0x3D, "INC", 1, AddrMode::Implied, 2);
    setOp(ops, 0x3E, "CMP", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0x3F, "CALL", 3, AddrMode::Absolute, 8);
    setOp(ops, 0x40, "SETP", 1, AddrMode::Implied, 2);
    setOp(ops, 0x41, "TCALL4", 1, AddrMode::Implied, 8);
    setOp(ops, 0x42, "SET1", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0x43, "BBS", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0x44, "EOR", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0x45, "EOR", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x46, "EOR", 1, AddrMode::SpcIndirectX, 3);
    setOp(ops, 0x47, "EOR", 2, AddrMode::DirectXIndirect, 6);
    setOp(ops, 0x48, "EOR", 2, AddrMode::Immediate8, 2);
    setOp(ops, 0x49, "EOR", 3, AddrMode::SpcDpPair, 6);
    setOp(ops, 0x4A, "AND1", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x4B, "LSR", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0x4C, "LSR", 3, AddrMode::Absolute, 5);
    setOp(ops, 0x4D, "PUSH", 1, AddrMode::Implied, 4);
    setOp(ops, 0x4E, "TCLR1", 3, AddrMode::Absolute, 6);
    setOp(ops, 0x4F, "PCALL", 2, AddrMode::Immediate8, 6);
    setOp(ops, 0x50, "BVC", 2, AddrMode::Relative8, 2);
    setOp(ops, 0x51, "TCALL5", 1, AddrMode::Implied, 8);
    setOp(ops, 0x52, "CLR1", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0x53, "BBC", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0x54, "EOR", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0x55, "EOR", 3, AddrMode::AbsoluteX, 5);
    setOp(ops, 0x56, "EOR", 3, AddrMode::AbsoluteY, 5);
    setOp(ops, 0x57, "EOR", 2, AddrMode::DirectIndirectY, 6);
    setOp(ops, 0x58, "EOR", 3, AddrMode::SpcDpImm8, 5);
    setOp(ops, 0x59, "EOR", 1, AddrMode::Implied, 5);
    setOp(ops, 0x5A, "CMPW", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0x5B, "LSR", 2, AddrMode::DirectPageX, 5);
    setOp(ops, 0x5C, "LSR", 1, AddrMode::Implied, 2);
    setOp(ops, 0x5D, "MOV", 1, AddrMode::Implied, 2);
    setOp(ops, 0x5E, "CMP", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x5F, "JMP", 3, AddrMode::Absolute, 3);
    setOp(ops, 0x60, "CLRC", 1, AddrMode::Implied, 2);
    setOp(ops, 0x61, "TCALL6", 1, AddrMode::Implied, 8);
    setOp(ops, 0x62, "SET1", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0x63, "BBS", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0x64, "CMP", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0x65, "CMP", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x66, "CMP", 1, AddrMode::SpcIndirectX, 3);
    setOp(ops, 0x67, "CMP", 2, AddrMode::DirectXIndirect, 6);
    setOp(ops, 0x68, "CMP", 2, AddrMode::Immediate8, 2);
    setOp(ops, 0x69, "CMP", 3, AddrMode::SpcDpPair, 6);
    setOp(ops, 0x6A, "AND1", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x6B, "ROR", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0x6C, "ROR", 3, AddrMode::Absolute, 5);
    setOp(ops, 0x6D, "PUSH", 1, AddrMode::Implied, 4);
    setOp(ops, 0x6E, "DBNZ", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0x6F, "RET", 1, AddrMode::Implied, 5);
    setOp(ops, 0x70, "BVS", 2, AddrMode::Relative8, 2);
    setOp(ops, 0x71, "TCALL7", 1, AddrMode::Implied, 8);
    setOp(ops, 0x72, "CLR1", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0x73, "BBC", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0x74, "CMP", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0x75, "CMP", 3, AddrMode::AbsoluteX, 5);
    setOp(ops, 0x76, "CMP", 3, AddrMode::AbsoluteY, 5);
    setOp(ops, 0x77, "CMP", 2, AddrMode::DirectIndirectY, 6);
    setOp(ops, 0x78, "CMP", 3, AddrMode::SpcDpImm8, 5);
    setOp(ops, 0x79, "CMP", 1, AddrMode::Implied, 5);
    setOp(ops, 0x7A, "ADDW", 2, AddrMode::DirectPage, 5);
    setOp(ops, 0x7B, "ROR", 2, AddrMode::DirectPageX, 5);
    setOp(ops, 0x7C, "ROR", 1, AddrMode::Implied, 2);
    setOp(ops, 0x7D, "MOV", 1, AddrMode::Implied, 2);
    setOp(ops, 0x7E, "CMP", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0x7F, "RET1", 1, AddrMode::Implied, 6);
    setOp(ops, 0x80, "SETC", 1, AddrMode::Implied, 2);
    setOp(ops, 0x81, "TCALL8", 1, AddrMode::Implied, 8);
    setOp(ops, 0x82, "SET1", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0x83, "BBS", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0x84, "ADC", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0x85, "ADC", 3, AddrMode::Absolute, 4);
    setOp(ops, 0x86, "ADC", 1, AddrMode::SpcIndirectX, 3);
    setOp(ops, 0x87, "ADC", 2, AddrMode::DirectXIndirect, 6);
    setOp(ops, 0x88, "ADC", 2, AddrMode::Immediate8, 2);
    setOp(ops, 0x89, "ADC", 3, AddrMode::SpcDpPair, 6);
    setOp(ops, 0x8A, "EOR1", 3, AddrMode::Absolute, 5);
    setOp(ops, 0x8B, "DEC", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0x8C, "DEC", 3, AddrMode::Absolute, 5);
    setOp(ops, 0x8D, "MOV", 2, AddrMode::Immediate8, 2);
    setOp(ops, 0x8E, "POP", 1, AddrMode::Implied, 4);
    setOp(ops, 0x8F, "MOV", 3, AddrMode::SpcDpImm8, 5);
    setOp(ops, 0x90, "BCC", 2, AddrMode::Relative8, 2);
    setOp(ops, 0x91, "TCALL9", 1, AddrMode::Implied, 8);
    setOp(ops, 0x92, "CLR1", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0x93, "BBC", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0x94, "ADC", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0x95, "ADC", 3, AddrMode::AbsoluteX, 5);
    setOp(ops, 0x96, "ADC", 3, AddrMode::AbsoluteY, 5);
    setOp(ops, 0x97, "ADC", 2, AddrMode::DirectIndirectY, 6);
    setOp(ops, 0x98, "ADC", 3, AddrMode::SpcDpImm8, 5);
    setOp(ops, 0x99, "ADC", 1, AddrMode::Implied, 5);
    setOp(ops, 0x9A, "SUBW", 2, AddrMode::DirectPage, 5);
    setOp(ops, 0x9B, "DEC", 2, AddrMode::DirectPageX, 5);
    setOp(ops, 0x9C, "DEC", 1, AddrMode::Implied, 2);
    setOp(ops, 0x9D, "MOV", 1, AddrMode::Implied, 2);
    setOp(ops, 0x9E, "DIV", 1, AddrMode::Implied, 12);
    setOp(ops, 0x9F, "XCN", 1, AddrMode::Implied, 5);
    setOp(ops, 0xA0, "EI", 1, AddrMode::Implied, 3);
    setOp(ops, 0xA1, "TCALL10", 1, AddrMode::Implied, 8);
    setOp(ops, 0xA2, "SET1", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0xA3, "BBS", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0xA4, "SBC", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0xA5, "SBC", 3, AddrMode::Absolute, 4);
    setOp(ops, 0xA6, "SBC", 1, AddrMode::SpcIndirectX, 3);
    setOp(ops, 0xA7, "SBC", 2, AddrMode::DirectXIndirect, 6);
    setOp(ops, 0xA8, "SBC", 2, AddrMode::Immediate8, 2);
    setOp(ops, 0xA9, "SBC", 3, AddrMode::SpcDpPair, 6);
    setOp(ops, 0xAA, "MOV1", 3, AddrMode::Absolute, 4);
    setOp(ops, 0xAB, "INC", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0xAC, "INC", 3, AddrMode::Absolute, 5);
    setOp(ops, 0xAD, "CMP", 2, AddrMode::Immediate8, 2);
    setOp(ops, 0xAE, "POP", 1, AddrMode::Implied, 4);
    setOp(ops, 0xAF, "MOV", 1, AddrMode::SpcIndirectXInc, 4);
    setOp(ops, 0xB0, "BCS", 2, AddrMode::Relative8, 2);
    setOp(ops, 0xB1, "TCALL11", 1, AddrMode::Implied, 8);
    setOp(ops, 0xB2, "CLR1", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0xB3, "BBC", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0xB4, "SBC", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0xB5, "SBC", 3, AddrMode::AbsoluteX, 5);
    setOp(ops, 0xB6, "SBC", 3, AddrMode::AbsoluteY, 5);
    setOp(ops, 0xB7, "SBC", 2, AddrMode::DirectIndirectY, 6);
    setOp(ops, 0xB8, "SBC", 3, AddrMode::SpcDpImm8, 5);
    setOp(ops, 0xB9, "SBC", 1, AddrMode::Implied, 5);
    setOp(ops, 0xBA, "MOVW", 2, AddrMode::DirectPage, 5);
    setOp(ops, 0xBB, "INC", 2, AddrMode::DirectPageX, 5);
    setOp(ops, 0xBC, "INC", 1, AddrMode::Implied, 2);
    setOp(ops, 0xBD, "MOV", 1, AddrMode::Implied, 2);
    setOp(ops, 0xBE, "DAS", 1, AddrMode::Implied, 3);
    setOp(ops, 0xBF, "MOV", 1, AddrMode::SpcIndirectXInc, 4);
    setOp(ops, 0xC0, "DI", 1, AddrMode::Implied, 3);
    setOp(ops, 0xC1, "TCALL12", 1, AddrMode::Implied, 8);
    setOp(ops, 0xC2, "SET1", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0xC3, "BBS", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0xC4, "MOV", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0xC5, "MOV", 3, AddrMode::Absolute, 5);
    setOp(ops, 0xC6, "MOV", 1, AddrMode::SpcIndirectX, 4);
    setOp(ops, 0xC7, "MOV", 2, AddrMode::DirectXIndirect, 7);
    setOp(ops, 0xC8, "CMP", 2, AddrMode::Immediate8, 2);
    setOp(ops, 0xC9, "MOV", 3, AddrMode::Absolute, 5);
    setOp(ops, 0xCA, "MOV1", 3, AddrMode::Absolute, 6);
    setOp(ops, 0xCB, "MOV", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0xCC, "MOV", 3, AddrMode::Absolute, 5);
    setOp(ops, 0xCD, "MOV", 2, AddrMode::Immediate8, 2);
    setOp(ops, 0xCE, "POP", 1, AddrMode::Implied, 4);
    setOp(ops, 0xCF, "MUL", 1, AddrMode::Implied, 9);
    setOp(ops, 0xD0, "BNE", 2, AddrMode::Relative8, 2);
    setOp(ops, 0xD1, "TCALL13", 1, AddrMode::Implied, 8);
    setOp(ops, 0xD2, "CLR1", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0xD3, "BBC", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0xD4, "MOV", 2, AddrMode::DirectPageX, 5);
    setOp(ops, 0xD5, "MOV", 3, AddrMode::AbsoluteX, 6);
    setOp(ops, 0xD6, "MOV", 3, AddrMode::AbsoluteY, 6);
    setOp(ops, 0xD7, "MOV", 2, AddrMode::DirectIndirectY, 7);
    setOp(ops, 0xD8, "MOV", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0xD9, "MOV", 2, AddrMode::DirectPageY, 5);
    setOp(ops, 0xDA, "MOVW", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0xDB, "MOV", 2, AddrMode::DirectPageX, 5);
    setOp(ops, 0xDC, "DEC", 1, AddrMode::Implied, 2);
    setOp(ops, 0xDD, "MOV", 1, AddrMode::Implied, 2);
    setOp(ops, 0xDE, "CBNE", 3, AddrMode::SpcDpRel8, 6);
    setOp(ops, 0xDF, "DAA", 1, AddrMode::Implied, 3);
    setOp(ops, 0xE0, "CLRV", 1, AddrMode::Implied, 2);
    setOp(ops, 0xE1, "TCALL14", 1, AddrMode::Implied, 8);
    setOp(ops, 0xE2, "SET1", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0xE3, "BBS", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0xE4, "MOV", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0xE5, "MOV", 3, AddrMode::Absolute, 4);
    setOp(ops, 0xE6, "MOV", 1, AddrMode::SpcIndirectX, 3);
    setOp(ops, 0xE7, "MOV", 2, AddrMode::DirectXIndirect, 6);
    setOp(ops, 0xE8, "MOV", 2, AddrMode::Immediate8, 2);
    setOp(ops, 0xE9, "MOV", 3, AddrMode::Absolute, 4);
    setOp(ops, 0xEA, "NOT1", 3, AddrMode::Absolute, 5);
    setOp(ops, 0xEB, "MOV", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0xEC, "MOV", 3, AddrMode::Absolute, 4);
    setOp(ops, 0xED, "NOTC", 1, AddrMode::Implied, 3);
    setOp(ops, 0xEE, "POP", 1, AddrMode::Implied, 4);
    setOp(ops, 0xEF, "SLEEP", 1, AddrMode::Implied, 3);
    setOp(ops, 0xF0, "BEQ", 2, AddrMode::Relative8, 2);
    setOp(ops, 0xF1, "TCALL15", 1, AddrMode::Implied, 8);
    setOp(ops, 0xF2, "CLR1", 2, AddrMode::DirectPage, 4);
    setOp(ops, 0xF3, "BBC", 3, AddrMode::SpcDpRel8, 5);
    setOp(ops, 0xF4, "MOV", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0xF5, "MOV", 3, AddrMode::AbsoluteX, 5);
    setOp(ops, 0xF6, "MOV", 3, AddrMode::AbsoluteY, 5);
    setOp(ops, 0xF7, "MOV", 2, AddrMode::DirectIndirectY, 6);
    setOp(ops, 0xF8, "MOV", 2, AddrMode::DirectPage, 3);
    setOp(ops, 0xF9, "MOV", 2, AddrMode::DirectPageY, 4);
    setOp(ops, 0xFA, "MOV", 3, AddrMode::SpcDpPair, 5);
    setOp(ops, 0xFB, "MOV", 2, AddrMode::DirectPageX, 4);
    setOp(ops, 0xFC, "INC", 1, AddrMode::Implied, 2);
    setOp(ops, 0xFD, "MOV", 1, AddrMode::Implied, 2);
    setOp(ops, 0xFE, "DBNZ", 2, AddrMode::Relative8, 4);
    setOp(ops, 0xFF, "STOP", 1, AddrMode::Implied, 3);

    return ops;
}

// Super FX (GSU) baseline decode table from the Super Famicom Development Wiki opcode matrix
// (<https://wiki.superfamicom.org/super-fx-opcode-matrix>). Cycles are rough placeholders.

constexpr const char* kGsuTo[16] = {
    "TO R0",
    "TO R1",
    "TO R2",
    "TO R3",
    "TO R4",
    "TO R5",
    "TO R6",
    "TO R7",
    "TO R8",
    "TO R9",
    "TO R10",
    "TO R11",
    "TO R12",
    "TO R13",
    "TO R14",
    "TO R15",
};

constexpr const char* kGsuMove[16] = {
    "MOVE R0",
    "MOVE R1",
    "MOVE R2",
    "MOVE R3",
    "MOVE R4",
    "MOVE R5",
    "MOVE R6",
    "MOVE R7",
    "MOVE R8",
    "MOVE R9",
    "MOVE R10",
    "MOVE R11",
    "MOVE R12",
    "MOVE R13",
    "MOVE R14",
    "MOVE R15",
};

constexpr const char* kGsuFrom[16] = {
    "FROM R0",
    "FROM R1",
    "FROM R2",
    "FROM R3",
    "FROM R4",
    "FROM R5",
    "FROM R6",
    "FROM R7",
    "FROM R8",
    "FROM R9",
    "FROM R10",
    "FROM R11",
    "FROM R12",
    "FROM R13",
    "FROM R14",
    "FROM R15",
};

constexpr const char* kGsuAdd[16] = {
    "ADD R0",
    "ADD R1",
    "ADD R2",
    "ADD R3",
    "ADD R4",
    "ADD R5",
    "ADD R6",
    "ADD R7",
    "ADD R8",
    "ADD R9",
    "ADD R10",
    "ADD R11",
    "ADD R12",
    "ADD R13",
    "ADD R14",
    "ADD R15",
};

constexpr const char* kGsuSub[16] = {
    "SUB R0",
    "SUB R1",
    "SUB R2",
    "SUB R3",
    "SUB R4",
    "SUB R5",
    "SUB R6",
    "SUB R7",
    "SUB R8",
    "SUB R9",
    "SUB R10",
    "SUB R11",
    "SUB R12",
    "SUB R13",
    "SUB R14",
    "SUB R15",
};

constexpr const char* kGsuMult[16] = {
    "MULT R0",
    "MULT R1",
    "MULT R2",
    "MULT R3",
    "MULT R4",
    "MULT R5",
    "MULT R6",
    "MULT R7",
    "MULT R8",
    "MULT R9",
    "MULT R10",
    "MULT R11",
    "MULT R12",
    "MULT R13",
    "MULT R14",
    "MULT R15",
};

constexpr const char* kGsuStwRn[16] = {
    "STW R0",
    "STW R1",
    "STW R2",
    "STW R3",
    "STW R4",
    "STW R5",
    "STW R6",
    "STW R7",
    "STW R8",
    "STW R9",
    "STW R10",
    "STW R11",
    "STW R12",
    "STW R13",
    "STW R14",
    "STW R15",
};

constexpr const char* kGsuLdwParen[16] = {
    "LDW (R0)",
    "LDW (R1)",
    "LDW (R2)",
    "LDW (R3)",
    "LDW (R4)",
    "LDW (R5)",
    "LDW (R6)",
    "LDW (R7)",
    "LDW (R8)",
    "LDW (R9)",
    "LDW (R10)",
    "LDW (R11)",
    "LDW (R12)",
    "LDW (R13)",
    "LDW (R14)",
    "LDW (R15)",
};

constexpr const char* kGsuMerAnd[16] = {
    "MERGE",
    "AND R1",
    "AND R2",
    "AND R3",
    "AND R4",
    "AND R5",
    "AND R6",
    "AND R7",
    "AND R8",
    "AND R9",
    "AND R10",
    "AND R11",
    "AND R12",
    "AND R13",
    "AND R14",
    "AND R15",
};

constexpr const char* kGsuHibOr[16] = {
    "HIB",
    "OR R1",
    "OR R2",
    "OR R3",
    "OR R4",
    "OR R5",
    "OR R6",
    "OR R7",
    "OR R8",
    "OR R9",
    "OR R10",
    "OR R11",
    "OR R12",
    "OR R13",
    "OR R14",
    "OR R15",
};

constexpr const char* kGsuIncGetc[16] = {
    "INC R0",
    "INC R1",
    "INC R2",
    "INC R3",
    "INC R4",
    "INC R5",
    "INC R6",
    "INC R7",
    "INC R8",
    "INC R9",
    "INC R10",
    "INC R11",
    "INC R12",
    "INC R13",
    "INC R14",
    "GETC",
};

constexpr const char* kGsuDecGetb[16] = {
    "DEC R0",
    "DEC R1",
    "DEC R2",
    "DEC R3",
    "DEC R4",
    "DEC R5",
    "DEC R6",
    "DEC R7",
    "DEC R8",
    "DEC R9",
    "DEC R10",
    "DEC R11",
    "DEC R12",
    "DEC R13",
    "DEC R14",
    "GETB",
};

constexpr const char* kGsuIbt[16] = {
    "IBT R0",
    "IBT R1",
    "IBT R2",
    "IBT R3",
    "IBT R4",
    "IBT R5",
    "IBT R6",
    "IBT R7",
    "IBT R8",
    "IBT R9",
    "IBT R10",
    "IBT R11",
    "IBT R12",
    "IBT R13",
    "IBT R14",
    "IBT R15",
};

constexpr const char* kGsuIwt[16] = {
    "IWT R0",
    "IWT R1",
    "IWT R2",
    "IWT R3",
    "IWT R4",
    "IWT R5",
    "IWT R6",
    "IWT R7",
    "IWT R8",
    "IWT R9",
    "IWT R10",
    "IWT R11",
    "IWT R12",
    "IWT R13",
    "IWT R14",
    "IWT R15",
};

std::array<CpuOpcode, 256> buildGsuOpcodeTable() {
    constexpr uint16_t kCyc = 6;

    std::array<CpuOpcode, 256> ops{};
    ops.fill(makeUnknown());

    setOp(ops, 0x00, "STOP", 1, AddrMode::Implied, 96);
    setOp(ops, 0x01, "NOP", 1, AddrMode::Implied, 24);
    setOp(ops, 0x02, "CACHE", 1, AddrMode::Implied, 96);
    setOp(ops, 0x03, "LSR", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x04, "ROL", 1, AddrMode::Implied, kCyc);

    setOp(ops, 0x05, "BRA", 2, AddrMode::Relative8, kCyc);
    setOp(ops, 0x06, "BGE", 2, AddrMode::Relative8, kCyc);
    setOp(ops, 0x07, "BLT", 2, AddrMode::Relative8, kCyc);
    setOp(ops, 0x08, "BNE", 2, AddrMode::Relative8, kCyc);
    setOp(ops, 0x09, "BEQ", 2, AddrMode::Relative8, kCyc);
    setOp(ops, 0x0A, "BPL", 2, AddrMode::Relative8, kCyc);
    setOp(ops, 0x0B, "BMI", 2, AddrMode::Relative8, kCyc);
    setOp(ops, 0x0C, "BCC", 2, AddrMode::Relative8, kCyc);
    setOp(ops, 0x0D, "BCS", 2, AddrMode::Relative8, kCyc);
    setOp(ops, 0x0E, "BVC", 2, AddrMode::Relative8, kCyc);
    setOp(ops, 0x0F, "BVS", 2, AddrMode::Relative8, kCyc);

    for (size_t i = 0; i < 16; ++i) {
        setOp(ops,
              static_cast<uint8_t>(0x10 | i),
              kGsuTo[i],
              1,
              AddrMode::Implied,
              kCyc);
        setOp(ops,
              static_cast<uint8_t>(0x20 | i),
              kGsuMove[i],
              1,
              AddrMode::Implied,
              kCyc);
        setOp(ops,
              static_cast<uint8_t>(0x50 | i),
              kGsuAdd[i],
              1,
              AddrMode::Implied,
              kCyc);
        setOp(ops,
              static_cast<uint8_t>(0x60 | i),
              kGsuSub[i],
              1,
              AddrMode::Implied,
              kCyc);
        setOp(ops,
              static_cast<uint8_t>(0x80 | i),
              kGsuMult[i],
              1,
              AddrMode::Implied,
              kCyc);
        setOp(ops,
              static_cast<uint8_t>(0x70 | i),
              kGsuMerAnd[i],
              1,
              AddrMode::Implied,
              kCyc);
        setOp(ops,
              static_cast<uint8_t>(0xC0 | i),
              kGsuHibOr[i],
              1,
              AddrMode::Implied,
              kCyc);
        setOp(ops,
              static_cast<uint8_t>(0xD0 | i),
              kGsuIncGetc[i],
              1,
              AddrMode::Implied,
              kCyc);
        setOp(ops,
              static_cast<uint8_t>(0xE0 | i),
              kGsuDecGetb[i],
              1,
              AddrMode::Implied,
              kCyc);
        setOp(ops,
              static_cast<uint8_t>(0xA0 | i),
              kGsuIbt[i],
              2,
              AddrMode::Immediate8,
              kCyc);
        setOp(ops,
              static_cast<uint8_t>(0xF0 | i),
              kGsuIwt[i],
              3,
              AddrMode::GsuImmediateWord,
              kCyc);
        setOp(ops,
              static_cast<uint8_t>(0xB0 | i),
              kGsuFrom[i],
              1,
              AddrMode::Implied,
              kCyc);
    }

    // STW R0 … STW R11 (opcode 30–3B); LOOP / ALT mods at end of row.
    for (size_t i = 0; i < 12; ++i) {
        setOp(ops,
              static_cast<uint8_t>(0x30 | i),
              kGsuStwRn[i],
              1,
              AddrMode::Implied,
              kCyc);
    }
    setOp(ops, 0x3C, "LOOP", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x3D, "ALT1", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x3E, "ALT2", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x3F, "ALT3", 1, AddrMode::Implied, kCyc);

    // LDW (R0) … LDW (R11) — opcode 40–4B; PIXEL-ish ops finish the row.
    for (size_t i = 0; i < 12; ++i) {
        setOp(ops,
              static_cast<uint8_t>(0x40 | i),
              kGsuLdwParen[i],
              1,
              AddrMode::Implied,
              kCyc);
    }
    setOp(ops, 0x4C, "PLOT", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x4D, "SWAP", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x4E, "COLOR", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x4F, "NOT", 1, AddrMode::Implied, kCyc);

    setOp(ops, 0x90, "SBK", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x91, "LINK #1", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x92, "LINK #2", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x93, "LINK #3", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x94, "LINK #4", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x95, "SEX", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x96, "ASR", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x97, "ROR", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x98, "JMP", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x99, "JMP", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x9A, "JMP", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x9B, "JMP", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x9C, "JMP", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x9D, "JMP", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x9E, "LOB", 1, AddrMode::Implied, kCyc);
    setOp(ops, 0x9F, "FMULT", 1, AddrMode::Implied, kCyc);

    return ops;
}

} // namespace

void printMissingCpuOpcodes(const std::array<CpuOpcode, 256>& ops) {
    int count = 0;

    std::cout << "=== Missing Opcodes ===\n";

    for (int i = 0; i < 256; ++i) {
        if (!ops[i].valid) {
            if (count % 8 == 0 && count != 0) {
                std::cout << "\n";
            }

            std::cout << "0x"
                      << std::hex << std::uppercase
                      << std::setw(2) << std::setfill('0')
                      << i << " ";

            count++;
        }
    }

    std::cout << "Total missing: " << std::dec << count << "\n";
}

const std::array<CpuOpcode, 256> cpuOpcodesTable = buildCpuOpcodeTable();
const std::array<CpuOpcode, 256> apuOpcodesTable = buildApuOpcodeTable();
const std::array<CpuOpcode, 256> gsuOpcodesTable = buildGsuOpcodeTable();
