#pragma once

#include <string>

// CLI entry point for the `header` subcommand: parses and prints the SNES-internal header.
int runHeader(const std::string& romPath);
