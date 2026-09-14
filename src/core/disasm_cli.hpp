#pragma once

#include <optional>
#include <string>

// CLI entry points for the `disasm`/`reasm` subcommands (see printUsage() in
// snesfox_app.cpp for their argument shapes).
int runDisasm(const std::string& romPath, const std::string& asmPath,
              const std::optional<std::string>& coveragePath);
int runReasm(const std::string& asmPath, const std::string& outRomPath);
