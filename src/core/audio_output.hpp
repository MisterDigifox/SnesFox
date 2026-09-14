#pragma once

#include <cstdint>
#include <memory>

class APU;

// Drives the platform audio device from the APU's S-DSP PCM output. `pump()` is meant to be
// called once per emulated video frame from the main loop. Two independent implementations
// exist: src/core/audio_output.cpp (SDL, used on macOS) and
// src/windows/audio_output_directsound.cpp (DirectSound, used on Windows) — selected at build
// time (each source file guards its whole body with #ifndef/#ifdef _WIN32), not via virtual
// dispatch, so the public interface here stays platform-agnostic via Pimpl.
class AudioOutput {
public:
    // nativeWindowHandle: an HWND on Windows (DirectSound's SetCooperativeLevel needs one);
    // ignored by the SDL/macOS implementation. Get one from Display::nativeWindowHandle().
    explicit AudioOutput(void* nativeWindowHandle = nullptr);
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    void setPaused(bool paused);
    void clearQueue();
    void pump(APU& apu);
    // Frames of audio still queued, not yet consumed by the audio hardware. Polling this
    // (rather than a software wall-clock timer) to pace frame production ties emulation speed
    // to the audio DAC's own clock — rock-solid, with none of the OS-scheduler wake-up jitter a
    // sleep-until-a-calculated-deadline timer is subject to.
    uint32_t queuedFrameCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
