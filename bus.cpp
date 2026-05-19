#include "bus.hpp"
#include "cpu.hpp"
#include <cstdio>
#include <fstream>

Bus::Bus(const std::vector<uint8_t>& rom, const std::string& savePath)
    : m_rom(rom), m_savePath(savePath) {
    m_mapMode = HeaderParser::detect(m_rom);

    const size_t sramOffset = (m_mapMode == RomMapping::HiROM) ? 0xFFD8 : 0x7FD8;
    if (sramOffset < m_rom.size()) {
        static const size_t kSramTable[] = { 0, 2*1024, 8*1024, 32*1024, 128*1024 };
        const uint8_t raw = m_rom[sramOffset];
        m_sramBytes = (raw < 5) ? kSramTable[raw] : 0;
    }

    if (m_sramBytes > 0) {
        m_sram.assign(m_sramBytes, 0x00);
        if (!m_savePath.empty()) {
            std::ifstream f(m_savePath, std::ios::binary);
            if (f) {
                f.read(reinterpret_cast<char*>(m_sram.data()),
                       static_cast<std::streamsize>(m_sram.size()));
            }
        }
    }

    reset();
}

Bus::~Bus() {
    if (m_sram.empty() || m_savePath.empty()) return;
    std::ofstream f(m_savePath, std::ios::binary);
    if (f) {
        f.write(reinterpret_cast<const char*>(m_sram.data()),
                static_cast<std::streamsize>(m_sram.size()));
    }
}

void Bus::reset() {
    m_wram.fill(0);
    m_wramAddr = 0;
    m_apu.reset();
    m_ppu.reset();
    m_vblankWaiPending = false;
}

static constexpr uint64_t CYCLES_PER_SCANLINE = 114; // ~30000 / 262
static constexpr uint16_t H_TOTAL   = 340;
static constexpr uint16_t V_TOTAL   = 262;
static constexpr uint16_t VBLANK_START = 225;

bool Bus::stepPeripherals(uint64_t totalCycles) {
    const uint64_t delta = totalCycles - m_lastCycles;
    m_lastCycles = totalCycles;

    const uint16_t prevV = m_vCounter;
    const uint16_t prevH = m_hCounter;

    m_cycleAccum += delta;
    while (m_cycleAccum >= CYCLES_PER_SCANLINE) {
        m_cycleAccum -= CYCLES_PER_SCANLINE;
        m_vCounter++;
        if (m_vCounter >= V_TOTAL) {
            m_vCounter = 0;
            m_irqVMatch = false;
        }
    }
    m_hCounter = static_cast<uint16_t>((m_cycleAccum * H_TOTAL) / CYCLES_PER_SCANLINE);

    // Render completed active scanline into PPU framebuffer
    {
        const bool newScanline = (m_vCounter != prevV);
        if (newScanline && prevV < VBLANK_START) {
            m_ppu.renderScanline(static_cast<int>(prevV));
        }
    }

    // IRQ condition
    if (m_irqMode != 0) {
        const bool newScanline = (m_vCounter != prevV);
        if (newScanline) {
            m_irqVMatch = (m_vCounter == m_vtime);
            if (m_irqMode == 2 && m_irqVMatch) {   // V-only: fire at scanline start
                m_irqFlag = true;
                m_irqPending = true;
            }
        }
        const bool hEdge = (prevH < m_htime && m_hCounter >= m_htime);
        if (hEdge) {
            if (m_irqMode == 1)                     // H-only
                { m_irqFlag = true; m_irqPending = true; }
            if (m_irqMode == 3 && m_irqVMatch)     // H+V
                { m_irqFlag = true; m_irqPending = true; }
        }
    }

    // VBlank / NMI
    const bool nowVBlank = (m_vCounter >= VBLANK_START);
    const bool vBlankEdge = nowVBlank && !m_inVBlank;
    m_inVBlank = nowVBlank;

    if (vBlankEdge) {
        m_nmiFlag = true;
        m_vblankWaiPending = true;
        if (m_nmiEnabled) {
            m_apu.step();
            return true;
        }
    }

    m_apu.step();
    return false;
}

void Bus::syncWaiAfterVblankEdge(CPU& cpu) {
    if (!m_vblankWaiPending) return;
    m_vblankWaiPending = false;
    if (!cpu.waiting()) return;
    // NMI not asserted to CPU (NMITIMEN): still leave WAI like IRQ with I=1 on 65C816,
    // otherwise setFadeEffectEx / WaitForVBlank loops never advance.
    cpu.wakeFromWaiSilently();
}

void Bus::setJoy1(uint16_t state) { m_joy1 = state; }

bool Bus::takePendingIrq() {
    const bool pending = m_irqPending;
    m_irqPending = false;
    return pending;
}

RomMapping Bus::mapMode() const { return m_mapMode; }
size_t Bus::sramBytes() const { return m_sramBytes; }


bool Bus::isLoRomArea(uint8_t bank, uint16_t addr) const {
    (void)bank;
    return addr >= 0x8000;
}

uint32_t Bus::loRomToFileOffset(uint8_t bank, uint16_t addr) const {
    return (static_cast<uint32_t>(bank & 0x7F) << 15)
         | static_cast<uint32_t>(addr - 0x8000);
}

bool Bus::isHiRomArea(uint8_t bank, uint16_t addr) const {
    if (bank >= 0xC0)                        return true; // $C0-$FF full banks
    if (bank >= 0x40 && bank <= 0x7D)        return true; // $40-$7D full banks (large ROMs)
    if (addr >= 0x8000)                      return true; // $00-$3F / $80-$BF upper half
    return false;
}

uint32_t Bus::hiRomToFileOffset(uint8_t bank, uint16_t addr) const {
    return static_cast<uint32_t>(bank & 0x3F) * 0x10000
         + static_cast<uint32_t>(addr);
}

uint8_t Bus::read(uint8_t bank, uint16_t addr) const {
    // ------------------------------------------------------------
    // WRAM full banks
    // ------------------------------------------------------------
    if (bank == 0x7E) {
        return m_wram[addr];
    }

    if (bank == 0x7F) {
        return m_wram[0x10000 + addr];
    }

    // ------------------------------------------------------------
    // WRAM mirrors in low banks
    // ------------------------------------------------------------
    if (((bank <= 0x3F) || (bank >= 0x80 && bank <= 0xBF)) && addr <= 0x1FFF) {
        return m_wram[addr];
    }

    // ------------------------------------------------------------
    // Hardware patches / temporary bring-up hacks
    // ------------------------------------------------------------

    // PPU registers ($2100-$213F) — Bus handles the H/V latch ports inline below;
    // everything else in the range goes to Ppu.
    if (addr >= 0x2100 && addr <= 0x213F) {
        // Fall through to the per-address checks below for $213C/$213D/$213E/$213F
        // so that the Bus-owned latch state is used.  All others → Ppu.
        if (addr != 0x213C && addr != 0x213D && addr != 0x213E && addr != 0x213F) {
            return m_ppu.readReg(addr);
        }
    }

    // APU I/O ports ($2140-$2143)
    if (((bank <= 0x3F) || (bank >= 0x80 && bank <= 0xBF)) &&
        addr >= 0x2140 && addr <= 0x2143) {
        return m_apu.readPort(addr);
    }

    // WRAM data port ($2180 WMDATA) — read with auto-increment
    if (addr == 0x2180) {
        const uint8_t val = m_wram[m_wramAddr & 0x1FFFF];
        m_wramAddr = (m_wramAddr + 1) & 0x1FFFF;
        return val;
    }

    // V/H counter latch reads ($213C OPHCT, $213D OPVCT)
    if (addr == 0x213C) {
        const uint8_t result = m_hvcLatch ? static_cast<uint8_t>(m_hCounter >> 8) : static_cast<uint8_t>(m_hCounter & 0xFF);
        m_hvcLatch = !m_hvcLatch;
        return result;
    }
    if (addr == 0x213D) {
        const uint8_t result = m_hvcLatch ? static_cast<uint8_t>(m_vCounter >> 8) : static_cast<uint8_t>(m_vCounter & 0xFF);
        m_hvcLatch = !m_hvcLatch;
        return result;
    }
    if (addr == 0x213E) return 0x01;
    if (addr == 0x213F) { m_hvcLatch = false; return 0x02; }

    // NMI flag — bit 7 set at VBlank, cleared on read; bits 3:0 = CPU version (2)
    if (addr == 0x4210) {
        const uint8_t result = (m_nmiFlag ? 0x80 : 0x00) | 0x02;
        m_nmiFlag = false;
        return result;
    }

    // TIMEUP — IRQ flag, cleared on read
    if (addr == 0x4211) {
        const uint8_t result = m_irqFlag ? 0x80 : 0x00;
        m_irqFlag = false;
        return result;
    }

    // HVBJOY — bit 7 = VBlank, bit 6 = HBlank, bit 0 = auto-joypad busy
    if (addr == 0x4212) {
        uint8_t status = 0x00;
        if (m_vCounter >= VBLANK_START) status |= 0x80;
        if (m_hCounter >= 274)          status |= 0x40;
        return status;
    }

    // Auto-joypad read results
    if (addr == 0x4218) return static_cast<uint8_t>(m_joy1 & 0xFF);
    if (addr == 0x4219) return static_cast<uint8_t>(m_joy1 >> 8);
    if (addr == 0x421A || addr == 0x421B) return 0x00; // controller 2

    // DMA channel registers $4300-$437F (read)
    if (addr >= 0x4300 && addr <= 0x437F) {
        const uint8_t ch  = static_cast<uint8_t>((addr - 0x4300) >> 4);
        const uint8_t reg = static_cast<uint8_t>((addr - 0x4300) & 0x0F);
        return (reg < 8) ? m_dma.readReg(ch, reg) : 0xFF;
    }

    // Multiply/divide result registers
    if (addr == 0x4214) return static_cast<uint8_t>(m_rddiv & 0xFF);
    if (addr == 0x4215) return static_cast<uint8_t>(m_rddiv >> 8);
    if (addr == 0x4216) return static_cast<uint8_t>(m_rdmpy & 0xFF);
    if (addr == 0x4217) return static_cast<uint8_t>(m_rdmpy >> 8);

    // ------------------------------------------------------------
    // SRAM
    // ------------------------------------------------------------
    if (!m_sram.empty()) {
        if (m_mapMode == RomMapping::LoROM &&
            bank >= 0x70 && bank <= 0x7D && addr < 0x8000) {
            const size_t off = (static_cast<size_t>(bank - 0x70) * 0x8000 + addr) % m_sramBytes;
            return m_sram[off];
        }
        if (m_mapMode == RomMapping::HiROM &&
            ((bank >= 0x20 && bank <= 0x3F) || (bank >= 0xA0 && bank <= 0xBF)) &&
            addr >= 0x6000 && addr < 0x8000) {
            const size_t off = (static_cast<size_t>(bank & 0x1F) * 0x2000 + (addr - 0x6000)) % m_sramBytes;
            return m_sram[off];
        }
    }

    // ------------------------------------------------------------
    // ROM area — LoROM or HiROM
    // ------------------------------------------------------------
    if (m_mapMode == RomMapping::HiROM) {
        if (isHiRomArea(bank, addr)) {
            const uint32_t offset = hiRomToFileOffset(bank, addr);
            if (offset < m_rom.size()) return m_rom[offset];
            return 0xFF;
        }
    } else {
        if (isLoRomArea(bank, addr)) {
            const uint32_t offset = loRomToFileOffset(bank, addr);
            if (offset < m_rom.size()) return m_rom[offset];
            return 0xFF;
        }
    }

    return 0x00;
}

void Bus::write(uint8_t bank, uint16_t addr, uint8_t value) {
    // ------------------------------------------------------------
    // WRAM full banks
    // ------------------------------------------------------------
    if (bank == 0x7E) {
        m_wram[addr] = value;
        return;
    }

    if (bank == 0x7F) {
        m_wram[0x10000 + addr] = value;
        return;
    }

    // ------------------------------------------------------------
    // WRAM mirrors
    // ------------------------------------------------------------
    if (((bank <= 0x3F) || (bank >= 0x80 && bank <= 0xBF)) && addr <= 0x1FFF) {
        m_wram[addr] = value;
        return;
    }

    // ------------------------------------------------------------
    // PPU registers ($2100-$213F)
    // ------------------------------------------------------------
    if (addr >= 0x2100 && addr <= 0x213F) {
        m_ppu.writeReg(addr, value);
        return;
    }

    // ------------------------------------------------------------
    // APU I/O ports ($2140-$2143)
    // ------------------------------------------------------------
    if (((bank <= 0x3F) || (bank >= 0x80 && bank <= 0xBF)) &&
        addr >= 0x2140 && addr <= 0x2143) {
        m_apu.writePort(addr, value);
        return;
    }

    // ------------------------------------------------------------
    // WRAM access ports ($2180-$2183)
    // ------------------------------------------------------------
    if (addr == 0x2180) {                          // WMDATA — write + auto-increment
        m_wram[m_wramAddr & 0x1FFFF] = value;
        m_wramAddr = (m_wramAddr + 1) & 0x1FFFF;
        return;
    }
    if (addr == 0x2181) { m_wramAddr = (m_wramAddr & 0x1FF00) | value;                          return; }
    if (addr == 0x2182) { m_wramAddr = (m_wramAddr & 0x100FF) | (static_cast<uint32_t>(value) << 8); return; }
    if (addr == 0x2183) { m_wramAddr = (m_wramAddr & 0x0FFFF) | ((value & 0x01) << 16);         return; }

    // ------------------------------------------------------------
    // NMITIMEN — NMI/IRQ/auto-joypad enable
    // ------------------------------------------------------------
    if (addr == 0x4200) {
        m_nmiEnabled = (value >> 7) & 1;
        m_irqMode    = (value >> 4) & 0x3;
        return;
    }

    // IRQ timer targets
    if (addr == 0x4207) { m_htime = (m_htime & 0x0100) | value; return; }
    if (addr == 0x4208) { m_htime = (m_htime & 0x00FF) | ((value & 0x01) << 8); return; }
    if (addr == 0x4209) { m_vtime = (m_vtime & 0x0100) | value; return; }
    if (addr == 0x420A) { m_vtime = (m_vtime & 0x00FF) | ((value & 0x01) << 8); return; }

    // ------------------------------------------------------------
    // Hardware multiply/divide unit
    // ------------------------------------------------------------
    if (addr == 0x4202) { m_wrmpya = value; return; }
    if (addr == 0x4203) {
        m_rdmpy = static_cast<uint16_t>(m_wrmpya) * static_cast<uint16_t>(value);
        m_rddiv = 0;
        return;
    }
    if (addr == 0x4204) { m_wrdiv = (m_wrdiv & 0xFF00) | value; return; }
    if (addr == 0x4205) { m_wrdiv = (m_wrdiv & 0x00FF) | (static_cast<uint16_t>(value) << 8); return; }
    if (addr == 0x4206) {
        if (value == 0) {
            m_rddiv = 0xFFFF;
            m_rdmpy = m_wrdiv;
        } else {
            m_rddiv = m_wrdiv / value;
            m_rdmpy = m_wrdiv % value;
        }
        return;
    }

    // ------------------------------------------------------------
    // DMA trigger ($420B)
    // ------------------------------------------------------------
    if (addr == 0x420B) {
        if (value) {
            // One-time trace of each unique $420B call (up to 16 calls total)
            if (m_dmaTraceCount < 16) {
                ++m_dmaTraceCount;
                std::fprintf(stderr, "[DMA#%u] $420B=$%02X  VMAIN=$%02X  VMADD=$%04X  VWr=%u\n",
                    m_dmaTraceCount, value,
                    m_ppu.vmain(), m_ppu.vramAddr(), m_ppu.vramWrites());
                for (int ch = 0; ch < 8; ++ch) {
                    if (!(value & (1 << ch))) continue;
                    const uint8_t  ctrl  = m_dma.readReg(ch, 0);
                    const uint8_t  bbus  = m_dma.readReg(ch, 1);
                    const uint16_t src   = static_cast<uint16_t>(
                        m_dma.readReg(ch, 2) | (m_dma.readReg(ch, 3) << 8));
                    const uint8_t  bank  = m_dma.readReg(ch, 4);
                    const uint16_t len   = static_cast<uint16_t>(
                        m_dma.readReg(ch, 5) | (m_dma.readReg(ch, 6) << 8));
                    // Sample the first 8 source bytes so we can see what the DMA actually reads
                    uint8_t s[8];
                    for (int i = 0; i < 8; ++i)
                        s[i] = read(bank, static_cast<uint16_t>(src + i));
                    std::fprintf(stderr,
                        "  ch%d ctrl=$%02X bBus=$%02X src=$%02X:%04X len=%u"
                        "  srcBytes[0..7]=%02X %02X %02X %02X %02X %02X %02X %02X\n",
                        ch, ctrl, bbus, bank, src, len,
                        s[0],s[1],s[2],s[3],s[4],s[5],s[6],s[7]);
                }
                m_dma.trigger(value, *this);
                // Post-DMA VRAM snapshot at key addresses + first non-zero word
                const uint16_t* vr = m_ppu.vram();
                uint16_t firstNZ = 0xFFFF;
                for (uint16_t i = 0; i < 0x8000; ++i) {
                    if (vr[i]) { firstNZ = i; break; }
                }
                std::fprintf(stderr,
                    "  post-DMA VWr=%u  firstNZ@%04X  @0000=%04X %04X  @2000=%04X %04X  @6800=%04X %04X\n",
                    m_ppu.vramWrites(), firstNZ,
                    vr[0x0000], vr[0x0001],
                    vr[0x2000], vr[0x2001],
                    vr[0x6800], vr[0x6801]);
            } else {
                m_dma.trigger(value, *this);
            }
        }
        return;
    }

    // DMA channel registers $4300-$437F
    if (addr >= 0x4300 && addr <= 0x437F) {
        const uint8_t ch  = static_cast<uint8_t>((addr - 0x4300) >> 4);
        const uint8_t reg = static_cast<uint8_t>((addr - 0x4300) & 0x0F);
        if (reg < 8) m_dma.writeReg(ch, reg, value);
        return;
    }

    // SRAM
    if (!m_sram.empty()) {
        if (m_mapMode == RomMapping::LoROM &&
            bank >= 0x70 && bank <= 0x7D && addr < 0x8000) {
            const size_t off = (static_cast<size_t>(bank - 0x70) * 0x8000 + addr) % m_sramBytes;
            m_sram[off] = value;
            return;
        }
        if (m_mapMode == RomMapping::HiROM &&
            ((bank >= 0x20 && bank <= 0x3F) || (bank >= 0xA0 && bank <= 0xBF)) &&
            addr >= 0x6000 && addr < 0x8000) {
            const size_t off = (static_cast<size_t>(bank & 0x1F) * 0x2000 + (addr - 0x6000)) % m_sramBytes;
            m_sram[off] = value;
            return;
        }
    }

    // ROM area ignored on write
}