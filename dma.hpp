#pragma once
#include <cstdint>

class Bus;

class Dma {
public:
    // Call when $420B is written; bitmask selects channels 0-7
    void trigger(uint8_t enableMask, Bus& bus);

    uint8_t readReg(uint8_t ch, uint8_t reg) const;
    void    writeReg(uint8_t ch, uint8_t reg, uint8_t value);

    void reset();

    // HDMA: reload table from $43x2-4 base and prime first entry at start of frame
    void beginHdmaFrame(uint8_t enableMask, Bus& bus);
    // HDMA: once per scanline (current V after increment). Updates $43x2-$43x4 like hardware.
    void runHdmaForScanline(int v, Bus& bus);

private:
    struct Channel {
        uint8_t  ctrl      = 0xFF; // $43x0
        uint8_t  bBus      = 0xFF; // $43x1
        uint16_t srcAddr   = 0xFFFF; // $43x2/$43x3 — current table pointer (HDMA advances)
        uint8_t  srcBank   = 0xFF; // $43x4
        uint16_t byteCount = 0xFFFF; // $43x5/$43x6
        uint8_t  unused7   = 0xFF; // $43x7
        // Programmed table origin (updated on writes to $43x2-$43x4). Reloaded each frame.
        uint16_t tableBaseAddr = 0xFFFF;
        uint8_t  tableBaseBank = 0xFF;
    };

    Channel m_ch[8];

    // HDMA (see snes9x dma.cpp: Repeat = !(line & 0x80) except line==0x80; DoTransfer = !Repeat after each line)
    uint8_t  m_hdmaFrameEnable = 0;
    bool     m_hdmaActive[8]{};
    bool     m_hdmaDoTransfer[8]{};
    bool     m_hdmaSnesRepeat[8]{}; // "Repeat" field in snes9x: true ⇒ single transfer / entry row
    uint8_t  m_hdmaLineCount[8]{};
    uint16_t m_hdmaIndirectPtr[8]{};
    uint8_t  m_hdmaIndirectBank[8]{};

    void runChannel(int ch, Bus& bus);
    static void stepA(Channel& c);
    void transferOneUnit(int ch, Bus& bus);
    void transferOneUnitIndirect(int ch, Bus& bus);
    // Returns false if HDMA for this channel ends (count byte 0, etc.)
    bool hdmaReadLineCount(int ch, Bus& bus);
};
