#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Memory / IRQ hooks used by the GSU core. The system bus will implement this later.
class GsuHost {
public:
    virtual ~GsuHost() = default;
    virtual uint8_t read(uint8_t bank, uint16_t addr) = 0;
    virtual void write(uint8_t bank, uint16_t addr, uint8_t value) = 0;
    virtual uint8_t readRom(uint32_t address24) = 0;
    virtual void onGsuIrq() {}
};

// Super FX (GSU) coprocessor — ported from ares (ares-emulator/ares).
class GSU final {
public:
    GSU() = default;

    void reset();
    void run(uint64_t cpuCycles, GsuHost& host);
    void step(GsuHost& host);

    uint8_t fetchOpcode(GsuHost& host);
    void launch(GsuHost& host);
    uint8_t readRegister(uint16_t addr);
    void writeRegister(GsuHost& host, uint16_t addr, uint8_t value);

    bool running() const { return m_go; }
    bool clsr() const { return m_clsr; }
    bool irqActive() const { return m_irq; }
    bool irqAssertedToCpu() const { return m_irq && m_cfgrIrq; }
    uint64_t cycles() const { return m_cycles; }
    uint64_t plotCount() const { return m_plotCount; }
    uint64_t sessionPlots() const { return m_sessionPlots; }
    uint64_t lastSessionPlots() const { return m_lastSessionPlots; }
    uint64_t plotFlushCount() const { return m_plotFlushCount; }
    uint32_t launchCount() const { return m_launchCount; }
    uint32_t stopCount() const { return m_stopCount; }
    uint16_t lastLaunchR15() const { return m_lastLaunchR15; }
    uint16_t lastStopPc() const { return m_lastStopPc; }
    uint16_t screenHeight() const { return m_screenHeight; }
    uint64_t lastSessionCycles() const { return m_lastSessionCycles; }
    uint64_t sessionCycles() const { return m_sessionCycles; }
    uint8_t scbr() const { return m_scbr; }
    uint8_t scmr() const { return m_scmrRaw; }
    uint8_t rombr() const { return m_rombr; }
    bool    rambr() const { return m_rambr; }
    bool    porObj() const { return m_porObj; }
    uint16_t ramaddr() const { return m_ramaddr; }
    uint16_t sessionMinRamAddr() const { return m_sessionMinRamAddr; }
    uint16_t sessionMaxRamAddr() const { return m_sessionMaxRamAddr; }
    uint8_t pbr() const { return m_pbr; }
    uint16_t pc() const { return m_r[15]; }
    uint16_t reg(uint8_t index) const { return m_r[index & 0x0F]; }
    void clearFlags();
    bool alt1() const { return m_alt1; }
    bool alt2() const { return m_alt2; }
    uint16_t sfr() const { return sfrRead(); }

    // Debug-only rolling log of executed instructions (for the "GSU Debugger" UI panel) — filled
    // every step while SFR.GO is set, oldest entries overwritten once full. `pc` is the value of
    // r15 immediately after the opcode byte was fetched (matching the real pipelined-fetch
    // convention: the opcode's own address is `pc-1`); `operand1`/`operand2` are peeked directly
    // from ROM (no side effects) so a UI can disassemble without re-deriving fetch timing.
    struct DebugLogEntry {
        uint8_t pbr = 0;
        uint16_t pc = 0;
        uint8_t opcode = 0;
        bool alt1 = false;
        bool alt2 = false;
        uint8_t operand1 = 0;
        uint8_t operand2 = 0;
    };
    static constexpr size_t kDebugLogSize = 48;
    size_t debugLogCount() const { return m_debugLogCount; }
    // index 0 = oldest retained entry, debugLogCount()-1 = most recently executed.
    const DebugLogEntry& debugLogEntry(size_t index) const {
        const size_t start = m_debugLogCount < kDebugLogSize
            ? 0 : (m_debugLogPos + kDebugLogSize - m_debugLogCount) % kDebugLogSize;
        return m_debugLog[(start + index) % kDebugLogSize];
    }

    static void setTrace(bool enabled) { s_trace = enabled; }
    static bool traceEnabled() { return s_trace; }

private:
    static constexpr size_t kCacheSize = 0x0200;
    static constexpr uint64_t kMaxStepsPerRun = 1048576;

    struct PixelCache {
        uint16_t offset = 0xFFFF;
        uint8_t bitpend = 0;
        std::array<uint8_t, 8> data{};
    };

    // --- ares-style execution core ---
    void mainStep(GsuHost& host);
    void tick(uint32_t clocks, GsuHost& host);
    void instruction(uint8_t opcode, GsuHost& host);
    void resetPrefixes();

    uint8_t peekpipe(GsuHost& host);
    uint8_t pipe(GsuHost& host);
    uint8_t readOpcode(GsuHost& host, uint16_t address);

    void flushCache();
    uint8_t readCache(uint16_t address) const;
    void writeCache(uint16_t address, uint8_t value);

    void syncRomBuffer(GsuHost& host);
    uint8_t readRomBuffer(GsuHost& host);
    void updateRomBuffer();

    void syncRamBuffer(GsuHost& host);
    uint8_t readRamBuffer(GsuHost& host, uint16_t address);
    void writeRamBuffer(GsuHost& host, uint16_t address, uint8_t value);

    uint8_t readRom(GsuHost& host, uint32_t address24);
    uint8_t readRam(GsuHost& host, uint16_t address);
    void writeRam(GsuHost& host, uint16_t address, uint8_t value);

    uint16_t& sr() { return m_r[m_sreg & 0x0F]; }
    uint16_t& dr() { return m_r[m_dreg & 0x0F]; }
    void writeDr(uint16_t value);

    void setR15(uint16_t value);
    void setR14(uint16_t value);
    void addR15(int16_t delta);

    uint8_t color(uint8_t source) const;
    void plot(GsuHost& host, uint8_t x, uint8_t y);
    uint8_t rpix(GsuHost& host, uint8_t x, uint8_t y);
    void flushPixelCache(GsuHost& host, PixelCache& cache);
    uint8_t porRaw() const;

    uint16_t sfrRead() const;
    void sfrWriteLow(GsuHost& host, uint8_t value);
    void sfrWriteHigh(GsuHost& host, uint8_t value);
    void parseScmr(uint8_t value);
    void parsePor(uint8_t value);
    void parseCfgr(uint8_t value);
    void updateScreenHeight();

    void onLaunch(GsuHost& host);
    void onStop(uint16_t stopPc, GsuHost& host);

    // Instruction handlers (ares/component/processor/gsu/instructions.cpp)
    void insnSTOP(GsuHost& host);
    void insnNOP();
    void insnCACHE();
    void insnLSR();
    void insnROL();
    void insnBranch(bool take, GsuHost& host);
    void insnTO_MOVE(uint8_t n);
    void insnWITH(uint8_t n);
    void insnStore(uint8_t n, GsuHost& host);
    void insnLOOP();
    void insnALT1();
    void insnALT2();
    void insnALT3();
    void insnLoad(uint8_t n, GsuHost& host);
    void insnPLOT_RPIX(GsuHost& host);
    void insnSWAP();
    void insnCOLOR_CMODE();
    void insnNOT();
    void insnADD_ADC(uint8_t n);
    void insnSUB_SBC_CMP(uint8_t n);
    void insnMERGE();
    void insnAND_BIC(uint8_t n);
    void insnMULT_UMULT(uint8_t n, GsuHost& host);
    void insnSBK(GsuHost& host);
    void insnLINK(uint8_t n);
    void insnSEX();
    void insnASR_DIV2();
    void insnROR();
    void insnJMP_LJMP(uint8_t n);
    void insnLOB();
    void insnFMULT_LMULT(GsuHost& host);
    void insnIBT_LMS_SMS(uint8_t n, GsuHost& host);
    void insnFROM_MOVES(uint8_t n);
    void insnHIB();
    void insnOR_XOR(uint8_t n);
    void insnINC(uint8_t n);
    void insnGETC_RAMB_ROMB(GsuHost& host);
    void insnDEC(uint8_t n);
    void insnGETB(GsuHost& host);
    void insnIWT_LM_SM(uint8_t n, GsuHost& host);

    std::array<uint16_t, 16> m_r{};
    std::array<uint8_t, kCacheSize> m_cache{};
    std::array<bool, 32> m_cacheValid{};
    PixelCache m_pixelCache[2]{};

    uint8_t m_pipeline = 0;
    uint16_t m_ramaddr = 0;
    uint32_t m_romaddr = 0;
    uint16_t m_cbr = 0;
    uint8_t m_pbr = 0;
    uint8_t m_rombr = 0;
    bool m_rambr = false;
    bool m_bramr = false;
    uint8_t m_scbr = 0;
    uint8_t m_scmrRaw = 0;
    uint8_t m_scmrHt = 0;
    uint16_t m_screenHeight = 192;
    bool m_scmrRon = true;
    bool m_scmrRan = true;
    uint8_t m_scmrMd = 0;
    uint8_t m_colr = 0;
    bool m_porObj = false;
    bool m_porFreezeHigh = false;
    bool m_porHighNibble = false;
    bool m_porDither = false;
    bool m_porTransparent = false;
    bool m_cfgrIrq = false;
    bool m_cfgrMs0 = false;
    bool m_clsr = false;
    uint8_t m_vcr = 0;

    uint32_t m_romcl = 0;
    uint8_t m_romdr = 0;
    uint32_t m_ramcl = 0;
    uint16_t m_ramar = 0;
    uint8_t m_ramdr = 0;

    uint32_t m_sreg = 0;
    uint32_t m_dreg = 0;

    bool m_b = false;
    bool m_alt1 = false;
    bool m_alt2 = false;

    std::array<DebugLogEntry, kDebugLogSize> m_debugLog{};
    size_t m_debugLogPos = 0;
    size_t m_debugLogCount = 0;
    bool m_z = false;
    bool m_s = false;
    bool m_cy = false;
    bool m_ov = false;
    bool m_go = false;
    bool m_romFlag = false;
    mutable bool m_irq = false;

    bool m_r15Modified = false;
    bool m_r14Modified = false;

    uint64_t m_gsuTicks = 0;
    uint64_t m_cycles = 0;
    uint64_t m_plotCount = 0;
    uint64_t m_plotCountAtLaunch = 0;
    uint64_t m_sessionPlots = 0;
    uint64_t m_lastSessionPlots = 0;
    bool m_goTracking = false;
    uint64_t m_plotFlushCount = 0;
    uint32_t m_launchCount = 0;
    uint32_t m_stopCount = 0;
    uint16_t m_lastLaunchR15 = 0;
    uint8_t m_launchRombr = 0;
    uint16_t m_lastStopPc = 0;
    uint64_t m_lastSessionCycles = 0;
    uint64_t m_sessionCycles = 0;
    uint64_t m_runStepBudget = 0;
    uint16_t m_sessionMinRamAddr = 0xFFFF;
    uint16_t m_sessionMaxRamAddr = 0;

    static bool s_trace;
};
