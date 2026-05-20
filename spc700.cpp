#include "spc700.hpp"

#include "apu.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

uint16_t spcPcPlusRel(uint16_t pc, int8_t rel) {
    return static_cast<uint16_t>(static_cast<int32_t>(pc) + static_cast<int32_t>(rel));
}

} // namespace

void Spc700::reset() {
    m_pc     = 0xFFC0;
    m_a      = 0;
    m_x      = 0;
    m_y      = 0;
    m_sp     = 0xEF;
    m_psw    = 0x02; // Z set at entry (Anomie's post-IPL user note differ; IPL clears Z first)
    m_halted = false;
}

uint16_t Spc700::dpAddr(uint8_t d) const {
    return static_cast<uint16_t>(flagP() ? (0x0100u | static_cast<unsigned>(d))
                                         : static_cast<unsigned>(d));
}

uint8_t Spc700::fetchPc(APU& apu) {
    const uint8_t b = ramRead(apu, m_pc);
    m_pc++;
    return b;
}

uint16_t Spc700::read16Zp(APU& apu, uint8_t d) const {
    const uint16_t base = dpAddr(d);
    const uint8_t  lo   = ramRead(apu, base);
    const uint8_t  hi =
        ramRead(apu,
                static_cast<uint16_t>((base & 0xFF00u) | ((base + 1u) & 0xFFu)));
    return static_cast<uint16_t>(static_cast<unsigned>(lo) | (static_cast<unsigned>(hi) << 8));
}

uint8_t Spc700::ramRead(APU& apu, uint16_t adr) const {
    return 0;//const_cast<const APU*>(&apu)->spcPeek(adr);
}

void Spc700::ramWrite(APU& apu, uint16_t adr, uint8_t v) {
    //apu.spcPoke(adr, v);
}

uint16_t Spc700::read16Mem(APU& apu, uint16_t adr) const {
    const uint8_t lo = ramRead(apu, adr);
    const uint8_t hi = ramRead(apu, static_cast<uint16_t>(adr + 1u));
    return static_cast<uint16_t>(static_cast<unsigned>(lo) | (static_cast<unsigned>(hi) << 8));
}

void Spc700::stackPush(APU& apu, uint8_t v) {
    ramWrite(apu, static_cast<uint16_t>(0x0100u | static_cast<unsigned>(m_sp)), v);
    --m_sp;
}

uint8_t Spc700::stackPop(APU& apu) {
    ++m_sp;
    return ramRead(apu, static_cast<uint16_t>(0x0100u | static_cast<unsigned>(m_sp)));
}

void Spc700::setC(bool v) {
    if (v)
        m_psw |= 0x01;
    else
        m_psw &= static_cast<uint8_t>(~0x01u);
}

void Spc700::setV(bool v) {
    if (v)
        m_psw |= 0x40;
    else
        m_psw &= static_cast<uint8_t>(~0x40u);
}

void Spc700::setH(bool v) {
    if (v)
        m_psw |= 0x08;
    else
        m_psw &= static_cast<uint8_t>(~0x08u);
}

void Spc700::adcA(uint8_t m) {
    const unsigned c = flagC() ? 1u : 0u;
    const unsigned a = m_a;
    const unsigned sum = a + static_cast<unsigned>(m) + c;
    setH(((a & 0xFu) + (static_cast<unsigned>(m) & 0xFu) + c) >= 0x10u);
    const uint8_t r = static_cast<uint8_t>(sum & 0xFFu);
    setNZFromByte(r);
    setC(sum > 0xFFu);
    setV((((a ^ static_cast<unsigned>(m)) & 0x80u) == 0) &&
         (((a ^ static_cast<unsigned>(r)) & 0x80u) != 0));
    m_a = r;
}

void Spc700::sbcA(uint8_t m) {
    const unsigned c = flagC() ? 1u : 0u;
    const unsigned a = m_a;
    const uint16_t   w = static_cast<uint16_t>(a) - static_cast<uint16_t>(m) - (c ? 0u : 1u);
    const uint8_t    r = static_cast<uint8_t>(w & 0xFFu);
    setNZFromByte(r);
    setC((w & 0x100u) == 0);
    setH(false);
    setV((((a ^ static_cast<unsigned>(m)) & 0x80u) != 0) &&
         (((a ^ static_cast<unsigned>(r)) & 0x80u) != 0));
    m_a = r;
}

void Spc700::write16Zp(APU& apu, uint8_t d, uint16_t val) {
    const uint16_t base = dpAddr(d);
    ramWrite(apu, base, static_cast<uint8_t>(val & 0xFFu));
    ramWrite(apu,
             static_cast<uint16_t>((base & 0xFF00u) | ((base + 1u) & 0xFFu)),
             static_cast<uint8_t>(val >> 8));
}

uint32_t Spc700::step(APU& apu) {
    if (m_halted) {
        return 2;
    }

    const uint8_t opc = fetchPc(apu);

    auto readDp = [&](uint8_t d) -> uint16_t {
        return dpAddr(d);
    };

    auto zpAddrPlusX = [&](uint8_t d) -> uint16_t {
        const uint16_t b = readDp(d);
        return static_cast<uint16_t>(
            (b & 0xFF00u) | ((static_cast<unsigned>(b) + m_x) & 0xFFu));
    };
    auto zpAddrPlusY = [&](uint8_t d) -> uint16_t {
        const uint16_t b = readDp(d);
        return static_cast<uint16_t>(
            (b & 0xFF00u) | ((static_cast<unsigned>(b) + m_y) & 0xFFu));
    };

    // BBS d.n,rel — opcodes ending in …$03 (Gilligan/MAME naming).
    if ((opc & 0x1Fu) == 0x03u) {
        const unsigned bit = static_cast<unsigned>(opc >> 5);
        const uint8_t  d   = fetchPc(apu);
        const int8_t   rel = static_cast<int8_t>(fetchPc(apu));
        const uint8_t  v   = ramRead(apu, readDp(d));
        const bool     take = (v & (1u << bit)) != 0;
        if (take)
            m_pc = spcPcPlusRel(m_pc, rel);
        return take ? 7u : 5u;
    }
    // BBC d.n,rel — opcodes ending in …$13.
    if ((opc & 0x1Fu) == 0x13u) {
        const unsigned bit = static_cast<unsigned>(opc >> 5);
        const uint8_t  d   = fetchPc(apu);
        const int8_t   rel = static_cast<int8_t>(fetchPc(apu));
        const uint8_t  v   = ramRead(apu, readDp(d));
        const bool     take = (v & (1u << bit)) == 0;
        if (take)
            m_pc = spcPcPlusRel(m_pc, rel);
        return take ? 7u : 5u;
    }

    // TCALLn ($01,$11,…,$F1): PC ← [ $FFDE − 2·N ]; push return PC.
    if ((opc & 0x0Fu) == 0x01u) {
        const unsigned   n    = static_cast<unsigned>(opc >> 4);
        const uint16_t   vect = static_cast<uint16_t>(0xFFDEu - 2u * n);
        const uint16_t   dest = read16Mem(apu, vect);
        const uint16_t   ret  = m_pc;
        stackPush(apu, static_cast<uint8_t>(ret >> 8));
        stackPush(apu, static_cast<uint8_t>(ret & 0xFF));
        m_pc = dest;
        return 8;
    }

    // SET1 d.n / CLR1 d.n — opcodes $02,$22,…,$E2 (set) vs $12,$32,…,$F2 (clear).
    if ((opc & 0x0Fu) == 0x02u) {
        const unsigned   bit = static_cast<unsigned>(opc >> 5) & 7u;
        const bool       clr = (opc & 0x10u) != 0u;
        const uint16_t   adr = readDp(fetchPc(apu));
        uint8_t          v   = ramRead(apu, adr);
        if (clr)
            v = static_cast<uint8_t>(v & ~static_cast<uint8_t>(1u << bit));
        else
            v = static_cast<uint8_t>(v | static_cast<uint8_t>(1u << bit));
        ramWrite(apu, adr, v);
        return 4;
    }

    switch (opc) {
        case 0x00: // NOP
            return 2;

        case 0xBA: // MOVW YA, dp
        {
            const uint16_t base = readDp(fetchPc(apu));
            m_a                 = ramRead(apu, base);
            m_y                 = ramRead(apu,
                            static_cast<uint16_t>((base & 0xFF00u) | ((base + 1u) & 0xFFu)));
            const uint16_t w = static_cast<uint16_t>(static_cast<unsigned>(m_y) << 8 | m_a);
            setZ(w == 0);
            setN((m_y & 0x80) != 0);
            return 5;
        }

        case 0xDA: // MOVW dp, YA
        {
            const uint16_t base = readDp(fetchPc(apu));
            ramWrite(apu, base, m_a);
            ramWrite(apu,
                     static_cast<uint16_t>((base & 0xFF00u) | ((base + 1u) & 0xFFu)),
                     m_y);
            return 5;
        }

        case 0xC4: // MOV dp, A
            ramWrite(apu, readDp(fetchPc(apu)), m_a);
            return 4;

        case 0xCB: // MOV dp, Y
            ramWrite(apu, readDp(fetchPc(apu)), m_y);
            return 4;

        case 0xE4: // MOV A, dp
            m_a = ramRead(apu, readDp(fetchPc(apu)));
            setNZFromByte(m_a);
            return 3;

        case 0xEB: // MOV Y, dp
            m_y = ramRead(apu, readDp(fetchPc(apu)));
            setNZFromByte(m_y);
            return 3;

        case 0xFB: // MOV Y, dp+X
            m_y =
                ramRead(apu,
                        static_cast<uint16_t>((readDp(fetchPc(apu)) + m_x) & 0xFFFFu));
            setNZFromByte(m_y);
            return 4;

        case 0xCC: // MOV !abs, Y
        {
            const uint8_t  lo = fetchPc(apu);
            const uint8_t  hi = fetchPc(apu);
            const uint16_t a  =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            ramWrite(apu, a, m_y);
            return 5;
        }

        case 0xC5: // MOV !abs, A
        {
            const uint8_t  lo = fetchPc(apu);
            const uint8_t  hi = fetchPc(apu);
            const uint16_t a  =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            ramWrite(apu, a, m_a);
            return 5;
        }

        case 0xE5: // MOV A, !abs
        {
            const uint8_t  lo = fetchPc(apu);
            const uint8_t  hi = fetchPc(apu);
            const uint16_t a  =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            m_a               = ramRead(apu, a);
            setNZFromByte(m_a);
            return 4;
        }

        case 0xD6: // MOV !abs+Y, A
        {
            const uint8_t lo = fetchPc(apu);
            const uint8_t hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>((static_cast<unsigned>(lo) |
                                       (static_cast<unsigned>(hi) << 8)) +
                                      m_y);
            ramWrite(apu, a, m_a);
            return 6;
        }

        case 0xF5: // MOV A, !abs+X
        {
            const uint8_t  lo = fetchPc(apu);
            const uint8_t  hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>((static_cast<unsigned>(lo) |
                                       (static_cast<unsigned>(hi) << 8)) +
                                      m_x);
            m_a               = ramRead(apu, a);
            setNZFromByte(m_a);
            return 5;
        }

        case 0xF6: // MOV A, !abs+Y
        {
            const uint8_t lo = fetchPc(apu);
            const uint8_t hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>((static_cast<unsigned>(lo) |
                                       (static_cast<unsigned>(hi) << 8)) +
                                      m_y);
            m_a              = ramRead(apu, a);
            setNZFromByte(m_a);
            return 5;
        }

        case 0xF7: // MOV A, [dp]+Y
        {
            const uint8_t  zp  = fetchPc(apu);
            const uint16_t ptr = static_cast<uint16_t>(read16Zp(apu, zp) + m_y);
            m_a                = ramRead(apu, ptr);
            setNZFromByte(m_a);
            return 6;
        }

        case 0xE6: // MOV A, (X)
            m_a = ramRead(apu, readDp(m_x));
            setNZFromByte(m_a);
            return 3;

        case 0xE7: // MOV A, [dp+X]
        {
            const uint8_t  zpBase = fetchPc(apu);
            const uint16_t wp     = read16Zp(apu,
                                              static_cast<uint8_t>(
                                                  static_cast<unsigned>(zpBase) +
                                                  static_cast<unsigned>(m_x)));
            m_a                   = ramRead(apu, wp);
            setNZFromByte(m_a);
            return 6;
        }

        case 0xF4: // MOV A, dp+X
            m_a = ramRead(apu,
                          readDp(static_cast<uint8_t>(static_cast<unsigned>(fetchPc(apu)) +
                                                      static_cast<unsigned>(m_x))));
            setNZFromByte(m_a);
            return 4;

        case 0xF8: // MOV X, dp
            m_x = ramRead(apu, readDp(fetchPc(apu)));
            setNZFromByte(m_x);
            return 3;

        case 0xE9: // MOV X, !abs
        {
            const uint8_t  lo = fetchPc(apu);
            const uint8_t  hi = fetchPc(apu);
            const uint16_t a  =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            m_x               = ramRead(apu, a);
            setNZFromByte(m_x);
            return 4;
        }

        case 0xEC: // MOV Y, !abs
        {
            const uint8_t  lo = fetchPc(apu);
            const uint8_t  hi = fetchPc(apu);
            const uint16_t a  =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            m_y               = ramRead(apu, a);
            setNZFromByte(m_y);
            return 4;
        }

        case 0x9D: // MOV X, SP
            m_x = m_sp;
            setNZFromByte(m_x);
            return 2;

        case 0xD8: // MOV dp, X
            ramWrite(apu, readDp(fetchPc(apu)), m_x);
            return 4;

        case 0xD9: // MOV dp+Y, X
            ramWrite(
                apu,
                readDp(static_cast<uint8_t>(static_cast<unsigned>(fetchPc(apu)) +
                                           static_cast<unsigned>(m_y))),
                m_x);
            return 5;

        case 0xDB: // MOV dp+X, Y
            ramWrite(
                apu,
                readDp(static_cast<uint8_t>(static_cast<unsigned>(fetchPc(apu)) +
                                          static_cast<unsigned>(m_x))),
                m_y);
            return 5;

        case 0xC7: { // MOV [dp+X], A
            const uint8_t  zp  = fetchPc(apu);
            const uint16_t ptr = read16Zp(
                apu, static_cast<uint8_t>(static_cast<unsigned>(zp) + static_cast<unsigned>(m_x)));
            ramWrite(apu, ptr, m_a);
            return 7;
        }

        case 0xD4: // MOV dp+X, A
            ramWrite(
                apu,
                readDp(static_cast<uint8_t>(static_cast<unsigned>(fetchPc(apu)) +
                                         static_cast<unsigned>(m_x))),
                m_a);
            return 5;

        case 0xD5: // MOV !abs+X, A
        {
            const uint8_t  lo = fetchPc(apu);
            const uint8_t  hi = fetchPc(apu);
            const uint16_t a  =
                static_cast<uint16_t>((static_cast<unsigned>(lo) |
                                       (static_cast<unsigned>(hi) << 8)) +
                                      m_x);
            ramWrite(apu, a, m_a);
            return 6;
        }

        case 0xAF: // MOV (X)+, A
            ramWrite(apu, readDp(m_x++), m_a);
            return 4;

        case 0xC6: // MOV (X), A
            ramWrite(apu, readDp(m_x), m_a);
            return 4;

        case 0xBD: // MOV SP, X
            m_sp = m_x;
            return 2;

        case 0xDD: // MOV A, Y
            m_a = m_y;
            setNZFromByte(m_a);
            return 2;

        case 0x5D: // MOV X, A
            m_x = m_a;
            setNZFromByte(m_x);
            return 2;

        case 0x7D: // MOV A, X
            m_a = m_x;
            setNZFromByte(m_a);
            return 2;

        case 0x8D: // MOV Y, #imm
            m_y = fetchPc(apu);
            setNZFromByte(m_y);
            return 2;

        case 0xFD: // MOV Y, A
            m_y = m_a;
            setNZFromByte(m_y);
            return 2;

        case 0xE8: // MOV A, #imm
            m_a = fetchPc(apu);
            setNZFromByte(m_a);
            return 2;

        case 0xCD: // MOV X, #imm
            m_x = fetchPc(apu);
            setNZFromByte(m_x);
            return 2;

        case 0xFC: // INC Y
            ++m_y;
            setNZFromByte(m_y);
            return 2;

        case 0xBC: // INC A
            ++m_a;
            setNZFromByte(m_a);
            return 2;

        case 0x9C: // DEC A
            --m_a;
            setNZFromByte(m_a);
            return 2;

        case 0xDC: // DEC Y
            --m_y;
            setNZFromByte(m_y);
            return 2;

        case 0xAB: // INC dp
        {
            const uint16_t a = readDp(fetchPc(apu));
            uint8_t        v = ramRead(apu, a) + 1;
            ramWrite(apu, a, v);
            setNZFromByte(v);
            return 4;
        }

        case 0x1D: // DEC X
            --m_x;
            setNZFromByte(m_x);
            return 2;

        case 0xBF: // MOV A, (X)+
            m_a = ramRead(apu, readDp(m_x++));
            setNZFromByte(m_a);
            return 4;

        case 0x1F: { // JMP [!abs+X]
            const uint8_t  lo  = fetchPc(apu);
            const uint8_t  hi  = fetchPc(apu);
            const uint16_t ptr = static_cast<uint16_t>(
                static_cast<unsigned>(lo) | (static_cast<unsigned>(hi) << 8));
            const uint16_t adr = static_cast<uint16_t>(ptr + m_x);
            const uint8_t  pcl = ramRead(apu, adr);
            const uint8_t  pch =
                ramRead(apu,
                        static_cast<uint16_t>((adr & 0xFF00u) | ((adr + 1u) & 0xFFu)));
            m_pc = static_cast<uint16_t>(static_cast<unsigned>(pcl) |
                                          (static_cast<unsigned>(pch) << 8));
            return 6;
        }

        case 0xD7: { // MOV [dp]+Y, A
            const uint8_t  zp  = fetchPc(apu);
            const uint16_t ptr = static_cast<uint16_t>(read16Zp(apu, zp) + m_y);
            ramWrite(apu, ptr, m_a);
            return 7;
        }

        case 0x78: // CMP dp, #imm (byte order: imm, dp per IPL assembler)
        {
            const uint8_t imm = fetchPc(apu);
            const uint8_t d   = fetchPc(apu);
            cmp8(ramRead(apu, readDp(d)), imm);
            return 5;
        }

        case 0x68: // CMP A, #imm
            cmp8(m_a, fetchPc(apu));
            return 2;

        case 0x69: // CMP dd, ds — (dd)-(ds); machine order: source ds first, destination dd second
        {
            const uint8_t srcDp = fetchPc(apu);
            const uint8_t dstDp = fetchPc(apu);
            cmp8(ramRead(apu, readDp(dstDp)),
                 ramRead(apu, readDp(srcDp)));
            return 6;
        }

        case 0x3E: // CMP X, dp
            cmp8(m_x, ramRead(apu, readDp(fetchPc(apu))));
            return 3;

        case 0xC8: // CMP X, #imm
            cmp8(m_x, fetchPc(apu));
            return 2;

        case 0xAD: // CMP Y, #imm
            cmp8(m_y, fetchPc(apu));
            return 2;

        case 0x7E: // CMP Y, dp
            cmp8(m_y, ramRead(apu, readDp(fetchPc(apu))));
            return 3;

        case 0x64: // CMP A, dp (not IPL; useful)
            cmp8(m_a, ramRead(apu, readDp(fetchPc(apu))));
            return 3;

        case 0x77: { // CMP A, [dp]+Y
            const uint8_t  zp  = fetchPc(apu);
            const uint16_t ptr = static_cast<uint16_t>(read16Zp(apu, zp) + m_y);
            cmp8(m_a, ramRead(apu, ptr));
            return 6;
        }

        case 0xD0: { // BNE rel
            int8_t rel = static_cast<int8_t>(fetchPc(apu));
            if (!flagZ())
                m_pc = spcPcPlusRel(m_pc, rel);
            return flagZ() ? 2 : 4;
        }

        case 0xF0: { // BEQ rel
            int8_t rel = static_cast<int8_t>(fetchPc(apu));
            if (flagZ())
                m_pc = spcPcPlusRel(m_pc, rel);
            return flagZ() ? 4 : 2;
        }

        case 0x10: { // BPL rel
            int8_t rel = static_cast<int8_t>(fetchPc(apu));
            if (!flagN())
                m_pc = spcPcPlusRel(m_pc, rel);
            return flagN() ? 2 : 4;
        }

        case 0x30: { // BMI rel
            int8_t rel = static_cast<int8_t>(fetchPc(apu));
            if (flagN())
                m_pc = spcPcPlusRel(m_pc, rel);
            return flagN() ? 4 : 2;
        }

        case 0x2F: // BRA rel
        {
            int8_t rel = static_cast<int8_t>(fetchPc(apu));
            m_pc       = spcPcPlusRel(m_pc, rel);
            return 4;
        }

        case 0x50: { // BVC rel
            int8_t rel = static_cast<int8_t>(fetchPc(apu));
            if (!flagV())
                m_pc = spcPcPlusRel(m_pc, rel);
            return flagV() ? 2 : 4;
        }

        case 0x70: { // BVS rel
            int8_t rel = static_cast<int8_t>(fetchPc(apu));
            if (flagV())
                m_pc = spcPcPlusRel(m_pc, rel);
            return flagV() ? 4 : 2;
        }

        case 0x90: { // BCC rel
            int8_t rel = static_cast<int8_t>(fetchPc(apu));
            if (!flagC())
                m_pc = spcPcPlusRel(m_pc, rel);
            return flagC() ? 2 : 4;
        }

        case 0xB0: { // BCS rel
            int8_t rel = static_cast<int8_t>(fetchPc(apu));
            if (flagC())
                m_pc = spcPcPlusRel(m_pc, rel);
            return flagC() ? 4 : 2;
        }

        case 0x4F: { // PCALL upage → PC = ($FF00|u); push return
            const uint8_t u = fetchPc(apu);
            stackPush(apu, static_cast<uint8_t>(m_pc >> 8));
            stackPush(apu, static_cast<uint8_t>(m_pc & 0xFF));
            m_pc = static_cast<uint16_t>(0xFF00u | static_cast<unsigned>(u));
            return 6;
        }

        case 0x3F: { // CALL !abs — push PC; jump
            const uint8_t  lo  = fetchPc(apu);
            const uint8_t  hi  = fetchPc(apu);
            const uint16_t dst =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            stackPush(apu, static_cast<uint8_t>(m_pc >> 8));
            stackPush(apu, static_cast<uint8_t>(m_pc & 0xFF));
            m_pc = dst;
            return 8;
        }

        case 0x5F: { // JMP !abs
            const uint8_t lo = fetchPc(apu);
            const uint8_t hi = fetchPc(apu);
            m_pc =
                static_cast<uint16_t>(static_cast<unsigned>(lo) | (static_cast<unsigned>(hi) << 8));
            return 3;
        }

        case 0x6F: // RET — PCL,PCH from stack (top = PCL)
        {
            const uint8_t pcl = stackPop(apu);
            const uint8_t pch = stackPop(apu);
            m_pc = static_cast<uint16_t>(static_cast<unsigned>(pcl) |
                                          (static_cast<unsigned>(pch) << 8));
            return 5;
        }

        case 0x0D: // PUSH PSW
            stackPush(apu, m_psw);
            return 4;

        case 0x2D: // PUSH A
            stackPush(apu, m_a);
            return 4;

        case 0x4D: // PUSH X
            stackPush(apu, m_x);
            return 4;

        case 0x6D: // PUSH Y
            stackPush(apu, m_y);
            return 4;

        case 0xAE: // POP A
            m_a = stackPop(apu);
            setNZFromByte(m_a);
            return 4;

        case 0xCE: // POP X
            m_x = stackPop(apu);
            setNZFromByte(m_x);
            return 4;

        case 0xEE: // POP Y
            m_y = stackPop(apu);
            setNZFromByte(m_y);
            return 4;

        case 0x8E: // POP PSW
            m_psw = stackPop(apu);
            return 4;

        case 0x60: // CLRC
            m_psw &= static_cast<uint8_t>(~0x01u);
            return 2;

        case 0x80: // SETC
            m_psw |= 0x01;
            return 2;

        case 0x8F: // MOV dp, #imm
        {
            const uint8_t v = fetchPc(apu);
            const uint8_t d = fetchPc(apu);
            ramWrite(apu, readDp(d), v);
            return 5;
        }

        case 0xFA: // MOV dd, ds — machine order: opcode, source ds, destination dd (spcasm manual)
        {
            const uint8_t srcDp = fetchPc(apu);
            const uint8_t dstDp = fetchPc(apu);
            ramWrite(apu,
                     readDp(dstDp),
                     ramRead(apu, readDp(srcDp)));
            return 5;
        }

        case 0xC0: // DI (clear I bit in PSW)
            m_psw &= static_cast<uint8_t>(~0x04u);
            return 3;

        case 0x20: // CLRP — direct page in page 0
            m_psw &= static_cast<uint8_t>(~0x20u);
            return 2;

        case 0x40: // SETP — direct page in page 1 ($01xx)
            m_psw |= 0x20;
            return 2;

        case 0x08: // OR A, #imm
            m_a |= fetchPc(apu);
            setNZFromByte(m_a);
            return 2;

        case 0x06: // OR A, (X)
            m_a |= ramRead(apu, readDp(m_x));
            setNZFromByte(m_a);
            return 3;

        case 0x04: // OR A, dp
            m_a |= ramRead(apu, readDp(fetchPc(apu)));
            setNZFromByte(m_a);
            return 3;

        case 0x05: // OR A, !abs
        {
            const uint8_t  lo = fetchPc(apu);
            const uint8_t  hi = fetchPc(apu);
            const uint16_t a  =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            m_a |= ramRead(apu, a);
            setNZFromByte(m_a);
            return 4;
        }

        case 0x18: // OR dp, #imm
        {
            const uint8_t  imm = fetchPc(apu);
            const uint16_t d   = readDp(fetchPc(apu));
            uint8_t        v   = ramRead(apu, d);
            v                  = static_cast<uint8_t>(v | imm);
            ramWrite(apu, d, v);
            setNZFromByte(v);
            return 5;
        }

        case 0x28: // AND A, #imm
            m_a &= fetchPc(apu);
            setNZFromByte(m_a);
            return 2;

        case 0x48: // EOR A, #imm
            m_a ^= fetchPc(apu);
            setNZFromByte(m_a);
            return 2;

        case 0x38: // AND dp, #imm
        {
            const uint8_t  imm = fetchPc(apu);
            const uint16_t d   = readDp(fetchPc(apu));
            uint8_t        v   = ramRead(apu, d);
            v                  = static_cast<uint8_t>(v & imm);
            ramWrite(apu, d, v);
            setNZFromByte(v);
            return 5;
        }

        case 0x1C: { // ASL A
            setC((m_a & 0x80u) != 0);
            m_a = static_cast<uint8_t>(static_cast<unsigned>(m_a) << 1);
            setNZFromByte(m_a);
            return 2;
        }

        case 0x5C: { // LSR A
            setC((m_a & 1u) != 0);
            m_a = static_cast<uint8_t>(static_cast<unsigned>(m_a) >> 1);
            setNZFromByte(m_a);
            return 2;
        }

        case 0x9F: // XCN A — swap nibbles (common in SNESmod DSP setup)
            m_a = static_cast<uint8_t>((m_a >> 4) | (m_a << 4));
            setNZFromByte(m_a);
            return 5;

        case 0xCF: { // MUL YA — unsigned Y×A → YA
            const uint16_t p = static_cast<uint16_t>(uint32_t{m_a} * uint32_t{m_y});
            m_a = static_cast<uint8_t>(p & 0xFFu);
            m_y = static_cast<uint8_t>(p >> 8);
            setZ(p == 0);
            setN((p & 0x8000u) != 0);
            return 9;
        }

        case 0x9E: { // DIV YA, X — 16-bit YA / X → quotient A, remainder Y
            const uint16_t ya =
                static_cast<uint16_t>(static_cast<unsigned>(m_a) | (static_cast<unsigned>(m_y) << 8));
            if (m_x == 0) {
                m_psw |= 0x40; // V (divide by 0)
                m_a = 0xFF;
                m_y = 0xFF;
                setNZFromByte(m_a);
                return 12;
            }
            const unsigned div = static_cast<unsigned>(m_x);
            const unsigned q   = static_cast<unsigned>(ya) / div;
            const unsigned r   = static_cast<unsigned>(ya) % div;
            if (q > 0xFFu)
                m_psw |= 0x40;
            else
                m_psw &= static_cast<uint8_t>(~0x40u);
            m_a = static_cast<uint8_t>(q & 0xFFu);
            m_y = static_cast<uint8_t>(r & 0xFFu);
            setNZFromByte(m_a);
            return 12;
        }

        case 0x88: // ADC A, #imm
            adcA(fetchPc(apu));
            return 2;

        case 0xA8: // SBC A, #imm
            sbcA(fetchPc(apu));
            return 2;

        case 0x84: // ADC A, dp
            adcA(ramRead(apu, readDp(fetchPc(apu))));
            return 3;

        case 0x86: // ADC A, (X)
            adcA(ramRead(apu, readDp(m_x)));
            return 3;

        case 0x98: { // ADC dp, #imm — (d) = (d) + imm + C
            const unsigned imm = fetchPc(apu);
            const uint16_t a   = readDp(fetchPc(apu));
            const unsigned m   = ramRead(apu, a);
            const unsigned c   = flagC() ? 1u : 0u;
            const unsigned sum = m + imm + c;
            setH(((m & 0xFu) + (imm & 0xFu) + c) >= 0x10u);
            const uint8_t r = static_cast<uint8_t>(sum & 0xFFu);
            ramWrite(apu, a, r);
            setNZFromByte(r);
            setC(sum > 0xFFu);
            const bool ov = (((m ^ imm) & 0x80u) == 0) &&
                            (((m ^ static_cast<unsigned>(r)) & 0x80u) != 0);
            if (ov)
                m_psw |= 0x40;
            else
                m_psw &= static_cast<uint8_t>(~0x40u);
            return 5;
        }

        case 0x89: { // ADC dd, ds — (dd)=(dd)+(ds)+C; encoding: source ds, destination dd
            const uint8_t  srcDp = fetchPc(apu);
            const uint8_t  dstDp = fetchPc(apu);
            const uint16_t dAdr  = readDp(dstDp);
            const uint16_t sAdr  = readDp(srcDp);
            const unsigned lhs   = ramRead(apu, dAdr);
            const unsigned rhs   = ramRead(apu, sAdr);
            const unsigned c     = flagC() ? 1u : 0u;
            const unsigned sum   = lhs + rhs + c;
            setH(((lhs & 0xFu) + (rhs & 0xFu) + c) >= 0x10u);
            const uint8_t r = static_cast<uint8_t>(sum & 0xFFu);
            ramWrite(apu, dAdr, r);
            setNZFromByte(r);
            setC(sum > 0xFFu);
            const bool ov = (((lhs ^ rhs) & 0x80u) == 0) &&
                            (((lhs ^ static_cast<unsigned>(r)) & 0x80u) != 0);
            if (ov)
                m_psw |= 0x40;
            else
                m_psw &= static_cast<uint8_t>(~0x40u);
            return 6;
        }

        case 0xA4: // SBC A, dp
            sbcA(ramRead(apu, readDp(fetchPc(apu))));
            return 3;

        case 0xB5: { // SBC A, !abs+X
            const uint8_t  lo = fetchPc(apu);
            const uint8_t  hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>((static_cast<unsigned>(lo) |
                                       (static_cast<unsigned>(hi) << 8)) +
                                      m_x);
            sbcA(ramRead(apu, a));
            return 5;
        }

        case 0xB8: { // SBC dp, #imm — (d) = (d) - imm - !C
            const unsigned imm    = fetchPc(apu);
            const uint16_t a      = readDp(fetchPc(apu));
            const unsigned m      = ramRead(apu, a);
            const unsigned borrow = flagC() ? 0u : 1u;
            const uint16_t w      = static_cast<uint16_t>(
                static_cast<uint16_t>(m) - static_cast<uint16_t>(imm) -
                static_cast<uint16_t>(borrow));
            const uint8_t  r = static_cast<uint8_t>(w & 0xFFu);
            ramWrite(apu, a, r);
            setNZFromByte(r);
            setC((w & 0x100u) == 0);
            setH(false);
            const bool ov = (((m ^ imm) & 0x80u) != 0) &&
                            (((m ^ static_cast<unsigned>(r)) & 0x80u) != 0);
            if (ov)
                m_psw |= 0x40;
            else
                m_psw &= static_cast<uint8_t>(~0x40u);
            return 5;
        }

        case 0x7A: { // ADDW YA, dp
            const uint8_t    d   = fetchPc(apu);
            const uint16_t   w   = read16Zp(apu, d);
            const uint32_t   ya  = static_cast<uint32_t>(m_a) | (static_cast<uint32_t>(m_y) << 8);
            const uint32_t   res = ya + static_cast<uint32_t>(w);
            m_a = static_cast<uint8_t>(res & 0xFFu);
            m_y = static_cast<uint8_t>((res >> 8) & 0xFFu);
            setZ((res & 0xFFFFu) == 0);
            setN((res & 0x8000u) != 0);
            setC(res > 0xFFFFu);
            return 5;
        }

        case 0x9A: { // SUBW YA, dp
            const uint8_t    d   = fetchPc(apu);
            const uint16_t   w   = read16Zp(apu, d);
            const uint16_t   ya  = static_cast<uint16_t>(
                static_cast<unsigned>(m_a) | (static_cast<unsigned>(m_y) << 8));
            const uint16_t   res = static_cast<uint16_t>(static_cast<uint32_t>(ya) - static_cast<uint32_t>(w));
            m_a = static_cast<uint8_t>(res & 0xFFu);
            m_y = static_cast<uint8_t>(res >> 8);
            setZ(res == 0);
            setN((res & 0x8000u) != 0);
            setC(ya >= w);
            return 5;
        }

        case 0x5A: { // CMPW YA, dp
            const uint16_t w  = read16Zp(apu, fetchPc(apu));
            const uint16_t ya = static_cast<uint16_t>(
                static_cast<unsigned>(m_a) | (static_cast<unsigned>(m_y) << 8));
            const uint16_t diff = static_cast<uint16_t>(
                static_cast<uint32_t>(ya) - static_cast<uint32_t>(w));
            setZ(diff == 0);
            setN((diff & 0x8000u) != 0);
            setC(static_cast<uint32_t>(ya) >= static_cast<uint32_t>(w));
            return 4;
        }

        case 0x3A: { // INCW dp
            const uint8_t d = fetchPc(apu);
            uint16_t      w = read16Zp(apu, d);
            ++w;
            write16Zp(apu, d, w);
            setZ(w == 0);
            setN((w & 0x8000u) != 0);
            return 6;
        }

        case 0x1A: { // DECW dp
            const uint8_t d = fetchPc(apu);
            uint16_t      w = read16Zp(apu, d);
            --w;
            write16Zp(apu, d, w);
            setZ(w == 0);
            setN((w & 0x8000u) != 0);
            return 6;
        }

        case 0x0F: { // BRK — push PC, PSW; PC ← [$FFDE]
            stackPush(apu, static_cast<uint8_t>((m_pc >> 8) & 0xFFu));
            stackPush(apu, static_cast<uint8_t>(m_pc & 0xFFu));
            stackPush(apu, static_cast<uint8_t>((m_psw | 0x10u) & 0xFFu));
            m_pc = read16Mem(apu, 0xFFDEu);
            return 8;
        }

        case 0x7F: { // RET1 — POP PSW, then PC (same stack order as RET)
            m_psw = stackPop(apu);
            const uint8_t pcl = stackPop(apu);
            const uint8_t pch = stackPop(apu);
            m_pc =
                static_cast<uint16_t>(static_cast<unsigned>(pcl) |
                                      (static_cast<unsigned>(pch) << 8));
            return 6;
        }

        case 0xE0: // CLRV
            setV(false);
            setH(false);
            return 2;

        case 0xED: // NOTC
            setC(!flagC());
            return 3;

        case 0xA0: // EI
            m_psw |= 0x04u;
            return 3;

        case 0xBE: { // DAS A (MAME SPC700 order)
            const uint8_t src = m_a;
            if ((!flagH()) || ((src & 0xFu) > 9u))
                m_a = static_cast<uint8_t>(m_a - 6);
            if ((!flagC()) || src > 0x99u) {
                m_a = static_cast<uint8_t>(m_a - 0x60);
                setC(false);
            }
            setNZFromByte(m_a);
            return 3;
        }

        case 0xDF: { // DAA A (MAME SPC700 order)
            const uint8_t src = m_a;
            if (((src & 0x0Fu) > 9u) || flagH()) {
                m_a = static_cast<uint8_t>(m_a + 6);
                if (m_a < 6)
                    setC(true);
            }
            if ((src > 0x99u) || flagC()) {
                m_a = static_cast<uint8_t>(m_a + 0x60);
                setC(true);
            }
            setNZFromByte(m_a);
            return 3;
        }

        case 0xAA: { // MOV1 C, m.b
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const unsigned w =
                static_cast<unsigned>(lo) | (static_cast<unsigned>(hi) << 8);
            const unsigned bi = (w >> 13) & 7u;
            const uint16_t a      = static_cast<uint16_t>(w & 0x1FFFu);
            const bool       bit  = (((ramRead(apu, a) >> bi)) & 1u) != 0;
            setC(bit);
            return 4;
        }

        case 0xCA: { // MOV1 m.b, C
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const unsigned w =
                static_cast<unsigned>(lo) | (static_cast<unsigned>(hi) << 8);
            const unsigned bi = (w >> 13) & 7u;
            const uint16_t a = static_cast<uint16_t>(w & 0x1FFFu);
            uint8_t        v = ramRead(apu, a);
            if (flagC())
                v |= static_cast<uint8_t>(1u << bi);
            else
                v = static_cast<uint8_t>(
                    v & static_cast<uint8_t>(~(1u << bi)));
            ramWrite(apu, a, v);
            return 6;
        }

        case 0xEA: { // NOT1 m.b
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const unsigned w =
                static_cast<unsigned>(lo) | (static_cast<unsigned>(hi) << 8);
            const unsigned bi = (w >> 13) & 7u;
            const uint16_t a =
                static_cast<uint16_t>(w & 0x1FFFu);
            uint8_t        v = ramRead(apu, a);
            v ^= static_cast<uint8_t>(1u << bi);
            ramWrite(apu, a, v);
            return 5;
        }

        case 0x4A: { // AND1 C, m.b
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const unsigned w =
                static_cast<unsigned>(lo) | (static_cast<unsigned>(hi) << 8);
            const unsigned bi = (w >> 13) & 7u;
            const uint16_t a =
                static_cast<uint16_t>(w & 0x1FFFu);
            const unsigned bit =
                (static_cast<unsigned>(ramRead(apu, a)) >> bi) & 1u;
            setC(flagC() && (bit != 0));
            return 4;
        }

        case 0x6A: { // AND1 C, /m.b
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const unsigned w =
                static_cast<unsigned>(lo) | (static_cast<unsigned>(hi) << 8);
            const unsigned bi = (w >> 13) & 7u;
            const uint16_t a =
                static_cast<uint16_t>(w & 0x1FFFu);
            const unsigned bit =
                (static_cast<unsigned>(ramRead(apu, a)) >> bi) & 1u;
            setC(flagC() && (bit == 0));
            return 4;
        }

        case 0x0A: { // OR1 C, m.b
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const unsigned w =
                static_cast<unsigned>(lo) | (static_cast<unsigned>(hi) << 8);
            const unsigned bi = (w >> 13) & 7u;
            const uint16_t a =
                static_cast<uint16_t>(w & 0x1FFFu);
            const unsigned bit =
                (static_cast<unsigned>(ramRead(apu, a)) >> bi) & 1u;
            setC(flagC() || (bit != 0));
            return 5;
        }

        case 0x2A: { // OR1 C, /m.b
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const unsigned w =
                static_cast<unsigned>(lo) | (static_cast<unsigned>(hi) << 8);
            const unsigned bi = (w >> 13) & 7u;
            const uint16_t a =
                static_cast<uint16_t>(w & 0x1FFFu);
            const unsigned bit =
                (static_cast<unsigned>(ramRead(apu, a)) >> bi) & 1u;
            setC(flagC() || (bit == 0));
            return 5;
        }

        case 0x8A: { // EOR1 C, m.b
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const unsigned w =
                static_cast<unsigned>(lo) | (static_cast<unsigned>(hi) << 8);
            const unsigned bi = (w >> 13) & 7u;
            const uint16_t a =
                static_cast<uint16_t>(w & 0x1FFFu);
            const bool bit =
                (((ramRead(apu, a)) >> bi) & 1u) != 0;
            setC(flagC() != bit);
            return 5;
        }

        case 0x0E: { // TSET1 !abs
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            const uint8_t m = ramRead(apu, a);
            cmp8(m_a, m);
            ramWrite(apu, a, static_cast<uint8_t>(m | m_a));
            return 6;
        }

        case 0x4E: { // TCLR1 !abs
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            const uint8_t m = ramRead(apu, a);
            cmp8(m_a, m);
            ramWrite(apu,
                     a,
                     static_cast<uint8_t>(m &
                                          static_cast<uint8_t>(~m_a)));
            return 6;
        }

        case 0x2E: { // CBNE d,rel — no flag update
            uint16_t        adr =
                readDp(fetchPc(apu));
            const uint8_t   mv = ramRead(apu, adr);
            const int8_t rel = static_cast<int8_t>(fetchPc(apu));
            if (m_a != mv) {
                m_pc = spcPcPlusRel(m_pc, rel);
                return 7;
            }
            return 5;
        }

        case 0xDE: { // CBNE d+X,rel
            const uint16_t adr =
                zpAddrPlusX(fetchPc(apu));
            const uint8_t  mv =
                ramRead(apu, adr);
            const int8_t rel = static_cast<int8_t>(fetchPc(apu));
            if (m_a != mv) {
                m_pc = spcPcPlusRel(m_pc, rel);
                return 8;
            }
            return 6;
        }

        case 0x6E: { // DBNZ d,rel — branches on non‑zero destination
            uint16_t adr = readDp(fetchPc(apu));
            uint8_t  v   =
                ramRead(apu, adr);
            --v;
            ramWrite(apu, adr, v);
            const int8_t rel = static_cast<int8_t>(fetchPc(apu));
            if (v != 0) {
                m_pc = spcPcPlusRel(m_pc, rel);
                return 7;
            }
            return 5;
        }

        case 0x3D: // INC X
            ++m_x;
            setNZFromByte(m_x);
            return 2;

        case 0x79: // CMP (X),(Y)
            cmp8(ramRead(apu, readDp(m_x)),
                 ramRead(apu, readDp(m_y)));
            return 5;

        case 0x66: // CMP A, (X)
            cmp8(m_a, ramRead(apu, readDp(m_x)));
            return 3;

        case 0x67: { // CMP A, [d+X]
            const uint8_t zp =
                fetchPc(apu);
            uint16_t      ptr =
                read16Zp(apu,
                         static_cast<uint8_t>(
                             static_cast<unsigned>(zp) +
                             static_cast<unsigned>(m_x)));
            cmp8(m_a, ramRead(apu, ptr));
            return 6;
        }

        case 0x65: { // CMP A, !abs
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            cmp8(m_a, ramRead(apu, a));
            return 4;
        }

        case 0x74: // CMP A, d+X
            cmp8(m_a, ramRead(apu, zpAddrPlusX(fetchPc(apu))));
            return 4;

        case 0x75: { // CMP A, !abs+X
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            uint16_t      a =
                static_cast<uint16_t>((static_cast<unsigned>(lo) |
                                       (static_cast<unsigned>(hi) << 8)) +
                                      m_x);
            cmp8(m_a, ramRead(apu, a));
            return 5;
        }

        case 0x76: { // CMP A, !abs+Y
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            uint16_t      a =
                static_cast<uint16_t>((static_cast<unsigned>(lo) |
                                       (static_cast<unsigned>(hi) << 8)) +
                                      m_y);
            cmp8(m_a, ramRead(apu, a));
            return 5;
        }

        case 0x1E: { // CMP X, !abs
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            cmp8(m_x, ramRead(apu, a));
            return 4;
        }

        case 0x5E: { // CMP Y, !abs
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            cmp8(m_y, ramRead(apu, a));
            return 4;
        }

        case 0xF9: // MOV X, d+Y
            m_x = ramRead(apu, zpAddrPlusY(fetchPc(apu)));
            setNZFromByte(m_x);
            return 4;

        case 0xC9: { // MOV !abs, X
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            ramWrite(apu, a, m_x);
            return 5;
        }

        case 0x19: { // OR (X),(Y)
            const uint16_t xa =
                readDp(m_x),
                ya = readDp(m_y);
            const uint8_t  v =
                static_cast<uint8_t>(ramRead(apu, xa) | ramRead(apu, ya));
            ramWrite(apu, xa, v);
            setNZFromByte(v);
            return 5;
        }

        case 0x39: { // AND (X),(Y)
            const uint16_t xa =
                readDp(m_x),
                ya = readDp(m_y);
            uint8_t        v =
                ramRead(apu, xa);
            v &= ramRead(apu, ya);
            ramWrite(apu, xa, v);
            setNZFromByte(v);
            return 5;
        }

        case 0x59: { // EOR (X),(Y)
            const uint16_t xa =
                readDp(m_x),
                ya = readDp(m_y);
            uint8_t        v =
                ramRead(apu, xa);
            v ^= ramRead(apu, ya);
            ramWrite(apu, xa, v);
            setNZFromByte(v);
            return 5;
        }

        case 0x99: { // ADC (X), (Y) — stores to address (X)
            const uint16_t xa =
                readDp(m_x),
                ya = readDp(m_y);
            const unsigned m = ramRead(apu, xa);
            const unsigned n = ramRead(apu, ya);
            const unsigned c = flagC() ? 1u : 0u;
            const unsigned sum =
                m + n + c;
            setH(((m & 0xFu) + (n & 0xFu) + c) >= 0x10u);
            const uint8_t r = static_cast<uint8_t>(sum & 0xFFu);
            ramWrite(apu, xa, r);
            setNZFromByte(r);
            setC(sum > 0xFFu);
            const bool ov = (((m ^ n) & 0x80u) == 0) &&
                            (((m ^ static_cast<unsigned>(r)) & 0x80u) != 0);
            if (ov)
                m_psw |= 0x40;
            else
                m_psw &= static_cast<uint8_t>(~0x40u);
            return 5;
        }

        case 0xB9: { // SBC (X), (Y)
            const uint16_t xa =
                readDp(m_x),
                ya = readDp(m_y);
            const unsigned lhs = ramRead(apu, xa);
            const unsigned rhs = ramRead(apu, ya);
            const unsigned borrow =
                flagC() ? 0u : 1u;
            const uint16_t w = static_cast<uint16_t>(
                static_cast<unsigned>(lhs) - static_cast<unsigned>(rhs) -
                borrow);
            const uint8_t  r =
                static_cast<uint8_t>(w & 0xFFu);
            ramWrite(apu, xa, r);
            setNZFromByte(r);
            setC((w & 0x100u) == 0);
            setH(false);
            const bool ov = (((lhs ^ rhs) & 0x80u) != 0) &&
                            (((lhs ^ static_cast<unsigned>(r)) & 0x80u) != 0);
            if (ov)
                m_psw |= 0x40;
            else
                m_psw &= static_cast<uint8_t>(~0x40u);
            return 5;
        }

        case 0x09: // OR dd, ds
        case 0x29: // AND dd, ds
        case 0x49: { // EOR dd, ds
            const uint8_t  srcDp = fetchPc(apu);
            const uint8_t  dstDp = fetchPc(apu);
            const uint16_t dAdr =
                readDp(dstDp),
                sAdr = readDp(srcDp);
            uint8_t dst = ramRead(apu, dAdr);
            const uint8_t src = ramRead(apu, sAdr);
            if (opc == 0x09u)
                dst |= src;
            else if (opc == 0x29u)
                dst &= src;
            else
                dst ^= src;
            ramWrite(apu, dAdr, dst);
            setNZFromByte(dst);
            return 6;
        }

        case 0xA9: { // SBC dd, ds
            const uint8_t  srcDp = fetchPc(apu);
            const uint8_t  dstDp = fetchPc(apu);
            const uint16_t dAdr =
                readDp(dstDp),
                sAdr = readDp(srcDp);
            const unsigned lhs =
                ramRead(apu, dAdr),
                rhs = ramRead(apu, sAdr);
            const unsigned bor =
                flagC() ? 0u : 1u;
            const uint16_t w = static_cast<uint16_t>(
                static_cast<unsigned>(lhs) - static_cast<unsigned>(rhs) - bor);
            const uint8_t  r =
                static_cast<uint8_t>(w & 0xFFu);
            ramWrite(apu, dAdr, r);
            setNZFromByte(r);
            setC((w & 0x100u) == 0);
            setH(false);
            const bool ov = (((lhs ^ rhs) & 0x80u) != 0) &&
                            (((lhs ^ static_cast<unsigned>(r)) & 0x80u) != 0);
            if (ov)
                m_psw |= 0x40;
            else
                m_psw &= static_cast<uint8_t>(~0x40u);
            return 6;
        }

        case 0x85: { // ADC A, !abs
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            adcA(ramRead(apu,
                          static_cast<uint16_t>(
                              static_cast<unsigned>(lo) |
                              (static_cast<unsigned>(hi) << 8))));
            return 4;
        }

        case 0x87: { // ADC A, [dp+X]
            const uint8_t zp =
                fetchPc(apu);
            adcA(ramRead(apu,
                          read16Zp(apu,
                                   static_cast<uint8_t>(
                                       static_cast<unsigned>(zp) +
                                       static_cast<unsigned>(m_x)))));
            return 6;
        }

        case 0x94: // ADC A, d+X
            adcA(ramRead(apu, zpAddrPlusX(fetchPc(apu))));
            return 4;

        case 0x95: { // ADC A, !abs+X
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>((static_cast<unsigned>(lo) |
                                       (static_cast<unsigned>(hi) << 8)) +
                                      m_x);
            adcA(ramRead(apu, a));
            return 5;
        }

        case 0x96: { // ADC A, !abs+Y
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>((static_cast<unsigned>(lo) |
                                       (static_cast<unsigned>(hi) << 8)) +
                                      m_y);
            adcA(ramRead(apu, a));
            return 5;
        }

        case 0x97: { // ADC A, [dp]+Y
            const uint8_t zp =
                fetchPc(apu);
            adcA(ramRead(apu,
                          static_cast<uint16_t>(
                              read16Zp(apu, zp) + m_y)));
            return 6;
        }

        case 0xA5: { // SBC A, !abs
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            sbcA(ramRead(apu,
                         static_cast<uint16_t>(
                             static_cast<unsigned>(lo) |
                             (static_cast<unsigned>(hi) << 8))));
            return 4;
        }

        case 0xA6: // SBC A, (X)
            sbcA(ramRead(apu, readDp(m_x)));
            return 3;

        case 0xA7: { // SBC A, [dp+X]
            const uint8_t zp =
                fetchPc(apu);
            sbcA(ramRead(apu,
                         read16Zp(apu,
                                  static_cast<uint8_t>(
                                      static_cast<unsigned>(zp) +
                                      static_cast<unsigned>(m_x)))));
            return 6;
        }

        case 0xB4: // SBC A, d+X
            sbcA(ramRead(apu, zpAddrPlusX(fetchPc(apu))));
            return 4;

        case 0xB6: { // SBC A, !abs+Y
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>((static_cast<unsigned>(lo) |
                                       (static_cast<unsigned>(hi) << 8)) +
                                      m_y);
            sbcA(ramRead(apu, a));
            return 5;
        }

        case 0xB7: { // SBC A, [dp]+Y
            const uint8_t zp = fetchPc(apu);
            sbcA(
                ramRead(apu,
                        static_cast<uint16_t>(
                            read16Zp(apu, zp) + m_y)));
            return 6;
        }

        case 0x07: // OR A, [dp+X]
            m_a |= ramRead(
                apu,
                read16Zp(apu,
                         static_cast<uint8_t>(
                             static_cast<unsigned>(fetchPc(apu)) +
                             static_cast<unsigned>(m_x))));
            setNZFromByte(m_a);
            return 6;

        case 0x17: // OR A, [dp]+Y
        {
            const uint8_t zp =
                fetchPc(apu);
            m_a |=
                ramRead(apu,
                        static_cast<uint16_t>(
                            read16Zp(apu, zp) +
                            m_y));
            setNZFromByte(m_a);
            return 6;
        }

        case 0x14: // OR A, d+X
            m_a |= ramRead(apu, zpAddrPlusX(fetchPc(apu)));
            setNZFromByte(m_a);
            return 4;

        case 0x15: { // OR A, !abs+X
            const uint8_t lo =
                fetchPc(apu),
                hi = fetchPc(apu);
            m_a |= ramRead(apu,
                            static_cast<uint16_t>((static_cast<unsigned>(
                                                       lo) |
                                                   (static_cast<unsigned>(hi)
                                                    << 8)) +
                                                  m_x));
            setNZFromByte(m_a);
            return 5;
        }

        case 0x16: { // OR A, !abs+Y
            const uint8_t lo =
                fetchPc(apu),
                hi = fetchPc(apu);
            m_a |= ramRead(apu,
                            static_cast<uint16_t>((static_cast<unsigned>(
                                                       lo) |
                                                   (static_cast<unsigned>(hi)
                                                    << 8)) +
                                                  m_y));
            setNZFromByte(m_a);
            return 5;
        }

        case 0x26: // AND A, (X)
            m_a &= ramRead(apu, readDp(m_x));
            setNZFromByte(m_a);
            return 3;

        case 0x27: // AND A, [dp+X]
            m_a &= ramRead(
                apu,
                read16Zp(apu,
                         static_cast<uint8_t>(
                             static_cast<unsigned>(fetchPc(apu)) +
                             static_cast<unsigned>(m_x))));
            setNZFromByte(m_a);
            return 6;

        case 0x37: // AND A, [dp]+Y
            m_a &= ramRead(
                apu,
                static_cast<uint16_t>(
                    read16Zp(apu, fetchPc(apu)) +
                    m_y));
            setNZFromByte(m_a);
            return 6;

        case 0x24: // AND A, dp
            m_a &= ramRead(apu, readDp(fetchPc(apu)));
            setNZFromByte(m_a);
            return 3;

        case 0x34: // AND A, d+X
            m_a &= ramRead(apu, zpAddrPlusX(fetchPc(apu)));
            setNZFromByte(m_a);
            return 4;

        case 0x25: {
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            m_a &= ramRead(apu, a);
            setNZFromByte(m_a);
            return 4;
        }

        case 0x35: {
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>((static_cast<unsigned>(lo) |
                                       (static_cast<unsigned>(hi) << 8)) +
                                      m_x);
            m_a &= ramRead(apu, a);
            setNZFromByte(m_a);
            return 5;
        }

        case 0x36: {
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>((static_cast<unsigned>(lo) |
                                       (static_cast<unsigned>(hi) << 8)) +
                                      m_y);
            m_a &= ramRead(apu, a);
            setNZFromByte(m_a);
            return 5;
        }

        case 0x46: // EOR A, (X)
            m_a ^= ramRead(apu, readDp(m_x));
            setNZFromByte(m_a);
            return 3;

        case 0x47: // EOR A, [dp+X]
            m_a ^= ramRead(
                apu,
                read16Zp(apu,
                         static_cast<uint8_t>(
                             static_cast<unsigned>(fetchPc(apu)) +
                             static_cast<unsigned>(m_x))));
            setNZFromByte(m_a);
            return 6;

        case 0x57: // EOR A, [dp]+Y
            m_a ^= ramRead(
                apu,
                static_cast<uint16_t>(
                    read16Zp(apu, fetchPc(apu)) +
                    m_y));
            setNZFromByte(m_a);
            return 6;

        case 0x44: // EOR A, dp
            m_a ^= ramRead(apu, readDp(fetchPc(apu)));
            setNZFromByte(m_a);
            return 3;

        case 0x45: { // EOR A, !abs
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            const uint16_t a =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            m_a ^= ramRead(apu, a);
            setNZFromByte(m_a);
            return 4;
        }

        case 0x54: // EOR A, d+X
            m_a ^= ramRead(apu, zpAddrPlusX(fetchPc(apu)));
            setNZFromByte(m_a);
            return 4;

        case 0x55: {
            const uint8_t lo =
                fetchPc(apu),
                hi = fetchPc(apu);
            m_a ^= ramRead(apu,
                           static_cast<uint16_t>((static_cast<unsigned>(
                                                      lo) |
                                                  (static_cast<unsigned>(hi)
                                                   << 8)) +
                                                 m_x));
            setNZFromByte(m_a);
            return 5;
        }

        case 0x56: {
            const uint8_t lo =
                fetchPc(apu),
                hi = fetchPc(apu);
            m_a ^= ramRead(apu,
                           static_cast<uint16_t>((static_cast<unsigned>(
                                                      lo) |
                                                  (static_cast<unsigned>(hi)
                                                   << 8)) +
                                                 m_y));
            setNZFromByte(m_a);
            return 5;
        }

        case 0x58: // EOR dp, #imm
        {
            const uint8_t imm = fetchPc(apu);
            const uint16_t a = readDp(fetchPc(apu));
            uint8_t        v =
                ramRead(apu, a);
            v ^= imm;
            ramWrite(apu, a, v);
            setNZFromByte(v);
            return 5;
        }

        case 0x8B: // DEC dp
            {
                const uint16_t a = readDp(fetchPc(apu));
                uint8_t        v =
                    ramRead(apu, a);
                --v;
                ramWrite(apu, a, v);
                setNZFromByte(v);
                return 4;
            }

        case 0x9B: // DEC d+X
            {
                uint16_t a = zpAddrPlusX(fetchPc(apu));
                uint8_t  v = ramRead(apu, a);
                --v;
                ramWrite(apu, a, v);
                setNZFromByte(v);
                return 5;
            }

        case 0x8C: { // DEC !abs
            const uint8_t lo = fetchPc(apu), hi = fetchPc(apu);
            uint16_t      a =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            uint8_t v =
                ramRead(apu, a);
            --v;
            ramWrite(apu, a, v);
            setNZFromByte(v);
            return 5;
        }

        case 0xBB: // INC d+X
            {
                uint16_t a = zpAddrPlusX(fetchPc(apu));
                uint8_t  v = ramRead(apu, a);
                ++v;
                ramWrite(apu, a, v);
                setNZFromByte(v);
                return 5;
            }

        case 0xAC: { // INC !abs
            const uint8_t lo =
                fetchPc(apu),
                hi = fetchPc(apu);
            uint16_t a =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            uint8_t v =
                ramRead(apu, a);
            ++v;
            ramWrite(apu, a, v);
            setNZFromByte(v);
            return 5;
        }

        case 0x0B: // ASL dp
            {
                uint16_t a = readDp(fetchPc(apu));
                uint8_t  v =
                    ramRead(apu, a);
                setC((v & 0x80u) != 0);
                v <<= 1u;
                ramWrite(apu, a, v);
                setNZFromByte(v);
                return 4;
            }

        case 0x1B: // ASL d+X
            {
                uint16_t a =
                    zpAddrPlusX(fetchPc(apu));
                uint8_t  v =
                    ramRead(apu, a);
                setC((v & 0x80u) != 0);
                v <<= 1u;
                ramWrite(apu, a, v);
                setNZFromByte(v);
                return 5;
            }

        case 0x0C: { // ASL !abs
            const uint8_t lo =
                fetchPc(apu),
                hi = fetchPc(apu);
            uint16_t a =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            uint8_t v =
                ramRead(apu, a);
            setC((v & 0x80u) != 0);
            v <<= 1u;
            ramWrite(apu, a, v);
            setNZFromByte(v);
            return 5;
        }

        case 0x4B: // LSR dp
            {
                uint16_t a = readDp(fetchPc(apu));
                uint8_t  v =
                    ramRead(apu, a);
                setC((v & 1u) != 0);
                v = static_cast<uint8_t>(
                    static_cast<unsigned>(v) >> 1u);
                ramWrite(apu, a, v);
                setNZFromByte(v);
                return 4;
            }

        case 0x5B: // LSR d+X
            {
                uint16_t a =
                    zpAddrPlusX(fetchPc(apu));
                uint8_t  v =
                    ramRead(apu, a);
                setC((v & 1u) != 0);
                v =
                    static_cast<uint8_t>(static_cast<unsigned>(v)
                                         >> 1u);
                ramWrite(apu, a, v);
                setNZFromByte(v);
                return 5;
            }

        case 0x4C: { // LSR !abs
            const uint8_t lo =
                fetchPc(apu),
                hi = fetchPc(apu);
            uint16_t a =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            uint8_t v =
                ramRead(apu, a);
            setC((v & 1u) != 0);
            v =
                static_cast<uint8_t>(static_cast<unsigned>(v) >> 1u);
            ramWrite(apu, a, v);
            setNZFromByte(v);
            return 5;
        }

        case 0x3C: // ROL A
            {
                const bool nc =
                    (m_a & 0x80u) != 0;
                m_a =
                    static_cast<uint8_t>((unsigned(m_a) << 1) |
                                         (flagC() ? 1u : 0u));
                setC(nc);
                setNZFromByte(m_a);
                return 2;
            }

        case 0x2B: // ROL dp
            {
                uint16_t    a =
                    readDp(fetchPc(apu));
                uint8_t     v =
                    ramRead(apu, a);
                const bool nh =
                    (v & 0x80u) != 0;
                v =
                    static_cast<uint8_t>((unsigned(v) << 1) |
                                         (flagC() ? 1u : 0u));
                setC(nh);
                ramWrite(apu, a, v);
                setNZFromByte(v);
                return 4;
            }

        case 0x3B: // ROL d+X
            {
                uint16_t    a =
                    zpAddrPlusX(fetchPc(apu));
                uint8_t     v =
                    ramRead(apu, a);
                const bool nh =
                    (v & 0x80u) != 0;
                v =
                    static_cast<uint8_t>((unsigned(v) << 1) |
                                         (flagC() ? 1u : 0u));
                setC(nh);
                ramWrite(apu, a, v);
                setNZFromByte(v);
                return 5;
            }

        case 0x2C: { // ROL !abs
            const uint8_t lo =
                fetchPc(apu),
                hi = fetchPc(apu);
            uint16_t a =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            uint8_t     v =
                ramRead(apu, a);
            const bool nh =
                (v & 0x80u) != 0;
            v =
                static_cast<uint8_t>((unsigned(v) << 1) |
                                     (flagC() ? 1u : 0u));
            setC(nh);
            ramWrite(apu, a, v);
            setNZFromByte(v);
            return 5;
        }

        case 0x7C: // ROR A
            {
                const unsigned low =
                    m_a & 1u;
                m_a =
                    static_cast<uint8_t>((unsigned(m_a) >> 1) |
                                         (flagC() ? 0x80u : 0u));
                setC(low != 0);
                setNZFromByte(m_a);
                return 2;
            }

        case 0x6B: // ROR dp
            {
                uint16_t a =
                    readDp(fetchPc(apu));
                uint8_t  v =
                    ramRead(apu, a);
                const unsigned low = v & 1u;
                v =
                    static_cast<uint8_t>((unsigned(v) >> 1) |
                                         (flagC() ? 0x80u : 0u));
                setC(low != 0);
                ramWrite(apu, a, v);
                setNZFromByte(v);
                return 4;
            }

        case 0x7B: // ROR d+X
            {
                uint16_t a =
                    zpAddrPlusX(fetchPc(apu));
                uint8_t  v =
                    ramRead(apu, a);
                const unsigned low = v & 1u;
                v =
                    static_cast<uint8_t>((unsigned(v) >> 1) |
                                         (flagC() ? 0x80u : 0u));
                setC(low != 0);
                ramWrite(apu, a, v);
                setNZFromByte(v);
                return 5;
            }

        case 0x6C: { // ROR !abs
            const uint8_t lo =
                fetchPc(apu),
                hi = fetchPc(apu);
            uint16_t a =
                static_cast<uint16_t>(static_cast<unsigned>(lo) |
                                      (static_cast<unsigned>(hi) << 8));
            uint8_t  v =
                ramRead(apu, a);
            const unsigned low = v & 1u;
            v =
                static_cast<uint8_t>((unsigned(v) >> 1) |
                                     (flagC() ? 0x80u : 0u));
            setC(low != 0);
            ramWrite(apu, a, v);
            setNZFromByte(v);
            return 5;
        }

        case 0xEF: // SLEEP — waits for DSP interrupt on hardware; DSP not emulated ⇒ keep advancing.
            return 8;

        case 0xFE: { // DBNZ Y,rel — decrement Y then branch back if nonzero.
            --m_y;
            setNZFromByte(m_y);
            int8_t rel = static_cast<int8_t>(fetchPc(apu));
            if (!flagZ())
                m_pc = spcPcPlusRel(m_pc, rel);
            return flagZ() ? 2 : 4;
        }

        case 0xFF: // STOP — normally driver exit; halt so we avoid burning host CPU.
            m_halted = true;
            if (std::getenv("SNESFOX_SPC_LOG")) {
                const uint16_t opcAddr =
                    static_cast<uint16_t>(static_cast<unsigned>(m_pc) - 1u);
                std::fprintf(stderr,
                             "[spc700] STOP at PC=$%04X (PSW=$%02X SP=$%02X)\n",
                             opcAddr,
                             m_psw,
                             m_sp);
            }
            return 12;

        default:
            m_halted = true;
            if (std::getenv("SNESFOX_SPC_LOG")) {
                const uint16_t opcAddr =
                    static_cast<uint16_t>(static_cast<unsigned>(m_pc) - 1u);
                std::fprintf(stderr,
                             "[spc700] illegal opcode $%02X at PC=$%04X "
                             "(A=$%02X X=$%02X Y=$%02X PSW=$%02X)\n",
                             opc,
                             opcAddr,
                             m_a,
                             m_x,
                             m_y,
                             m_psw);
            }
            return 2;
    }
}
