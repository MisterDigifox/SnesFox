#include "gsu_disasm.hpp"
#include <cstdio>

namespace {
// idx: 0=ALT0, 1=ALT1, 2=ALT2, 3=ALT3
int altIndex(bool alt1, bool alt2) {
    return (alt2 ? 2 : 0) | (alt1 ? 1 : 0);
}
}

std::string gsuDisassemble(uint16_t opcodeAddr, uint8_t opcode, bool alt1, bool alt2,
                            uint8_t operand1, uint8_t operand2) {
    char buf[64];
    const int idx = altIndex(alt1, alt2);
    const uint8_t n = opcode & 0x0F;

    if (opcode <= 0x04 || opcode == 0x3C || opcode == 0x3D || opcode == 0x3E || opcode == 0x3F ||
        opcode == 0x4D || opcode == 0x4F || opcode == 0x70 || opcode == 0x90 || opcode == 0x95 ||
        opcode == 0x97 || opcode == 0x9E || opcode == 0xC0) {
        static const char* fixed[] = {"stop", "nop", "cache", "lsr", "rol"};
        switch (opcode) {
        case 0x00: return "stop";
        case 0x01: return "nop";
        case 0x02: return "cache";
        case 0x03: return "lsr";
        case 0x04: return "rol";
        case 0x3C: return "loop";
        case 0x3D: return "alt1";
        case 0x3E: return "alt2";
        case 0x3F: return "alt3";
        case 0x4D: return "swap";
        case 0x4F: return "not";
        case 0x70: return "merge";
        case 0x90: return "sbk";
        case 0x95: return "sex";
        case 0x97: return "ror";
        case 0x9E: return "lob";
        case 0xC0: return "hib";
        default: break;
        }
        (void)fixed;
    }

    if (opcode >= 0x05 && opcode <= 0x0F) {
        static const char* names[] = {"bra", "blt", "bge", "bne", "beq", "bpl", "bmi",
                                       "bcc", "bcs", "bvc", "bvs"};
        const int8_t disp = static_cast<int8_t>(operand1);
        const uint16_t target = static_cast<uint16_t>(opcodeAddr + 2 + disp);
        std::snprintf(buf, sizeof(buf), "%s %+d  -> $%04x", names[opcode - 0x05], disp, target);
        return buf;
    }
    if (opcode >= 0x10 && opcode <= 0x1F) { std::snprintf(buf, sizeof(buf), "to r%u", n); return buf; }
    if (opcode >= 0x20 && opcode <= 0x2F) { std::snprintf(buf, sizeof(buf), "with r%u", n); return buf; }
    if (opcode >= 0x30 && opcode <= 0x3B) {
        std::snprintf(buf, sizeof(buf), "st%c (r%u)", (idx == 1 || idx == 3) ? 'b' : 'w', n);
        return buf;
    }
    if (opcode >= 0x40 && opcode <= 0x4B) {
        std::snprintf(buf, sizeof(buf), "ld%c (r%u)", (idx == 1 || idx == 3) ? 'b' : 'w', n);
        return buf;
    }
    if (opcode == 0x4C) return (idx == 1 || idx == 3) ? "rpix" : "plot";
    if (opcode == 0x4E) return (idx == 1 || idx == 3) ? "cmode" : "color";
    if (opcode >= 0x50 && opcode <= 0x5F) {
        const char* nm = (idx == 0) ? "add" : (idx == 1) ? "adc" : (idx == 2) ? "add" : "adc";
        if (idx <= 1) std::snprintf(buf, sizeof(buf), "%s r%u", nm, n);
        else std::snprintf(buf, sizeof(buf), "%s #%u", nm, n);
        return buf;
    }
    if (opcode >= 0x60 && opcode <= 0x6F) {
        const char* nm = (idx == 0) ? "sub" : (idx == 1) ? "sbc" : (idx == 2) ? "sub" : "cmp";
        if (idx == 0 || idx == 1 || idx == 3) std::snprintf(buf, sizeof(buf), "%s r%u", nm, n);
        else std::snprintf(buf, sizeof(buf), "sub #%u", n);
        return buf;
    }
    if (opcode >= 0x71 && opcode <= 0x7F) {
        const char* nm = (idx == 0) ? "and" : (idx == 1) ? "bic" : (idx == 2) ? "and" : "bic";
        if (idx <= 1) std::snprintf(buf, sizeof(buf), "%s r%u", nm, n);
        else std::snprintf(buf, sizeof(buf), "%s #%u", nm, n);
        return buf;
    }
    if (opcode >= 0x80 && opcode <= 0x8F) {
        const char* nm = (idx == 0) ? "mult" : (idx == 1) ? "umult" : (idx == 2) ? "mult" : "umult";
        if (idx <= 1) std::snprintf(buf, sizeof(buf), "%s r%u", nm, n);
        else std::snprintf(buf, sizeof(buf), "%s #%u", nm, n);
        return buf;
    }
    if (opcode >= 0x91 && opcode <= 0x94) { std::snprintf(buf, sizeof(buf), "link #%u", n); return buf; }
    if (opcode == 0x96) return (idx == 1 || idx == 3) ? "div2" : "asr";
    if (opcode >= 0x98 && opcode <= 0x9D) {
        std::snprintf(buf, sizeof(buf), "%s r%u", (idx == 1 || idx == 3) ? "ljmp" : "jmp", n);
        return buf;
    }
    if (opcode == 0x9F) return (idx == 1 || idx == 3) ? "lmult" : "fmult";
    if (opcode >= 0xA0 && opcode <= 0xAF) {
        if (idx == 0) { std::snprintf(buf, sizeof(buf), "ibt r%u,#$%02x", n, operand1); return buf; }
        if (idx == 1) { std::snprintf(buf, sizeof(buf), "lms r%u,($%04x)", n, operand1 << 1); return buf; }
        if (idx == 2) { std::snprintf(buf, sizeof(buf), "sms r%u,($%04x)", n, operand1 << 1); return buf; }
        std::snprintf(buf, sizeof(buf), "lms r%u", n);
        return buf;
    }
    if (opcode >= 0xB0 && opcode <= 0xBF) {
        std::snprintf(buf, sizeof(buf), "%s r%u", (idx == 1 || idx == 3) ? "moves" : "from", n);
        return buf;
    }
    if (opcode >= 0xC1 && opcode <= 0xCF) {
        const char* nm = (idx == 0) ? "or" : (idx == 1) ? "xor" : (idx == 2) ? "or" : "xor";
        if (idx <= 1) std::snprintf(buf, sizeof(buf), "%s r%u", nm, n);
        else std::snprintf(buf, sizeof(buf), "%s #%u", nm, n);
        return buf;
    }
    if (opcode >= 0xD0 && opcode <= 0xDE) { std::snprintf(buf, sizeof(buf), "inc r%u", n); return buf; }
    if (opcode == 0xDF) {
        static const char* names[] = {"getc", "getc", "ramb", "romb"};
        return names[idx];
    }
    if (opcode >= 0xE0 && opcode <= 0xEE) { std::snprintf(buf, sizeof(buf), "dec r%u", n); return buf; }
    if (opcode == 0xEF) {
        static const char* names[] = {"getb", "getbh", "getbl", "getbs"};
        return names[idx];
    }
    if (opcode >= 0xF0 && opcode <= 0xFF) {
        const uint16_t imm = static_cast<uint16_t>(operand1 | (operand2 << 8));
        const char* nm = (idx == 0) ? "iwt" : (idx == 1) ? "lm" : (idx == 2) ? "sm" : "lm";
        std::snprintf(buf, sizeof(buf), "%s r%u,#$%04x", nm, n, imm);
        return buf;
    }

    std::snprintf(buf, sizeof(buf), "?? $%02x", opcode);
    return buf;
}
