#pragma once
#include <cstdint>
#include <string>
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
    const std::string& instruction() const;
    const std::string& bytes() const;
    uint8_t p() const;
    bool flagM() const;
    bool flagX() const;
    uint16_t a() const;
    uint16_t x() const;
    uint16_t y() const;
    uint16_t sp() const;
    uint64_t cycles() const;

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
    std::string m_instruction = "???";
    std::string m_bytes;
};
