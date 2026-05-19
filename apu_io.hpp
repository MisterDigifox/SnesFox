#pragma once

#include <array>
#include <cstdint>

class ApuIo {
public:
    void reset();

    uint8_t readPort(uint16_t addr) const;
    void writePort(uint16_t addr, uint8_t value);

    void step();

private:
    // Last value written by CPU ($2140-$2143 write side).
    std::array<uint8_t, 4> m_cpuToApu{};

    // Value returned on reads. After IPL boot responds with $BBAA on ports 0/1,
    // each CPU write mirrors into these bytes so handshake / dspWait loops can
    // complete without a live SPC700.
    std::array<uint8_t, 4> m_apuToCpu{};
};
