#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

class Rom;

// Shared helpers for the CLI subcommand handlers (disasm/reasm/header/cov/snap) in
// snesfox_app.cpp: ROM header banner, reset-vector lookup, and coverage-file parsing.
void printRomInfo(const Rom& rom, const std::vector<uint8_t>& data);
uint16_t readResetVector(const std::vector<uint8_t>& rom, bool isLoRom);
std::unordered_set<uint32_t> loadCoverageFile(const std::string& path);
