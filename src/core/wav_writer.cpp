#include "wav_writer.hpp"

#include <cstdio>

bool writeWavFile(const std::string& path, const std::vector<int16_t>& samples, int sampleRateHz) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    constexpr uint16_t kNumChannels = 1;
    constexpr uint16_t kBitsPerSample = 16;
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint32_t byteRate = static_cast<uint32_t>(sampleRateHz) * kNumChannels * (kBitsPerSample / 8);
    const uint16_t blockAlign = kNumChannels * (kBitsPerSample / 8);
    const uint32_t riffSize = 36 + dataBytes;

    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riffSize, 4, 1, f);
    std::fwrite("WAVE", 1, 4, f);

    std::fwrite("fmt ", 1, 4, f);
    const uint32_t fmtSize = 16;
    std::fwrite(&fmtSize, 4, 1, f);
    const uint16_t audioFormat = 1; // PCM
    std::fwrite(&audioFormat, 2, 1, f);
    std::fwrite(&kNumChannels, 2, 1, f);
    const uint32_t sampleRate = static_cast<uint32_t>(sampleRateHz);
    std::fwrite(&sampleRate, 4, 1, f);
    std::fwrite(&byteRate, 4, 1, f);
    std::fwrite(&blockAlign, 2, 1, f);
    std::fwrite(&kBitsPerSample, 2, 1, f);

    std::fwrite("data", 1, 4, f);
    std::fwrite(&dataBytes, 4, 1, f);
    if (!samples.empty()) {
        std::fwrite(samples.data(), sizeof(int16_t), samples.size(), f);
    }

    std::fclose(f);
    return true;
}
