#pragma once
#include <cstdint>
#include <string>
#include "opcodes.hpp"
class Bus;
class CPU final {
public:
    CPU() = default;
    void reset(const Bus& bus, uint16_t resetVector);
    void step(Bus& bus);
    void triggerNmi(Bus& bus);
    void triggerIrq(Bus& bus);

    bool waiting() const { return m_waiting; }
    /// WAI wakeup without branching to an ISR (SRAM / VBlank when NMITIMEN blocks NMI).
    void wakeFromWaiSilently();
    uint16_t resetVector() const;
    uint8_t bank() const;
    uint16_t pc() const;
    uint32_t pc24() const;
    uint8_t opcode() const;
    // Built on demand from the raw bytes/addressing-mode step() stashes below, rather than
    // formatted eagerly inside step() itself — these disassembly strings are only ever
    // actually read a handful of times per frame (debug UI, --log-cpu trace, single-step),
    // so doing the formatting (heap-allocating ostringstream calls) on every single
    // instruction executed was pure waste on the hottest path in the emulator.
    std::string instruction() const;
    std::string bytes() const;
    uint8_t p() const;
    bool flagM() const;
    bool flagX() const;
    uint16_t a() const;
    uint16_t x() const;
    uint16_t y() const;
    uint16_t sp() const;
    uint64_t cycles() const;
    // Same accounting as cycles(), but without cycles()'s single whole-instruction rounding
    // step — kept at "eighths of a cycles() unit" resolution (see CPU::step's comment) so
    // Bus can derive a finer H-counter without the integer-aliasing that rounding causes.
    uint64_t fineCycles() const;

private:
    bool m_waiting = false;
    bool m_stopped = false;
    bool m_e = false;
    uint16_t m_resetVector = 0;
    uint8_t m_bank = 0x00;
    uint8_t m_db = 0x00;
    uint16_t m_pc = 0x0000;
    uint8_t m_opcode = 0x00;
    uint8_t m_p = 0x34;
    uint16_t m_a = 0x0000;
    uint16_t m_x = 0x0000;
    uint16_t m_y = 0x0000;
    uint16_t m_sp = 0x01FF;
    uint16_t m_d = 0x0000;
    uint64_t m_cycles = 0;
    uint64_t m_fineCycles = 0;

    // Raw decode info from the last step(), used to lazily format instruction()/bytes().
    enum class DecodeKind : uint8_t { Reset, Invalid, Normal };
    DecodeKind m_decodeKind = DecodeKind::Reset;
    uint8_t m_decodeB0 = 0;
    uint8_t m_decodeB1 = 0;
    uint8_t m_decodeB2 = 0;
    uint8_t m_decodeB3 = 0;
    uint8_t m_decodeSize = 1;
    AddrMode m_decodeMode = AddrMode::Implied;
    const char* m_decodeOpName = "";
    uint16_t m_decodePc = 0;
    uint8_t m_decodeP = 0;
};
