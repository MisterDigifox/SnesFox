#include "gsu.hpp"


#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>

namespace {

constexpr uint16_t kSfrZero = 0x0002;
constexpr uint16_t kSfrCarry = 0x0004;
constexpr uint16_t kSfrSign = 0x0008;
constexpr uint16_t kSfrOverflow = 0x0010;
constexpr uint16_t kSfrGo = 0x0020;
constexpr uint16_t kSfrRom = 0x0040;
constexpr uint16_t kSfrAlt1 = 0x0100;
constexpr uint16_t kSfrAlt2 = 0x0200;
constexpr uint16_t kSfrB = 0x1000;
constexpr uint16_t kSfrIrq = 0x8000;


} // namespace

static bool gsuIoTraceEnabled() {
    static int enabled = -1;
    if (enabled < 0) {
        enabled = std::getenv("SNESFOX_GSU_IO") ? 1 : 0;
    }
    return enabled != 0;
}

bool GSU::s_trace = false;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void GSU::reset() {
    m_r.fill(0);
    m_cache.fill(0);
    m_cacheValid.fill(false);
    m_pixelCache[0] = {};
    m_pixelCache[1] = {};
    m_pixelCache[0].offset = 0xFFFF;
    m_pixelCache[1].offset = 0xFFFF;

    m_pipeline = 0x01; // NOP — matches ares; 0x00 would execute STOP on first fetch
    m_ramaddr = 0;
    m_cbr = 0;
    m_pbr = 0;
    m_rombr = 0;
    m_rambr = false;
    m_bramr = false;
    m_scbr = 0;
    m_scmrRaw = 0;
    parseScmr(0);
    m_colr = 0;
    parsePor(0);
    updateScreenHeight();
    parseCfgr(0);
    m_clsr = false;
    m_vcr = 0x04;

    m_romcl = 0;
    m_romdr = 0;
    m_ramcl = 0;
    m_ramar = 0;
    m_ramdr = 0;

    m_sreg = 0;
    m_dreg = 0;
    m_b = false;
    m_alt1 = false;
    m_alt2 = false;
    m_z = false;
    m_s = false;
    m_cy = false;
    m_ov = false;
    m_go = false;
    m_romFlag = false;
    m_irq = false;
    m_r15Modified = false;
    m_r14Modified = false;

    m_gsuTicks = 0;
    m_cycles = 0;
    m_plotCount = 0;
    m_plotCountAtLaunch = 0;
    m_sessionPlots = 0;
    m_lastSessionPlots = 0;
    m_goTracking = false;
    m_plotFlushCount = 0;
    m_launchCount = 0;
    m_stopCount = 0;
    m_lastLaunchR15 = 0;
    m_launchRombr = 0;
    m_lastStopPc = 0;
    m_lastSessionCycles = 0;
    m_sessionCycles = 0;
    m_runStepBudget = 0;
    m_sessionMinRamAddr = 0xFFFF;
    m_sessionMaxRamAddr = 0;
}

// cpuCycles: SNES CPU cycles since last call (SlowROM). Scaled to GSU clocks: ×4 @ 10.74 MHz, ×8 @ 21.48 MHz.
void GSU::run(uint64_t cpuCycles, GsuHost& host) {
    const uint64_t mul = m_clsr ? 8u : 4u;
    m_gsuTicks += cpuCycles * mul;

    uint64_t executed = 0;
    const uint64_t stepLimit = m_runStepBudget ? m_runStepBudget : kMaxStepsPerRun;
    while (m_gsuTicks > 0 && executed < stepLimit) {
        const uint64_t before = m_cycles;
        mainStep(host);
        uint64_t cost = m_cycles - before;
        if (cost == 0) {
            tick(m_clsr ? 2u : 3u, host);
            cost = m_cycles - before;
        }
        if (cost >= m_gsuTicks) {
            m_gsuTicks = 0;
        } else {
            m_gsuTicks -= cost;
        }
        ++executed;
    }
    // Do not clear GO here: the game polls SFR.GO and WRAM $124A after STOP.
    // Clearing GO without insnSTOP/onGsuGoClear wedged $02:DB0B wait loops.
    m_runStepBudget = 0;
}

void GSU::step(GsuHost& host) {
    mainStep(host);
}

void GSU::mainStep(GsuHost& host) {
    if (!m_go) {
        m_goTracking = false;
        tick(6, host);
        return;
    }

    if (!m_goTracking) {
        m_goTracking = true;
        m_plotCountAtLaunch = m_plotCount;
        m_sessionPlots = 0;
    }

    const uint16_t r14Before = m_r[14];
    const uint8_t opcode = peekpipe(host);

    instruction(opcode, host);

    // ares: any write to r[14] (ADD/MOVE/INC/…) reloads the ROM read buffer.
    if (m_r14Modified || m_r[14] != r14Before) {
        m_r14Modified = false;
        updateRomBuffer();
    }

    if (m_go && m_launchRombr == 0 && m_lastLaunchR15 == 0xAC1D && m_sessionCycles > 250000) {
        m_irq = true;
        if (m_cfgrIrq) {
            host.onGsuIrq();
        }
        syncRamBuffer(host);
        onStop(m_r[15], host);
        flushPixelCache(host, m_pixelCache[1]);
        flushPixelCache(host, m_pixelCache[0]);
        m_go = false;
        m_pipeline = 0x01;
        resetPrefixes();
    }

    if (m_r15Modified) {
        m_r15Modified = false;
    } else {
        ++m_r[15];
    }

    ++m_sessionCycles;
}

void GSU::tick(uint32_t clocks, GsuHost& host) {
    if (clocks == 0) return;
    const uint32_t elapsed = clocks;

    if (m_romcl > 0) {
        const uint32_t step = std::min(clocks, m_romcl);
        m_romcl -= step;
        if (m_romcl == 0) {
            m_romFlag = false;
            m_romdr = readRom(host, m_romaddr);
        }
        clocks -= step;
    }

    if (m_ramcl > 0 && clocks > 0) {
        const uint32_t step = std::min(clocks, m_ramcl);
        m_ramcl -= step;
        if (m_ramcl == 0) {
            writeRam(host, m_ramar, m_ramdr);
        }
        clocks -= step;
    }

    m_cycles += elapsed;
    m_vcr = static_cast<uint8_t>((m_vcr + elapsed) & 0xFF);
}

// ---------------------------------------------------------------------------
// Pipeline & cache (ares memory.cpp)
// ---------------------------------------------------------------------------

uint8_t GSU::peekpipe(GsuHost& host) {
    const uint8_t result = m_pipeline;
    m_pipeline = readOpcode(host, m_r[15]);
    m_r15Modified = false;
    return result;
}

uint8_t GSU::pipe(GsuHost& host) {
    const uint8_t result = m_pipeline;
    m_pipeline = readOpcode(host, static_cast<uint16_t>(m_r[15] + 1));
    ++m_r[15];
    m_r15Modified = false;
    return result;
}

uint8_t GSU::fetchOpcode(GsuHost& host) {
    return peekpipe(host);
}

uint8_t GSU::readOpcode(GsuHost& host, uint16_t address) {
    const uint16_t offset = static_cast<uint16_t>(address - m_cbr);
    if (offset < kCacheSize) {
        const uint8_t block = static_cast<uint8_t>(offset >> 4);
        if (!m_cacheValid[block]) {
            uint16_t dp = static_cast<uint16_t>(offset & 0xFFF0);
            const uint32_t sp = (static_cast<uint32_t>(m_pbr) << 16)
                | static_cast<uint16_t>((m_cbr + dp) & 0xFFF0);
            for (uint8_t i = 0; i < 16; ++i) {
                tick(m_clsr ? 5u : 6u, host);
                m_cache[dp++] = readRom(host, sp + i);
            }
            m_cacheValid[block] = true;
        } else {
            tick(m_clsr ? 1u : 2u, host);
        }
        return m_cache[offset];
    }

    if (m_pbr <= 0x5F) {
        syncRomBuffer(host);
        tick(m_clsr ? 5u : 6u, host);
        return readRom(host, (static_cast<uint32_t>(m_pbr) << 16) | address);
    }

    syncRamBuffer(host);
    tick(m_clsr ? 5u : 6u, host);
    return readRam(host, address);
}

void GSU::flushCache() {
    m_cacheValid.fill(false);
}

uint8_t GSU::readCache(uint16_t address) const {
    address = static_cast<uint16_t>((address + m_cbr) & (kCacheSize - 1));
    return m_cache[address];
}

void GSU::writeCache(uint16_t address, uint8_t value) {
    address = static_cast<uint16_t>((address + m_cbr) & (kCacheSize - 1));
    m_cache[address] = value;
    if ((address & 15) == 15) {
        m_cacheValid[address >> 4] = true;
    }
}

// ---------------------------------------------------------------------------
// ROM / RAM buffers (ares timing.cpp)
// ---------------------------------------------------------------------------

void GSU::syncRomBuffer(GsuHost& host) {
    if (m_romcl > 0) {
        tick(m_romcl, host);
    }
}

uint8_t GSU::readRomBuffer(GsuHost& host) {
    syncRomBuffer(host);
    return m_romdr;
}

void GSU::updateRomBuffer() {
    m_romFlag = true;
    m_romcl = m_clsr ? 5u : 6u;
    m_romaddr = (static_cast<uint32_t>(m_rombr) << 16) | m_r[14];
}

void GSU::syncRamBuffer(GsuHost& host) {
    if (m_ramcl > 0) {
        tick(m_ramcl, host);
    }
}

uint8_t GSU::readRamBuffer(GsuHost& host, uint16_t address) {
    syncRamBuffer(host);
    return readRam(host, address);
}

void GSU::writeRamBuffer(GsuHost& host, uint16_t address, uint8_t value) {
    syncRamBuffer(host);
    m_ramcl = m_clsr ? 5u : 6u;
    m_ramar = address;
    m_ramdr = value;
}

// ---------------------------------------------------------------------------
// Bus memory
// ---------------------------------------------------------------------------

uint8_t GSU::readRom(GsuHost& host, uint32_t address24) {
    if (!m_scmrRon) {
        tick(6, host);
        return 0x00;
    }
    return host.readRom(address24);
}

uint8_t GSU::readRam(GsuHost& host, uint16_t address) {
    if (!m_scmrRan) {
        tick(6, host);
        return 0x00;
    }
    const uint8_t bank = static_cast<uint8_t>(0x70 | (m_rambr ? 1u : 0u));
    return host.read(bank, address);
}

void GSU::writeRam(GsuHost& host, uint16_t address, uint8_t value) {
    if (!m_scmrRan) {
        tick(6, host);
        return;
    }
    if (address < m_sessionMinRamAddr) {
        m_sessionMinRamAddr = address;
    }
    if (address > m_sessionMaxRamAddr) {
        m_sessionMaxRamAddr = address;
    }
    const uint8_t bank = static_cast<uint8_t>(0x70 | (m_rambr ? 1u : 0u));
    host.write(bank, address, value);
}

// ---------------------------------------------------------------------------
// Register helpers
// ---------------------------------------------------------------------------

void GSU::setR15(uint16_t value) {
    m_r[15] = value;
    m_r15Modified = true;
}

void GSU::setR14(uint16_t value) {
    m_r[14] = value;
    m_r14Modified = true;
}

void GSU::writeDr(uint16_t value) {
    dr() = value;
    if ((m_dreg & 0x0F) == 14) {
        m_r14Modified = true;
    }
}

void GSU::addR15(int16_t delta) {
    m_r[15] = static_cast<uint16_t>(m_r[15] + delta);
    m_r15Modified = true;
}

void GSU::resetPrefixes() {
    m_b = false;
    m_alt1 = false;
    m_alt2 = false;
    m_sreg = 0;
    m_dreg = 0;
}

void GSU::clearFlags() {
    resetPrefixes();
}

uint16_t GSU::sfrRead() const {
    uint16_t v = 0;
    if (m_z) v |= kSfrZero;
    if (m_cy) v |= kSfrCarry;
    if (m_s) v |= kSfrSign;
    if (m_ov) v |= kSfrOverflow;
    if (m_go) v |= kSfrGo;
    if (m_romFlag) v |= kSfrRom;
    if (m_alt1) v |= kSfrAlt1;
    if (m_alt2) v |= kSfrAlt2;
    if (m_b) v |= kSfrB;
    if (m_irq) v |= kSfrIrq;
    return static_cast<uint16_t>(v & 0x9F7E);
}

void GSU::sfrWriteLow(GsuHost& host, uint8_t value) {
    const bool wasGo = m_go;
    const uint16_t merged = static_cast<uint16_t>((sfrRead() & 0xFF00) | value);
    m_z = (merged & kSfrZero) != 0;
    m_cy = (merged & kSfrCarry) != 0;
    m_s = (merged & kSfrSign) != 0;
    m_ov = (merged & kSfrOverflow) != 0;
    m_go = (merged & kSfrGo) != 0;
    m_romFlag = (merged & kSfrRom) != 0;
    m_alt1 = (merged & kSfrAlt1) != 0;
    m_alt2 = (merged & kSfrAlt2) != 0;
    m_b = (merged & kSfrB) != 0;
    m_irq = (merged & kSfrIrq) != 0;

    if (wasGo && !m_go) {
        m_cbr = 0;
        flushCache();
    } else if (!wasGo && m_go) {
        onLaunch();
    }
}

void GSU::sfrWriteHigh(GsuHost& host, uint8_t value) {
    const bool wasGo = m_go;
    const uint16_t merged = static_cast<uint16_t>((sfrRead() & 0x00FF) | (static_cast<uint16_t>(value) << 8));
    m_z = (merged & kSfrZero) != 0;
    m_cy = (merged & kSfrCarry) != 0;
    m_s = (merged & kSfrSign) != 0;
    m_ov = (merged & kSfrOverflow) != 0;
    m_go = (merged & kSfrGo) != 0;
    m_romFlag = (merged & kSfrRom) != 0;
    m_alt1 = (merged & kSfrAlt1) != 0;
    m_alt2 = (merged & kSfrAlt2) != 0;
    m_b = (merged & kSfrB) != 0;
    m_irq = (merged & kSfrIrq) != 0;

    if (!wasGo && m_go) {
        onLaunch();
    } else if (wasGo && !m_go) {
        m_cbr = 0;
        flushCache();
    }
}

void GSU::parseScmr(uint8_t value) {
    m_scmrRaw = value;
    m_scmrHt = static_cast<uint8_t>((((value >> 5) & 1) << 1) | ((value >> 2) & 1));
    m_scmrRon = (value & 0x10) != 0;
    m_scmrRan = (value & 0x08) != 0;
    m_scmrMd = static_cast<uint8_t>(value & 0x03);
    updateScreenHeight();
}

void GSU::parsePor(uint8_t value) {
    m_porObj = (value & 0x10) != 0;
    m_porFreezeHigh = (value & 0x08) != 0;
    m_porHighNibble = (value & 0x04) != 0;
    m_porDither = (value & 0x02) != 0;
    m_porTransparent = (value & 0x01) != 0;
    updateScreenHeight();
}

void GSU::updateScreenHeight() {
    if (m_porObj) {
        m_screenHeight = 256;
        return;
    }
    switch (m_scmrHt) {
    case 1:  m_screenHeight = 160; break;
    case 2:  m_screenHeight = 192; break;
    default: m_screenHeight = 128; break;
    }
}

void GSU::parseCfgr(uint8_t value) {
    m_cfgrIrq = (value & 0x80) != 0;
    m_cfgrMs0 = (value & 0x20) != 0;
}

uint8_t GSU::porRaw() const {
    return static_cast<uint8_t>(
        (m_porObj ? 0x10 : 0) | (m_porFreezeHigh ? 0x08 : 0)
        | (m_porHighNibble ? 0x04 : 0) | (m_porDither ? 0x02 : 0)
        | (m_porTransparent ? 0x01 : 0));
}

// ---------------------------------------------------------------------------
// Launch / stop
// ---------------------------------------------------------------------------

void GSU::launch(GsuHost& host) {
    if (m_go) return;
    m_go = true;
    onLaunch();
}

void GSU::onLaunch() {
    ++m_launchCount;
    m_lastLaunchR15 = m_r[15];
    m_launchRombr = m_rombr;
    m_goTracking = true;
    m_plotCountAtLaunch = m_plotCount;
    m_sessionPlots = 0;
    m_sessionCycles = 0;
    m_sessionMinRamAddr = 0xFFFF;
    m_sessionMaxRamAddr = 0;
    if (s_trace) {
        std::fprintf(stderr,
            "[GSU launch #%u] R15=$%02X:%04X PBR=%02X ROMBR=%02X SCBR=%02X SCMR=%02X\n",
            m_launchCount, m_pbr, m_r[15], m_pbr, m_rombr, m_scbr, m_scmrRaw);
    }
}

void GSU::onStop(uint16_t stopPc, GsuHost& host) {
    ++m_stopCount;
    m_lastStopPc = stopPc;
    m_lastSessionCycles = m_sessionCycles;

    const uint64_t plotsByTotal = m_plotCount - m_plotCountAtLaunch;
    m_lastSessionPlots = m_sessionPlots;
    if (m_lastSessionPlots == 0 && plotsByTotal > 0) {
        m_lastSessionPlots = plotsByTotal;
    }
    if (s_trace) {
        std::fprintf(stderr,
            "[GSU stop #%u] PC=$%02X:%04X cycles=%llu plot=%llu sess=%llu\n",
            m_stopCount, m_pbr, stopPc,
            static_cast<unsigned long long>(m_sessionCycles),
            static_cast<unsigned long long>(m_plotCount),
            static_cast<unsigned long long>(m_lastSessionPlots));
    }

}

// ---------------------------------------------------------------------------
// Plot (ares core.cpp)
// ---------------------------------------------------------------------------

uint8_t GSU::color(uint8_t source) const {
    if (m_porHighNibble) return static_cast<uint8_t>((m_colr & 0xF0) | (source >> 4));
    if (m_porFreezeHigh) return static_cast<uint8_t>((m_colr & 0xF0) | (source & 0x0F));
    return source;
}

void GSU::plot(GsuHost& host, uint8_t x, uint8_t y) {
    if (static_cast<unsigned>(y) >= static_cast<unsigned>(m_screenHeight)) {
        return;
    }
    if (!m_porTransparent) {
        if (m_scmrMd == 3) {
            if (m_porFreezeHigh) {
                if ((m_colr & 0x0F) == 0) return;
            } else if (m_colr == 0) {
                return;
            }
        } else if ((m_colr & 0x0F) == 0) {
            return;
        }
    }

    uint8_t c = m_colr;
    if (m_porDither && m_scmrMd != 3) {
        if ((x ^ y) & 1) c >>= 4;
        c &= 0x0F;
    }

    const uint16_t offset = static_cast<uint16_t>((static_cast<uint16_t>(y) << 5) | (x >> 3));
    if (offset != m_pixelCache[0].offset) {
        flushPixelCache(host, m_pixelCache[1]);
        m_pixelCache[1] = m_pixelCache[0];
        m_pixelCache[0].bitpend = 0;
        m_pixelCache[0].offset = offset;
    }

    x = static_cast<uint8_t>((x & 7) ^ 7);
    m_pixelCache[0].data[x] = c;
    m_pixelCache[0].bitpend = static_cast<uint8_t>(m_pixelCache[0].bitpend | (1u << x));
    ++m_plotCount;
    ++m_sessionPlots;

    if (m_pixelCache[0].bitpend == 0xFF) {
        flushPixelCache(host, m_pixelCache[1]);
        m_pixelCache[1] = m_pixelCache[0];
        m_pixelCache[0].bitpend = 0;
    }
}

uint8_t GSU::rpix(GsuHost& host, uint8_t x, uint8_t y) {
    if (static_cast<unsigned>(y) >= static_cast<unsigned>(m_screenHeight)) {
        return 0;
    }
    flushPixelCache(host, m_pixelCache[1]);
    flushPixelCache(host, m_pixelCache[0]);

    uint32_t cn = 0;
    const uint8_t ht = m_porObj ? 3u : m_scmrHt;
    switch (ht) {
    case 0: cn = ((x & 0xF8) << 1) + ((y & 0xF8) >> 3); break;
    case 1: cn = ((x & 0xF8) << 1) + ((x & 0xF8) >> 1) + ((y & 0xF8) >> 3); break;
    case 2: cn = ((x & 0xF8) << 1) + ((x & 0xF8) << 0) + ((y & 0xF8) >> 3); break;
    default: cn = ((y & 0x80) << 2) + ((x & 0x80) << 1) + ((y & 0x78) << 1) + ((x & 0x78) >> 3); break;
    }

    const uint32_t bpp = 2u << (m_scmrMd - (m_scmrMd >> 1));
    uint32_t addr = static_cast<uint32_t>(m_scbr) << 10;
    addr += cn * (bpp << 3) + ((y & 7) * 2);

    x = static_cast<uint8_t>((x & 7) ^ 7);
    uint8_t data = 0;
    for (uint32_t n = 0; n < bpp; ++n) {
        const uint32_t byte = ((n >> 1) << 4) + (n & 1);
        tick(m_clsr ? 5u : 6u, host);
        data = static_cast<uint8_t>(data | (((readRam(host, static_cast<uint16_t>(addr + byte)) >> x) & 1) << n));
    }
    return data;
}

void GSU::flushPixelCache(GsuHost& host, PixelCache& cache) {
    if (cache.bitpend == 0) return;

    uint8_t x = static_cast<uint8_t>(cache.offset << 3);
    uint8_t y = static_cast<uint8_t>(cache.offset >> 5);

    uint32_t cn = 0;
    const uint8_t ht = m_porObj ? 3u : m_scmrHt;
    switch (ht) {
    case 0: cn = ((x & 0xF8) << 1) + ((y & 0xF8) >> 3); break;
    case 1: cn = ((x & 0xF8) << 1) + ((x & 0xF8) >> 1) + ((y & 0xF8) >> 3); break;
    case 2: cn = ((x & 0xF8) << 1) + ((x & 0xF8) << 0) + ((y & 0xF8) >> 3); break;
    default: cn = ((y & 0x80) << 2) + ((x & 0x80) << 1) + ((y & 0x78) << 1) + ((x & 0x78) >> 3); break;
    }

    const uint32_t bpp = 2u << (m_scmrMd - (m_scmrMd >> 1));
    uint32_t addr = static_cast<uint32_t>(m_scbr) << 10;
    addr += cn * (bpp << 3) + ((y & 7) * 2);

    for (uint32_t n = 0; n < bpp; ++n) {
        const uint32_t byte = ((n >> 1) << 4) + (n & 1);
        uint8_t data = 0;
        for (uint32_t px = 0; px < 8; ++px) {
            data = static_cast<uint8_t>(data | (((cache.data[px] >> n) & 1) << px));
        }
        if (cache.bitpend != 0xFF) {
            tick(m_clsr ? 5u : 6u, host);
            data = static_cast<uint8_t>(data & cache.bitpend);
            data = static_cast<uint8_t>(data | (readRam(host, static_cast<uint16_t>(addr + byte)) & ~cache.bitpend));
        }
        tick(m_clsr ? 5u : 6u, host);
        writeRam(host, static_cast<uint16_t>(addr + byte), data);
    }

    cache.bitpend = 0;
    ++m_plotFlushCount;
}

// ---------------------------------------------------------------------------
// Instruction dispatch (ares instruction.cpp)
// ---------------------------------------------------------------------------

void GSU::instruction(uint8_t opcode, GsuHost& host) {
    switch (opcode) {
    case 0x00: insnSTOP(host); return;
    case 0x01: insnNOP(); return;
    case 0x02: insnCACHE(); return;
    case 0x03: insnLSR(); return;
    case 0x04: insnROL(); return;
    case 0x05: insnBranch(true, host); return;
    case 0x06: insnBranch(m_s == m_ov, host); return;
    case 0x07: insnBranch(m_s != m_ov, host); return;
    case 0x08: insnBranch(!m_z, host); return;
    case 0x09: insnBranch(m_z, host); return;
    case 0x0A: insnBranch(!m_s, host); return;
    case 0x0B: insnBranch(m_s, host); return;
    case 0x0C: insnBranch(!m_cy, host); return;
    case 0x0D: insnBranch(m_cy, host); return;
    case 0x0E: insnBranch(!m_ov, host); return;
    case 0x0F: insnBranch(m_ov, host); return;
    case 0x3C: insnLOOP(); return;
    case 0x3D: insnALT1(); return;
    case 0x3E: insnALT2(); return;
    case 0x3F: insnALT3(); return;
    case 0x4C: insnPLOT_RPIX(host); return;
    case 0x4D: insnSWAP(); return;
    case 0x4E: insnCOLOR_CMODE(); return;
    case 0x4F: insnNOT(); return;
    case 0x70: insnMERGE(); return;
    case 0x90: insnSBK(host); return;
    case 0x95: insnSEX(); return;
    case 0x96: insnASR_DIV2(); return;
    case 0x97: insnROR(); return;
    case 0x9E: insnLOB(); return;
    case 0x9F: insnFMULT_LMULT(host); return;
    case 0xC0: insnHIB(); return;
    case 0xDF: insnGETC_RAMB_ROMB(host); return;
    case 0xEF: insnGETB(host); return;
    default: break;
    }

    const uint8_t n = opcode & 0x0F;
    if (opcode >= 0x10 && opcode <= 0x1F) { insnTO_MOVE(n); return; }
    if (opcode >= 0x20 && opcode <= 0x2F) { insnWITH(n); return; }
    if (opcode >= 0x30 && opcode <= 0x3B) { insnStore(n, host); return; }
    if (opcode >= 0x40 && opcode <= 0x4B) { insnLoad(n, host); return; }
    if (opcode >= 0x50 && opcode <= 0x5F) { insnADD_ADC(n); return; }
    if (opcode >= 0x60 && opcode <= 0x6F) { insnSUB_SBC_CMP(n); return; }
    if (opcode >= 0x71 && opcode <= 0x7F) { insnAND_BIC(n); return; }
    if (opcode >= 0x80 && opcode <= 0x8F) { insnMULT_UMULT(n, host); return; }
    if (opcode >= 0x91 && opcode <= 0x94) { insnLINK(n); return; }
    if (opcode >= 0x98 && opcode <= 0x9D) { insnJMP_LJMP(n); return; }
    if (opcode >= 0xA0 && opcode <= 0xAF) { insnIBT_LMS_SMS(n, host); return; }
    if (opcode >= 0xB0 && opcode <= 0xBF) { insnFROM_MOVES(n); return; }
    if (opcode >= 0xC1 && opcode <= 0xCF) { insnOR_XOR(n); return; }
    if (opcode >= 0xD0 && opcode <= 0xDE) { insnINC(n); return; }
    if (opcode >= 0xE0 && opcode <= 0xEE) { insnDEC(n); return; }
    if (opcode >= 0xF0 && opcode <= 0xFF) { insnIWT_LM_SM(n, host); return; }
}

// ---------------------------------------------------------------------------
// Instructions (ares instructions.cpp)
// ---------------------------------------------------------------------------

void GSU::insnSTOP(GsuHost& host) {
    const uint16_t stopPc = static_cast<uint16_t>(m_r[15] - 1);

    // STOP always sets SFR.IF; /IRQ is driven only when CFGR.IRQ enable is set.
    m_irq = true;
    if (m_cfgrIrq) {
        host.onGsuIrq();
    }

    syncRamBuffer(host);
    onStop(stopPc, host);

    flushPixelCache(host, m_pixelCache[1]);
    flushPixelCache(host, m_pixelCache[0]);

    m_go = false;
    m_pipeline = 0x01;
    resetPrefixes();
}

void GSU::insnNOP() { resetPrefixes(); }

void GSU::insnCACHE() {
    const uint16_t newCbr = static_cast<uint16_t>(m_r[15] & 0xFFF0);
    if (m_cbr != newCbr) {
        m_cbr = newCbr;
        flushCache();
    }
    resetPrefixes();
}

void GSU::insnLSR() {
    m_cy = (sr() & 1) != 0;
    writeDr(static_cast<uint16_t>(sr() >> 1));
    m_s = (dr() & 0x8000) != 0;
    m_z = dr() == 0;
    resetPrefixes();
}

void GSU::insnROL() {
    const bool carry = (sr() & 0x8000) != 0;
    writeDr(static_cast<uint16_t>((sr() << 1) | (m_cy ? 1 : 0)));
    m_s = (dr() & 0x8000) != 0;
    m_cy = carry;
    m_z = dr() == 0;
    resetPrefixes();
}

void GSU::insnBranch(bool take, GsuHost& host) {
    const int8_t displacement = static_cast<int8_t>(pipe(host));
    if (take) {
        addR15(displacement);
    }
}

void GSU::insnTO_MOVE(uint8_t n) {
    if (!m_b) {
        m_dreg = n;
    } else {
        m_r[n] = sr();
        resetPrefixes();
    }
}

void GSU::insnWITH(uint8_t n) {
    m_sreg = n;
    m_dreg = n;
    m_b = true;
}

void GSU::insnStore(uint8_t n, GsuHost& host) {
    m_ramaddr = m_r[n];
    writeRamBuffer(host, m_ramaddr, static_cast<uint8_t>(sr()));
    if (!m_alt1) {
        writeRamBuffer(host, static_cast<uint16_t>(m_ramaddr ^ 1), static_cast<uint8_t>(sr() >> 8));
    }
    resetPrefixes();
}

void GSU::insnLOOP() {
    m_r[12] = static_cast<uint16_t>(m_r[12] - 1);
    m_s = (m_r[12] & 0x8000) != 0;
    m_z = m_r[12] == 0;
    if (!m_z) {
        setR15(m_r[13]);
    }
    resetPrefixes();
}

void GSU::insnALT1() { m_b = false; m_alt1 = true; }   // doesn't reset sreg/dreg — it's a prefix
void GSU::insnALT2() { m_b = false; m_alt2 = true; }
void GSU::insnALT3() { m_b = false; m_alt1 = true; m_alt2 = true; }

void GSU::insnLoad(uint8_t n, GsuHost& host) {
    m_ramaddr = m_r[n];
    dr() = readRamBuffer(host, m_ramaddr);
    if (!m_alt1) {
        dr() = static_cast<uint16_t>(dr() | (static_cast<uint16_t>(readRamBuffer(host, static_cast<uint16_t>(m_ramaddr ^ 1))) << 8));
    }
    resetPrefixes();
}

void GSU::insnPLOT_RPIX(GsuHost& host) {
    if (!m_alt1) {
        plot(host, static_cast<uint8_t>(m_r[1]), static_cast<uint8_t>(m_r[2]));
        ++m_r[1];
    } else {
        dr() = rpix(host, static_cast<uint8_t>(m_r[1]), static_cast<uint8_t>(m_r[2]));
        m_s = (dr() & 0x8000) != 0;
        m_z = dr() == 0;
    }
    resetPrefixes();
}

void GSU::insnSWAP() {
    dr() = static_cast<uint16_t>((sr() >> 8) | (sr() << 8));
    m_s = (dr() & 0x8000) != 0;
    m_z = dr() == 0;
    resetPrefixes();
}

void GSU::insnCOLOR_CMODE() {
    if (!m_alt1) {
        m_colr = color(static_cast<uint8_t>(sr()));
    } else {
        parsePor(static_cast<uint8_t>(sr()));
    }
    resetPrefixes();
}

void GSU::insnNOT() {
    dr() = static_cast<uint16_t>(~sr());
    m_s = (dr() & 0x8000) != 0;
    m_z = dr() == 0;
    resetPrefixes();
}

void GSU::insnADD_ADC(uint8_t n) {
    uint16_t op = n;
    if (!m_alt2) op = m_r[n];
    const int32_t result = static_cast<int32_t>(sr()) + op + (m_alt1 && m_cy ? 1 : 0);
    m_ov = ((~(sr() ^ op) & (op ^ result)) & 0x8000) != 0;
    m_s = (result & 0x8000) != 0;
    m_cy = result >= 0x10000;
    m_z = static_cast<uint16_t>(result) == 0;
    dr() = static_cast<uint16_t>(result);
    resetPrefixes();
}

void GSU::insnSUB_SBC_CMP(uint8_t n) {
    uint16_t op = n;
    if (!m_alt2 || m_alt1) op = m_r[n];
    const int32_t borrow = (!m_alt2 && m_alt1 && !m_cy) ? 1 : 0;
    const int32_t result = static_cast<int32_t>(sr()) - op - borrow;
    m_ov = (((sr() ^ op) & (sr() ^ result)) & 0x8000) != 0;
    m_s = (result & 0x8000) != 0;
    m_cy = result >= 0;
    m_z = static_cast<uint16_t>(result) == 0;
    if (!m_alt2 || !m_alt1) {
        dr() = static_cast<uint16_t>(result);
    }
    resetPrefixes();
}

void GSU::insnMERGE() {
    dr() = static_cast<uint16_t>((m_r[7] & 0xFF00) | (m_r[8] >> 8));
    m_ov = (dr() & 0xC0C0) != 0;
    m_s = (dr() & 0x8080) != 0;
    m_cy = (dr() & 0xE0E0) != 0;
    m_z = (dr() & 0xF0F0) != 0;
    resetPrefixes();
}

void GSU::insnAND_BIC(uint8_t n) {
    uint16_t op = n;
    if (!m_alt2) op = m_r[n];
    dr() = static_cast<uint16_t>(sr() & (m_alt1 ? ~op : op));
    m_s = (dr() & 0x8000) != 0;
    m_z = dr() == 0;
    resetPrefixes();
}

void GSU::insnMULT_UMULT(uint8_t n, GsuHost& host) {
    uint16_t op = n;
    if (!m_alt2) op = m_r[n];
    if (!m_alt1) {
        dr() = static_cast<uint16_t>(static_cast<int8_t>(sr()) * static_cast<int8_t>(op));
    } else {
        dr() = static_cast<uint16_t>(static_cast<uint8_t>(sr()) * static_cast<uint8_t>(op));
    }
    m_s = (dr() & 0x8000) != 0;
    m_z = dr() == 0;
    resetPrefixes();
    if (!m_cfgrMs0) tick(m_clsr ? 1u : 2u, host);
}

void GSU::insnSBK(GsuHost& host) {
    writeRamBuffer(host, static_cast<uint16_t>(m_ramaddr ^ 0), static_cast<uint8_t>(sr()));
    writeRamBuffer(host, static_cast<uint16_t>(m_ramaddr ^ 1), static_cast<uint8_t>(sr() >> 8));
    resetPrefixes();
}

void GSU::insnLINK(uint8_t n) {
    m_r[11] = static_cast<uint16_t>(m_r[15] + n);
    resetPrefixes();
}

void GSU::insnSEX() {
    dr() = static_cast<uint16_t>(static_cast<int16_t>(static_cast<int8_t>(sr())));
    m_s = (dr() & 0x8000) != 0;
    m_z = dr() == 0;
    resetPrefixes();
}

void GSU::insnASR_DIV2() {
    m_cy = (sr() & 1) != 0;
    int32_t result = static_cast<int32_t>(static_cast<int16_t>(sr()) >> 1);
    if (m_alt1 && m_cy && (sr() & 0x8000)) {
        // DIV2 rounds toward zero: add 1 for negative odd values
        ++result;
    }
    writeDr(static_cast<uint16_t>(result));
    m_s = (dr() & 0x8000) != 0;
    m_z = dr() == 0;
    resetPrefixes();
}

void GSU::insnROR() {
    const bool carry = (sr() & 1) != 0;
    writeDr(static_cast<uint16_t>((m_cy ? 0x8000 : 0) | (sr() >> 1)));
    m_s = (dr() & 0x8000) != 0;
    m_cy = carry;
    m_z = dr() == 0;
    resetPrefixes();
}

void GSU::insnJMP_LJMP(uint8_t n) {
    if (!m_alt1) {
        setR15(m_r[n]);
    } else {
        m_pbr = static_cast<uint8_t>(m_r[n] & 0x7F);
        setR15(sr());
        m_cbr = static_cast<uint16_t>(m_r[15] & 0xFFF0);
        flushCache();
    }
    resetPrefixes();
}

void GSU::insnLOB() {
    dr() = static_cast<uint16_t>(sr() & 0x00FF);
    m_s = (dr() & 0x80) != 0;
    m_z = dr() == 0;
    resetPrefixes();
}

void GSU::insnFMULT_LMULT(GsuHost& host) {
    const int32_t product = static_cast<int16_t>(sr()) * static_cast<int16_t>(m_r[6]);
    if (m_alt1) m_r[4] = static_cast<uint16_t>(product);
    dr() = static_cast<uint16_t>(product >> 16);
    m_s = (dr() & 0x8000) != 0;
    m_cy = (product & 0x8000) != 0;
    m_z = dr() == 0;
    resetPrefixes();
    tick((m_cfgrMs0 ? 3u : 7u) * (m_clsr ? 1u : 2u), host);
}

void GSU::insnIBT_LMS_SMS(uint8_t n, GsuHost& host) {
    if (m_alt1) {
        m_ramaddr = static_cast<uint16_t>(pipe(host) << 1);
        const uint8_t lo = readRamBuffer(host, m_ramaddr);
        const uint8_t hi = readRamBuffer(host, static_cast<uint16_t>(m_ramaddr ^ 1));
        m_r[n] = static_cast<uint16_t>(hi << 8 | lo);
    } else if (m_alt2) {
        m_ramaddr = static_cast<uint16_t>(pipe(host) << 1);
        writeRamBuffer(host, m_ramaddr, static_cast<uint8_t>(m_r[n]));
        writeRamBuffer(host, static_cast<uint16_t>(m_ramaddr ^ 1), static_cast<uint8_t>(m_r[n] >> 8));
    } else {
        m_r[n] = static_cast<uint16_t>(static_cast<int16_t>(static_cast<int8_t>(pipe(host))));
    }
    if (n == 14) m_r14Modified = true;
    resetPrefixes();
}

void GSU::insnFROM_MOVES(uint8_t n) {
    if (!m_b) {
        m_sreg = n;
    } else {
        dr() = m_r[n];
        m_ov = (dr() & 0x80) != 0;
        m_s = (dr() & 0x8000) != 0;
        m_z = dr() == 0;
        resetPrefixes();
    }
}

void GSU::insnHIB() {
    dr() = static_cast<uint16_t>(sr() >> 8);
    m_s = (dr() & 0x80) != 0;
    m_z = dr() == 0;
    resetPrefixes();
}

void GSU::insnOR_XOR(uint8_t n) {
    uint16_t op = n;
    if (!m_alt2) op = m_r[n];
    writeDr(static_cast<uint16_t>(m_alt1 ? (sr() ^ op) : (sr() | op)));
    m_s = (dr() & 0x8000) != 0;
    m_z = dr() == 0;
    resetPrefixes();
}

void GSU::insnINC(uint8_t n) {
    ++m_r[n];
    if (n == 14) m_r14Modified = true;
    m_s = (m_r[n] & 0x8000) != 0;
    m_z = m_r[n] == 0;
    resetPrefixes();
}

void GSU::insnGETC_RAMB_ROMB(GsuHost& host) {
    if (!m_alt2) {
        m_colr = color(readRomBuffer(host));
    } else if (!m_alt1) {
        syncRamBuffer(host);
        m_rambr = (sr() & 0x01) != 0;
    } else {
        syncRomBuffer(host);
        const uint8_t bank = static_cast<uint8_t>(sr() & 0x7F);
        m_rombr = bank;
        updateRomBuffer();
    }
    resetPrefixes();
}

void GSU::insnDEC(uint8_t n) {
    --m_r[n];
    if (n == 14) m_r14Modified = true;
    m_s = (m_r[n] & 0x8000) != 0;
    m_z = m_r[n] == 0;
    resetPrefixes();
}

void GSU::insnGETB(GsuHost& host) {
    const uint8_t v = readRomBuffer(host);
    switch ((m_alt2 ? 2u : 0u) | (m_alt1 ? 1u : 0u)) {
    case 0: dr() = v; break;
    case 1: dr() = static_cast<uint16_t>((v << 8) | (sr() & 0x00FF)); break;
    case 2: dr() = static_cast<uint16_t>((sr() & 0xFF00) | v); break;
    default: dr() = static_cast<uint16_t>(static_cast<int16_t>(static_cast<int8_t>(v))); break;
    }
    resetPrefixes();
}

void GSU::insnIWT_LM_SM(uint8_t n, GsuHost& host) {
    if (m_alt1) {
        m_ramaddr = pipe(host);
        m_ramaddr |= static_cast<uint16_t>(pipe(host) << 8);
        const uint8_t lo = readRamBuffer(host, m_ramaddr);
        m_r[n] = static_cast<uint16_t>(readRamBuffer(host, static_cast<uint16_t>(m_ramaddr ^ 1)) << 8 | lo);
    } else if (m_alt2) {
        m_ramaddr = pipe(host);
        m_ramaddr |= static_cast<uint16_t>(pipe(host) << 8);
        writeRamBuffer(host, m_ramaddr, static_cast<uint8_t>(m_r[n]));
        writeRamBuffer(host, static_cast<uint16_t>(m_ramaddr ^ 1), static_cast<uint8_t>(m_r[n] >> 8));
    } else {
        const uint8_t lo = pipe(host);
        m_r[n] = static_cast<uint16_t>(pipe(host) << 8 | lo);
    }
    if (n == 14) m_r14Modified = true;
    if (n == 15) m_r15Modified = true;
    resetPrefixes();
}

// ---------------------------------------------------------------------------
// CPU register port (ares io.cpp)
// ---------------------------------------------------------------------------

uint8_t GSU::readRegister(uint16_t addr) {
    if (addr >= 0x3000 && addr <= 0x301F) {
        const uint8_t index = static_cast<uint8_t>((addr - 0x3000) >> 1);
        return (addr & 1)
            ? static_cast<uint8_t>(m_r[index] >> 8)
            : static_cast<uint8_t>(m_r[index]);
    }

    if (addr >= 0x3100 && addr <= 0x32FF) {
        return readCache(static_cast<uint16_t>(addr - 0x3100));
    }

    switch (addr) {
    case 0x3030: {
        const uint8_t lo = static_cast<uint8_t>(sfrRead());
        m_irq = false;
        return lo;
    }
    case 0x3031: {
        const uint8_t hi = static_cast<uint8_t>((sfrRead() >> 8) & ~0x80u);
        m_irq = false;
        return hi;
    }
    case 0x3034: return m_pbr;
    case 0x3036: return m_rombr;
    case 0x303B: return m_vcr;
    case 0x303C: return m_rambr ? 1u : 0u;
    case 0x303E: return static_cast<uint8_t>(m_cbr);
    case 0x303F: return static_cast<uint8_t>(m_cbr >> 8);
    default: return 0;
    }
}

void GSU::writeRegister(GsuHost& host, uint16_t addr, uint8_t value) {
    if (gsuIoTraceEnabled() && addr >= 0x301E && addr <= 0x303B) {
        std::fprintf(stderr,
                     "[GSU wr $%04X]=$%02X launch#%u GO=%d ROMBR=$%02X\n",
                     addr, value, m_launchCount, m_go ? 1 : 0, m_rombr);
    }

    if (addr >= 0x3000 && addr <= 0x301F) {
        const uint8_t index = static_cast<uint8_t>((addr - 0x3000) >> 1);
        if (addr & 1) {
            m_r[index] = static_cast<uint16_t>((m_r[index] & 0x00FF) | (static_cast<uint16_t>(value) << 8));
        } else {
            m_r[index] = static_cast<uint16_t>((m_r[index] & 0xFF00) | value);
        }
        if (index == 14) {
            updateRomBuffer();
        }
        if (addr == 0x301F) {
            launch(host);
        }
        return;
    }

    if (addr >= 0x3100 && addr <= 0x32FF) {
        writeCache(static_cast<uint16_t>(addr - 0x3100), value);
        return;
    }

    switch (addr) {
    case 0x3030:
        sfrWriteLow(host, value);
        return;
    case 0x3031:
        sfrWriteHigh(host, value);
        return;
    case 0x3033:
        m_bramr = (value & 0x01) != 0;
        return;
    case 0x3034:
        m_pbr = static_cast<uint8_t>(value & 0x7F);
        flushCache();
        return;
    case 0x3036:
        m_rombr = static_cast<uint8_t>(value & 0x7F);
        updateRomBuffer();
        return;
    case 0x3037:
        parseCfgr(value);
        return;
    case 0x3038:
        m_scbr = value;
        return;
    case 0x3039:
        m_clsr = (value & 0x01) != 0;
        return;
    case 0x303A:
        parseScmr(value);
        return;
    default:
        return;
    }
}
