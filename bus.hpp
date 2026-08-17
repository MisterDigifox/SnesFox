#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "apu.hpp"
#include "dma.hpp"
#include "gsu.hpp"
#include "header.hpp"
#include "ppu.hpp"

class CPU;

class Bus {
public:
    /// CPU-side cycles consumed per emulated scanline (must match `stepPeripherals` accumulator).
    static constexpr uint64_t kCyclesPerScanline = 114;
    /// Total scanlines before V counter wraps (matches internal `m_vCounter` ceiling).
    static constexpr uint16_t kScanlinesPerFrame = 262;
    /// One emulated TV frame — keep equal to `kCyclesPerScanline * kScanlinesPerFrame`.
    static constexpr uint64_t kCyclesPerFrame = kCyclesPerScanline * kScanlinesPerFrame;
    /// Scanline length in CPU::fineCycles()'s finer (un-rounded, "×8") unit — see that
    /// accessor's doc comment. Used only to derive m_hCounter at finer-than-instruction
    /// resolution; scanline/NMI/DMA cadence still runs off kCyclesPerScanline above.
    static constexpr uint64_t kFineCyclesPerScanline = kCyclesPerScanline * 8;

    explicit Bus(const std::vector<uint8_t>& rom, const std::string& savePath = "");
    ~Bus();

    void reset();
    // Returns true when VBlank starts and NMI should be delivered
    bool stepPeripherals(uint64_t totalCycles, uint64_t totalFineCycles);

    // After stepPeripherals + optional triggerNmi/triggerIrq, call once per CPU step.
    // Wakes WAI on VBlank edges when NMITIMEN masks NMI (65C816 libs use WAI in WaitForVBlank).
    void syncWaiAfterVblankEdge(CPU& cpu);

    uint8_t read(uint8_t bank, uint16_t addr) const;
    void write(uint8_t bank, uint16_t addr, uint8_t value);

    /// Memory-access speed in master-clock cycles (6=Fast, 8=Slow, 12=XSlow) for the given
    /// address, per the SNES bus timing table — used to scale CPU cycle cost so that FastROM
    /// ($420D bit0) actually speeds up execution instead of being ignored.
    unsigned accessSpeedCycles(uint8_t bank, uint16_t addr) const;
    bool fastRomEnabled() const { return m_fastRomEnabled; }

    // The CPU is genuinely halted while GP-DMA runs — CPU::step() drains this (real master
    // clocks, accumulated by write()'s $420B handler via Dma::trigger's return value) right
    // after the instruction that triggered it, so its own cycles()/fineCycles() reflect the
    // real elapsed time. Returns and resets to 0.
    uint32_t takeDmaStolenMasterClocks() { return std::exchange(m_dmaStolenMasterClocks, 0u); }

    RomMapping mapMode() const;
    size_t sramBytes() const;
    bool takePendingIrq();
    void setJoy1(uint16_t state);
    void setJoy2(uint16_t state);
    Ppu& ppu() { return m_ppu; }
    const Ppu& ppu() const { return m_ppu; }
    APU& apu() { return m_apu; }
    const APU& apu() const { return m_apu; }

    // Used by BusGsuHost (bus.cpp) to bridge the GSU core to this Bus.
    uint8_t gsuReadRom(uint32_t address24) const;
    void raiseGsuIrq();

    bool hasSuperFx() const { return m_hasSuperFx; }
    const GSU& gsu() const { return m_gsu; }

private:
    const std::vector<uint8_t>& m_rom;

    // 128 KB WRAM
    std::array<uint8_t, 128 * 1024> m_wram{};

    APU m_apu;
    Dma   m_dma;
    Ppu   m_ppu;

    RomMapping m_mapMode = RomMapping::LoROM;
    size_t m_sramBytes = 0;

    bool m_nmiEnabled = false;
    mutable bool m_nmiFlag = false;
    bool             m_vblankWaiPending = false;

    // V/H counters (start at last line so first emulated scan step wraps 261→0 like post-reset HW)
    uint16_t m_hCounter   = 0;
    uint16_t m_vCounter   = 261;
    uint64_t m_lastCycles = 0;
    uint64_t m_cycleAccum = 0;
    uint64_t m_lastFineCycles = 0;
    uint64_t m_fineCycleAccum = 0;
    uint32_t m_dmaStolenMasterClocks = 0;
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
    uint16_t m_joy2 = 0;

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

    // DMA / HDMA ($420C write-only latch)
    uint8_t m_dmaTraceCount = 0;
    uint8_t m_reg420c       = 0;

    // MEMSEL ($420D) — bit0 selects FastROM (6-cycle) vs SlowROM (8-cycle) access
    // for banks 00-3F/80-BF upper half and C0-FF.
    bool m_fastRomEnabled = false;

    bool isLoRomArea(uint8_t bank, uint16_t addr) const;
    uint32_t loRomToFileOffset(uint8_t bank, uint16_t addr) const;
    bool isHiRomArea(uint8_t bank, uint16_t addr) const;
    uint32_t hiRomToFileOffset(uint8_t bank, uint16_t addr) const;

    // ------------------------------------------------------------
    // Super FX / GSU coprocessor
    // ------------------------------------------------------------
    bool m_hasSuperFx = false;
    mutable GSU m_gsu;
    // Banks $70 (0x0000-0xFFFF) + $71 (0x0000-0xFFFF), the GSU's own work RAM —
    // addressed by both the main CPU and the GSU core through this same Bus.
    std::vector<uint8_t> m_gsuRam;
    std::unique_ptr<GsuHost> m_gsuHost;
    bool m_gsuIrqPending = false;
};