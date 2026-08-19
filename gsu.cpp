#include "gsu.hpp"
#include "gsu_disasm.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

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

// Shared by every SCMR/CFGR/etc. mode-change trace site below — was previously re-read via its
// own independent std::getenv() at each of the 4 call sites.
static bool gsuModeTraceEnabled() {
    static const bool enabled = std::getenv("SNESFOX_GSU_MODE_TRACE") != nullptr;
    return enabled;
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
    m_debugLog.fill(DebugLogEntry{});
    m_debugLogPos = 0;
    m_debugLogCount = 0;
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
    const uint16_t pcBefore = m_r[15];
    const uint8_t pbrBefore = m_pbr;
    const uint8_t opcode = peekpipe(host);

    {
        DebugLogEntry entry;
        entry.pbr = pbrBefore;
        entry.pc = pcBefore;
        entry.opcode = opcode;
        entry.alt1 = m_alt1;
        entry.alt2 = m_alt2;
        entry.operand1 = host.readRom((static_cast<uint32_t>(pbrBefore) << 16) | pcBefore);
        entry.operand2 = host.readRom((static_cast<uint32_t>(pbrBefore) << 16) | static_cast<uint16_t>(pcBefore + 1));
        m_debugLog[m_debugLogPos] = entry;
        m_debugLogPos = (m_debugLogPos + 1) % kDebugLogSize;
        if (m_debugLogCount < kDebugLogSize) ++m_debugLogCount;

        // --write-gsu (snesfox_app.cpp): full-session, unbounded twin of the ring-buffer log
        // above — same fields, same gsuDisassemble() call the live UI panel uses, just written
        // out immediately instead of being kept in a fixed-size buffer.
        if (m_traceFile) {
            const uint16_t opcodeAddr = static_cast<uint16_t>(entry.pc - 1);
            const std::string instr = gsuDisassemble(opcodeAddr, entry.opcode, entry.alt1,
                                                       entry.alt2, entry.operand1, entry.operand2);
            std::fprintf(m_traceFile, "$%02X:%04X  %s\n", entry.pbr, opcodeAddr, instr.c_str());
        }
    }

    static int traceLeft = [] {
        const char* v = std::getenv("SNESFOX_GSU_TRACE");
        return v ? std::atoi(v) : 0;
    }();
    if (traceLeft > 0) {
        --traceLeft;
        std::fprintf(stderr, "[GSU %02X:%04X] op=%02X r1=%04X r2=%04X r14=%04X sfr=%04X\n",
                     pbrBefore, pcBefore, opcode, m_r[1], m_r[2], m_r[14], sfrRead());
    }

    // Only starts tracing once PC actually enters [lo,hi] *and* launchCount has reached
    // minLaunch — avoids paying for every earlier launch just to reach one specific hot loop
    // late in boot. SNESFOX_GSU_RANGE_TRACE=lo-hi[:count[:minLaunch]]
    static const char* rangeEnv = std::getenv("SNESFOX_GSU_RANGE_TRACE");
    static uint16_t rangeLo = 0, rangeHi = 0;
    static int rangeCount = 2000;
    static unsigned rangeMinLaunch = 0;
    static bool rangeParsed = false;
    static int rangeLeft = -1;
    if (rangeEnv && !rangeParsed) {
        rangeParsed = true;
        unsigned lo = 0, hi = 0, cnt = 2000, minLaunch = 0;
        if (std::sscanf(rangeEnv, "%x-%x:%d:%u", &lo, &hi, &cnt, &minLaunch) >= 2) {
            rangeLo = static_cast<uint16_t>(lo);
            rangeHi = static_cast<uint16_t>(hi);
            rangeCount = cnt;
            rangeMinLaunch = minLaunch;
        }
    }
    const bool inRange = rangeEnv && m_launchCount >= rangeMinLaunch
        && pcBefore >= rangeLo && pcBefore <= rangeHi;

    // One-shot, unconditional (every launch, not gated by RANGE_TRACE) entry marker for the
    // dlptr-lazy-reinit follow-on block at $01:B2ED, to distinguish "good" (r12 legitimately
    // small, LOOP falls through) visits from the known-bad ones (r12 garbage, LOOP misfires
    // into the unrelated fill-loop template via a stale r13).
    static const bool b2edTrace = std::getenv("SNESFOX_GSU_B2ED_TRACE") != nullptr;
    if (b2edTrace && m_pbr == 0x01 && pcBefore == 0xB2ED) {
        std::fprintf(stderr,
            "[B2ED-ENTRY] launch=%u r2=%04X r7=%04X r9=%04X r12=%04X r13=%04X\n",
            m_launchCount, m_r[2], m_r[7], m_r[9], m_r[12], m_r[13]);
    }

    if (inRange && rangeLeft < 0) {
        rangeLeft = rangeCount;
    }
    // Once triggered, log every instruction gap-free (not just while inside [lo,hi]) so a call
    // out of the window doesn't look like a direct jump to whatever address is logged next.
    if (rangeLeft > 0) {
        --rangeLeft;
        std::fprintf(stderr,
            "[RNG launch=%u %02X:%04X] op=%02X r0=%04X r1=%04X r2=%04X r3=%04X r7=%04X r9=%04X "
            "r10=%04X r11=%04X r12=%04X r13=%04X sreg=%u dreg=%u ramaddr=%04X sfr=%04X\n",
            m_launchCount, pbrBefore, pcBefore, opcode, m_r[0], m_r[1], m_r[2], m_r[3], m_r[7],
            m_r[9], m_r[10], m_r[11], m_r[12], m_r[13],
            m_sreg & 0xF, m_dreg & 0xF, m_ramaddr, sfrRead());
    }

    m_pipeCallCount = 0;
    instruction(opcode, host);

    // ares: any write to r[14] (ADD/MOVE/INC/…) reloads the ROM read buffer.
    if (m_r14Modified || m_r[14] != r14Before) {
        m_r14Modified = false;
        updateRomBuffer();
    }

    // Belt-and-suspenders, matching the r14/updateRomBuffer check above: any instruction whose
    // destination register (via TO/WITH prefix chaining — dr() when m_dreg==15) happens to land
    // on r15 must suppress the auto-increment below, but only a few call sites (setR15/addR15,
    // IWT/LM/SM when n==15) explicitly set m_r15Modified. Plain register-family ops that can
    // also target r15 as their dr() (LOAD chief among them — this is exactly the "to pc / ldw
    // (rsp)" idiom real GSU code uses for a stack-based subroutine return, e.g. mpop pc) do not,
    // so relying on m_r15Modified alone silently incremented a just-restored return address by
    // one. Comparing against the raw pre-instruction pcBefore (instead of auditing/patching every
    // dr()-writing call site) fixed that, but overshot: pipe() itself also advances r15 (once per
    // operand byte fetched), so any multi-byte instruction — IWT/LM/SM/IBT/LMS/SMS, a taken-or-not
    // branch's displacement byte, etc. — makes m_r[15] != pcBefore too, even though nothing
    // "jumped." That's not a real write in ares's sense (ares's own pipe() explicitly resets its
    // Register's modified flag right after advancing r15), so this was wrongly suppressing the
    // final +1 on every such instruction, leaving r15 one byte short and corrupting the next
    // instruction's first operand. Comparing against pcBefore + m_pipeCallCount (the r15 value
    // pipe() naturally advances to, tracked above) instead of raw pcBefore keeps catching a
    // genuine explicit write (LOAD-to-r15, still != the natural value) without misfiring on
    // ordinary operand fetches.
    const uint16_t naturalR15 = static_cast<uint16_t>(pcBefore + m_pipeCallCount);
    if (m_r15Modified || m_r[15] != naturalR15) {
        m_r15Modified = false;
    } else {
        ++m_r[15];
    }

    // POST-execution instruction-boundary trace: m_r[15] here is fully resolved (no pipeline
    // lag) and is genuinely the address of the next instruction about to be queued - unlike
    // pcBefore/opcode above, safe to compare directly against static disassembly addresses.
    static const bool postTrace = std::getenv("SNESFOX_GSU_POST_TRACE") != nullptr;
    if (postTrace && m_pbr == 0x01
        && m_r[15] >= 0xB180 && m_r[15] <= 0xB200) {
        std::fprintf(stderr, "[POST] launch=%u next-pc=$01:%04X r12=%04X r13=%04X\n",
            m_launchCount, m_r[15], m_r[12], m_r[13]);
    }

    ++m_sessionCycles;
}

void GSU::tick(uint32_t clocks, GsuHost& host) {
    if (clocks == 0) return;
    const uint32_t elapsed = clocks;

    // ROM and RAM pending-buffer timers run independently (each counts down the full
    // `clocks` amount, ares timing.cpp's SuperFX::step) — not sequentially off a shared,
    // shrinking budget. Consuming `clocks` on the ROM branch before checking RAM here used
    // to shortchange a simultaneously-pending RAM write's countdown, delaying when it
    // actually commits into RAM (and, transitively, how long syncRamBuffer must wait for a
    // read of that same address to observe the new value instead of stale data).
    if (m_romcl > 0) {
        const uint32_t step = std::min(clocks, m_romcl);
        m_romcl -= step;
        if (m_romcl == 0) {
            m_romFlag = false;
            m_romdr = readRom(host, m_romaddr);
        }
    }

    if (m_ramcl > 0) {
        const uint32_t step = std::min(clocks, m_ramcl);
        m_ramcl -= step;
        if (m_ramcl == 0) {
            writeRam(host, m_ramar, m_ramdr);
        }
    }

    m_cycles += elapsed;
    m_vcr = static_cast<uint8_t>((m_vcr + elapsed) & 0xFF);
}

// ---------------------------------------------------------------------------
// Pipeline & cache (ares memory.cpp)
// ---------------------------------------------------------------------------

uint8_t GSU::peekpipe(GsuHost& host) {
    const uint8_t result = m_pipeline;
    // Matches ares's peekpipe() literally: unconditionally refetch from the current r15. This
    // is safe (doesn't re-read the same byte twice) only because mainStep()'s epilogue always
    // advances r15 by exactly (1 + however many pipe() calls this instruction made) unless r15
    // was genuinely overwritten by a jump/branch/load — see m_pipeCallCount below. An earlier
    // version tried to dodge the "redundant" refetch here with an address-equality check instead
    // of fixing that epilogue accounting; it was solving a symptom, not the cause, and diverged
    // from ares for no benefit once the real bug was found.
    m_pipeline = readOpcode(host, m_r[15]);
    m_r15Modified = false;
    return result;
}

uint8_t GSU::pipe(GsuHost& host) {
    const uint8_t result = m_pipeline;
    const uint16_t fetchAddr = static_cast<uint16_t>(m_r[15] + 1);
    m_pipeline = readOpcode(host, fetchAddr);
    static const bool pipeTrace = std::getenv("SNESFOX_GSU_PIPE_TRACE") != nullptr;
    if (pipeTrace) {
        std::fprintf(stderr, "[PIPE] r15=%04X result=%02X fetchAddr=%04X newPipeline=%02X\n",
            m_r[15], result, fetchAddr, m_pipeline);
    }
    ++m_r[15];
    ++m_pipeCallCount;
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

    // Executing code out of GSU RAM ($60-7F bank range): the fetch bank is PBR itself
    // (whichever RAM bank the code lives in), NOT RAMBR — RAMBR only selects the bank for
    // LOAD/STORE data access, a separate, unrelated register (ares memory.cpp:readOpcode).
    syncRamBuffer(host);
    tick(m_clsr ? 5u : 6u, host);
    if (!m_scmrRan) return 0x00;
    return host.read(m_pbr, address);
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

static void debugGsuRamWatch(uint8_t pbr, uint16_t pc, uint16_t addr, uint8_t value,
                              uint16_t r1, uint16_t r10, uint16_t r11, uint16_t r12,
                              uint16_t r13, uint32_t launchCount) {
    static const char* watchEnv = std::getenv("SNESFOX_GSU_RAM_WATCH");
    static uint16_t watchLo = watchEnv ? static_cast<uint16_t>(std::strtol(watchEnv, nullptr, 16)) : 0xFFFF;
    static uint16_t watchHi = [] {
        if (!watchEnv) return static_cast<uint16_t>(0xFFFE);
        const char* dash = std::strchr(watchEnv, '-');
        return dash ? static_cast<uint16_t>(std::strtol(dash + 1, nullptr, 16))
                    : static_cast<uint16_t>(std::strtol(watchEnv, nullptr, 16));
    }();
    static unsigned long writeSeq = 0;
    ++writeSeq;
    if (watchLo != 0xFFFF && addr >= watchLo && addr <= watchHi) {
        std::fprintf(stderr, "[GSU-RAM-WATCH #%lu] launch=%u pc=$%02X:%04X addr=%04X <= %02X "
            "r1=%04X r10=%04X r11=%04X r12=%04X r13=%04X\n",
            writeSeq, launchCount, pbr, pc, addr, value, r1, r10, r11, r12, r13);
    }
}

void GSU::writeRam(GsuHost& host, uint16_t address, uint8_t value) {
    if (!m_scmrRan) {
        tick(6, host);
        return;
    }
    debugGsuRamWatch(m_pbr, m_r[15], address, value, m_r[1], m_r[10], m_r[11], m_r[12], m_r[13],
        m_launchCount);
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
        onLaunch(host);
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
        onLaunch(host);
    } else if (wasGo && !m_go) {
        m_cbr = 0;
        flushCache();
    }
}

void GSU::parseScmr(uint8_t value) {
    const bool traceMode = gsuModeTraceEnabled();
    if (traceMode && value != m_scmrRaw) {
        std::fprintf(stderr, "[MODE] pc=$%02X:%04X SCMR %02X->%02X (md=%u ht=%u) scbr=%02X rambr=%d\n",
                     m_pbr, m_r[15], m_scmrRaw, value, value & 0x03,
                     static_cast<unsigned>((((value >> 5) & 1) << 1) | ((value >> 2) & 1)),
                     m_scbr, m_rambr ? 1 : 0);
    }
    m_scmrRaw = value;
    m_scmrHt = static_cast<uint8_t>((((value >> 5) & 1) << 1) | ((value >> 2) & 1));
    m_scmrRon = (value & 0x10) != 0;
    m_scmrRan = (value & 0x08) != 0;
    m_scmrMd = static_cast<uint8_t>(value & 0x03);
    updateScreenHeight();
}

void GSU::parsePor(uint8_t value) {
    const bool traceMode = gsuModeTraceEnabled();
    const bool newObj = (value & 0x10) != 0;
    if (traceMode && newObj != m_porObj) {
        std::fprintf(stderr, "[MODE] pc=$%02X:%04X POR.obj %d->%d scbr=%02X rambr=%d\n",
                     m_pbr, m_r[15], m_porObj ? 1 : 0, newObj ? 1 : 0, m_scbr, m_rambr ? 1 : 0);
    }
    m_porObj = newObj;
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
    onLaunch(host);
}

void GSU::onLaunch(GsuHost& host) {
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
    static const bool launchTrace = std::getenv("SNESFOX_GSU_LAUNCH_TRACE") != nullptr;
    if (launchTrace) {
        std::fprintf(stderr,
            "[GSU launch #%u] R15=$%02X:%04X ROMBR=%02X SCBR=%02X SCMR=%02X RAMBR=%d RAN=%d RON=%d\n",
            m_launchCount, m_pbr, m_r[15], m_rombr, m_scbr, m_scmrRaw,
            m_rambr ? 1 : 0, m_scmrRan ? 1 : 0, m_scmrRon ? 1 : 0);
    }

    // Targeted dump for StarFox's mdo_3d_display ($AC1D) — dumps the world rotation matrix,
    // RNG seed, and dust-loop state right at launch, to check for a degenerate matrix causing
    // mshowdust's out-of-range retry loop (.ov1/.ov2/.ov3 in mgdots.mc) to never resolve.
    static const bool dustTrace = std::getenv("SNESFOX_GSU_DUST_TRACE") != nullptr;
    if (dustTrace && m_r[15] == 0xAC1D) {
        auto rd16 = [&](uint16_t addr) -> int16_t {
            const uint8_t lo = host.read(0x70, addr);
            const uint8_t hi = host.read(0x70, static_cast<uint16_t>(addr + 1));
            return static_cast<int16_t>(static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8));
        };
        std::fprintf(stderr,
            "[GSU dust @launch #%u] wmat11=%d wmat12=%d wmat13=%d wmat21=%d wmat22=%d wmat23=%d "
            "wmat31=%d wmat32=%d wmat33=%d rand=%d cnt=%d dotsorstars=%d viewpos=(%d,%d,%d)\n",
            m_launchCount,
            rd16(0x00D2), rd16(0x00D4), rd16(0x00D6),
            rd16(0x00D8), rd16(0x00DA), rd16(0x00DC),
            rd16(0x00DE), rd16(0x00E0), rd16(0x00E2),
            rd16(0x0140), rd16(0x0040), rd16(0x019E),
            rd16(0x00C6), rd16(0x00C8), rd16(0x00CA));
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
    static int plotTraceLeft = [] {
        const char* v = std::getenv("SNESFOX_GSU_PLOT_TRACE");
        return v ? std::atoi(v) : 0;
    }();
    if (plotTraceLeft > 0) {
        --plotTraceLeft;
        std::fprintf(stderr, "[PLOT] pc=$%02X:%04X x=%3u y=%3u colr=%02X scbr=%02X\n",
                     m_pbr, m_r[15], x, y, m_colr, m_scbr);
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
    static const char* traceEnv = std::getenv("SNESFOX_GSU_CACHE_TRACE");
    static long traceMinLaunch = traceEnv ? std::strtol(traceEnv, nullptr, 10) : -1;
    if (traceEnv && static_cast<long>(m_launchCount) >= traceMinLaunch
        && m_pbr == 0x01 && m_r[15] >= 0xB0A0 && m_r[15] <= 0xB0FF) {
        std::fprintf(stderr,
            "[CACHE-ENTRY] launch=%u pc=$%02X:%04X r1=%04X r10=%04X r11=%04X r12=%04X r13=%04X\n",
            m_launchCount, m_pbr, m_r[15], m_r[1], m_r[10], m_r[11], m_r[12], m_r[13]);
    }

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

    static const char* traceEnv = std::getenv("SNESFOX_GSU_STW_TRACE");
    if (traceEnv && n == 1 && m_pbr == 0x01 && m_r[15] >= 0xB0A0 && m_r[15] <= 0xB0FF) {
        static uint16_t lastAddr = 0;
        static bool haveLast = false;
        static uint32_t lastLaunch = 0xFFFFFFFFu;
        const int32_t delta = haveLast
            ? (static_cast<int32_t>(m_ramaddr) - static_cast<int32_t>(lastAddr)) : 0;
        const bool launchChanged = (lastLaunch != m_launchCount);
        // Normal steady-state is +2 per store (one INC R1 in the loop body, one in the delay
        // slot) within the same launch; only log discontinuities/launch boundaries so this stays
        // a sparse signal instead of one line per store.
        if (!haveLast || delta != 2 || launchChanged) {
            std::fprintf(stderr,
                "[STW] launch=%u pc=$%02X:%04X addr=%04X r12=%04X r13=%04X (delta=%d launchChanged=%d)\n",
                m_launchCount, m_pbr, m_r[15], m_ramaddr, m_r[12], m_r[13], delta, launchChanged ? 1 : 0);
        }
        lastAddr = m_ramaddr;
        haveLast = true;
        lastLaunch = m_launchCount;
    }

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

    static const char* traceEnv = std::getenv("SNESFOX_GSU_LOOP_TRACE");
    static long traceMinLaunch = traceEnv ? std::strtol(traceEnv, nullptr, 10) : -1;
    if (traceEnv && static_cast<long>(m_launchCount) >= traceMinLaunch
        && (m_r[12] <= 5 || m_r[12] >= 0xFFFB)) {
        std::fprintf(stderr,
            "[LOOP] launch=%u pc=$%02X:%04X r12=%04X z=%d take=%d r13=%04X r1=%04X r11=%04X\n",
            m_launchCount, m_pbr, m_r[15], m_r[12], m_z ? 1 : 0, m_z ? 0 : 1, m_r[13], m_r[1], m_r[11]);
    }

    // Catches a LOOP instruction whose branch target (r13) happens to equal one of the two
    // known fill-loop entry addresses while the LOOP itself is executing from OUTSIDE that
    // fill-loop's own code range — i.e. some unrelated routine's LOOP, using a stale r13 left
    // over from this fill-loop's last "TO R13" many launches earlier, accidentally jumping in.
    static const char* foreignEnv = std::getenv("SNESFOX_GSU_FOREIGN_LOOP_TRACE");
    if (foreignEnv && !m_z && (m_r[13] == 0xB0C5 || m_r[13] == 0xB0DC)
        && !(m_pbr == 0x01 && m_r[15] >= 0xB0A0 && m_r[15] <= 0xB0FF)) {
        std::fprintf(stderr,
            "[FOREIGN-LOOP] launch=%u from=$%02X:%04X -> r13=%04X r1=%04X r10=%04X r11=%04X r12=%04X\n",
            m_launchCount, m_pbr, m_r[15], m_r[13], m_r[1], m_r[10], m_r[11], m_r[12]);
        for (size_t i = 0; i < debugLogCount(); ++i) {
            const DebugLogEntry& e = debugLogEntry(i);
            std::fprintf(stderr, "  [%2zu] $%02X:%04X op=%02X alt1=%d alt2=%d opnd=%02X %02X\n",
                i, e.pbr, e.pc, e.opcode, e.alt1 ? 1 : 0, e.alt2 ? 1 : 0, e.operand1, e.operand2);
        }
    }

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
        const bool traceMode = gsuModeTraceEnabled();
        const bool newRambr = (sr() & 0x01) != 0;
        if (traceMode && newRambr != m_rambr) {
            std::fprintf(stderr, "[MODE] pc=$%02X:%04X RAMBR %d->%d scbr=%02X\n",
                         m_pbr, m_r[15], m_rambr ? 1 : 0, newRambr ? 1 : 0, m_scbr);
        }
        m_rambr = newRambr;
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
        return static_cast<uint8_t>(sfrRead());
    }
    case 0x3031: {
        const uint8_t hi = static_cast<uint8_t>(sfrRead() >> 8);
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
    if (gsuIoTraceEnabled() && addr >= 0x3000 && addr <= 0x303B) {
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
    case 0x3038: {
        const bool traceMode = gsuModeTraceEnabled();
        if (traceMode && value != m_scbr) {
            std::fprintf(stderr, "[MODE] pc=$%02X:%04X SCBR %02X->%02X\n", m_pbr, m_r[15], m_scbr, value);
        }
        m_scbr = value;
        return;
    }
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
