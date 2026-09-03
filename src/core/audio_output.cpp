#include "audio_output.hpp"

#include <array>
#include <cstdint>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

#include "apu.hpp"

namespace {
constexpr int AUDIO_SAMPLE_RATE = 32000;
constexpr int AUDIO_CHANNELS = 2;
constexpr int AUDIO_DEVICE_SAMPLES = 1024;
constexpr int AUDIO_QUEUE_MAX_FRAMES = AUDIO_SAMPLE_RATE / 4;
} // namespace

AudioOutput::AudioOutput() {
#ifdef _WIN32
    // Windows' default ~15.6ms system timer resolution makes the main loop's SDL_Delay-paced
    // frame timing (see emu_cli.cpp) too imprecise — the audio queue this feeds every frame
    // intermittently underruns and crackles as a result. Request 1ms resolution for this
    // process's lifetime instead (matched by timeEndPeriod in the destructor).
    timeBeginPeriod(1);
#endif

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL audio disabled: " << SDL_GetError() << "\n";
        return;
    }

    SDL_AudioSpec want{};
    want.freq = AUDIO_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = AUDIO_CHANNELS;
    want.samples = AUDIO_DEVICE_SAMPLES;

    SDL_AudioSpec have{};
    m_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (m_device == 0) {
        std::cerr << "SDL audio disabled: " << SDL_GetError() << "\n";
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    if (have.freq != want.freq || have.format != want.format || have.channels != want.channels) {
        std::cerr << "SDL audio disabled: unsupported device format\n";
        SDL_CloseAudioDevice(m_device);
        m_device = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }

    SDL_PauseAudioDevice(m_device, 0);
}

AudioOutput::~AudioOutput() {
    if (m_device != 0) {
        SDL_CloseAudioDevice(m_device);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

void AudioOutput::setPaused(bool paused) {
    if (m_device == 0) return;
    SDL_PauseAudioDevice(m_device, paused ? 1 : 0);
    if (paused) {
        SDL_ClearQueuedAudio(m_device);
    }
}

void AudioOutput::clearQueue() {
    if (m_device == 0) return;
    SDL_ClearQueuedAudio(m_device);
}

void AudioOutput::pump(APU& apu) {
    if (m_device == 0) return;

    const uint32_t queuedBytes = SDL_GetQueuedAudioSize(m_device);
    const uint32_t frameBytes = static_cast<uint32_t>(sizeof(Sdsp::PcmFrame));
    const uint32_t queuedFrames = queuedBytes / frameBytes;
    if (queuedFrames > AUDIO_QUEUE_MAX_FRAMES) {
        // Way over budget (paused/reset/load hiccup) — resync instead of playing
        // through a multi-frame-old backlog.
        SDL_ClearQueuedAudio(m_device);
    }

    // Drip-feed every call instead of gating on a low watermark: the APU only ever
    // produces about one video frame's worth of samples (~532 at 32kHz/60.0988Hz)
    // between calls, so pushing that immediately keeps the queue level steady rather
    // than sawtoothing down toward empty between periodic bulk top-ups — the latter
    // is what left too little slack against the audio thread's own pull cadence.
    std::array<Sdsp::PcmFrame, 2048> frames{};
    const size_t n = apu.popAudioSamples(frames.data(), frames.size());
    if (n == 0) return;

    const uint32_t bytes = static_cast<uint32_t>(n * sizeof(Sdsp::PcmFrame));
    if (SDL_QueueAudio(m_device, frames.data(), bytes) != 0) {
        std::cerr << "SDL_QueueAudio failed: " << SDL_GetError() << "\n";
    }
}
