#include "../core/audio_output.hpp"

#ifdef _WIN32

#include <windows.h>
#include <dsound.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "../core/apu.hpp"

// DirectSound instead of SDL2's audio device on Windows — mirrors Mesen2's own per-platform
// split (its Windows build uses DirectSound directly; SDL2 is reserved for its Linux/macOS
// build), rather than going through SDL's WASAPI/DirectSound backend selection and buffer
// negotiation, which is a source of uncertainty this project hasn't been able to fully pin
// down (see docs/tickets/AUDIO-2, AUDIO-3). Written independently against the public
// DirectSound API — not a port of Mesen2's GPLv3 SoundManager.cpp (this project is MIT).
namespace {
constexpr int AUDIO_SAMPLE_RATE = 32000;
constexpr int AUDIO_CHANNELS = 2;
constexpr int AUDIO_BYTES_PER_FRAME = AUDIO_CHANNELS * static_cast<int>(sizeof(int16_t));
// Secondary (ring) buffer capacity: ~0.5s, generous headroom against Windows' coarser audio
// scheduling — same intent as the old SDL path's AUDIO_QUEUE_MAX_FRAMES safety margin.
constexpr DWORD AUDIO_BUFFER_BYTES = AUDIO_SAMPLE_RATE * AUDIO_BYTES_PER_FRAME / 2;
// Don't start playing until at least this much is buffered, so playback doesn't immediately
// underrun on the very first buffer swap.
constexpr DWORD AUDIO_START_THRESHOLD_BYTES = AUDIO_BUFFER_BYTES / 8;
} // namespace

struct AudioOutput::Impl {
    HWND hwnd = nullptr;
    IDirectSound8* directSound = nullptr;
    IDirectSoundBuffer* primaryBuffer = nullptr;
    IDirectSoundBuffer8* secondaryBuffer = nullptr;
    DWORD writeOffset = 0;
    bool playing = false;

    bool init();
    void release();
    void resetBuffer();
    void validateWriteCursor(DWORD safeWriteCursor);
};

bool AudioOutput::Impl::init() {
    if (FAILED(DirectSoundCreate8(nullptr, &directSound, nullptr))) {
        std::cerr << "DirectSound audio disabled: DirectSoundCreate8 failed\n";
        return false;
    }

    // DSSCL_PRIORITY requires *some* window handle; fall back to the desktop window if we
    // weren't handed a real one — bare mode always has a native HWND, debug-UI mode gets one
    // from the SDL window via Display::nativeWindowHandle(), so this is just a defensive
    // last resort.
    HWND coopWindow = hwnd ? hwnd : GetDesktopWindow();
    if (FAILED(directSound->SetCooperativeLevel(coopWindow, DSSCL_PRIORITY))) {
        std::cerr << "DirectSound audio disabled: SetCooperativeLevel failed\n";
        return false;
    }

    WAVEFORMATEX waveFormat{};
    waveFormat.wFormatTag = WAVE_FORMAT_PCM;
    waveFormat.nChannels = static_cast<WORD>(AUDIO_CHANNELS);
    waveFormat.nSamplesPerSec = AUDIO_SAMPLE_RATE;
    waveFormat.wBitsPerSample = 16;
    waveFormat.nBlockAlign = static_cast<WORD>(AUDIO_BYTES_PER_FRAME);
    waveFormat.nAvgBytesPerSec = AUDIO_SAMPLE_RATE * AUDIO_BYTES_PER_FRAME;

    DSBUFFERDESC primaryDesc{};
    primaryDesc.dwSize = sizeof(primaryDesc);
    primaryDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;
    if (FAILED(directSound->CreateSoundBuffer(&primaryDesc, &primaryBuffer, nullptr))) {
        std::cerr << "DirectSound audio disabled: failed to create primary buffer\n";
        return false;
    }
    primaryBuffer->SetFormat(&waveFormat); // best-effort; some devices ignore this

    DSBUFFERDESC secondaryDesc{};
    secondaryDesc.dwSize = sizeof(secondaryDesc);
    secondaryDesc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLVOLUME | DSBCAPS_LOCSOFTWARE;
    secondaryDesc.dwBufferBytes = AUDIO_BUFFER_BYTES;
    secondaryDesc.lpwfxFormat = &waveFormat;

    IDirectSoundBuffer* tempBuffer = nullptr;
    if (FAILED(directSound->CreateSoundBuffer(&secondaryDesc, &tempBuffer, nullptr))) {
        std::cerr << "DirectSound audio disabled: failed to create secondary buffer\n";
        return false;
    }
    const HRESULT hr = tempBuffer->QueryInterface(IID_IDirectSoundBuffer8, reinterpret_cast<void**>(&secondaryBuffer));
    tempBuffer->Release();
    if (FAILED(hr)) {
        std::cerr << "DirectSound audio disabled: failed to obtain IDirectSoundBuffer8\n";
        return false;
    }

    resetBuffer();
    return true;
}

void AudioOutput::Impl::release() {
    if (secondaryBuffer) {
        secondaryBuffer->Stop();
        secondaryBuffer->Release();
        secondaryBuffer = nullptr;
    }
    if (primaryBuffer) {
        primaryBuffer->Release();
        primaryBuffer = nullptr;
    }
    if (directSound) {
        directSound->Release();
        directSound = nullptr;
    }
}

void AudioOutput::Impl::resetBuffer() {
    if (!secondaryBuffer) return;
    secondaryBuffer->Stop();
    void* ptr = nullptr;
    DWORD bytes = 0;
    if (SUCCEEDED(secondaryBuffer->Lock(0, 0, &ptr, &bytes, nullptr, nullptr, DSBLOCK_ENTIREBUFFER))) {
        std::memset(ptr, 0, bytes);
        secondaryBuffer->Unlock(ptr, bytes, nullptr, 0);
    }
    secondaryBuffer->SetCurrentPosition(0);
    writeOffset = 0;
    playing = false;
}

void AudioOutput::Impl::validateWriteCursor(DWORD safeWriteCursor) {
    // If playback has caught up to (or passed) our last write position, we underran — resync
    // to the hardware's own safe-write cursor rather than writing into audio it's about to
    // play (or already played), which would otherwise sound like a stutter/loop-back.
    const int32_t gap = static_cast<int32_t>(writeOffset) - static_cast<int32_t>(safeWriteCursor);
    if (gap < 0 && gap >= -static_cast<int32_t>(AUDIO_BUFFER_BYTES) / 2) {
        writeOffset = safeWriteCursor;
    }
}

AudioOutput::AudioOutput(void* nativeWindowHandle) : m_impl(std::make_unique<Impl>()) {
    m_impl->hwnd = static_cast<HWND>(nativeWindowHandle);
    if (!m_impl->init()) {
        m_impl->release();
    }
}

AudioOutput::~AudioOutput() {
    m_impl->release();
}

void AudioOutput::setPaused(bool paused) {
    if (!m_impl->secondaryBuffer) return;
    if (paused) {
        m_impl->resetBuffer();
    }
}

void AudioOutput::clearQueue() {
    if (!m_impl->secondaryBuffer) return;
    m_impl->resetBuffer();
}

void AudioOutput::pump(APU& apu) {
    if (!m_impl->secondaryBuffer) return;

    std::array<Sdsp::PcmFrame, 2048> frames{};
    const size_t n = apu.popAudioSamples(frames.data(), frames.size());
    if (n == 0) return;

    DWORD playCursor = 0, safeWriteCursor = 0;
    m_impl->secondaryBuffer->GetCurrentPosition(&playCursor, &safeWriteCursor);
    m_impl->validateWriteCursor(safeWriteCursor);

    DWORD bytes = static_cast<DWORD>(n * sizeof(Sdsp::PcmFrame));
    if (bytes > AUDIO_BUFFER_BYTES) bytes = AUDIO_BUFFER_BYTES; // defensive; can't happen at 2048-frame chunks

    void* ptr1 = nullptr;
    void* ptr2 = nullptr;
    DWORD bytes1 = 0, bytes2 = 0;
    const HRESULT hr = m_impl->secondaryBuffer->Lock(m_impl->writeOffset, bytes, &ptr1, &bytes1, &ptr2, &bytes2, 0);
    if (FAILED(hr)) return;

    std::memcpy(ptr1, frames.data(), bytes1);
    if (ptr2 && bytes2 > 0) {
        std::memcpy(ptr2, reinterpret_cast<const uint8_t*>(frames.data()) + bytes1, bytes2);
    }
    m_impl->secondaryBuffer->Unlock(ptr1, bytes1, ptr2, bytes2);
    m_impl->writeOffset = (m_impl->writeOffset + bytes1 + bytes2) % AUDIO_BUFFER_BYTES;

    if (!m_impl->playing) {
        // How much is actually buffered ahead of the play cursor right now.
        int32_t buffered = static_cast<int32_t>(m_impl->writeOffset) - static_cast<int32_t>(playCursor);
        if (buffered < 0) buffered += static_cast<int32_t>(AUDIO_BUFFER_BYTES);
        if (static_cast<DWORD>(buffered) >= AUDIO_START_THRESHOLD_BYTES) {
            m_impl->secondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);
            m_impl->playing = true;
        }
    }
}

uint32_t AudioOutput::queuedFrameCount() const {
    if (!m_impl->secondaryBuffer) return 0;
    DWORD playCursor = 0, safeWriteCursor = 0;
    m_impl->secondaryBuffer->GetCurrentPosition(&playCursor, &safeWriteCursor);
    int32_t queuedBytes = static_cast<int32_t>(m_impl->writeOffset) - static_cast<int32_t>(playCursor);
    if (queuedBytes < 0) queuedBytes += static_cast<int32_t>(AUDIO_BUFFER_BYTES);
    return static_cast<uint32_t>(queuedBytes) / static_cast<uint32_t>(sizeof(Sdsp::PcmFrame));
}

#endif // _WIN32
