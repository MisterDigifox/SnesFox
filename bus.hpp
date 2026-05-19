#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "apu_io.hpp"
#include "dma.hpp"
#include "header.hpp"
#include "ppu.hpp"

class CPU;

class Bus {
public:
    explicit Bus(const std::vector<uint8_t>& rom, const std::string& savePath = "");
    ~Bus();

    void reset();
    // Returns true when VBlank starts and NMI should be delivered
    bool stepPeripherals(uint64_t totalCycles);

    // After stepPeripherals + optional triggerNmi/triggerIrq, call once per CPU step.
    // Wakes WAI on VBlank edges when NMITIMEN masks NMI (65C816 libs use WAI in WaitForVBlank).
    void syncWaiAfterVblankEdge(CPU& cpu);

    uint8_t read(uint8_t bank, uint16_t addr) const;
    void write(uint8_t bank, uint16_t addr, uint8_t value);

    RomMapping mapMode() const;
    size_t sramBytes() const;
    bool takePendingIrq();
    void setJoy1(uint16_t state);
    Ppu& ppu() { return m_ppu; }
    const Ppu& ppu() const { return m_ppu; }

private:
    const std::vector<uint8_t>& m_rom;

    // 128 KB WRAM
    std::array<uint8_t, 128 * 1024> m_wram{};

    ApuIo m_apu;
    Dma   m_dma;
    Ppu   m_ppu;

    RomMapping m_mapMode = RomMapping::LoROM;
    size_t m_sramBytes = 0;

    bool m_nmiEnabled = false;
    mutable bool m_nmiFlag = false;
    bool             m_vblankWaiPending = false;

    // V/H counters
    uint16_t m_hCounter   = 0;
    uint16_t m_vCounter   = 0;
    uint64_t m_lastCycles = 0;
    uint64_t m_cycleAccum = 0;
    mutable bool m_hvcLatch = false;
    bool m_inVBlank = false;

    // IRQ
    uint8_t  m_irqMode    = 0;
    uint16_t m_htime      = 0x01FF;
    uint16_t m_vtime      = 0x01FF;
    mutable bool m_irqFlag = false;
    bool         m_irqPending = false;
    bool     m_irqVMatch  = false;

    // Joypad
    uint16_t m_joy1 = 0;

    // SRAM
    std::vector<uint8_t> m_sram;
    std::string m_savePath;

    // Hardware multiply/divide unit
    uint8_t  m_wrmpya = 0xFF;
    uint16_t m_wrdiv  = 0xFFFF;
    uint16_t m_rddiv  = 0;
    uint16_t m_rdmpy  = 0;

    // WRAM access ports ($2180-$2183)
    mutable uint32_t m_wramAddr = 0; // 17-bit address (0-0x1FFFF)

    // DMA diagnostic
    uint8_t m_dmaTraceCount = 0;

    bool isLoRomArea(uint8_t bank, uint16_t addr) const;
    uint32_t loRomToFileOffset(uint8_t bank, uint16_t addr) const;
    bool isHiRomArea(uint8_t bank, uint16_t addr) const;
    uint32_t hiRomToFileOffset(uint8_t bank, uint16_t addr) const;
};