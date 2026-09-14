#include "audio_output.hpp"

#ifndef _WIN32

#include <array>
#include <cstdint>
#include <iostream>
#include <SDL2/SDL.h>

#include "apu.hpp"

namespace {
constexpr int AUDIO_SAMPLE_RATE = 32000;
constexpr int AUDIO_CHANNELS = 2;
constexpr int AUDIO_DEVICE_SAMPLES = 1024;
constexpr int AUDIO_QUEUE_MAX_FRAMES = AUDIO_SAMPLE_RATE / 4;
} // namespace

struct AudioOutput::Impl {
    SDL_AudioDeviceID device = 0;
};

AudioOutput::AudioOutput(void* /*nativeWindowHandle*/) : m_impl(std::make_unique<Impl>()) {
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
    m_impl->device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (m_impl->device == 0) {
        std::cerr << "SDL audio disabled: " << SDL_GetError() << "\n";
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    if (have.freq != want.freq || have.format != want.format || have.channels != want.channels) {
        std::cerr << "SDL audio disabled: unsupported device format\n";
        SDL_CloseAudioDevice(m_impl->device);
        m_impl->device = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }

    SDL_PauseAudioDevice(m_impl->device, 0);
}

AudioOutput::~AudioOutput() {
    if (m_impl->device != 0) {
        SDL_CloseAudioDevice(m_impl->device);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void AudioOutput::setPaused(bool paused) {
    if (m_impl->device == 0) return;
    SDL_PauseAudioDevice(m_impl->device, paused ? 1 : 0);
    if (paused) {
        SDL_ClearQueuedAudio(m_impl->device);
    }
}

void AudioOutput::clearQueue() {
    if (m_impl->device == 0) return;
    SDL_ClearQueuedAudio(m_impl->device);
}

void AudioOutput::pump(APU& apu) {
    if (m_impl->device == 0) return;

    const uint32_t queuedBytes = SDL_GetQueuedAudioSize(m_impl->device);
    const uint32_t frameBytes = static_cast<uint32_t>(sizeof(Sdsp::PcmFrame));
    const uint32_t queuedFrames = queuedBytes / frameBytes;
    if (queuedFrames > AUDIO_QUEUE_MAX_FRAMES) {
        // Way over budget (paused/reset/load hiccup) — resync instead of playing
        // through a multi-frame-old backlog.
        SDL_ClearQueuedAudio(m_impl->device);
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
    if (SDL_QueueAudio(m_impl->device, frames.data(), bytes) != 0) {
        std::cerr << "SDL_QueueAudio failed: " << SDL_GetError() << "\n";
    }
}

uint32_t AudioOutput::queuedFrameCount() const {
    if (m_impl->device == 0) return 0;
    return SDL_GetQueuedAudioSize(m_impl->device) / static_cast<uint32_t>(sizeof(Sdsp::PcmFrame));
}

#endif // !_WIN32
