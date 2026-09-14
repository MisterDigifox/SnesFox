#include "rom_cli_helpers.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "header.hpp"
#include "rom.hpp"

namespace {

std::string trimCopy(std::string s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

bool parseHex24Line(const std::string& t, uint32_t& out) {
    std::string h = trimCopy(t);
    if (h.empty()) return false;
    if (h.size() >= 2 && h[0] == '$') h = h.substr(1);
    if (h.size() > 6) return false;
    uint32_t v = 0;
    for (char c : h) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= static_cast<uint32_t>(c - '0');
        else if (c >= 'A' && c <= 'F')
            v |= static_cast<uint32_t>(10 + c - 'A');
        else if (c >= 'a' && c <= 'f')
            v |= static_cast<uint32_t>(10 + c - 'a');
        else
            return false;
    }
    out = v & 0xFFFFFFu;
    return true;
}

} // namespace

void printRomInfo(const Rom& rom, const std::vector<uint8_t>& data) {
    std::cout << "=== Rom Header ===\n";
    std::cout << "File size      : " << rom.fileSize() << " bytes\n";
    std::cout << "ROM size       : " << rom.size() << " bytes\n";
    std::cout << "Copier header  : " << (rom.hasHeader() ? "Yes (512 bytes)" : "No") << "\n";
    std::cout << "Data offset    : " << rom.offset() << "\n";
    HeaderParser::print(data);
}

uint16_t readResetVector(const std::vector<uint8_t>& rom, bool isLoRom) {
    const size_t addr = isLoRom ? 0x7FFC : 0xFFFC;
    if (rom.size() <= addr + 1) return 0x0000;
    return static_cast<uint16_t>(rom[addr] | (rom[addr + 1] << 8));
}

std::unordered_set<uint32_t> loadCoverageFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open coverage file: " + path);
    }

    std::unordered_set<uint32_t> pcs;
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trimCopy(line);
        if (t.empty() || t[0] == '#') continue;

        uint32_t pc = 0;
        if (!parseHex24Line(t, pc)) {
            throw std::runtime_error("invalid coverage line (expected hex PC): " + path);
        }
        pcs.insert(pc);
    }

    return pcs;
}
