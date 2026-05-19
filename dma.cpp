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

void Dma::stepA(Channel& c) {
    if (c.ctrl & 0x08) return;
    if (c.ctrl & 0x10) {
        --c.srcAddr;
    } else {
        ++c.srcAddr;
    }
}

void Dma::transferOneUnit(int ch, Bus& bus) {
    Channel& c = m_ch[ch];
    const bool toB   = !(c.ctrl & 0x80);
    const bool fixed = (c.ctrl & 0x08);
    const bool decr  = (c.ctrl & 0x10);
    const uint8_t mode = c.ctrl & 0x07;
    const uint8_t  unitSz  = kUnitSize[mode];
    const uint8_t* offsets = kUnitOffsets[mode];

    for (uint8_t i = 0; i < unitSz; ++i) {
        const uint8_t bOff = offsets[i];
        const uint16_t bAddr = static_cast<uint16_t>(0x2100 + c.bBus + bOff);

        if (toB) {
            const uint8_t val = bus.read(c.srcBank, c.srcAddr);
            bus.write(0x00, bAddr, val);
        } else {
            const uint8_t val = bus.read(0x00, bAddr);
            bus.write(c.srcBank, c.srcAddr, val);
        }

        if (!fixed) {
            if (decr) {
                --c.srcAddr;
            } else {
                ++c.srcAddr;
            }
        }
    }
}

void Dma::transferOneUnitIndirect(int ch, Bus& bus) {
    Channel& c = m_ch[ch];
    const bool toB = !(c.ctrl & 0x80);
    const uint8_t mode = c.ctrl & 0x07;
    const uint8_t  unitSz  = kUnitSize[mode];
    const uint8_t* offsets = kUnitOffsets[mode];

    uint16_t ptr = m_hdmaIndirectPtr[ch];
    const uint8_t bank = m_hdmaIndirectBank[ch];

    for (uint8_t i = 0; i < unitSz; ++i) {
        const uint16_t bAddr = static_cast<uint16_t>(0x2100 + c.bBus + offsets[i]);
        if (toB) {
            const uint8_t val = bus.read(bank, ptr++);
            bus.write(0x00, bAddr, val);
        } else {
            const uint8_t val = bus.read(0x00, bAddr);
            bus.write(bank, ptr++, val);
        }
    }
    m_hdmaIndirectPtr[ch] = ptr;
}

void Dma::reset() {
    for (int i = 0; i < 8; ++i) {
        m_ch[i] = Channel{};
        m_hdmaActive[i]      = false;
        m_hdmaDoTransfer[i]  = false;
        m_hdmaSnesRepeat[i]  = false;
        m_hdmaLineCount[i]   = 0;
    }
    m_hdmaFrameEnable = 0;
}

bool Dma::hdmaReadLineCount(int ch, Bus& bus) {
    Channel& c          = m_ch[ch];
    const bool indirect = (c.ctrl & 0x40) != 0;

    const uint8_t line = bus.read(c.srcBank, c.srcAddr);
    stepA(c);

    if (line == 0) {
        // Terminator (snes9x also has indirect special case; treat as end for now)
        return false;
    }

    if (line == 0x80) {
        // One transfer, then 127 scanlines of idle (same as snes9x)
        m_hdmaSnesRepeat[ch] = true;
        m_hdmaLineCount[ch]  = 128;
    } else {
        m_hdmaSnesRepeat[ch] = !(line & 0x80);
        m_hdmaLineCount[ch]  = line & 0x7F;
    }

    m_hdmaDoTransfer[ch] = true;

    if (indirect) {
        uint8_t lo = bus.read(c.srcBank, c.srcAddr);
        stepA(c);
        uint8_t hi = bus.read(c.srcBank, c.srcAddr);
        stepA(c);
        m_hdmaIndirectPtr[ch]  = static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
        m_hdmaIndirectBank[ch] = c.unused7;
    }

    return true;
}

void Dma::beginHdmaFrame(uint8_t enableMask, Bus& bus) {
    m_hdmaFrameEnable = enableMask;
    for (int ch = 0; ch < 8; ++ch) {
        if (!(enableMask & (1 << ch))) {
            m_hdmaActive[ch] = false;
            continue;
        }

        m_ch[ch].srcAddr = m_ch[ch].tableBaseAddr;
        m_ch[ch].srcBank = m_ch[ch].tableBaseBank;
        m_hdmaActive[ch] = true;

        if (!hdmaReadLineCount(ch, bus)) {
            m_hdmaActive[ch] = false;
        }
    }
}

void Dma::runHdmaForScanline(int v, Bus& bus) {
    (void)v;
    for (int ch = 0; ch < 8; ++ch) {
        if ((m_hdmaFrameEnable & (1 << ch)) == 0) continue;
        if (!m_hdmaActive[ch]) continue;

        const bool indirect = (m_ch[ch].ctrl & 0x40) != 0;

        if (m_hdmaDoTransfer[ch]) {
            if (indirect) {
                transferOneUnitIndirect(ch, bus);
            } else {
                transferOneUnit(ch, bus);
            }
        }

        // snes9x: p->DoTransfer = !p->Repeat;
        m_hdmaDoTransfer[ch] = !m_hdmaSnesRepeat[ch];

        if (--m_hdmaLineCount[ch] == 0) {
            if (!hdmaReadLineCount(ch, bus)) {
                m_hdmaActive[ch] = false;
            }
        }
    }
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
    if (reg >= 2 && reg <= 4) {
        c.tableBaseAddr = c.srcAddr;
        c.tableBaseBank = c.srcBank;
    }
}
