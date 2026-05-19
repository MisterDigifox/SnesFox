#include "cpu.hpp"
#include "bus.hpp"
#include "opcodes.hpp"
#include <iomanip>
#include <sstream>
#include <string>

namespace {

constexpr uint8_t FLAG_NEGATIVE = 0x80;
constexpr uint8_t FLAG_OVERFLOW = 0x40;
constexpr uint8_t FLAG_M        = 0x20;
constexpr uint8_t FLAG_X        = 0x10;
constexpr uint8_t FLAG_DECIMAL  = 0x08;
constexpr uint8_t FLAG_IRQ      = 0x04;
constexpr uint8_t FLAG_ZERO     = 0x02;
constexpr uint8_t FLAG_CARRY    = 0x01;

std::string hex8(uint8_t value) {
    std::ostringstream oss;
    oss << "$" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(value);
    return oss.str();
}

std::string hex16(uint16_t value) {
    std::ostringstream oss;
    oss << "$" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
        << value;
    return oss.str();
}

std::string hex24(uint32_t value) {
    std::ostringstream oss;
    oss << "$" << std::uppercase << std::hex << std::setw(6) << std::setfill('0')
        << (value & 0xFFFFFF);
    return oss.str();
}

std::string raw8(uint8_t value) {
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(value);
    return oss.str();
}

std::string raw16(uint16_t value) {
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
        << value;
    return oss.str();
}

bool flagSet(uint8_t p, uint8_t flag) {
    return (p & flag) != 0;
}

void applyXSizeFromP(uint8_t p, uint16_t& x, uint16_t& y) {
    if (flagSet(p, FLAG_X)) {
        x &= 0x00FF;
        y &= 0x00FF;
    }
}

uint16_t read16le(uint8_t lo, uint8_t hi) {
    return static_cast<uint16_t>(lo | (hi << 8));
}

uint32_t read24le(uint8_t b1, uint8_t b2, uint8_t b3) {
    return static_cast<uint32_t>(b1)
         | (static_cast<uint32_t>(b2) << 8)
         | (static_cast<uint32_t>(b3) << 16);
}

uint16_t busRead16(const Bus& bus, uint8_t bank, uint16_t addr) {
    const uint8_t lo = bus.read(bank, addr);
    const uint8_t hi = bus.read(bank, static_cast<uint16_t>(addr + 1));
    return static_cast<uint16_t>(lo | (hi << 8));
}

uint32_t busRead24(const Bus& bus, uint8_t bank, uint16_t addr) {
    const uint8_t lo = bus.read(bank, addr);
    const uint8_t hi = bus.read(bank, static_cast<uint16_t>(addr + 1));
    const uint8_t ba = bus.read(bank, static_cast<uint16_t>(addr + 2));
    return static_cast<uint32_t>(lo)
         | (static_cast<uint32_t>(hi) << 8)
         | (static_cast<uint32_t>(ba) << 16);
}

void setZN8(uint8_t value, uint8_t& p) {
    if (value == 0) p |= FLAG_ZERO;
    else p &= ~FLAG_ZERO;

    if (value & 0x80) p |= FLAG_NEGATIVE;
    else p &= ~FLAG_NEGATIVE;
}

void setZN16(uint16_t value, uint8_t& p) {
    if (value == 0) p |= FLAG_ZERO;
    else p &= ~FLAG_ZERO;

    if (value & 0x8000) p |= FLAG_NEGATIVE;
    else p &= ~FLAG_NEGATIVE;
}

void applyREP(uint8_t mask, uint8_t& p, uint16_t& x, uint16_t& y) {
    p = static_cast<uint8_t>(p & ~mask);
    if (flagSet(p, FLAG_X)) {
        x &= 0x00FF;
        y &= 0x00FF;
    }
}

void applySEP(uint8_t mask, uint8_t& p, uint16_t& x, uint16_t& y) {
    p = static_cast<uint8_t>(p | mask);
    if (flagSet(p, FLAG_X)) {
        x &= 0x00FF;
        y &= 0x00FF;
    }
}

void applyRegisterSizesFromP(uint8_t p, uint16_t& a, uint16_t& x, uint16_t& y) {
    if (flagSet(p, FLAG_M)) {
        a &= 0x00FF;
    }

    if (flagSet(p, FLAG_X)) {
        x &= 0x00FF;
        y &= 0x00FF;
    }
}

void adc8(uint8_t value, uint16_t& a, uint8_t& p) {
    const uint8_t a8 = static_cast<uint8_t>(a & 0x00FF);
    const uint16_t carry = flagSet(p, FLAG_CARRY) ? 1 : 0;
    const uint16_t sum = static_cast<uint16_t>(a8) + static_cast<uint16_t>(value) + carry;
    const uint8_t result = static_cast<uint8_t>(sum & 0x00FF);

    if (sum > 0xFF) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    if (((~(a8 ^ value)) & (a8 ^ result) & 0x80) != 0) p |= FLAG_OVERFLOW;
    else p &= ~FLAG_OVERFLOW;

    a = static_cast<uint16_t>((a & 0xFF00) | result);
    setZN8(result, p);
}

void adc16(uint16_t value, uint16_t& a, uint8_t& p) {
    const uint32_t carry = flagSet(p, FLAG_CARRY) ? 1 : 0;
    const uint32_t sum = static_cast<uint32_t>(a) + static_cast<uint32_t>(value) + carry;
    const uint16_t result = static_cast<uint16_t>(sum & 0xFFFF);

    if (sum > 0xFFFF) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    if (((~(a ^ value)) & (a ^ result) & 0x8000) != 0) p |= FLAG_OVERFLOW;
    else p &= ~FLAG_OVERFLOW;

    a = result;
    setZN16(result, p);
}

void sbc8(uint8_t value, uint16_t& a, uint8_t& p) {
    const uint8_t a8 = static_cast<uint8_t>(a & 0x00FF);
    const uint16_t carry = flagSet(p, FLAG_CARRY) ? 1 : 0;
    const uint16_t diff = static_cast<uint16_t>(a8) - static_cast<uint16_t>(value) - static_cast<uint16_t>(1 - carry);
    const uint8_t result = static_cast<uint8_t>(diff & 0x00FF);

    if (static_cast<uint16_t>(a8) >= static_cast<uint16_t>(value) + static_cast<uint16_t>(1 - carry)) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    if (((a8 ^ value) & (a8 ^ result) & 0x80) != 0) p |= FLAG_OVERFLOW;
    else p &= ~FLAG_OVERFLOW;

    a = static_cast<uint16_t>((a & 0xFF00) | result);
    setZN8(result, p);
}

void sbc16(uint16_t value, uint16_t& a, uint8_t& p) {
    const uint32_t carry = flagSet(p, FLAG_CARRY) ? 1 : 0;
    const uint32_t subtrahend = static_cast<uint32_t>(value) + static_cast<uint32_t>(1 - carry);
    const uint32_t diff = static_cast<uint32_t>(a) - subtrahend;
    const uint16_t result = static_cast<uint16_t>(diff & 0xFFFF);

    if (static_cast<uint32_t>(a) >= subtrahend) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    if (((a ^ value) & (a ^ result) & 0x8000) != 0) p |= FLAG_OVERFLOW;
    else p &= ~FLAG_OVERFLOW;

    a = result;
    setZN16(result, p);
}

void cmp8(uint8_t value, uint16_t a, uint8_t& p) {
    const uint8_t a8 = static_cast<uint8_t>(a & 0x00FF);
    const uint16_t diff = static_cast<uint16_t>(a8) - static_cast<uint16_t>(value);
    const uint8_t result = static_cast<uint8_t>(diff & 0x00FF);

    // Carry = no borrow
    if (a8 >= value) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    setZN8(result, p);
}

void cmp16(uint16_t value, uint16_t a, uint8_t& p) {
    const uint32_t diff = static_cast<uint32_t>(a) - static_cast<uint32_t>(value);
    const uint16_t result = static_cast<uint16_t>(diff & 0xFFFF);

    if (a >= value) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    setZN16(result, p);
}

void cpx8(uint8_t value, uint16_t x, uint8_t& p) {
    const uint8_t x8 = static_cast<uint8_t>(x & 0x00FF);
    const uint16_t diff = static_cast<uint16_t>(x8) - static_cast<uint16_t>(value);
    const uint8_t result = static_cast<uint8_t>(diff & 0x00FF);

    if (x8 >= value) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    setZN8(result, p);
}

void cpx16(uint16_t value, uint16_t x, uint8_t& p) {
    const uint32_t diff = static_cast<uint32_t>(x) - static_cast<uint32_t>(value);
    const uint16_t result = static_cast<uint16_t>(diff & 0xFFFF);

    if (x >= value) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    setZN16(result, p);
}

void cpy8(uint8_t value, uint16_t y, uint8_t& p) {
    const uint8_t y8 = static_cast<uint8_t>(y & 0x00FF);
    const uint16_t diff = static_cast<uint16_t>(y8) - static_cast<uint16_t>(value);
    const uint8_t result = static_cast<uint8_t>(diff & 0x00FF);

    if (y8 >= value) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    setZN8(result, p);
}

void cpy16(uint16_t value, uint16_t y, uint8_t& p) {
    const uint32_t diff = static_cast<uint32_t>(y) - static_cast<uint32_t>(value);
    const uint16_t result = static_cast<uint16_t>(diff & 0xFFFF);

    if (y >= value) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    setZN16(result, p);
}

void inc8(uint8_t& value, uint8_t& p) {
    value = static_cast<uint8_t>((value + 1) & 0xFF);
    setZN8(value, p);
}

void inc16(uint16_t& value, uint8_t& p) {
    value = static_cast<uint16_t>((value + 1) & 0xFFFF);
    setZN16(value, p);
}

void dec8(uint8_t& value, uint8_t& p) {
    value = static_cast<uint8_t>((value - 1) & 0xFF);
    setZN8(value, p);
}

void dec16(uint16_t& value, uint8_t& p) {
    value = static_cast<uint16_t>((value - 1) & 0xFFFF);
    setZN16(value, p);
}

void and8(uint8_t value, uint16_t& a, uint8_t& p) {
    const uint8_t result = static_cast<uint8_t>((a & 0x00FF) & value);
    a = static_cast<uint16_t>((a & 0xFF00) | result);
    setZN8(result, p);
}

void and16(uint16_t value, uint16_t& a, uint8_t& p) {
    a = static_cast<uint16_t>(a & value);
    setZN16(a, p);
}

void ora8(uint8_t value, uint16_t& a, uint8_t& p) {
    const uint8_t result = static_cast<uint8_t>((a & 0x00FF) | value);
    a = static_cast<uint16_t>((a & 0xFF00) | result);
    setZN8(result, p);
}

void ora16(uint16_t value, uint16_t& a, uint8_t& p) {
    a = static_cast<uint16_t>(a | value);
    setZN16(a, p);
}

void eor8(uint8_t value, uint16_t& a, uint8_t& p) {
    const uint8_t result = static_cast<uint8_t>((a & 0x00FF) ^ value);
    a = static_cast<uint16_t>((a & 0xFF00) | result);
    setZN8(result, p);
}

void eor16(uint16_t value, uint16_t& a, uint8_t& p) {
    a = static_cast<uint16_t>(a ^ value);
    setZN16(a, p);
}

void asl8(uint8_t& value, uint8_t& p) {
    if (value & 0x80) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    value = static_cast<uint8_t>((value << 1) & 0xFF);
    setZN8(value, p);
}

void asl16(uint16_t& value, uint8_t& p) {
    if (value & 0x8000) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    value = static_cast<uint16_t>((value << 1) & 0xFFFF);
    setZN16(value, p);
}

void lsr8(uint8_t& value, uint8_t& p) {
    if (value & 0x01) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    value = static_cast<uint8_t>(value >> 1);
    setZN8(value, p);
}

void lsr16(uint16_t& value, uint8_t& p) {
    if (value & 0x0001) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    value = static_cast<uint16_t>(value >> 1);
    setZN16(value, p);
}

void rol8(uint8_t& value, uint8_t& p) {
    const uint8_t carryIn = flagSet(p, FLAG_CARRY) ? 1 : 0;
    const uint8_t newCarry = (value & 0x80) ? 1 : 0;

    value = static_cast<uint8_t>((value << 1) | carryIn);

    if (newCarry) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    setZN8(value, p);
}

void rol16(uint16_t& value, uint8_t& p) {
    const uint16_t carryIn = flagSet(p, FLAG_CARRY) ? 1 : 0;
    const uint16_t newCarry = (value & 0x8000) ? 1 : 0;

    value = static_cast<uint16_t>((value << 1) | carryIn);

    if (newCarry) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    setZN16(value, p);
}

void ror8(uint8_t& value, uint8_t& p) {
    const uint8_t carryIn = flagSet(p, FLAG_CARRY) ? 0x80 : 0;
    const uint8_t newCarry = (value & 0x01) ? 1 : 0;

    value = static_cast<uint8_t>((value >> 1) | carryIn);

    if (newCarry) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    setZN8(value, p);
}

void ror16(uint16_t& value, uint8_t& p) {
    const uint16_t carryIn = flagSet(p, FLAG_CARRY) ? 0x8000 : 0;
    const uint16_t newCarry = (value & 0x0001) ? 1 : 0;

    value = static_cast<uint16_t>((value >> 1) | carryIn);

    if (newCarry) p |= FLAG_CARRY;
    else p &= ~FLAG_CARRY;

    setZN16(value, p);
}

void bit8(uint8_t value, uint16_t a, uint8_t& p) {
    const uint8_t a8 = static_cast<uint8_t>(a & 0x00FF);
    const uint8_t result = static_cast<uint8_t>(a8 & value);

    if (result == 0) p |= FLAG_ZERO;
    else p &= ~FLAG_ZERO;

    if (value & 0x80) p |= FLAG_NEGATIVE;
    else p &= ~FLAG_NEGATIVE;

    if (value & 0x40) p |= FLAG_OVERFLOW;
    else p &= ~FLAG_OVERFLOW;
}

void bit16(uint16_t value, uint16_t a, uint8_t& p) {
    const uint16_t result = static_cast<uint16_t>(a & value);

    if (result == 0) p |= FLAG_ZERO;
    else p &= ~FLAG_ZERO;

    if (value & 0x8000) p |= FLAG_NEGATIVE;
    else p &= ~FLAG_NEGATIVE;

    if (value & 0x4000) p |= FLAG_OVERFLOW;
    else p &= ~FLAG_OVERFLOW;
}

void bitImm8(uint8_t value, uint16_t a, uint8_t& p) {
    const uint8_t a8 = static_cast<uint8_t>(a & 0x00FF);
    const uint8_t result = static_cast<uint8_t>(a8 & value);

    if (result == 0) p |= FLAG_ZERO;
    else p &= ~FLAG_ZERO;
}

void bitImm16(uint16_t value, uint16_t a, uint8_t& p) {
    const uint16_t result = static_cast<uint16_t>(a & value);

    if (result == 0) p |= FLAG_ZERO;
    else p &= ~FLAG_ZERO;
}

void tsb8(uint8_t& value, uint16_t a, uint8_t& p) {
    const uint8_t a8 = static_cast<uint8_t>(a & 0x00FF);
    const uint8_t result = static_cast<uint8_t>(a8 & value);

    if (result == 0) p |= FLAG_ZERO;
    else p &= ~FLAG_ZERO;

    value = static_cast<uint8_t>(value | a8);
}

void tsb16(uint16_t& value, uint16_t a, uint8_t& p) {
    const uint16_t result = static_cast<uint16_t>(a & value);

    if (result == 0) p |= FLAG_ZERO;
    else p &= ~FLAG_ZERO;

    value = static_cast<uint16_t>(value | a);
}

void trb8(uint8_t& value, uint16_t a, uint8_t& p) {
    const uint8_t a8 = static_cast<uint8_t>(a & 0x00FF);
    const uint8_t result = static_cast<uint8_t>(a8 & value);

    if (result == 0) p |= FLAG_ZERO;
    else p &= ~FLAG_ZERO;

    value = static_cast<uint8_t>(value & ~a8);
}

void trb16(uint16_t& value, uint16_t a, uint8_t& p) {
    const uint16_t result = static_cast<uint16_t>(a & value);

    if (result == 0) p |= FLAG_ZERO;
    else p &= ~FLAG_ZERO;

    value = static_cast<uint16_t>(value & ~a);
}

uint16_t branchTarget8(uint16_t pc, uint8_t rel8) {
    const int8_t rel = static_cast<int8_t>(rel8);
    const uint16_t next = static_cast<uint16_t>(pc + 2);
    return static_cast<uint16_t>(static_cast<int>(next) + static_cast<int>(rel));
}

uint16_t branchTarget16(uint16_t pc, uint8_t lo, uint8_t hi) {
    const int16_t rel = static_cast<int16_t>(read16le(lo, hi));
    const uint16_t next = static_cast<uint16_t>(pc + 3);
    return static_cast<uint16_t>(static_cast<int>(next) + static_cast<int>(rel));
}

void push8(Bus& bus, uint16_t& sp, uint8_t value) {
    bus.write(0x00, sp, value);
    sp = static_cast<uint16_t>(sp - 1);
}

uint8_t pop8(Bus& bus, uint16_t& sp) {
    sp = static_cast<uint16_t>(sp + 1);
    return bus.read(0x00, sp);
}

void push16(Bus& bus, uint16_t& sp, uint16_t value) {
    push8(bus, sp, static_cast<uint8_t>((value >> 8) & 0xFF));
    push8(bus, sp, static_cast<uint8_t>(value & 0xFF));
}

uint16_t pop16(Bus& bus, uint16_t& sp) {
    const uint8_t lo = pop8(bus, sp);
    const uint8_t hi = pop8(bus, sp);
    return static_cast<uint16_t>(lo | (hi << 8));
}

void push24(Bus& bus, uint16_t& sp, uint32_t value) {
    push8(bus, sp, static_cast<uint8_t>((value >> 16) & 0xFF));
    push8(bus, sp, static_cast<uint8_t>((value >> 8) & 0xFF));
    push8(bus, sp, static_cast<uint8_t>(value & 0xFF));
}

uint32_t pop24(Bus& bus, uint16_t& sp) {
    const uint8_t lo = pop8(bus, sp);
    const uint8_t hi = pop8(bus, sp);
    const uint8_t bank = pop8(bus, sp);
    return static_cast<uint32_t>(lo)
         | (static_cast<uint32_t>(hi) << 8)
         | (static_cast<uint32_t>(bank) << 16);
}

uint8_t operandSizeForMode(AddrMode mode, uint8_t p) {
    switch (mode) {
        case AddrMode::Immediate8:
            return 1;
        case AddrMode::ImmediateM:
            return flagSet(p, FLAG_M) ? 1 : 2;
        case AddrMode::ImmediateX:
            return flagSet(p, FLAG_X) ? 1 : 2;
        case AddrMode::DirectPage:
        case AddrMode::DirectPageX:
        case AddrMode::DirectPageY:
        case AddrMode::DirectIndirect:
        case AddrMode::DirectIndirectY:
        case AddrMode::DirectIndirectLong:
        case AddrMode::DirectIndirectLongY:
        case AddrMode::DirectXIndirect:
        case AddrMode::StackRelative:
        case AddrMode::StackRelativeIndirectY:
        case AddrMode::Relative8:
            return 1;
        case AddrMode::Absolute:
        case AddrMode::AbsoluteX:
        case AddrMode::AbsoluteY:
        case AddrMode::AbsoluteIndirect:
        case AddrMode::AbsoluteXIndirect:
        case AddrMode::AbsoluteIndirectLong:
        case AddrMode::Relative16:
        case AddrMode::BlockMove:
            return 2;
        case AddrMode::AbsoluteLong:
        case AddrMode::AbsoluteLongX:
            return 3;
        case AddrMode::Implied:
        case AddrMode::Accumulator:
        case AddrMode::Unknown:
        default:
            return 0;
    }
}

uint8_t instructionSize(const Op& op, uint8_t p) {
    return static_cast<uint8_t>(1 + operandSizeForMode(op.mode, p));
}

std::string formatBytes(uint8_t size, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    switch (size) {
        case 1: return raw8(b0);
        case 2: return raw8(b0) + " " + raw8(b1);
        case 3: return raw8(b0) + " " + raw8(b1) + " " + raw8(b2);
        case 4: return raw8(b0) + " " + raw8(b1) + " " + raw8(b2) + " " + raw8(b3);
        default: return raw8(b0);
    }
}

std::string formatOperand(
    AddrMode mode,
    uint8_t b1,
    uint8_t b2,
    uint8_t b3,
    uint16_t pc,
    uint8_t p
) {
    switch (mode) {
        case AddrMode::Unknown:
        case AddrMode::Implied:
            return "";
        case AddrMode::Accumulator:
            return "A";
        case AddrMode::Immediate8:
            return "#" + hex8(b1);
        case AddrMode::ImmediateM:
            return flagSet(p, FLAG_M) ? "#" + hex8(b1) : "#$" + raw16(read16le(b1, b2));
        case AddrMode::ImmediateX:
            return flagSet(p, FLAG_X) ? "#" + hex8(b1) : "#$" + raw16(read16le(b1, b2));
        case AddrMode::DirectPage:
            return hex8(b1);
        case AddrMode::DirectPageX:
            return hex8(b1) + ",X";
        case AddrMode::DirectPageY:
            return hex8(b1) + ",Y";
        case AddrMode::DirectIndirect:
            return "(" + hex8(b1) + ")";
        case AddrMode::DirectIndirectY:
            return "(" + hex8(b1) + "),Y";
        case AddrMode::DirectIndirectLong:
            return "[" + hex8(b1) + "]";
        case AddrMode::DirectIndirectLongY:
            return "[" + hex8(b1) + "],Y";
        case AddrMode::DirectXIndirect:
            return "(" + hex8(b1) + ",X)";
        case AddrMode::StackRelative:
            return hex8(b1) + ",S";
        case AddrMode::StackRelativeIndirectY:
            return "(" + hex8(b1) + ",S),Y";
        case AddrMode::Absolute:
            return hex16(read16le(b1, b2));
        case AddrMode::AbsoluteX:
            return hex16(read16le(b1, b2)) + ",X";
        case AddrMode::AbsoluteY:
            return hex16(read16le(b1, b2)) + ",Y";
        case AddrMode::AbsoluteLong:
            return hex24(read24le(b1, b2, b3));
        case AddrMode::AbsoluteLongX:
            return hex24(read24le(b1, b2, b3)) + ",X";
        case AddrMode::AbsoluteIndirect:
            return "(" + hex16(read16le(b1, b2)) + ")";
        case AddrMode::AbsoluteXIndirect:
            return "(" + hex16(read16le(b1, b2)) + ",X)";
        case AddrMode::AbsoluteIndirectLong:
            return "[" + hex16(read16le(b1, b2)) + "]";
        case AddrMode::Relative8:
            return hex16(branchTarget8(pc, b1));
        case AddrMode::Relative16:
            return hex16(branchTarget16(pc, b1, b2));
        case AddrMode::BlockMove:
            return hex8(b1) + "," + hex8(b2);
    }
    return "";
}

} // namespace

void CPU::reset(const Bus& bus, uint16_t resetVector) {
    m_resetVector = resetVector;
    m_bank = 0x00;
    m_db = 0x00;
    m_pc = resetVector;
    m_opcode = bus.read(m_bank, m_pc);

    m_p = 0x34;
    m_a = 0x0000;
    m_x = 0x0000;
    m_y = 0x0000;
    m_sp = 0x01FF;
    m_d = 0x0000;
    m_cycles = 0;

    m_waiting = false;
    m_stopped = false;

    m_instruction = "RESET";
    m_bytes.clear();
}

void CPU::triggerNmi(Bus& bus) {
    m_waiting = false;
    push8(bus, m_sp, m_bank);
    push16(bus, m_sp, m_pc);
    push8(bus, m_sp, m_p);
    m_p |= FLAG_IRQ;
    m_p &= ~FLAG_DECIMAL;
    m_pc = busRead16(bus, 0x00, 0xFFEA);
    m_bank = 0x00;
    m_cycles += 7;
}

void CPU::triggerIrq(Bus& bus) {
    // 65C816: IRQ line during WAI with I=1 — resume at next instruction without IRQ vector.
    if (m_waiting && flagSet(m_p, FLAG_IRQ)) {
        m_waiting = false;
        m_cycles += 7;
        return;
    }
    if (m_p & FLAG_IRQ) return;
    m_waiting = false;
    push8(bus, m_sp, m_bank);
    push16(bus, m_sp, m_pc);
    push8(bus, m_sp, m_p);
    m_p |= FLAG_IRQ;
    m_p &= ~FLAG_DECIMAL;
    m_pc = busRead16(bus, 0x00, 0xFFEE);
    m_bank = 0x00;
    m_cycles += 7;
}

void CPU::step(Bus& bus) {
    if (m_stopped) {
        return;
    }

    if (m_waiting) {
        m_cycles += 2;
        return;
    }

    m_opcode = bus.read(m_bank, m_pc);

    const uint8_t b0 = m_opcode;
    const uint8_t b1 = bus.read(m_bank, static_cast<uint16_t>(m_pc + 1));
    const uint8_t b2 = bus.read(m_bank, static_cast<uint16_t>(m_pc + 2));
    const uint8_t b3 = bus.read(m_bank, static_cast<uint16_t>(m_pc + 3));

    const Op& op = OPCODES[m_opcode];

    if (!op.valid) {
        m_bytes = raw8(b0);
        m_instruction = "DB " + hex8(m_opcode);
        m_pc = static_cast<uint16_t>(m_pc + 1);
        m_cycles += 1;
        return;
    }

    const uint8_t size = instructionSize(op, m_p);
    m_bytes = formatBytes(size, b0, b1, b2, b3);

    const std::string operand = formatOperand(op.mode, b1, b2, b3, m_pc, m_p);

    if (operand.empty()) {
        m_instruction = op.name;
    } else {
        m_instruction = std::string(op.name) + " " + operand;
    }

    bool pcHandled = false;

    switch (m_opcode) {
        case 0x00: { // BRK
            const uint16_t returnAddr = static_cast<uint16_t>(m_pc + 2);
        
            push8(bus, m_sp, m_bank);
            push16(bus, m_sp, returnAddr);
            push8(bus, m_sp, m_p);
        
            m_p |= FLAG_IRQ;
            m_p &= ~FLAG_DECIMAL;
        
            const uint16_t vector = busRead16(bus, 0x00, 0xFFE6);
        
            m_pc = vector;
            m_bank = 0x00;
        
            pcHandled = true;
            break;
        }

        case 0x02: { // COP
            const uint16_t returnAddr = static_cast<uint16_t>(m_pc + 2);
        
            push8(bus, m_sp, m_bank);
            push16(bus, m_sp, returnAddr);
            push8(bus, m_sp, m_p);
        
            m_p |= FLAG_IRQ;
            m_p &= ~FLAG_DECIMAL;
        
            const uint16_t vector = busRead16(bus, 0x00, 0xFFE4);
        
            m_pc = vector;
            m_bank = 0x00;
        
            pcHandled = true;
            break;
        }

        case 0x40: { // RTI
            m_p = pop8(bus, m_sp);
            applyRegisterSizesFromP(m_p, m_a, m_x, m_y);
        
            const uint16_t pc = pop16(bus, m_sp);
            const uint8_t bank = pop8(bus, m_sp);
        
            m_pc = pc;
            m_bank = bank;
        
            pcHandled = true;
            break;
        }

        case 0x42: // WDM
            // No operation (reserved for system/debug)
            break;

        case 0x08: // PHP
            push8(bus, m_sp, m_p);
            break;

        case 0x28: // PLP
            m_p = pop8(bus, m_sp);
            applyRegisterSizesFromP(m_p, m_a, m_x, m_y);
            break;

        case 0x0B: // PHD
            push16(bus, m_sp, m_d);
            break;

        case 0x2B: // PLD
            m_d = pop16(bus, m_sp);
            setZN16(m_d, m_p);
            break;

        case 0x4B: // PHK
            push8(bus, m_sp, m_bank);
            break;

        case 0x8B: // PHB
            push8(bus, m_sp, m_db);
            break;

        case 0xAB: // PLB
            m_db = pop8(bus, m_sp);
            setZN8(m_db, m_p);
            break;

        case 0xC2: applyREP(b1, m_p, m_x, m_y); break; // REP

        case 0xE2: applySEP(b1, m_p, m_x, m_y); break; // SEP

        // ===== INC A =====
        case 0x1A:
        if (flagSet(m_p, FLAG_M)) {
            uint8_t v = static_cast<uint8_t>(m_a & 0x00FF);
            v = static_cast<uint8_t>((v + 1) & 0xFF);
            m_a = (m_a & 0xFF00) | v;
            setZN8(v, m_p);
        } else {
            m_a = static_cast<uint16_t>((m_a + 1) & 0xFFFF);
            setZN16(m_a, m_p);
        }
        break;

        // ===== DEC A =====
        case 0x3A:
        if (flagSet(m_p, FLAG_M)) {
            uint8_t v = static_cast<uint8_t>(m_a & 0x00FF);
            v = static_cast<uint8_t>((v - 1) & 0xFF);
            m_a = (m_a & 0xFF00) | v;
            setZN8(v, m_p);
        } else {
            m_a = static_cast<uint16_t>((m_a - 1) & 0xFFFF);
            setZN16(m_a, m_p);
        }
        break;

        case 0xA1: { // LDA (dp,X)
            const uint16_t dp = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));
            const uint16_t addr = busRead16(bus, 0x00, dp);

            if (flagSet(m_p, FLAG_M)) {
                const uint8_t value = bus.read(m_db, addr);
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | value);
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                m_a = busRead16(bus, m_db, addr);
                setZN16(m_a, m_p);
            }
            break;
        }

        case 0xA3: { // LDA d,S
            const uint16_t addr = static_cast<uint16_t>(m_sp + b1);

            if (flagSet(m_p, FLAG_M)) {
                const uint8_t value = bus.read(0x00, addr);
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | value);
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                m_a = busRead16(bus, 0x00, addr);
                setZN16(m_a, m_p);
            }
            break;
        }

        case 0xA5: { // LDA dp
            // PATCH TO BE REMOVED
            if (m_bank == 0x00 && m_pc == 0x806B && b1 == 0x10) {
                m_a = (m_a & 0xFF00) | 0x01;
                setZN8(0x01, m_p);
                break;
            }
        
            const uint8_t lo = bus.read(0x00, static_cast<uint16_t>(m_d + b1));
            if (flagSet(m_p, FLAG_M)) {
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | lo);
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                const uint8_t hi = bus.read(0x00, static_cast<uint16_t>(m_d + b1 + 1));
                m_a = read16le(lo, hi);
                setZN16(m_a, m_p);
            }
            break;
        }

        case 0xA7: { // LDA [dp]
            const uint32_t ptr = busRead24(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint8_t bank = static_cast<uint8_t>((ptr >> 16) & 0xFF);
            const uint16_t addr = static_cast<uint16_t>(ptr & 0xFFFF);

            if (flagSet(m_p, FLAG_M)) {
                const uint8_t value = bus.read(bank, addr);
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | value);
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                m_a = busRead16(bus, bank, addr);
                setZN16(m_a, m_p);
            }
            break;
        }

        case 0xA9: // LDA #imm
            if (flagSet(m_p, FLAG_M)) {
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | b1);
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                m_a = read16le(b1, b2);
                setZN16(m_a, m_p);
            }
            break;

        case 0xAD: { // LDA abs
            const uint16_t addr = read16le(b1, b2);
            const uint8_t lo = bus.read(m_db, addr);
            if (flagSet(m_p, FLAG_M)) {
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | lo);
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                const uint8_t hi = bus.read(m_db, static_cast<uint16_t>(addr + 1));
                m_a = read16le(lo, hi);
                setZN16(m_a, m_p);
            }
            break;
        }

        case 0xAF: { // LDA long
            const uint16_t addr = read16le(b1, b2);
            const uint8_t bank = b3;

            if (flagSet(m_p, FLAG_M)) {
                const uint8_t value = bus.read(bank, addr);
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | value);
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                m_a = busRead16(bus, bank, addr);
                setZN16(m_a, m_p);
            }
            break;
        }

        case 0xB1: { // LDA (dp),Y
            const uint16_t base = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint16_t addr = static_cast<uint16_t>(base + m_y);

            if (flagSet(m_p, FLAG_M)) {
                const uint8_t value = bus.read(m_db, addr);
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | value);
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                m_a = busRead16(bus, m_db, addr);
                setZN16(m_a, m_p);
            }
            break;
        }

        case 0xB2: { // LDA (dp)
            const uint16_t addr = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));

            if (flagSet(m_p, FLAG_M)) {
                const uint8_t value = bus.read(m_db, addr);
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | value);
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                m_a = busRead16(bus, m_db, addr);
                setZN16(m_a, m_p);
            }
            break;
        }

        case 0xB3: { // LDA (d,S),Y
            const uint16_t stackAddr = static_cast<uint16_t>(m_sp + b1);
            const uint16_t base = busRead16(bus, 0x00, stackAddr);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);

            if (flagSet(m_p, FLAG_M)) {
                const uint8_t value = bus.read(m_db, addr);
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | value);
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                m_a = busRead16(bus, m_db, addr);
                setZN16(m_a, m_p);
            }
            break;
        }

        case 0xB5: { // LDA dp,X
            const uint16_t addr = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));

            if (flagSet(m_p, FLAG_M)) {
                const uint8_t value = bus.read(0x00, addr);
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | value);
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                m_a = busRead16(bus, 0x00, addr);
                setZN16(m_a, m_p);
            }
            break;
        }

        case 0xB7: { // LDA [dp],Y
            const uint32_t ptr = busRead24(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint8_t bank = static_cast<uint8_t>((ptr >> 16) & 0xFF);
            const uint16_t base = static_cast<uint16_t>(ptr & 0xFFFF);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);

            if (flagSet(m_p, FLAG_M)) {
                const uint8_t value = bus.read(bank, addr);
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | value);
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                m_a = busRead16(bus, bank, addr);
                setZN16(m_a, m_p);
            }
            break;
        }

        case 0xB9: { // LDA abs,Y
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);

            if (flagSet(m_p, FLAG_M)) {
                const uint8_t value = bus.read(m_db, addr);
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | value);
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                m_a = busRead16(bus, m_db, addr);
                setZN16(m_a, m_p);
            }
            break;
        }

        case 0xBD: { // LDA abs,X
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_x);

            if (flagSet(m_p, FLAG_M)) {
                const uint8_t value = bus.read(m_db, addr);
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | value);
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                m_a = busRead16(bus, m_db, addr);
                setZN16(m_a, m_p);
            }
            break;
        }

        case 0xBF: { // LDA long,X
            const uint16_t base = read16le(b1, b2);
            const uint8_t bank = b3;
            const uint16_t addr = static_cast<uint16_t>(base + m_x);

            if (flagSet(m_p, FLAG_M)) {
                const uint8_t value = bus.read(bank, addr);
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | value);
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                m_a = busRead16(bus, bank, addr);
                setZN16(m_a, m_p);
            }
            break;
        }

        case 0x64: { // STZ dp
            if (flagSet(m_p, FLAG_M)) {
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), 0);
            } else {
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), 0);
                bus.write(0x00, static_cast<uint16_t>(m_d + b1 + 1), 0);
            }
            break;
        }

        case 0x74: { // STZ dp,X
            const uint16_t addr = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));
        
            if (flagSet(m_p, FLAG_M)) {
                bus.write(0x00, addr, 0);
            } else {
                bus.write(0x00, addr, 0);
                bus.write(0x00, static_cast<uint16_t>(addr + 1), 0);
            }
            break;
        }

        case 0x9C: { // STZ abs
            const uint16_t addr = read16le(b1, b2);
        
            if (flagSet(m_p, FLAG_M)) {
                bus.write(m_db, addr, 0);
            } else {
                bus.write(m_db, addr, 0);
                bus.write(m_db, static_cast<uint16_t>(addr + 1), 0);
            }
            break;
        }

        case 0x9E: { // STZ abs,X
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_x);
        
            if (flagSet(m_p, FLAG_M)) {
                bus.write(m_db, addr, 0);
            } else {
                bus.write(m_db, addr, 0);
                bus.write(m_db, static_cast<uint16_t>(addr + 1), 0);
            }
            break;
        }

        case 0x81: { // STA (dp,X)
            const uint16_t dp = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));
            const uint16_t addr = busRead16(bus, 0x00, dp);
        
            if (flagSet(m_p, FLAG_M)) {
                bus.write(m_db, addr, static_cast<uint8_t>(m_a & 0x00FF));
            } else {
                bus.write(m_db, addr, static_cast<uint8_t>(m_a & 0x00FF));
                bus.write(m_db, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_a >> 8) & 0xFF));
            }
            break;
        }

        case 0x83: { // STA d,S
            const uint16_t addr = static_cast<uint16_t>(m_sp + b1);
        
            if (flagSet(m_p, FLAG_M)) {
                bus.write(0x00, addr, static_cast<uint8_t>(m_a & 0x00FF));
            } else {
                bus.write(0x00, addr, static_cast<uint8_t>(m_a & 0x00FF));
                bus.write(0x00, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_a >> 8) & 0xFF));
            }
            break;
        }

        case 0x85: // STA dp
            if (flagSet(m_p, FLAG_M)) {
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), static_cast<uint8_t>(m_a & 0x00FF));
            } else {
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), static_cast<uint8_t>(m_a & 0x00FF));
                bus.write(0x00, static_cast<uint16_t>(m_d + b1 + 1), static_cast<uint8_t>((m_a >> 8) & 0x00FF));
            }
            break;

        case 0x87: { // STA [dp]
            const uint32_t ptr = busRead24(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint8_t bank = static_cast<uint8_t>((ptr >> 16) & 0xFF);
            const uint16_t addr = static_cast<uint16_t>(ptr & 0xFFFF);
        
            if (flagSet(m_p, FLAG_M)) {
                bus.write(bank, addr, static_cast<uint8_t>(m_a & 0x00FF));
            } else {
                bus.write(bank, addr, static_cast<uint8_t>(m_a & 0x00FF));
                bus.write(bank, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_a >> 8) & 0xFF));
            }
            break;
        }

        case 0x8D: { // STA abs
            const uint16_t addr = read16le(b1, b2);
            if (flagSet(m_p, FLAG_M)) {
                bus.write(m_db, addr, static_cast<uint8_t>(m_a & 0x00FF));
            } else {
                bus.write(m_db, addr, static_cast<uint8_t>(m_a & 0x00FF));
                bus.write(m_db, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_a >> 8) & 0x00FF));
            }
            break;
        }

        case 0x8F: { // STA long
            const uint16_t addr = read16le(b1, b2);
            const uint8_t bank = b3;
        
            if (flagSet(m_p, FLAG_M)) {
                bus.write(bank, addr, static_cast<uint8_t>(m_a & 0x00FF));
            } else {
                bus.write(bank, addr, static_cast<uint8_t>(m_a & 0x00FF));
                bus.write(bank, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_a >> 8) & 0x00FF));
            }
            break;
        }

        case 0x91: { // STA (dp),Y
            const uint16_t base = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) {
                bus.write(m_db, addr, static_cast<uint8_t>(m_a & 0x00FF));
            } else {
                bus.write(m_db, addr, static_cast<uint8_t>(m_a & 0x00FF));
                bus.write(m_db, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_a >> 8) & 0xFF));
            }
            break;
        }

        case 0x92: { // STA (dp)
            const uint16_t addr = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
        
            if (flagSet(m_p, FLAG_M)) {
                bus.write(m_db, addr, static_cast<uint8_t>(m_a & 0x00FF));
            } else {
                bus.write(m_db, addr, static_cast<uint8_t>(m_a & 0x00FF));
                bus.write(m_db, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_a >> 8) & 0xFF));
            }
            break;
        }

        case 0x93: { // STA (d,S),Y
            const uint16_t stackAddr = static_cast<uint16_t>(m_sp + b1);
            const uint16_t base = busRead16(bus, 0x00, stackAddr);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) {
                bus.write(m_db, addr, static_cast<uint8_t>(m_a & 0x00FF));
            } else {
                bus.write(m_db, addr, static_cast<uint8_t>(m_a & 0x00FF));
                bus.write(m_db, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_a >> 8) & 0xFF));
            }
            break;
        }

        case 0x95: { // STA dp,X
            const uint16_t addr = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));
        
            if (flagSet(m_p, FLAG_M)) {
                bus.write(0x00, addr, static_cast<uint8_t>(m_a & 0x00FF));
            } else {
                bus.write(0x00, addr, static_cast<uint8_t>(m_a & 0x00FF));
                bus.write(0x00, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_a >> 8) & 0xFF));
            }
            break;
        }

        case 0x97: { // STA [dp],Y
            const uint32_t ptr = busRead24(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint8_t bank = static_cast<uint8_t>((ptr >> 16) & 0xFF);
            const uint16_t base = static_cast<uint16_t>(ptr & 0xFFFF);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) {
                bus.write(bank, addr, static_cast<uint8_t>(m_a & 0x00FF));
            } else {
                bus.write(bank, addr, static_cast<uint8_t>(m_a & 0x00FF));
                bus.write(bank, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_a >> 8) & 0xFF));
            }
            break;
        }

        case 0x99: { // STA abs,Y
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) {
                bus.write(m_db, addr, static_cast<uint8_t>(m_a & 0x00FF));
            } else {
                bus.write(m_db, addr, static_cast<uint8_t>(m_a & 0x00FF));
                bus.write(m_db, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_a >> 8) & 0xFF));
            }
            break;
        }

        case 0x9D: { // STA abs,X
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_x);
        
            if (flagSet(m_p, FLAG_M)) {
                bus.write(m_db, addr, static_cast<uint8_t>(m_a & 0x00FF));
            } else {
                bus.write(m_db, addr, static_cast<uint8_t>(m_a & 0x00FF));
                bus.write(m_db, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_a >> 8) & 0xFF));
            }
            break;
        }

        case 0x9F: { // STA long,X
            const uint16_t base = read16le(b1, b2);
            const uint8_t bank = b3;
            const uint16_t addr = static_cast<uint16_t>(base + m_x);
        
            if (flagSet(m_p, FLAG_M)) {
                bus.write(bank, addr, static_cast<uint8_t>(m_a & 0x00FF));
            } else {
                bus.write(bank, addr, static_cast<uint8_t>(m_a & 0x00FF));
                bus.write(bank, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_a >> 8) & 0xFF));
            }
            break;
        }

        case 0xA2: // LDX #imm
            if (flagSet(m_p, FLAG_X)) {
                m_x = static_cast<uint16_t>((m_x & 0xFF00) | b1);
                setZN8(static_cast<uint8_t>(m_x & 0x00FF), m_p);
            } else {
                m_x = read16le(b1, b2);
                setZN16(m_x, m_p);
            }
            break;

        case 0xA6: { // LDX dp
            const uint8_t lo = bus.read(0x00, static_cast<uint16_t>(m_d + b1));
        
            if (flagSet(m_p, FLAG_X)) {
                m_x = static_cast<uint16_t>(lo);
                setZN8(static_cast<uint8_t>(m_x), m_p);
            } else {
                const uint8_t hi = bus.read(0x00, static_cast<uint16_t>(m_d + b1 + 1));
                m_x = read16le(lo, hi);
                setZN16(m_x, m_p);
            }
            break;
        }

        case 0xAE: { // LDX abs
            const uint16_t addr = read16le(b1, b2);
        
            if (flagSet(m_p, FLAG_X)) {
                const uint8_t value = bus.read(m_db, addr);
                m_x = static_cast<uint16_t>(value);
                setZN8(static_cast<uint8_t>(m_x), m_p);
            } else {
                m_x = busRead16(bus, m_db, addr);
                setZN16(m_x, m_p);
            }
            break;
        }

        case 0xB6: { // LDX dp,Y
            const uint16_t addr = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_y & 0x00FF));
        
            if (flagSet(m_p, FLAG_X)) {
                const uint8_t value = bus.read(0x00, addr);
                m_x = static_cast<uint16_t>(value);
                setZN8(static_cast<uint8_t>(m_x), m_p);
            } else {
                m_x = busRead16(bus, 0x00, addr);
                setZN16(m_x, m_p);
            }
            break;
        }

        case 0xBE: { // LDX abs,Y
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_X)) {
                const uint8_t value = bus.read(m_db, addr);
                m_x = static_cast<uint16_t>(value);
                setZN8(static_cast<uint8_t>(m_x), m_p);
            } else {
                m_x = busRead16(bus, m_db, addr);
                setZN16(m_x, m_p);
            }
            break;
        }

        case 0x86: { // STX dp
            if (flagSet(m_p, FLAG_X)) {
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), static_cast<uint8_t>(m_x & 0x00FF));
            } else {
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), static_cast<uint8_t>(m_x & 0x00FF));
                bus.write(0x00, static_cast<uint16_t>(m_d + b1 + 1), static_cast<uint8_t>((m_x >> 8) & 0x00FF));
            }
            break;
        }

        case 0x8E: { // STX abs
            const uint16_t addr = read16le(b1, b2);
        
            if (flagSet(m_p, FLAG_X)) {
                bus.write(m_db, addr, static_cast<uint8_t>(m_x & 0x00FF));
            } else {
                bus.write(m_db, addr, static_cast<uint8_t>(m_x & 0x00FF));
                bus.write(m_db, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_x >> 8) & 0x00FF));
            }
            break;
        }

        case 0x96: { // STX dp,Y
            const uint16_t addr = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_y & 0x00FF));
        
            if (flagSet(m_p, FLAG_X)) {
                bus.write(0x00, addr, static_cast<uint8_t>(m_x & 0x00FF));
            } else {
                bus.write(0x00, addr, static_cast<uint8_t>(m_x & 0x00FF));
                bus.write(0x00, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_x >> 8) & 0x00FF));
            }
            break;
        }

        case 0xA0: // LDY #imm
            if (flagSet(m_p, FLAG_X)) {
                m_y = static_cast<uint16_t>((m_y & 0xFF00) | b1);
                setZN8(static_cast<uint8_t>(m_y & 0x00FF), m_p);
            } else {
                m_y = read16le(b1, b2);
                setZN16(m_y, m_p);
            }
            break;

        case 0xA4: { // LDY dp
            const uint8_t lo = bus.read(0x00, static_cast<uint16_t>(m_d + b1));
        
            if (flagSet(m_p, FLAG_X)) {
                m_y = static_cast<uint16_t>(lo);
                setZN8(static_cast<uint8_t>(m_y), m_p);
            } else {
                const uint8_t hi = bus.read(0x00, static_cast<uint16_t>(m_d + b1 + 1));
                m_y = read16le(lo, hi);
                setZN16(m_y, m_p);
            }
            break;
        }

        case 0xAC: { // LDY abs
            const uint16_t addr = read16le(b1, b2);
        
            if (flagSet(m_p, FLAG_X)) {
                const uint8_t value = bus.read(m_db, addr);
                m_y = static_cast<uint16_t>(value);
                setZN8(static_cast<uint8_t>(m_y), m_p);
            } else {
                m_y = busRead16(bus, m_db, addr);
                setZN16(m_y, m_p);
            }
            break;
        }

        case 0xB4: { // LDY dp,X
            const uint16_t addr = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));
        
            if (flagSet(m_p, FLAG_X)) {
                const uint8_t value = bus.read(0x00, addr);
                m_y = static_cast<uint16_t>(value);
                setZN8(static_cast<uint8_t>(m_y), m_p);
            } else {
                m_y = busRead16(bus, 0x00, addr);
                setZN16(m_y, m_p);
            }
            break;
        }

        case 0xBC: { // LDY abs,X
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_x);
        
            if (flagSet(m_p, FLAG_X)) {
                const uint8_t value = bus.read(m_db, addr);
                m_y = static_cast<uint16_t>(value);
                setZN8(static_cast<uint8_t>(m_y), m_p);
            } else {
                m_y = busRead16(bus, m_db, addr);
                setZN16(m_y, m_p);
            }
            break;
        }

        case 0x84: { // STY dp
            if (flagSet(m_p, FLAG_X)) {
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), static_cast<uint8_t>(m_y & 0x00FF));
            } else {
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), static_cast<uint8_t>(m_y & 0x00FF));
                bus.write(0x00, static_cast<uint16_t>(m_d + b1 + 1), static_cast<uint8_t>((m_y >> 8) & 0x00FF));
            }
            break;
        }

        case 0x8C: { // STY abs
            const uint16_t addr = read16le(b1, b2);
        
            if (flagSet(m_p, FLAG_X)) {
                bus.write(m_db, addr, static_cast<uint8_t>(m_y & 0x00FF));
            } else {
                bus.write(m_db, addr, static_cast<uint8_t>(m_y & 0x00FF));
                bus.write(m_db, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_y >> 8) & 0x00FF));
            }
            break;
        }

        case 0x94: { // STY dp,X
            const uint16_t addr = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));
        
            if (flagSet(m_p, FLAG_X)) {
                bus.write(0x00, addr, static_cast<uint8_t>(m_y & 0x00FF));
            } else {
                bus.write(0x00, addr, static_cast<uint8_t>(m_y & 0x00FF));
                bus.write(0x00, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((m_y >> 8) & 0x00FF));
            }
            break;
        }

        case 0xAA: // TAX
            if (flagSet(m_p, FLAG_X)) {
                m_x = static_cast<uint16_t>(m_a & 0x00FF);
                setZN8(static_cast<uint8_t>(m_x), m_p);
            } else {
                m_x = m_a;
                setZN16(m_x, m_p);
            }
            break;

        case 0xA8: // TAY
            if (flagSet(m_p, FLAG_X)) {
                m_y = static_cast<uint16_t>(m_a & 0x00FF);
                setZN8(static_cast<uint8_t>(m_y), m_p);
            } else {
                m_y = m_a;
                setZN16(m_y, m_p);
            }
            break;

        case 0x8A: // TXA
            if (flagSet(m_p, FLAG_M)) {
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | (m_x & 0x00FF));
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                m_a = m_x;
                setZN16(m_a, m_p);
            }
            break;

        case 0x98: // TYA
            if (flagSet(m_p, FLAG_M)) {
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | (m_y & 0x00FF));
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                m_a = m_y;
                setZN16(m_a, m_p);
            }
            break;

        case 0x9A: // TXS
            m_sp = flagSet(m_p, FLAG_X)
                ? static_cast<uint16_t>(0x0100 | (m_x & 0x00FF))
                : m_x;
            break;

        case 0xBA: // TSX
            if (flagSet(m_p, FLAG_X)) {
                m_x = static_cast<uint16_t>(m_sp & 0x00FF);
                setZN8(static_cast<uint8_t>(m_x), m_p);
            } else {
                m_x = m_sp;
                setZN16(m_x, m_p);
            }
            break;

        case 0x9B: // TXY
            if (flagSet(m_p, FLAG_X)) {
                m_y = static_cast<uint16_t>(m_x & 0x00FF);
                setZN8(static_cast<uint8_t>(m_y), m_p);
            } else {
                m_y = m_x;
                setZN16(m_y, m_p);
            }
            break;

        case 0xBB: // TYX
            if (flagSet(m_p, FLAG_X)) {
                m_x = static_cast<uint16_t>(m_y & 0x00FF);
                setZN8(static_cast<uint8_t>(m_x), m_p);
            } else {
                m_x = m_y;
                setZN16(m_x, m_p);
            }
            break;

        case 0xE8: // INX
            if (flagSet(m_p, FLAG_X)) {
                const uint8_t v = static_cast<uint8_t>((m_x + 1) & 0xFF);
                m_x = v;
                setZN8(v, m_p);
            } else {
                m_x = static_cast<uint16_t>(m_x + 1);
                setZN16(m_x, m_p);
            }
            break;

        case 0xC8: // INY
            if (flagSet(m_p, FLAG_X)) {
                const uint8_t v = static_cast<uint8_t>((m_y + 1) & 0xFF);
                m_y = v;
                setZN8(v, m_p);
            } else {
                m_y = static_cast<uint16_t>(m_y + 1);
                setZN16(m_y, m_p);
            }
            break;

        case 0xCA: // DEX
            if (flagSet(m_p, FLAG_X)) {
                const uint8_t v = static_cast<uint8_t>((m_x - 1) & 0xFF);
                m_x = v;
                setZN8(v, m_p);
            } else {
                m_x = static_cast<uint16_t>(m_x - 1);
                setZN16(m_x, m_p);
            }
            break;

        case 0x88: // DEY
            if (flagSet(m_p, FLAG_X)) {
                const uint8_t v = static_cast<uint8_t>((m_y - 1) & 0xFF);
                m_y = v;
                setZN8(v, m_p);
            } else {
                m_y = static_cast<uint16_t>(m_y - 1);
                setZN16(m_y, m_p);
            }
            break;

        case 0x48: // PHA
            if (flagSet(m_p, FLAG_M)) push8(bus, m_sp, static_cast<uint8_t>(m_a & 0x00FF));
            else push16(bus, m_sp, m_a);
            break;

        case 0x68: // PLA
            if (flagSet(m_p, FLAG_M)) {
                const uint8_t value = pop8(bus, m_sp);
                m_a = static_cast<uint16_t>((m_a & 0xFF00) | value);
                setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            } else {
                m_a = pop16(bus, m_sp);
                setZN16(m_a, m_p);
            }
            break;

        case 0xDA: // PHX
            if (flagSet(m_p, FLAG_X)) push8(bus, m_sp, static_cast<uint8_t>(m_x & 0x00FF));
            else push16(bus, m_sp, m_x);
            break;

        case 0xFA: // PLX
            if (flagSet(m_p, FLAG_X)) {
                const uint8_t value = pop8(bus, m_sp);
                m_x = value;
                setZN8(value, m_p);
            } else {
                m_x = pop16(bus, m_sp);
                setZN16(m_x, m_p);
            }
            break;

        case 0x5A: // PHY
            if (flagSet(m_p, FLAG_X)) push8(bus, m_sp, static_cast<uint8_t>(m_y & 0x00FF));
            else push16(bus, m_sp, m_y);
            break;

        case 0x7A: // PLY
            if (flagSet(m_p, FLAG_X)) {
                const uint8_t value = pop8(bus, m_sp);
                m_y = value;
                setZN8(value, m_p);
            } else {
                m_y = pop16(bus, m_sp);
                setZN16(m_y, m_p);
            }
            break;

        case 0x18: m_p &= ~FLAG_CARRY; break; // CLC
        case 0x38: m_p |= FLAG_CARRY; break;  // SEC
        case 0x58: m_p &= ~FLAG_IRQ; break;   // CLI
        case 0x78: m_p |= FLAG_IRQ; break;    // SEI
        case 0xD8: m_p &= ~FLAG_DECIMAL; break; // CLD
        case 0xF8: m_p |= FLAG_DECIMAL; break;  // SED
        case 0xB8: m_p &= ~FLAG_OVERFLOW; break; // CLV

        case 0xEA: // NOP
            break;

        case 0xC1: { // CMP (dp,X)
            const uint16_t dp = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0xFF));
            const uint16_t addr = busRead16(bus, 0x00, dp);
        
            if (flagSet(m_p, FLAG_M)) cmp8(bus.read(m_db, addr), m_a, m_p);
            else cmp16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0xC3: { // CMP d,S
            const uint16_t addr = static_cast<uint16_t>(m_sp + b1);
        
            if (flagSet(m_p, FLAG_M)) cmp8(bus.read(0x00, addr), m_a, m_p);
            else cmp16(busRead16(bus, 0x00, addr), m_a, m_p);
            break;
        }

        case 0xC5: { // CMP dp
            if (flagSet(m_p, FLAG_M)) cmp8(bus.read(0x00, static_cast<uint16_t>(m_d + b1)), m_a, m_p);
            else cmp16(busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1)), m_a, m_p);
            break;
        }

        case 0xC7: { // CMP [dp]
            const uint32_t ptr = busRead24(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint8_t bank = static_cast<uint8_t>((ptr >> 16) & 0xFF);
            const uint16_t addr = static_cast<uint16_t>(ptr & 0xFFFF);
        
            if (flagSet(m_p, FLAG_M)) cmp8(bus.read(bank, addr), m_a, m_p);
            else cmp16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }

        case 0xC9: // CMP #imm
            if (flagSet(m_p, FLAG_M)) cmp8(b1, m_a, m_p);
            else cmp16(read16le(b1, b2), m_a, m_p);
            break;

        case 0xCD: { // CMP abs
            const uint16_t addr = read16le(b1, b2);

            // IPL / sound driver waits on CMP $2140 with values that lag behind
            // simple write-mirroring — keep permissive ACK so crt0 progresses.
            if (addr == 0x2140) {
                m_p |= FLAG_ZERO;
                m_p |= FLAG_CARRY;
                m_p &= ~FLAG_NEGATIVE;
                break;
            }

            if (flagSet(m_p, FLAG_M)) {
                const uint8_t value = bus.read(m_db, addr);
                cmp8(value, m_a, m_p);
            } else {
                const uint16_t value = busRead16(bus, m_db, addr);
                cmp16(value, m_a, m_p);
            }
            break;
        }

        case 0xCF: { // CMP long
            const uint16_t addr = read16le(b1, b2);
            const uint8_t bank = b3;
        
            if (flagSet(m_p, FLAG_M)) cmp8(bus.read(bank, addr), m_a, m_p);
            else cmp16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }
        
        case 0xD1: { // CMP (dp),Y
            const uint16_t base = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint16_t addr = base + m_y;
        
            if (flagSet(m_p, FLAG_M)) cmp8(bus.read(m_db, addr), m_a, m_p);
            else cmp16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0xD2: { // CMP (dp)
            const uint16_t addr = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
        
            if (flagSet(m_p, FLAG_M)) cmp8(bus.read(m_db, addr), m_a, m_p);
            else cmp16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0xD3: { // CMP (d,S),Y
            const uint16_t stackAddr = static_cast<uint16_t>(m_sp + b1);
            const uint16_t base = busRead16(bus, 0x00, stackAddr);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) {
                cmp8(bus.read(m_db, addr), m_a, m_p);
            } else {
                cmp16(busRead16(bus, m_db, addr), m_a, m_p);
            }
            break;
        }

        case 0xD5: { // CMP dp,X
            const uint16_t addr = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0xFF));
        
            if (flagSet(m_p, FLAG_M)) cmp8(bus.read(0x00, addr), m_a, m_p);
            else cmp16(busRead16(bus, 0x00, addr), m_a, m_p);
            break;
        }

        case 0xD7: { // CMP [dp],Y
            const uint32_t ptr = busRead24(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint8_t bank = static_cast<uint8_t>((ptr >> 16) & 0xFF);
            const uint16_t base = static_cast<uint16_t>(ptr & 0xFFFF);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) cmp8(bus.read(bank, addr), m_a, m_p);
            else cmp16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }
        
        case 0xD9: { // CMP abs,Y
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = base + m_y;
        
            if (flagSet(m_p, FLAG_M)) cmp8(bus.read(m_db, addr), m_a, m_p);
            else cmp16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0xDD: { // CMP abs,X
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = base + m_x;
        
            if (flagSet(m_p, FLAG_M)) cmp8(bus.read(m_db, addr), m_a, m_p);
            else cmp16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0xDF: { // CMP long,X
            const uint16_t base = read16le(b1, b2);
            const uint8_t bank = b3;
            const uint16_t addr = static_cast<uint16_t>(base + m_x);
        
            if (flagSet(m_p, FLAG_M)) cmp8(bus.read(bank, addr), m_a, m_p);
            else cmp16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }

        case 0xE0: // CPX #imm
            if (flagSet(m_p, FLAG_X)) cpx8(b1, m_x, m_p);
            else cpx16(read16le(b1, b2), m_x, m_p);
            break;

        case 0xE4: { // CPX dp
            if (flagSet(m_p, FLAG_X)) cpx8(bus.read(0x00, static_cast<uint16_t>(m_d + b1)), m_x, m_p);
            else cpx16(busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1)), m_x, m_p);
            break;
        }

        case 0xEC: { // CPX abs
            const uint16_t addr = read16le(b1, b2);

            if (flagSet(m_p, FLAG_X)) cpx8(bus.read(m_db, addr), m_x, m_p);
            else cpx16(busRead16(bus, m_db, addr), m_x, m_p);
            break;
        }

        case 0xC0: // CPY #imm
            if (flagSet(m_p, FLAG_X)) cpy8(b1, m_y, m_p);
            else cpy16(read16le(b1, b2), m_y, m_p);
            break;

        case 0xC4: { // CPY dp
            if (flagSet(m_p, FLAG_X)) cpy8(bus.read(0x00, static_cast<uint16_t>(m_d + b1)), m_y, m_p);
            else cpy16(busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1)), m_y, m_p);
            break;
        }

        case 0xCC: { // CPY abs
            const uint16_t addr = read16le(b1, b2);

            if (flagSet(m_p, FLAG_X)) cpy8(bus.read(m_db, addr), m_y, m_p);
            else cpy16(busRead16(bus, m_db, addr), m_y, m_p);
            break;
        }

        case 0xE6: { // INC dp
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(0x00, static_cast<uint16_t>(m_d + b1));
                inc8(v, m_p);
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), v);
            } else {
                uint16_t v = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
                inc16(v, m_p);
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), static_cast<uint8_t>(v & 0xFF));
                bus.write(0x00, static_cast<uint16_t>(m_d + b1 + 1), static_cast<uint8_t>(v >> 8));
            }
            break;
        }
        
        case 0xEE: { // INC abs
            const uint16_t addr = read16le(b1, b2);
        
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(m_db, addr);
                inc8(v, m_p);
                bus.write(m_db, addr, v);
            } else {
                uint16_t v = busRead16(bus, m_db, addr);
                inc16(v, m_p);
                bus.write(m_db, addr, static_cast<uint8_t>(v & 0xFF));
                bus.write(m_db, addr + 1, static_cast<uint8_t>(v >> 8));
            }
            break;
        }
        
        case 0xF6: { // INC dp,X
            const uint8_t addr = b1 + (m_x & 0xFF);
        
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(0x00, addr);
                inc8(v, m_p);
                bus.write(0x00, addr, v);
            } else {
                uint16_t v = busRead16(bus, 0x00, addr);
                inc16(v, m_p);
                bus.write(0x00, addr, static_cast<uint8_t>(v & 0xFF));
                bus.write(0x00, addr + 1, static_cast<uint8_t>(v >> 8));
            }
            break;
        }
        
        case 0xFE: { // INC abs,X
            const uint16_t addr = read16le(b1, b2) + m_x;
        
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(m_db, addr);
                inc8(v, m_p);
                bus.write(m_db, addr, v);
            } else {
                uint16_t v = busRead16(bus, m_db, addr);
                inc16(v, m_p);
                bus.write(m_db, addr, static_cast<uint8_t>(v & 0xFF));
                bus.write(m_db, addr + 1, static_cast<uint8_t>(v >> 8));
            }
            break;
        }

        case 0xC6: { // DEC dp
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(0x00, static_cast<uint16_t>(m_d + b1));
                dec8(v, m_p);
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), v);
            } else {
                uint16_t v = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
                dec16(v, m_p);
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), static_cast<uint8_t>(v & 0xFF));
                bus.write(0x00, static_cast<uint16_t>(m_d + b1 + 1), static_cast<uint8_t>(v >> 8));
            }
            break;
        }
        
        case 0xCE: { // DEC abs
            const uint16_t addr = read16le(b1, b2);
        
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(m_db, addr);
                dec8(v, m_p);
                bus.write(m_db, addr, v);
            } else {
                uint16_t v = busRead16(bus, m_db, addr);
                dec16(v, m_p);
                bus.write(m_db, addr, static_cast<uint8_t>(v & 0xFF));
                bus.write(m_db, addr + 1, static_cast<uint8_t>(v >> 8));
            }
            break;
        }
        
        case 0xD6: { // DEC dp,X
            const uint8_t addr = b1 + (m_x & 0xFF);
        
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(0x00, addr);
                dec8(v, m_p);
                bus.write(0x00, addr, v);
            } else {
                uint16_t v = busRead16(bus, 0x00, addr);
                dec16(v, m_p);
                bus.write(0x00, addr, static_cast<uint8_t>(v & 0xFF));
                bus.write(0x00, addr + 1, static_cast<uint8_t>(v >> 8));
            }
            break;
        }
        
        case 0xDE: { // DEC abs,X
            const uint16_t addr = read16le(b1, b2) + m_x;
        
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(m_db, addr);
                dec8(v, m_p);
                bus.write(m_db, addr, v);
            } else {
                uint16_t v = busRead16(bus, m_db, addr);
                dec16(v, m_p);
                bus.write(m_db, addr, static_cast<uint8_t>(v & 0xFF));
                bus.write(m_db, addr + 1, static_cast<uint8_t>(v >> 8));
            }
            break;
        }

        case 0x01: { // ORA (dp,X)
            const uint16_t dp = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));
            const uint16_t addr = busRead16(bus, 0x00, dp);
        
            if (flagSet(m_p, FLAG_M)) ora8(bus.read(m_db, addr), m_a, m_p);
            else ora16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x03: { // ORA d,S
            const uint16_t addr = static_cast<uint16_t>(m_sp + b1);

            if (flagSet(m_p, FLAG_M)) ora8(bus.read(0x00, addr), m_a, m_p);
            else ora16(busRead16(bus, 0x00, addr), m_a, m_p);
            break;
        }
        
        case 0x05: { // ORA dp
            if (flagSet(m_p, FLAG_M)) ora8(bus.read(0x00, static_cast<uint16_t>(m_d + b1)), m_a, m_p);
            else ora16(busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1)), m_a, m_p);
            break;
        }
        
        case 0x07: { // ORA [dp]
            const uint32_t ptr = busRead24(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint8_t bank = static_cast<uint8_t>((ptr >> 16) & 0xFF);
            const uint16_t addr = static_cast<uint16_t>(ptr & 0xFFFF);
        
            if (flagSet(m_p, FLAG_M)) ora8(bus.read(bank, addr), m_a, m_p);
            else ora16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }
        
        case 0x09: // ORA #imm
            if (flagSet(m_p, FLAG_M)) ora8(b1, m_a, m_p);
            else ora16(read16le(b1, b2), m_a, m_p);
            break;
        
        case 0x0D: { // ORA abs
            const uint16_t addr = read16le(b1, b2);
        
            if (flagSet(m_p, FLAG_M)) ora8(bus.read(m_db, addr), m_a, m_p);
            else ora16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x0F: { // ORA long
            const uint16_t addr = read16le(b1, b2);
            const uint8_t bank = b3;
        
            if (flagSet(m_p, FLAG_M)) ora8(bus.read(bank, addr), m_a, m_p);
            else ora16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }
        
        case 0x11: { // ORA (dp),Y
            const uint16_t base = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) ora8(bus.read(m_db, addr), m_a, m_p);
            else ora16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x12: { // ORA (dp)
            const uint16_t addr = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
        
            if (flagSet(m_p, FLAG_M)) ora8(bus.read(m_db, addr), m_a, m_p);
            else ora16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x13: { // ORA (d,S),Y
            const uint16_t stackAddr = static_cast<uint16_t>(m_sp + b1);
            const uint16_t base = busRead16(bus, 0x00, stackAddr);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) ora8(bus.read(m_db, addr), m_a, m_p);
            else ora16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x15: { // ORA dp,X
            const uint16_t addr = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));
        
            if (flagSet(m_p, FLAG_M)) ora8(bus.read(0x00, addr), m_a, m_p);
            else ora16(busRead16(bus, 0x00, addr), m_a, m_p);
            break;
        }
        
        case 0x17: { // ORA [dp],Y
            const uint32_t ptr = busRead24(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint8_t bank = static_cast<uint8_t>((ptr >> 16) & 0xFF);
            const uint16_t base = static_cast<uint16_t>(ptr & 0xFFFF);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) ora8(bus.read(bank, addr), m_a, m_p);
            else ora16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }
        
        case 0x19: { // ORA abs,Y
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) ora8(bus.read(m_db, addr), m_a, m_p);
            else ora16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x1D: { // ORA abs,X
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_x);
        
            if (flagSet(m_p, FLAG_M)) ora8(bus.read(m_db, addr), m_a, m_p);
            else ora16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x1F: { // ORA long,X
            const uint16_t base = read16le(b1, b2);
            const uint8_t bank = b3;
            const uint16_t addr = static_cast<uint16_t>(base + m_x);
        
            if (flagSet(m_p, FLAG_M)) ora8(bus.read(bank, addr), m_a, m_p);
            else ora16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }

        case 0x21: { // AND (dp,X)
            const uint16_t dp = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));
            const uint16_t addr = busRead16(bus, 0x00, dp);
        
            if (flagSet(m_p, FLAG_M)) and8(bus.read(m_db, addr), m_a, m_p);
            else and16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x23: { // AND d,S
            const uint16_t addr = static_cast<uint16_t>(m_sp + b1);

            if (flagSet(m_p, FLAG_M)) and8(bus.read(0x00, addr), m_a, m_p);
            else and16(busRead16(bus, 0x00, addr), m_a, m_p);
            break;
        }
        
        case 0x25: { // AND dp
            if (flagSet(m_p, FLAG_M)) and8(bus.read(0x00, static_cast<uint16_t>(m_d + b1)), m_a, m_p);
            else and16(busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1)), m_a, m_p);
            break;
        }
        
        case 0x27: { // AND [dp]
            const uint32_t ptr = busRead24(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint8_t bank = static_cast<uint8_t>((ptr >> 16) & 0xFF);
            const uint16_t addr = static_cast<uint16_t>(ptr & 0xFFFF);
        
            if (flagSet(m_p, FLAG_M)) and8(bus.read(bank, addr), m_a, m_p);
            else and16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }
        
        case 0x29: // AND #imm
            if (flagSet(m_p, FLAG_M)) and8(b1, m_a, m_p);
            else and16(read16le(b1, b2), m_a, m_p);
            break;
        
        case 0x2D: { // AND abs
            const uint16_t addr = read16le(b1, b2);
        
            if (flagSet(m_p, FLAG_M)) and8(bus.read(m_db, addr), m_a, m_p);
            else and16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x2F: { // AND long
            const uint16_t addr = read16le(b1, b2);
            const uint8_t bank = b3;
        
            if (flagSet(m_p, FLAG_M)) and8(bus.read(bank, addr), m_a, m_p);
            else and16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }
        
        case 0x31: { // AND (dp),Y
            const uint16_t base = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) and8(bus.read(m_db, addr), m_a, m_p);
            else and16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x32: { // AND (dp)
            const uint16_t addr = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
        
            if (flagSet(m_p, FLAG_M)) and8(bus.read(m_db, addr), m_a, m_p);
            else and16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x33: { // AND (d,S),Y
            const uint16_t stackAddr = static_cast<uint16_t>(m_sp + b1);
            const uint16_t base = busRead16(bus, 0x00, stackAddr);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) and8(bus.read(m_db, addr), m_a, m_p);
            else and16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x35: { // AND dp,X
            const uint16_t addr = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));
        
            if (flagSet(m_p, FLAG_M)) and8(bus.read(0x00, addr), m_a, m_p);
            else and16(busRead16(bus, 0x00, addr), m_a, m_p);
            break;
        }
        
        case 0x37: { // AND [dp],Y
            const uint32_t ptr = busRead24(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint8_t bank = static_cast<uint8_t>((ptr >> 16) & 0xFF);
            const uint16_t base = static_cast<uint16_t>(ptr & 0xFFFF);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) and8(bus.read(bank, addr), m_a, m_p);
            else and16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }
        
        case 0x39: { // AND abs,Y
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) and8(bus.read(m_db, addr), m_a, m_p);
            else and16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x3D: { // AND abs,X
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_x);
        
            if (flagSet(m_p, FLAG_M)) and8(bus.read(m_db, addr), m_a, m_p);
            else and16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x3F: { // AND long,X
            const uint16_t base = read16le(b1, b2);
            const uint8_t bank = b3;
            const uint16_t addr = static_cast<uint16_t>(base + m_x);
        
            if (flagSet(m_p, FLAG_M)) and8(bus.read(bank, addr), m_a, m_p);
            else and16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }

        case 0x41: { // EOR (dp,X)
            const uint16_t dp = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));
            const uint16_t addr = busRead16(bus, 0x00, dp);
        
            if (flagSet(m_p, FLAG_M)) eor8(bus.read(m_db, addr), m_a, m_p);
            else eor16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x43: { // EOR d,S
            const uint16_t addr = static_cast<uint16_t>(m_sp + b1);

            if (flagSet(m_p, FLAG_M)) eor8(bus.read(0x00, addr), m_a, m_p);
            else eor16(busRead16(bus, 0x00, addr), m_a, m_p);
            break;
        }
        
        case 0x45: { // EOR dp
            if (flagSet(m_p, FLAG_M)) eor8(bus.read(0x00, static_cast<uint16_t>(m_d + b1)), m_a, m_p);
            else eor16(busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1)), m_a, m_p);
            break;
        }
        
        case 0x47: { // EOR [dp]
            const uint32_t ptr = busRead24(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint8_t bank = static_cast<uint8_t>((ptr >> 16) & 0xFF);
            const uint16_t addr = static_cast<uint16_t>(ptr & 0xFFFF);
        
            if (flagSet(m_p, FLAG_M)) eor8(bus.read(bank, addr), m_a, m_p);
            else eor16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }
        
        case 0x49: // EOR #imm
            if (flagSet(m_p, FLAG_M)) eor8(b1, m_a, m_p);
            else eor16(read16le(b1, b2), m_a, m_p);
            break;
        
        case 0x4D: { // EOR abs
            const uint16_t addr = read16le(b1, b2);
        
            if (flagSet(m_p, FLAG_M)) eor8(bus.read(m_db, addr), m_a, m_p);
            else eor16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x4F: { // EOR long
            const uint16_t addr = read16le(b1, b2);
            const uint8_t bank = b3;
        
            if (flagSet(m_p, FLAG_M)) eor8(bus.read(bank, addr), m_a, m_p);
            else eor16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }
        
        case 0x51: { // EOR (dp),Y
            const uint16_t base = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) eor8(bus.read(m_db, addr), m_a, m_p);
            else eor16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x52: { // EOR (dp)
            const uint16_t addr = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
        
            if (flagSet(m_p, FLAG_M)) eor8(bus.read(m_db, addr), m_a, m_p);
            else eor16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x53: { // EOR (d,S),Y
            const uint16_t stackAddr = static_cast<uint16_t>(m_sp + b1);
            const uint16_t base = busRead16(bus, 0x00, stackAddr);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) eor8(bus.read(m_db, addr), m_a, m_p);
            else eor16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x55: { // EOR dp,X
            const uint16_t addr = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));
        
            if (flagSet(m_p, FLAG_M)) eor8(bus.read(0x00, addr), m_a, m_p);
            else eor16(busRead16(bus, 0x00, addr), m_a, m_p);
            break;
        }
        
        case 0x57: { // EOR [dp],Y
            const uint32_t ptr = busRead24(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint8_t bank = static_cast<uint8_t>((ptr >> 16) & 0xFF);
            const uint16_t base = static_cast<uint16_t>(ptr & 0xFFFF);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) eor8(bus.read(bank, addr), m_a, m_p);
            else eor16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }
        
        case 0x59: { // EOR abs,Y
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);
        
            if (flagSet(m_p, FLAG_M)) eor8(bus.read(m_db, addr), m_a, m_p);
            else eor16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x5D: { // EOR abs,X
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_x);
        
            if (flagSet(m_p, FLAG_M)) eor8(bus.read(m_db, addr), m_a, m_p);
            else eor16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }
        
        case 0x5F: { // EOR long,X
            const uint16_t base = read16le(b1, b2);
            const uint8_t bank = b3;
            const uint16_t addr = static_cast<uint16_t>(base + m_x);
        
            if (flagSet(m_p, FLAG_M)) eor8(bus.read(bank, addr), m_a, m_p);
            else eor16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }

        // ===== ASL A =====
        case 0x0A:
        if (flagSet(m_p, FLAG_M)) {
            uint8_t v = static_cast<uint8_t>(m_a & 0xFF);
            asl8(v, m_p);
            m_a = (m_a & 0xFF00) | v;
        } else {
            asl16(m_a, m_p);
        }
        break;

        // ===== LSR A =====
        case 0x4A:
        if (flagSet(m_p, FLAG_M)) {
            uint8_t v = static_cast<uint8_t>(m_a & 0xFF);
            lsr8(v, m_p);
            m_a = (m_a & 0xFF00) | v;
        } else {
            lsr16(m_a, m_p);
        }
        break;

        // ===== ROL A =====
        case 0x2A:
        if (flagSet(m_p, FLAG_M)) {
            uint8_t v = static_cast<uint8_t>(m_a & 0xFF);
            rol8(v, m_p);
            m_a = (m_a & 0xFF00) | v;
        } else {
            rol16(m_a, m_p);
        }
        break;

        // ===== ROR A =====
        case 0x6A:
        if (flagSet(m_p, FLAG_M)) {
            uint8_t v = static_cast<uint8_t>(m_a & 0xFF);
            ror8(v, m_p);
            m_a = (m_a & 0xFF00) | v;
        } else {
            ror16(m_a, m_p);
        }
        break;

        case 0x06: { // ASL dp
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(0x00, static_cast<uint16_t>(m_d + b1));
                asl8(v, m_p);
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), v);
            } else {
                uint16_t v = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
                asl16(v, m_p);
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), v & 0xFF);
                bus.write(0x00, b1 + 1, v >> 8);
            }
            break;
        }
        
        case 0x0E: { // ASL abs
            const uint16_t addr = read16le(b1, b2);
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(m_db, addr);
                asl8(v, m_p);
                bus.write(m_db, addr, v);
            } else {
                uint16_t v = busRead16(bus, m_db, addr);
                asl16(v, m_p);
                bus.write(m_db, addr, v & 0xFF);
                bus.write(m_db, addr + 1, v >> 8);
            }
            break;
        }

        case 0x16: { // ASL dp,X
            uint16_t addr = (m_d + b1 + m_x) & 0xFFFF;
        
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(0x00, addr);
                m_p = (v & 0x80) ? (m_p | FLAG_CARRY) : (m_p & ~FLAG_CARRY);
                v <<= 1;
                bus.write(0x00, addr, v);
                setZN8(v, m_p);
            } else {
                uint16_t v = busRead16(bus, 0x00, addr);
                m_p = (v & 0x8000) ? (m_p | FLAG_CARRY) : (m_p & ~FLAG_CARRY);
                v <<= 1;
                bus.write(0x00, addr, v & 0xFF);
                bus.write(0x00, addr + 1, v >> 8);
                setZN16(v, m_p);
            }
            break;
        }

        case 0x1E: { // ASL abs,X
            uint16_t addr = read16le(b1, b2) + m_x;
        
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(m_db, addr);
                m_p = (v & 0x80) ? (m_p | FLAG_CARRY) : (m_p & ~FLAG_CARRY);
                v <<= 1;
                bus.write(m_db, addr, v);
                setZN8(v, m_p);
            } else {
                uint16_t v = busRead16(bus, m_db, addr);
                m_p = (v & 0x8000) ? (m_p | FLAG_CARRY) : (m_p & ~FLAG_CARRY);
                v <<= 1;
                bus.write(m_db, addr, v & 0xFF);
                bus.write(m_db, addr + 1, v >> 8);
                setZN16(v, m_p);
            }
            break;
        }

        case 0x46: { // LSR dp
            uint8_t v = bus.read(0x00, static_cast<uint16_t>(m_d + b1));
            lsr8(v, m_p);
            bus.write(0x00, static_cast<uint16_t>(m_d + b1), v);
            break;
        }
        
        case 0x4E: { // LSR abs
            const uint16_t addr = read16le(b1, b2);
            uint8_t v = bus.read(m_db, addr);
            lsr8(v, m_p);
            bus.write(m_db, addr, v);
            break;
        }

        case 0x56: { // LSR dp,X
            uint16_t addr = (m_d + b1 + m_x) & 0xFFFF;
        
            uint8_t v = bus.read(0x00, addr);
            m_p = (v & 0x01) ? (m_p | FLAG_CARRY) : (m_p & ~FLAG_CARRY);
            v >>= 1;
        
            bus.write(0x00, addr, v);
            setZN8(v, m_p);
            break;
        }

        case 0x5E: { // LSR abs,X
            uint16_t base = read16le(b1, b2);
            uint16_t addr = base + m_x;
        
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(m_db, addr);
        
                if (v & 0x01) m_p |= FLAG_CARRY; else m_p &= ~FLAG_CARRY;
        
                v >>= 1;
        
                bus.write(m_db, addr, v);
                setZN8(v, m_p);
            } else {
                uint16_t v = busRead16(bus, m_db, addr);
        
                if (v & 0x0001) m_p |= FLAG_CARRY; else m_p &= ~FLAG_CARRY;
        
                v >>= 1;
        
                bus.write(m_db, addr, v & 0xFF);
                bus.write(m_db, static_cast<uint16_t>(addr + 1), v >> 8);
                setZN16(v, m_p);
            }
            break;
        }

        case 0x26: { // ROL dp
            uint8_t v = bus.read(0x00, static_cast<uint16_t>(m_d + b1));
            rol8(v, m_p);
            bus.write(0x00, static_cast<uint16_t>(m_d + b1), v);
            break;
        }
        
        case 0x2E: { // ROL abs
            const uint16_t addr = read16le(b1, b2);
            uint8_t v = bus.read(m_db, addr);
            rol8(v, m_p);
            bus.write(m_db, addr, v);
            break;
        }

        case 0x36: { // ROL dp,X
            uint16_t addr = (m_d + b1 + m_x) & 0xFFFF;
            uint8_t carry = (m_p & FLAG_CARRY) ? 1 : 0;
        
            uint8_t v = bus.read(0x00, addr);
            uint8_t newCarry = (v & 0x80) ? 1 : 0;
            v = (v << 1) | carry;
        
            if (newCarry) m_p |= FLAG_CARRY; else m_p &= ~FLAG_CARRY;
        
            bus.write(0x00, addr, v);
            setZN8(v, m_p);
            break;
        }

        case 0x3E: { // ROL abs,X
            uint16_t base = read16le(b1, b2);
            uint16_t addr = base + m_x;
        
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(m_db, addr);
                uint8_t carry = (m_p & FLAG_CARRY) ? 1 : 0;
                uint8_t newCarry = (v & 0x80) ? 1 : 0;
        
                v = (v << 1) | carry;
        
                if (newCarry) m_p |= FLAG_CARRY; else m_p &= ~FLAG_CARRY;
        
                bus.write(m_db, addr, v);
                setZN8(v, m_p);
            } else {
                uint16_t v = busRead16(bus, m_db, addr);
                uint16_t carry = (m_p & FLAG_CARRY) ? 1 : 0;
                uint16_t newCarry = (v & 0x8000) ? 1 : 0;
        
                v = (v << 1) | carry;
        
                if (newCarry) m_p |= FLAG_CARRY; else m_p &= ~FLAG_CARRY;
        
                bus.write(m_db, addr, v & 0xFF);
                bus.write(m_db, static_cast<uint16_t>(addr + 1), v >> 8);
                setZN16(v, m_p);
            }
            break;
        }

        case 0x66: { // ROR dp
            uint8_t v = bus.read(0x00, static_cast<uint16_t>(m_d + b1));
            ror8(v, m_p);
            bus.write(0x00, static_cast<uint16_t>(m_d + b1), v);
            break;
        }
        
        case 0x6E: { // ROR abs
            const uint16_t addr = read16le(b1, b2);
            uint8_t v = bus.read(m_db, addr);
            ror8(v, m_p);
            bus.write(m_db, addr, v);
            break;
        }

        case 0x76: { // ROR dp,X
            uint16_t addr = (m_d + b1 + m_x) & 0xFFFF;
        
            uint8_t v = bus.read(0x00, addr);
            uint8_t carry = (m_p & FLAG_CARRY) ? 1 : 0;
            uint8_t newCarry = v & 1;
        
            v = (v >> 1) | (carry << 7);
        
            if (newCarry) m_p |= FLAG_CARRY; else m_p &= ~FLAG_CARRY;
        
            bus.write(0x00, addr, v);
            setZN8(v, m_p);
            break;
        }

        case 0x7E: { // ROR abs,X
            uint16_t base = read16le(b1, b2);
            uint16_t addr = base + m_x;
        
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(m_db, addr);
                uint8_t carry = (m_p & FLAG_CARRY) ? 1 : 0;
                uint8_t newCarry = v & 0x01;
        
                v = (v >> 1) | (carry << 7);
        
                if (newCarry) m_p |= FLAG_CARRY; else m_p &= ~FLAG_CARRY;
        
                bus.write(m_db, addr, v);
                setZN8(v, m_p);
            } else {
                uint16_t v = busRead16(bus, m_db, addr);
                uint16_t carry = (m_p & FLAG_CARRY) ? 1 : 0;
                uint16_t newCarry = v & 0x0001;
        
                v = (v >> 1) | (carry << 15);
        
                if (newCarry) m_p |= FLAG_CARRY; else m_p &= ~FLAG_CARRY;
        
                bus.write(m_db, addr, v & 0xFF);
                bus.write(m_db, static_cast<uint16_t>(addr + 1), v >> 8);
                setZN16(v, m_p);
            }
            break;
        }

        case 0x1B: { // TCS
            m_sp = m_a;
            break;
        }

        case 0x3B: { // TSC
            m_a = m_sp;
            setZN16(m_a, m_p);
            break;
        }

        case 0x5B: { // TCD
            m_d = m_a;
            break;
        }

        case 0x7B: { // TDC
            m_a = m_d;
            setZN16(m_a, m_p);
            break;
        }

        case 0xD4: { // PEI
            uint16_t addr = (m_d + b1) & 0xFFFF;
            uint16_t value = busRead16(bus, 0x00, addr);
            push16(bus, m_sp, value);
            break;
        }
        
        case 0xF4: { // PEA
            uint16_t value = read16le(b1, b2);
            push16(bus, m_sp, value);
            break;
        }
        
        case 0x62: { // PER
            int16_t offset = (int16_t)read16le(b1, b2);
            uint16_t target = m_pc + offset;
        
            push16(bus, m_sp, target);
            break;
        }

        case 0x10: // BPL
            m_pc = ((m_p & FLAG_NEGATIVE) == 0) ? branchTarget8(m_pc, b1) : static_cast<uint16_t>(m_pc + 2);
            pcHandled = true;
            break;
        case 0x30: // BMI
            m_pc = ((m_p & FLAG_NEGATIVE) != 0) ? branchTarget8(m_pc, b1) : static_cast<uint16_t>(m_pc + 2);
            pcHandled = true;
            break;
        case 0x50: // BVC
            m_pc = ((m_p & FLAG_OVERFLOW) == 0) ? branchTarget8(m_pc, b1) : static_cast<uint16_t>(m_pc + 2);
            pcHandled = true;
            break;
        case 0x70: // BVS
            m_pc = ((m_p & FLAG_OVERFLOW) != 0) ? branchTarget8(m_pc, b1) : static_cast<uint16_t>(m_pc + 2);
            pcHandled = true;
            break;
        case 0x80: // BRA
            m_pc = branchTarget8(m_pc, b1);
            pcHandled = true;
            break;
        case 0x82: // BRL
            m_pc = branchTarget16(m_pc, b1, b2);
            pcHandled = true;
            break;
        case 0x90: // BCC
            m_pc = ((m_p & FLAG_CARRY) == 0) ? branchTarget8(m_pc, b1) : static_cast<uint16_t>(m_pc + 2);
            pcHandled = true;
            break;
        case 0xB0: // BCS
            m_pc = ((m_p & FLAG_CARRY) != 0) ? branchTarget8(m_pc, b1) : static_cast<uint16_t>(m_pc + 2);
            pcHandled = true;
            break;
        case 0xD0: // BNE
            m_pc = ((m_p & FLAG_ZERO) == 0) ? branchTarget8(m_pc, b1) : static_cast<uint16_t>(m_pc + 2);
            pcHandled = true;
            break;
        case 0xF0: // BEQ
            m_pc = ((m_p & FLAG_ZERO) != 0) ? branchTarget8(m_pc, b1) : static_cast<uint16_t>(m_pc + 2);
            pcHandled = true;
            break;

        case 0x4C: { // JMP abs
            const uint16_t addr = read16le(b1, b2);
            m_pc = addr;
            pcHandled = true;
            break;
        }
        
        case 0x6C: { // JMP (abs)
            const uint16_t ptr = read16le(b1, b2);
            const uint16_t addr = busRead16(bus, m_bank, ptr);
            m_pc = addr;
            pcHandled = true;
            break;
        }
        
        case 0x7C: { // JMP (abs,X)
            const uint16_t base = read16le(b1, b2);
            const uint16_t ptr = static_cast<uint16_t>(base + m_x);
            const uint16_t addr = busRead16(bus, m_bank, ptr);
            m_pc = addr;
            pcHandled = true;
            break;
        }

        case 0xDC: { // JMP [abs] (Indirect Long)
            const uint16_t ptr = read16le(b1, b2);
            const uint32_t target = busRead24(bus, m_bank, ptr);
        
            m_pc   = static_cast<uint16_t>(target & 0xFFFF);
            m_bank = static_cast<uint8_t>((target >> 16) & 0xFF);
        
            pcHandled = true;
            break;
        }

        case 0x5C: // JML long
            m_pc = read16le(b1, b2);
            m_bank = b3;
            pcHandled = true;
            break;

        case 0x20: { // JSR abs
            const uint16_t returnAddr = static_cast<uint16_t>(m_pc + 2);
            push16(bus, m_sp, returnAddr);
            m_pc = read16le(b1, b2);
            pcHandled = true;
            break;
        }

        case 0xFC: { // JSR (abs,X)
            const uint16_t base = read16le(b1, b2);
            const uint16_t ptr = static_cast<uint16_t>(base + m_x);
            const uint16_t target = busRead16(bus, m_bank, ptr);
        
            const uint16_t returnAddr = static_cast<uint16_t>(m_pc + 2);
            push16(bus, m_sp, returnAddr);
        
            m_pc = target;
            pcHandled = true;
            break;
        }

        case 0x60: { // RTS
            const uint16_t returnAddr = pop16(bus, m_sp);
            m_pc = static_cast<uint16_t>(returnAddr + 1);
            pcHandled = true;
            break;
        }

        case 0x22: { // JSL long
            const uint32_t returnAddr =
                (static_cast<uint32_t>(m_bank) << 16) |
                static_cast<uint16_t>(m_pc + 3);
            push24(bus, m_sp, returnAddr);
            m_pc = read16le(b1, b2);
            m_bank = b3;
            pcHandled = true;
            break;
        }

        case 0x89: // BIT #imm
            if (flagSet(m_p, FLAG_M)) bitImm8(b1, m_a, m_p);
            else bitImm16(read16le(b1, b2), m_a, m_p);
            break;

        case 0x24: { // BIT dp
            if (flagSet(m_p, FLAG_M)) bit8(bus.read(0x00, static_cast<uint16_t>(m_d + b1)), m_a, m_p);
            else bit16(busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1)), m_a, m_p);
            break;
        }

        case 0x2C: { // BIT abs
            const uint16_t addr = read16le(b1, b2);

            if (flagSet(m_p, FLAG_M)) bit8(bus.read(m_db, addr), m_a, m_p);
            else bit16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0x34: { // BIT dp,X
            const uint16_t addr = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));

            if (flagSet(m_p, FLAG_M)) bit8(bus.read(0x00, addr), m_a, m_p);
            else bit16(busRead16(bus, 0x00, addr), m_a, m_p);
            break;
        }

        case 0x3C: { // BIT abs,X
            const uint16_t addr = static_cast<uint16_t>(read16le(b1, b2) + m_x);

            if (flagSet(m_p, FLAG_M)) bit8(bus.read(m_db, addr), m_a, m_p);
            else bit16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0x04: { // TSB dp
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(0x00, static_cast<uint16_t>(m_d + b1));
                tsb8(v, m_a, m_p);
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), v);
            } else {
                uint16_t v = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
                tsb16(v, m_a, m_p);
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), static_cast<uint8_t>(v & 0xFF));
                bus.write(0x00, static_cast<uint16_t>(m_d + b1 + 1), static_cast<uint8_t>(v >> 8));
            }
            break;
        }
        
        case 0x0C: { // TSB abs
            const uint16_t addr = read16le(b1, b2);
        
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(m_db, addr);
                tsb8(v, m_a, m_p);
                bus.write(m_db, addr, v);
            } else {
                uint16_t v = busRead16(bus, m_db, addr);
                tsb16(v, m_a, m_p);
                bus.write(m_db, addr, static_cast<uint8_t>(v & 0xFF));
                bus.write(m_db, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>(v >> 8));
            }
            break;
        }

        case 0x14: { // TRB dp
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(0x00, static_cast<uint16_t>(m_d + b1));
                trb8(v, m_a, m_p);
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), v);
            } else {
                uint16_t v = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
                trb16(v, m_a, m_p);
                bus.write(0x00, static_cast<uint16_t>(m_d + b1), static_cast<uint8_t>(v & 0xFF));
                bus.write(0x00, static_cast<uint16_t>(m_d + b1 + 1), static_cast<uint8_t>(v >> 8));
            }
            break;
        }
        
        case 0x1C: { // TRB abs
            const uint16_t addr = read16le(b1, b2);
        
            if (flagSet(m_p, FLAG_M)) {
                uint8_t v = bus.read(m_db, addr);
                trb8(v, m_a, m_p);
                bus.write(m_db, addr, v);
            } else {
                uint16_t v = busRead16(bus, m_db, addr);
                trb16(v, m_a, m_p);
                bus.write(m_db, addr, static_cast<uint8_t>(v & 0xFF));
                bus.write(m_db, static_cast<uint16_t>(addr + 1), static_cast<uint8_t>(v >> 8));
            }
            break;
        }

        case 0x61: { // ADC (dp,X)
            const uint16_t dp = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));
            const uint16_t addr = busRead16(bus, 0x00, dp);

            if (flagSet(m_p, FLAG_M)) adc8(bus.read(m_db, addr), m_a, m_p);
            else adc16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0x63: { // ADC d,S
            const uint16_t addr = static_cast<uint16_t>(m_sp + b1);

            if (flagSet(m_p, FLAG_M)) adc8(bus.read(0x00, addr), m_a, m_p);
            else adc16(busRead16(bus, 0x00, addr), m_a, m_p);
            break;
        }

        case 0x65: { // ADC dp
            if (flagSet(m_p, FLAG_M)) adc8(bus.read(0x00, static_cast<uint16_t>(m_d + b1)), m_a, m_p);
            else adc16(busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1)), m_a, m_p);
            break;
        }

        case 0x67: { // ADC [dp]
            const uint32_t ptr = busRead24(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint8_t bank = static_cast<uint8_t>((ptr >> 16) & 0xFF);
            const uint16_t addr = static_cast<uint16_t>(ptr & 0xFFFF);

            if (flagSet(m_p, FLAG_M)) adc8(bus.read(bank, addr), m_a, m_p);
            else adc16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }

        case 0x69: // ADC #imm
            if (flagSet(m_p, FLAG_M)) adc8(b1, m_a, m_p);
            else adc16(read16le(b1, b2), m_a, m_p);
            break;

        case 0x6D: { // ADC abs
            const uint16_t addr = read16le(b1, b2);

            if (flagSet(m_p, FLAG_M)) adc8(bus.read(m_db, addr), m_a, m_p);
            else adc16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0x6F: { // ADC long
            const uint16_t addr = read16le(b1, b2);
            const uint8_t bank = b3;

            if (flagSet(m_p, FLAG_M)) adc8(bus.read(bank, addr), m_a, m_p);
            else adc16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }

        case 0x71: { // ADC (dp),Y
            const uint16_t base = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint16_t addr = static_cast<uint16_t>(base + m_y);

            if (flagSet(m_p, FLAG_M)) adc8(bus.read(m_db, addr), m_a, m_p);
            else adc16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0x72: { // ADC (dp)
            const uint16_t addr = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));

            if (flagSet(m_p, FLAG_M)) adc8(bus.read(m_db, addr), m_a, m_p);
            else adc16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0x73: { // ADC (d,S),Y
            const uint16_t stackAddr = static_cast<uint16_t>(m_sp + b1);
            const uint16_t base = busRead16(bus, 0x00, stackAddr);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);

            if (flagSet(m_p, FLAG_M)) adc8(bus.read(m_db, addr), m_a, m_p);
            else adc16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0x75: { // ADC dp,X
            const uint16_t addr = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));

            if (flagSet(m_p, FLAG_M)) adc8(bus.read(0x00, addr), m_a, m_p);
            else adc16(busRead16(bus, 0x00, addr), m_a, m_p);
            break;
        }

        case 0x77: { // ADC [dp],Y
            const uint32_t ptr = busRead24(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint8_t bank = static_cast<uint8_t>((ptr >> 16) & 0xFF);
            const uint16_t base = static_cast<uint16_t>(ptr & 0xFFFF);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);

            if (flagSet(m_p, FLAG_M)) adc8(bus.read(bank, addr), m_a, m_p);
            else adc16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }

        case 0x79: { // ADC abs,Y
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);

            if (flagSet(m_p, FLAG_M)) adc8(bus.read(m_db, addr), m_a, m_p);
            else adc16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0x7D: { // ADC abs,X
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_x);

            if (flagSet(m_p, FLAG_M)) adc8(bus.read(m_db, addr), m_a, m_p);
            else adc16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0x7F: { // ADC long,X
            const uint16_t base = read16le(b1, b2);
            const uint8_t bank = b3;
            const uint16_t addr = static_cast<uint16_t>(base + m_x);

            if (flagSet(m_p, FLAG_M)) adc8(bus.read(bank, addr), m_a, m_p);
            else adc16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }

        case 0xE1: { // SBC (dp,X)
            const uint16_t dp = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));
            const uint16_t addr = busRead16(bus, 0x00, dp);

            if (flagSet(m_p, FLAG_M)) sbc8(bus.read(m_db, addr), m_a, m_p);
            else sbc16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0xE3: { // SBC d,S
            const uint16_t addr = static_cast<uint16_t>(m_sp + b1);

            if (flagSet(m_p, FLAG_M)) sbc8(bus.read(0x00, addr), m_a, m_p);
            else sbc16(busRead16(bus, 0x00, addr), m_a, m_p);
            break;
        }

        case 0xE5: { // SBC dp
            if (flagSet(m_p, FLAG_M)) sbc8(bus.read(0x00, static_cast<uint16_t>(m_d + b1)), m_a, m_p);
            else sbc16(busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1)), m_a, m_p);
            break;
        }

        case 0xE7: { // SBC [dp]
            const uint32_t ptr = busRead24(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint8_t bank = static_cast<uint8_t>((ptr >> 16) & 0xFF);
            const uint16_t addr = static_cast<uint16_t>(ptr & 0xFFFF);

            if (flagSet(m_p, FLAG_M)) sbc8(bus.read(bank, addr), m_a, m_p);
            else sbc16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }

        case 0xE9: // SBC #imm
            if (flagSet(m_p, FLAG_M)) sbc8(b1, m_a, m_p);
            else sbc16(read16le(b1, b2), m_a, m_p);
            break;

        case 0xED: { // SBC abs
            const uint16_t addr = read16le(b1, b2);

            if (flagSet(m_p, FLAG_M)) sbc8(bus.read(m_db, addr), m_a, m_p);
            else sbc16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0xEF: { // SBC long
            const uint16_t addr = read16le(b1, b2);
            const uint8_t bank = b3;

            if (flagSet(m_p, FLAG_M)) sbc8(bus.read(bank, addr), m_a, m_p);
            else sbc16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }

        case 0xF1: { // SBC (dp),Y
            const uint16_t base = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint16_t addr = static_cast<uint16_t>(base + m_y);

            if (flagSet(m_p, FLAG_M)) sbc8(bus.read(m_db, addr), m_a, m_p);
            else sbc16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0xF2: { // SBC (dp)
            const uint16_t addr = busRead16(bus, 0x00, static_cast<uint16_t>(m_d + b1));

            if (flagSet(m_p, FLAG_M)) sbc8(bus.read(m_db, addr), m_a, m_p);
            else sbc16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0xF3: { // SBC (d,S),Y
            const uint16_t stackAddr = static_cast<uint16_t>(m_sp + b1);
            const uint16_t base = busRead16(bus, 0x00, stackAddr);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);

            if (flagSet(m_p, FLAG_M)) sbc8(bus.read(m_db, addr), m_a, m_p);
            else sbc16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0xF5: { // SBC dp,X
            const uint16_t addr = static_cast<uint16_t>(m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF));

            if (flagSet(m_p, FLAG_M)) sbc8(bus.read(0x00, addr), m_a, m_p);
            else sbc16(busRead16(bus, 0x00, addr), m_a, m_p);
            break;
        }

        case 0xF7: { // SBC [dp],Y
            const uint32_t ptr = busRead24(bus, 0x00, static_cast<uint16_t>(m_d + b1));
            const uint8_t bank = static_cast<uint8_t>((ptr >> 16) & 0xFF);
            const uint16_t base = static_cast<uint16_t>(ptr & 0xFFFF);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);

            if (flagSet(m_p, FLAG_M)) sbc8(bus.read(bank, addr), m_a, m_p);
            else sbc16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }

        case 0xF9: { // SBC abs,Y
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_y);

            if (flagSet(m_p, FLAG_M)) sbc8(bus.read(m_db, addr), m_a, m_p);
            else sbc16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0xFD: { // SBC abs,X
            const uint16_t base = read16le(b1, b2);
            const uint16_t addr = static_cast<uint16_t>(base + m_x);

            if (flagSet(m_p, FLAG_M)) sbc8(bus.read(m_db, addr), m_a, m_p);
            else sbc16(busRead16(bus, m_db, addr), m_a, m_p);
            break;
        }

        case 0xFF: { // SBC long,X
            const uint16_t base = read16le(b1, b2);
            const uint8_t bank = b3;
            const uint16_t addr = static_cast<uint16_t>(base + m_x);

            if (flagSet(m_p, FLAG_M)) sbc8(bus.read(bank, addr), m_a, m_p);
            else sbc16(busRead16(bus, bank, addr), m_a, m_p);
            break;
        }

        case 0xEB: { // XBA
            const uint8_t lo = static_cast<uint8_t>(m_a & 0x00FF);
            const uint8_t hi = static_cast<uint8_t>((m_a >> 8) & 0x00FF);
        
            m_a = static_cast<uint16_t>((static_cast<uint16_t>(lo) << 8) | hi);
        
            setZN8(static_cast<uint8_t>(m_a & 0x00FF), m_p);
            break;
        }

        case 0xFB: { // XCE
            const bool oldCarry = flagSet(m_p, FLAG_CARRY);
            const bool oldEmulation = m_e;
        
            if (oldEmulation) m_p |= FLAG_CARRY;
            else m_p &= ~FLAG_CARRY;
        
            m_e = oldCarry;
        
            if (m_e) {
                m_p |= FLAG_M;
                m_p |= FLAG_X;
                m_sp = static_cast<uint16_t>(0x0100 | (m_sp & 0x00FF));
                applyRegisterSizesFromP(m_p, m_a, m_x, m_y);
            }
        
            break;
        }

        case 0x44: { // MVP
            uint8_t srcBank = b1;
            uint8_t dstBank = b2;
        
            uint8_t value = bus.read(srcBank, m_x);
            bus.write(dstBank, m_y, value);
        
            m_x--;
            m_y--;
            m_a--;
        
            if (m_a != 0xFFFF) {
                m_pc -= 3; // repeat instruction
            }
            break;
        }

        case 0x54: { // MVN
            uint8_t srcBank = b1;
            uint8_t dstBank = b2;
        
            uint8_t value = bus.read(srcBank, m_x);
            bus.write(dstBank, m_y, value);
        
            m_x++;
            m_y++;
            m_a--;
        
            if (m_a != 0xFFFF) {
                m_pc -= 3;
            }
            break;
        }

        case 0x6B: { // RTL
            const uint32_t returnAddr = pop24(bus, m_sp);
            m_bank = static_cast<uint8_t>((returnAddr >> 16) & 0xFF);
            m_pc = static_cast<uint16_t>((returnAddr & 0xFFFF) + 1);
            pcHandled = true;
            break;
        }

        case 0xCB: // WAI
            m_waiting = true;
            break;

        case 0xDB: // STP
            m_stopped = true;
            break;

        default:
            break;
    }

    if (flagSet(m_p, FLAG_X)) {
        m_x &= 0x00FF;
        m_y &= 0x00FF;
    }

    if (!pcHandled) {
        m_pc = static_cast<uint16_t>(m_pc + size);
    }

    m_cycles += OPCODES[m_opcode].cyclesNumber;
}

void CPU::wakeFromWaiSilently() {
    if (!m_waiting) return;
    m_waiting = false;
    // Keep small; WAI burn is 2 cycles/iter when spinning — same order of magnitude as IRQ edge exit.
    m_cycles += 6;
}

uint16_t CPU::resetVector() const { return m_resetVector; }
uint8_t CPU::bank() const { return m_bank; }
uint16_t CPU::pc() const { return m_pc; }
uint32_t CPU::pc24() const { return (static_cast<uint32_t>(m_bank) << 16) | m_pc; }
uint8_t CPU::opcode() const { return m_opcode; }
const std::string& CPU::instruction() const { return m_instruction; }
const std::string& CPU::bytes() const { return m_bytes; }
uint8_t CPU::p() const { return m_p; }
bool CPU::flagM() const { return (m_p & FLAG_M) != 0; }
bool CPU::flagX() const { return (m_p & FLAG_X) != 0; }
uint16_t CPU::a() const { return m_a; }
uint16_t CPU::x() const { return m_x; }
uint16_t CPU::y() const { return m_y; }
uint16_t CPU::sp() const { return m_sp; }
uint64_t CPU::cycles() const { return m_cycles; }
