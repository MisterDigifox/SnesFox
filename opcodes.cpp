#include "opcodes.hpp"

#include <array>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {

Op makeUnknown() {
    return {"???", 1, AddrMode::Unknown, false};
}

void setOp(std::array<Op, 256>& ops, uint8_t opcode, const char* name, uint8_t size, AddrMode mode, uint16_t cyclesNumber) {
    if (ops[opcode].valid) {
        throw std::runtime_error("Duplicate opcode in table");
    }
    ops[opcode] = {name, size, mode, true, cyclesNumber};
}

std::array<Op, 256> buildOpcodeTable() {
    std::array<Op, 256> ops{};
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

} // namespace

void printMissingOpcodes(const std::array<Op, 256>& ops) {
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

const std::array<Op, 256> OPCODES = buildOpcodeTable();
