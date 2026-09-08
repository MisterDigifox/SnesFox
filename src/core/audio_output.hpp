#pragma once

#include <cstdint>
#include <SDL2/SDL.h>

class APU;

// Drives an SDL audio device from the APU's S-DSP PCM output. `pump()` is meant to be
// called once per emulated video frame from the main loop.
class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    void setPaused(bool paused);
    void clearQueue();
    void pump(APU& apu);
    // Frames of audio still sitting in the SDL queue, not yet consumed by the audio hardware.
    // Polling this (rather than a software wall-clock timer) to pace frame production ties
    // emulation speed to the audio DAC's own clock — rock-solid, with none of the OS-scheduler
    // wake-up jitter a sleep-until-a-calculated-deadline timer is subject to.
    uint32_t queuedFrameCount() const;

private:
    SDL_AudioDeviceID m_device = 0;
};
