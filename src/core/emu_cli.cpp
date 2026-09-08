#include "emu_cli.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <SDL2/SDL.h>

#include "audio_output.hpp"
#include "bus.hpp"
#include "cli_common.hpp"
#include "cpu.hpp"
#include "debug_panel_builder.hpp"
#include "display.hpp"
#include "header.hpp"
#include "../macOS/display_link.hpp"
#include "../macOS/native_file_dialog.hpp"
#include "opcodes.hpp"
#include "rom.hpp"
#include "rom_cli_helpers.hpp"

// Window title / app name. Overridden at compile time by release-game-binary.sh
// (-DSNESFOX_APP_NAME="\"RomName\"") so kiosk builds show the embedded game's name instead.
#ifndef SNESFOX_APP_NAME
#define SNESFOX_APP_NAME "snesfox"
#endif

namespace {

constexpr uint64_t CYCLES_PER_FRAME = Bus::kCyclesPerFrame;
// "Step" for the paused APU advances it by roughly one average main-CPU instruction's worth
// of cycles (matching the CLAUDE.md-documented 8-cycle SlowROM access cost), rather than a
// full frame — enough to see S-DSP Voices/ARAM change by a small, watchable increment.
constexpr uint64_t APU_MANUAL_STEP_CYCLES = 8;

constexpr int LOG_SIZE = 4;

std::string formatDisasmLine(uint32_t pc24, const CPU& cpu, bool isCurrent = false) {
    std::ostringstream oss;
    oss << (isCurrent ? "> " : "  ");
    oss << "$" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << ((pc24 >> 16) & 0xFF) << ":" << std::setw(4) << (pc24 & 0xFFFF) << "  ";
    oss << std::left << std::setw(10) << std::setfill(' ') << cpu.bytes();
    oss << " " << cpu.instruction();
    return oss.str();
}

} // namespace

int runEmu(const std::string& initialRomPath, bool writeTrace, bool debugUi) {
    printMissingCpuOpcodes(cpuOpcodesTable);

    // --log-cpu: dump every 65816 instruction actually executed by the CPU, in order, as
    // disassembly text, to cpu.asm — a full execution trace rather than a fixed-size ring
    // buffer like instructionLog below. Opened once for the whole session (truncates any
    // previous cpu.asm) and reused across ROM swaps via the "Load" button.
    std::ofstream traceFile;
    if (writeTrace) {
        traceFile.open("cpu.asm", std::ios::out | std::ios::trunc);
        if (!traceFile) {
            std::cerr << "--log-cpu: could not open cpu.asm for writing\n";
        }
    }

    // Display/audio persist across a ROM swap (the "Load" button) — only the ROM-derived
    // state below gets torn down and rebuilt, same as this function's original one-shot
    // setup used to do exactly once.
    Display display(SNESFOX_APP_NAME, debugUi);
#ifdef SNESFOX_KIOSK_MODE
    removeDefaultWindowMenu(); // no File > Open ROM… item either — see release-game-binary.sh
#else
    installOpenRomMenu(); // native File > Open ROM… menu item, works in both debug and bare mode
#endif
    AudioOutput audio;

    std::string romPath = initialRomPath;

    for (;;) {
        if (romPath.empty()) {
            // No ROM path was given on the command line — show just the toolbar (and an
            // otherwise-black framebuffer) until Load is clicked or the window is closed.
            // Bus/CPU construction below is simply deferred until romPath is non-empty;
            // Display/audio already exist independent of any ROM (same objects the
            // Load-triggered ROM swap below reuses).
            bool waitingForRom = true;
            while (waitingForRom) {
                DebugAction action = DebugAction::None;
                if (!display.processEvents(action)) {
                    return 0;
                }

                if (debugUi) {
                    display.beginFrame();
                    const DebugAction uiAction = display.drawControls(false);
                    if (uiAction != DebugAction::None) action = uiAction;
                }
                if (action == DebugAction::LoadRom) {
                    romPath = display.pendingRomLoadPath();
                }
                if (const auto menuPath = takeMenuOpenRomPath()) {
                    romPath = *menuPath;
                }

                if (debugUi) {
                    DebugPanel panel;
                    panel.sections.push_back(DebugSection{"ROM", {"No ROM loaded : click Load to open a .sfc file"}});
                    display.presentWithFrame(nullptr, panel);
                } else {
                    display.presentNativeFrame(nullptr);
                }

                waitingForRom = romPath.empty();
            }
            continue;
        }

        Rom rom(romPath);
        const auto& data = rom.data();
        if (data.size() < 0x10000) {
            throw std::runtime_error("ROM: unexpected size");
        }

        printRomInfo(rom, data);

        const auto headerLines = HeaderParser::toLines(data);
        const RomMapping mapping = HeaderParser::detect(data);
        const bool isLoRom = (mapping == RomMapping::LoROM);
        const uint16_t resetVector = readResetVector(data, isLoRom);

        std::string savePath = romPath;
        const auto dot = savePath.rfind('.');
        if (dot != std::string::npos)
            savePath.replace(dot, std::string::npos, ".sav");
        else
            savePath += ".sav";

        Bus bus(data, savePath);
        bus.reset();

        CPU cpu;
        cpu.reset(bus, resetVector);

        std::deque<std::string> instructionLog;
        bool paused = false;
        bool stepOnce = false;
        bool nextFrameOnce = false;
        bool loadRequested = false;
        audio.clearQueue();

        auto logInstruction = [&](uint32_t pcBefore) {
            if (!instructionLog.empty() && instructionLog.front().rfind("> ", 0) == 0) {
                instructionLog.front().replace(0, 2, "  ");
            }
            instructionLog.push_front(formatDisasmLine(pcBefore, cpu, true));
            if (instructionLog.size() > LOG_SIZE) {
                instructionLog.pop_back();
            }
        };

        if (!debugUi) {
            // Bare game window: decouple emulation from presentation the way Mesen2's SDL
            // renderer does — a dedicated emulation thread produces frames into a lock-protected
            // buffer at a precise 60.0988Hz cadence; the main thread only pumps SDL events and
            // hands the latest complete frame to presentWithFrame(), letting
            // SDL_RENDERER_PRESENTVSYNC alone pace actual screen updates. No call site waits on
            // both an independent software timer AND vsync at once, which is what let the two
            // clocks drift out of phase with each other.
            constexpr size_t kFrameWords = 256 * 224;
            std::mutex frameMutex;
            std::array<uint32_t, kFrameWords> sharedFrame{};
            bool frameReady = false;
            std::atomic<bool> stopEmuThread{false};

            std::thread emuThread([&]() {
                // Paced by how much audio the hardware has actually consumed, not by a
                // software wall-clock timer: SDL_Delay-until-a-calculated-deadline is at the
                // mercy of OS thread-scheduler wake-up jitter (can be off by several ms under
                // load, regardless of how precisely the target duration itself is computed —
                // already tried, didn't help). The audio DAC's clock has none of that jitter,
                // so blocking frame production on real queued-audio drain inherits that
                // steadiness for free. AUDIO_SAMPLE_RATE here must match AudioOutput's own
                // internal rate (audio_output.cpp).
                constexpr double kAudioSampleRate = 32000.0;
                constexpr double kTargetFps = 60.0988;
                // A little slack (3 video frames' worth) so the audio device's own buffer
                // never runs dry between our production bursts, without adding enough delay
                // for input-lag to matter.
                const uint32_t kHighWaterFrames = static_cast<uint32_t>(kAudioSampleRate / kTargetFps * 3.0);
                // Safety cap: if audio isn't actually draining for any reason (no output
                // device, muted, disconnected headphones, no live audio session at all) this
                // must never block forever — fall back to proceeding anyway (accepting a
                // possible audio glitch) rather than freezing the emulator. Capped at one
                // video frame's worth so a non-draining audio device degrades to roughly the
                // old timer-based cadence instead of running far slower.
                const double kMaxWaitMs = 1000.0 / kTargetFps;
                const uint64_t perfFreq = SDL_GetPerformanceFrequency();
                std::array<uint32_t, kFrameWords> localFrame;

                while (!stopEmuThread.load(std::memory_order_relaxed)) {
                    // Break exactly on the VBlank edge (bus.consumeFrameReady(), checked after
                    // every single instruction) rather than once a raw elapsed-cycle threshold
                    // is crossed — see Bus::consumeFrameReady's doc comment: a cycle-count
                    // threshold checked only between whole instructions let one cycle-expensive
                    // instruction (chiefly a large GP-DMA transfer) overshoot past VBlank into
                    // the next frame's active scanlines before this loop noticed, tearing the
                    // framebuffer captured just below.
                    bool frameComplete = false;
                    while (!frameComplete) {
                        const uint32_t pcBefore = cpu.pc24();
                        cpu.step(bus);
                        advanceCpuScheduling(bus, cpu, true, false);
                        if (traceFile) traceFile << formatDisasmLine(pcBefore, cpu, false) << "\n";
                        frameComplete = bus.consumeFrameReady();
                    }
                    audio.pump(bus.apu());

                    std::memcpy(localFrame.data(), bus.ppu().framebuffer(), sizeof(localFrame));
                    {
                        std::lock_guard<std::mutex> lock(frameMutex);
                        sharedFrame = localFrame;
                        frameReady = true;
                    }

                    const uint64_t waitStartPerf = SDL_GetPerformanceCounter();
                    while (audio.queuedFrameCount() > kHighWaterFrames && !stopEmuThread.load(std::memory_order_relaxed)) {
                        const double waitedMs = static_cast<double>(SDL_GetPerformanceCounter() - waitStartPerf) * 1000.0 / static_cast<double>(perfFreq);
                        if (waitedMs > kMaxWaitMs) break;
                        SDL_Delay(1);
                    }
                }
            });

            std::array<uint32_t, kFrameWords> presentFrame{};
            bool hasPresented = false;

            startDisplayLink();
            bool keepGoing = true;
            while (keepGoing) {
                DebugAction action = DebugAction::None;
                if (!display.processEvents(action)) {
                    keepGoing = false;
                }

                if (action == DebugAction::LoadRom) {
                    romPath = display.pendingRomLoadPath();
                    loadRequested = true;
                    keepGoing = false;
                }
                if (const auto menuPath = takeMenuOpenRomPath()) {
                    romPath = *menuPath;
                    loadRequested = true;
                    keepGoing = false;
                }

                {
                    std::lock_guard<std::mutex> lock(frameMutex);
                    if (frameReady) {
                        presentFrame = sharedFrame;
                        hasPresented = true;
                    }
                }
                // Block on the real hardware vsync tick (via CVDisplayLink), then draw through
                // the native NSView (presentNativeFrame — see display.cpp's constructor and
                // native_game_view.mm) instead of SDL's Metal renderer entirely.
                waitForVsync();
                if (hasPresented) {
                    display.presentNativeFrame(presentFrame.data());
                }
            }
            stopDisplayLink();

            stopEmuThread.store(true, std::memory_order_relaxed);
            emuThread.join();
        } else {
            // Pace the loop to the SNES's real NTSC refresh rate rather than running
            // as fast as the host CPU allows (SDL_RENDERER_PRESENTVSYNC alone isn't
            // reliable pacing — it tracks the display's refresh rate, not 60.0988Hz,
            // and some platforms/drivers ignore it entirely).
            constexpr double kTargetFps = 60.0988;
            const uint64_t perfFreq = SDL_GetPerformanceFrequency();
            uint64_t frameStartPerf = SDL_GetPerformanceCounter();

            bool running = true;
            while (running) {
                DebugAction action = DebugAction::None;
                running = display.processEvents(action);

                display.beginFrame();
                const DebugAction uiAction = display.drawControls(paused);
                if (uiAction != DebugAction::None) {
                    action = uiAction;
                }
                const bool suppressJoypad = display.wantsKeyboardCapture();

                if (action == DebugAction::LoadRom) {
                    romPath = display.pendingRomLoadPath();
                    loadRequested = true;
                    running = false;
                    // Fall through to finish this frame normally (still need the matching
                    // presentWithFrame()/ImGui::Render() for the NewFrame() already started
                    // above via beginFrame() — skipping it here trips ImGui's own assertion
                    // on the next iteration's NewFrame() call after the ROM is swapped in).
                }
                if (const auto menuPath = takeMenuOpenRomPath()) {
                    romPath = *menuPath;
                    loadRequested = true;
                    running = false; // see the LoadRom comment above — same fall-through requirement
                }
                if (action == DebugAction::TogglePause) {
                    paused = !paused;
                    audio.setPaused(paused);
                }
                if (action == DebugAction::Reset) {
                    bus.reset();
                    cpu.reset(bus, resetVector);
                    instructionLog.clear();
                    audio.clearQueue();
                }
                if (action == DebugAction::StepOne && paused) {
                    stepOnce = true;
                }
                if (action == DebugAction::NextFrame && paused) {
                    nextFrameOnce = true;
                }

                if (!paused) {
                    // See Bus::consumeFrameReady's doc comment (checked in the bare-mode loop
                    // above too): break on the actual VBlank edge, not a raw elapsed-cycle
                    // threshold, so a cycle-expensive instruction (large GP-DMA) can't overshoot
                    // into the next frame's active scanlines before the framebuffer is read.
                    bool frameComplete = false;
                    while (!frameComplete) {
                        const uint32_t pcBefore = cpu.pc24();
                        cpu.step(bus);
                        advanceCpuScheduling(bus, cpu, true, suppressJoypad);
                        if (traceFile) traceFile << formatDisasmLine(pcBefore, cpu, false) << "\n";
                        frameComplete = bus.consumeFrameReady();
                    }
                    audio.pump(bus.apu());
                } else if (stepOnce) {
                    stepOnce = false;

                    const uint32_t pcBefore = cpu.pc24();
                    cpu.step(bus);
                    advanceCpuScheduling(bus, cpu, true, suppressJoypad);
                    logInstruction(pcBefore);
                    if (traceFile) traceFile << formatDisasmLine(pcBefore, cpu, false) << "\n";
                } else if (nextFrameOnce) {
                    nextFrameOnce = false;

                    bool frameComplete = false;
                    while (!frameComplete) {
                        const uint32_t pcBefore = cpu.pc24();
                        cpu.step(bus);
                        advanceCpuScheduling(bus, cpu, true, suppressJoypad);
                        logInstruction(pcBefore);
                        if (traceFile) traceFile << formatDisasmLine(pcBefore, cpu, false) << "\n";
                        frameComplete = bus.consumeFrameReady();
                    }
                }

                const auto panel = makeDebugPanel(headerLines, cpu, bus.ppu(), bus, instructionLog, paused, debugUi);
                const PaletteEdit paletteEdit = display.presentWithFrame(bus.ppu().framebuffer(), panel);
                if (paletteEdit.applied) {
                    bus.ppu().setCgramEntry(paletteEdit.index, paletteEdit.bgr555);
                }
                bus.ppu().setDebugLayerDisable(display.layerDisableMask());
                bus.setApuPaused(display.apuPaused());
                if (display.apuStepRequested()) {
                    bus.apu().step(APU_MANUAL_STEP_CYCLES);
                }
                if (display.apuNextFrameRequested()) {
                    bus.apu().step(CYCLES_PER_FRAME);
                }

                double elapsedMs = static_cast<double>(SDL_GetPerformanceCounter() - frameStartPerf) * 1000.0 / static_cast<double>(perfFreq);
                constexpr double kTargetFrameMs = 1000.0 / kTargetFps;
                if (elapsedMs < kTargetFrameMs) {
                    // SDL_Delay only guarantees millisecond granularity; truncating the fractional
                    // remainder here every frame made the loop run consistently a fraction of a
                    // millisecond faster than 60.0988Hz. That drift slowly rotates each present call
                    // through the display's real vsync phase, which showed up as a tear/judder whose
                    // visible displacement scaled with how much on-screen content moved per frame —
                    // most visible on fast parallax scrolling. Sleep the bulk of the remainder in
                    // whole milliseconds (leaving a small margin), then spin the last fraction
                    // against the performance counter for sub-millisecond precision.
                    const double remainingMs = kTargetFrameMs - elapsedMs;
                    if (remainingMs > 1.0) {
                        SDL_Delay(static_cast<uint32_t>(remainingMs - 1.0));
                    }
                    do {
                        elapsedMs = static_cast<double>(SDL_GetPerformanceCounter() - frameStartPerf) * 1000.0 / static_cast<double>(perfFreq);
                    } while (elapsedMs < kTargetFrameMs);
                }
                frameStartPerf = SDL_GetPerformanceCounter();
            }
        }

        if (!loadRequested) {
            return 0;
        }
        // else: loop back to the top of the for(;;) with the new romPath from Load.
    }
}
