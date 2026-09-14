#include "disasm_cli.hpp"

#include <iostream>
#include <stdexcept>
#include <unordered_set>

#include "disasm_dump.hpp"
#include "header.hpp"
#include "reasm.hpp"
#include "rom.hpp"
#include "rom_cli_helpers.hpp"

int runDisasm(
    const std::string& romPath,
    const std::string& asmPath,
    const std::optional<std::string>& coveragePath
) {
    Rom rom(romPath);
    const auto& data = rom.data();
    if (data.size() < 0x10000) {
        throw std::runtime_error("ROM: unexpected size");
    }

    printRomInfo(rom, data);

    const RomMapping mapping = HeaderParser::detect(data);
    const bool isLoRom = (mapping == RomMapping::LoROM);
    const uint16_t resetVector = readResetVector(data, isLoRom);

    const std::unordered_set<uint32_t>* covPtr = nullptr;
    std::unordered_set<uint32_t> covStorage;
    if (coveragePath) {
        covStorage = loadCoverageFile(*coveragePath);
        covPtr = &covStorage;
        std::cout << "Coverage PCs    : " << covStorage.size() << " (from " << *coveragePath << ")\n";
    }

    dumpRomAsAsmFull(data, resetVector, asmPath, covPtr);
    std::cout << "=== Disassembler ===\n";
    std::cout << "Disassembly written to: " << asmPath << "\n";
    return 0;
}

int runReasm(const std::string& asmPath, const std::string& outRomPath) {
    std::string error;
    if (!reassembleDumpAsmToRomFile(asmPath, outRomPath, error)) {
        throw std::runtime_error("Reassembly failed: " + error);
    }

    std::cout << "=== Reassembler ===\n";
    std::cout << "ROM rebuilt successfully: " << outRomPath << "\n";
    return 0;
}
