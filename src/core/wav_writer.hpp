#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// Writes `samples` as a mono 16-bit PCM WAV file at `sampleRateHz`. Returns false if the
/// file couldn't be opened for writing.
bool writeWavFile(const std::string& path, const std::vector<int16_t>& samples, int sampleRateHz);
