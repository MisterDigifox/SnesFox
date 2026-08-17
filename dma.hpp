#pragma once
#include <cstdint>

class Bus;

class Dma {
public:
    // Call when $420B is written; bitmask selects channels 0-7. Returns the real-hardware
    // master-clock cost of the transfer(s) (8 cycles/byte plus fixed per-channel overhead) —
    // the CPU is genuinely halted for this long during GP-DMA, so the caller must fold it
    // into CPU cycle accounting or timing-sensitive polling loops drift out of sync with
    // real hardware (see CPU::fineCycles()'s doc comment for why that matters).
    uint32_t trigger(uint8_t enableMask, Bus& bus);

    uint8_t readReg(uint8_t ch, uint8_t reg) const;
    void    writeReg(uint8_t ch, uint8_t reg, uint8_t value);

    void reset();

    // HDMA: reload table from $43x2-4 base and prime first entry at start of frame. Returns
    // the real-hardware master-clock cost of this setup pass (see runHdmaForScanline).
    uint32_t beginHdmaFrame(uint8_t enableMask, Bus& bus);
    // HDMA: once per scanline (current V after increment). Updates $43x2-$43x4 like hardware.
    // Returns the real-hardware master-clock cost of this scanline's HDMA processing — the CPU
    // is halted for this too, same reasoning as Dma::trigger's return.
    uint32_t runHdmaForScanline(int v, Bus& bus);

private:
    struct Channel {
        uint8_t  ctrl      = 0xFF; // $43x0
        uint8_t  bBus      = 0xFF; // $43x1
        uint16_t srcAddr   = 0xFFFF; // $43x2/$43x3 — A1TxL/H: CPU-visible table/general-DMA address
        uint8_t  srcBank   = 0xFF; // $43x4 — A1Bx
        uint16_t byteCount = 0xFFFF; // $43x5/$43x6
        uint8_t  unused7   = 0xFF; // $43x7
        // A2AxL/H+bank equivalent: HDMA's own live table cursor, reloaded from srcAddr/
        // srcBank once at the start of each frame (beginHdmaFrame) and advanced only by
        // HDMA playback. Kept separate from srcAddr/srcBank so that a CPU write to A1Tx
        // — routine every frame in HDMA-driven code — can never redirect an in-progress
        // HDMA transfer; real hardware has the same A1Tx/A2Ax separation for this reason.
        uint16_t hdmaCurAddr = 0xFFFF;
        uint8_t  hdmaCurBank = 0xFF;
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

    uint32_t runChannel(int ch, Bus& bus); // returns bytes transferred
    static void stepA(Channel& c);
    void transferOneUnit(int ch, Bus& bus);
    void transferOneUnitIndirect(int ch, Bus& bus);
    // Returns false if HDMA for this channel ends (count byte 0, etc.); bytesRead is
    // incremented by the number of table bytes actually consumed (for cycle costing).
    bool hdmaReadLineCount(int ch, Bus& bus, uint32_t& bytesRead);
};
