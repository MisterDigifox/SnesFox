#pragma once

#include <cstdint>

class APU;

// Partial SPC700 CPU (Sony S-NES IPL + common uploads). Undefined opcodes halt the core.
class Spc700 {
public:
    void reset();

    bool halted() const { return m_halted; }
    void setHalted(bool v) { m_halted = v; }

    // Returns SMP cycle debit (instruction-level table, approximate).
    uint32_t step(APU& apu);

private:
    uint16_t m_pc{};
    uint8_t  m_a{};
    uint8_t  m_x{};
    uint8_t  m_y{};
    uint8_t  m_sp{};
    uint8_t  m_psw{};

    uint8_t  fetchPc(APU& apu);
    uint16_t read16Zp(APU& apu, uint8_t d) const;

    uint16_t dpAddr(uint8_t d) const;

    uint8_t  ramRead(APU& apu, uint16_t adr) const;
    void     ramWrite(APU& apu, uint16_t adr, uint8_t v);

    uint16_t read16Mem(APU& apu, uint16_t adr) const;

    void     stackPush(APU& apu, uint8_t v);
    uint8_t  stackPop(APU& apu);

    bool flagZ() const { return (m_psw & 0x02) != 0; }
    bool flagN() const { return (m_psw & 0x80) != 0; }
    bool flagP() const { return (m_psw & 0x20) != 0; }
    bool flagC() const { return (m_psw & 0x01) != 0; }
    bool flagV() const { return (m_psw & 0x40) != 0; }
    bool flagH() const { return (m_psw & 0x08) != 0; }
    void setZ(bool v) {
        if (v)
            m_psw |= 0x02;
        else
            m_psw &= static_cast<uint8_t>(~0x02);
    }
    void setN(bool v) {
        if (v)
            m_psw |= 0x80;
        else
            m_psw &= static_cast<uint8_t>(~0x80);
    }

    void setNZFromByte(uint8_t v) {
        setZ(v == 0);
        setN((v & 0x80) != 0);
    }

    void setC(bool v);
    void setV(bool v);
    void setH(bool v);

    /// 8-bit add with carry → A; updates NVH·ZC (SPC700 ADC).
    void adcA(uint8_t operand);
    /// 8-bit subtract with borrow → A; updates NV··ZC (SPC700 SBC).
    void sbcA(uint8_t operand);

    void write16Zp(APU& apu, uint8_t d, uint16_t w);

    void cmp8(uint8_t lhs, uint8_t rhs) {
        const int     res = lhs - rhs;
        const uint8_t r8  = static_cast<uint8_t>(res & 0xFF);
        setNZFromByte(r8);
        if (lhs >= rhs)
            m_psw |= 0x01;
        else
            m_psw &= static_cast<uint8_t>(~0x01);
        // SPC700 CMP only updates N, Z, and C (Gilligan opcode table leaves V/H alone).
    }

    bool m_halted{};
};
