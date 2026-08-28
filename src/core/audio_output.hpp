#pragma once

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

private:
    SDL_AudioDeviceID m_device = 0;
};
