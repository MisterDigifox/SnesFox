#pragma once
#include <cstdint>

class Bus;

class Dma {
public:
    // Call when $420B is written; bitmask selects channels 0-7
    void trigger(uint8_t enableMask, Bus& bus);

    uint8_t readReg(uint8_t ch, uint8_t reg) const;
    void    writeReg(uint8_t ch, uint8_t reg, uint8_t value);

private:
    struct Channel {
        uint8_t  ctrl      = 0xFF; // $43x0: transfer mode, direction, etc.
        uint8_t  bBus      = 0xFF; // $43x1: B-bus address (PPU register)
        uint16_t srcAddr   = 0xFFFF; // $43x2/$43x3
        uint8_t  srcBank   = 0xFF; // $43x4
        uint16_t byteCount = 0xFFFF; // $43x5/$43x6 (0 = 65536)
        uint8_t  unused7   = 0xFF; // $43x7
    };

    Channel m_ch[8];

    void runChannel(int ch, Bus& bus);
};
