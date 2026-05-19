#include "dma.hpp"
#include "bus.hpp"

// Transfer unit patterns for the 3-bit mode field (bits 2:0 of $43x0).
// Each entry is the sequence of B-bus offsets written per "unit".
static const uint8_t kUnitOffsets[8][4] = {
    {0, 0, 0, 0}, // mode 0: 1 byte  → B+0
    {0, 1, 0, 1}, // mode 1: 2 bytes → B+0, B+1
    {0, 0, 0, 0}, // mode 2: 2 bytes → B+0, B+0
    {0, 0, 1, 1}, // mode 3: 4 bytes → B+0, B+0, B+1, B+1
    {0, 1, 2, 3}, // mode 4: 4 bytes → B+0..B+3
    {0, 1, 0, 1}, // mode 5: same as 1 (fixed address variant)
    {0, 0, 0, 0}, // mode 6: same as 2
    {0, 0, 1, 1}, // mode 7: same as 3
};

static const uint8_t kUnitSize[8] = { 1, 2, 2, 4, 4, 2, 2, 4 };

void Dma::trigger(uint8_t enableMask, Bus& bus) {
    for (int c = 0; c < 8; ++c) {
        if (enableMask & (1 << c)) {
            runChannel(c, bus);
        }
    }
}

void Dma::runChannel(int ch, Bus& bus) {
    Channel& c = m_ch[ch];

    const bool toB   = !(c.ctrl & 0x80); // direction: 0=A→B, 1=B→A
    const bool fixed = (c.ctrl & 0x08);  // fixed A-bus address
    const bool decr  = (c.ctrl & 0x10);  // A-bus step: 0=+1, 1=-1 (if !fixed)
    const uint8_t mode = c.ctrl & 0x07;

    const uint8_t  unitSz  = kUnitSize[mode];
    const uint8_t* offsets = kUnitOffsets[mode];

    uint16_t count  = c.byteCount; // 0 treated as 65536
    uint16_t srcA   = c.srcAddr;
    const uint8_t srcBank = c.srcBank;
    const uint8_t bBase   = c.bBus;

    uint32_t transferred = 0;
    const uint32_t limit = (count == 0) ? 65536u : count;

    for (uint32_t i = 0; i < limit; ++i) {
        const uint8_t bOff = offsets[i % unitSz];
        const uint16_t bAddr = static_cast<uint16_t>(0x2100 + bBase + bOff);

        if (toB) {
            const uint8_t val = bus.read(srcBank, srcA);
            bus.write(0x00, bAddr, val);
        } else {
            const uint8_t val = bus.read(0x00, bAddr);
            bus.write(srcBank, srcA, val);
        }

        if (!fixed) {
            if (decr) --srcA;
            else      ++srcA;
        }
        ++transferred;
    }

    // Update registers to reflect post-transfer state
    c.srcAddr   = srcA;
    c.byteCount = 0;
}

uint8_t Dma::readReg(uint8_t ch, uint8_t reg) const {
    const Channel& c = m_ch[ch];
    switch (reg) {
        case 0: return c.ctrl;
        case 1: return c.bBus;
        case 2: return static_cast<uint8_t>(c.srcAddr & 0xFF);
        case 3: return static_cast<uint8_t>(c.srcAddr >> 8);
        case 4: return c.srcBank;
        case 5: return static_cast<uint8_t>(c.byteCount & 0xFF);
        case 6: return static_cast<uint8_t>(c.byteCount >> 8);
        case 7: return c.unused7;
        default: return 0xFF;
    }
}

void Dma::writeReg(uint8_t ch, uint8_t reg, uint8_t value) {
    Channel& c = m_ch[ch];
    switch (reg) {
        case 0: c.ctrl      = value; break;
        case 1: c.bBus      = value; break;
        case 2: c.srcAddr   = (c.srcAddr   & 0xFF00) | value; break;
        case 3: c.srcAddr   = (c.srcAddr   & 0x00FF) | (static_cast<uint16_t>(value) << 8); break;
        case 4: c.srcBank   = value; break;
        case 5: c.byteCount = (c.byteCount & 0xFF00) | value; break;
        case 6: c.byteCount = (c.byteCount & 0x00FF) | (static_cast<uint16_t>(value) << 8); break;
        case 7: c.unused7   = value; break;
    }
}
