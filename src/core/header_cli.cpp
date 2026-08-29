#include "header_cli.hpp"

#include <stdexcept>

#include "rom.hpp"
#include "rom_cli_helpers.hpp"

int runHeader(const std::string& romPath) {
    Rom rom(romPath);
    const auto& data = rom.data();
    if (data.size() < 0x10000) {
        throw std::runtime_error("ROM: unexpected size");
    }
    printRomInfo(rom, data);
    return 0;
}
