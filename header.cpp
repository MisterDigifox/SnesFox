#include "header.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>

// --------------------------------------------------
// Helpers
// --------------------------------------------------

static std::string readString(const std::vector<uint8_t>& data, size_t offset, size_t len) {
    std::string s;
    for (size_t i = 0; i < len; ++i) {
        char c = static_cast<char>(data[offset + i]);
        if (c == '\0') break;
        s += c;
    }
    return s;
}

static bool isAscii(const std::string& s) {
    for (char c : s) {
        if (c < 32 || c > 126) return false;
    }
    return true;
}

static int scoreHeader(const SnesHeader& h) {
    int score = 0;

    if (isAscii(h.title)) score += 2;

    if (h.mapMode == 0x20 || h.mapMode == 0x21) score += 2;

    if (h.romType != RomTypeField::UNKNOWN) score += 1;
    if (h.romSize != RomSize::UNKNOWN) score += 1;
    if (h.country != Country::UNKNOWN) score += 1;

    return score;
}

// --------------------------------------------------
// Parsing
// --------------------------------------------------

SnesHeader HeaderParser::parse(const std::vector<uint8_t>& data, size_t offset) {
    SnesHeader h{};

    h.title = readString(data, offset, 21);
    h.mapMode = data[offset + 0x15];
    h.romType = static_cast<RomTypeField>(data[offset + 0x16]);
    h.romSize = static_cast<RomSize>(data[offset + 0x17]);
    h.sramSize = static_cast<SramSize>(data[offset + 0x18]);
    h.country = static_cast<Country>(data[offset + 0x19]);
    h.license = static_cast<License>(data[offset + 0x1A]);
    h.version = data[offset + 0x1B];
    h.checksum = data[offset + 0x1E] | (data[offset + 0x1F] << 8);
    h.complement = data[offset + 0x1C] | (data[offset + 0x1D] << 8);
    h.romBytes = computeRomBytes(h.romSize);
    h.chip = detectChip(h.romType);
    h.validHeader = validateHeader(h.checksum, h.complement);

    return h;
}

// --------------------------------------------------
// Detection LoROM / HiROM
// --------------------------------------------------

RomMapping HeaderParser::detect(const std::vector<uint8_t>& data) {
    auto lorom = parse(data, 0x7FC0);
    auto hirom = parse(data, 0xFFC0);

    int scoreLo = scoreHeader(lorom);
    int scoreHi = scoreHeader(hirom);

    if (scoreLo > scoreHi) return RomMapping::LoROM;
    if (scoreHi > scoreLo) return RomMapping::HiROM;

    return RomMapping::Unknown;
}

// --------------------------------------------------
// toString helpers
// --------------------------------------------------

std::string HeaderParser::toString(RomTypeField v) {
    switch (v) {
        // Standard
        case RomTypeField::ROM_ONLY:        return "ROM only";
        case RomTypeField::ROM_RAM:         return "ROM + RAM";
        case RomTypeField::ROM_RAM_BATTERY: return "ROM + RAM + Battery";

        // DSP
        case RomTypeField::DSP:             return "ROM + DSP";

        // SuperFX
        case RomTypeField::SUPERFX:         return "ROM + SuperFX";
        case RomTypeField::SUPERFX2:        return "ROM + SuperFX2";
        case RomTypeField::SUPERFX3:        return "ROM + SuperFX3";
        case RomTypeField::SUPERFX4:        return "ROM + SuperFX4";

        // SA-1
        case RomTypeField::SA1:             return "ROM + SA-1";

        case RomTypeField::UNKNOWN:
        default:
            return "Unknown";
    }
}

std::string HeaderParser::toString(RomSize v) {
    switch (v) {
        case RomSize::KB_256: return "256 KB";
        case RomSize::KB_512: return "512 KB";
        case RomSize::MB_1: return "1 MB";
        case RomSize::MB_2: return "2 MB";
        case RomSize::MB_4: return "4 MB";
        case RomSize::MB_8: return "8 MB";
        default: return "Unknown";
    }
}

std::string HeaderParser::toString(SramSize v) {
    switch (v) {
        case SramSize::NONE: return "None";
        case SramSize::KB_2: return "2 KB";
        case SramSize::KB_8: return "8 KB";
        case SramSize::KB_32: return "32 KB";
        case SramSize::KB_128: return "128 KB";
        default: return "Unknown";
    }
}

std::string HeaderParser::toString(Country country) {
    switch (country) {
        case Country::JAPAN:       return "Japan";
        case Country::USA:         return "USA";
        case Country::EUROPE:      return "Europe";
        case Country::SWEDEN:      return "Sweden";
        case Country::FINLAND:     return "Finland";
        case Country::DENMARK:     return "Denmark";
        case Country::FRANCE:      return "France";
        case Country::HOLLAND:     return "Holland";
        case Country::SPAIN:       return "Spain";
        case Country::GERMANY:     return "Germany";
        case Country::ITALY:       return "Italy";
        case Country::CHINA:       return "China";
        case Country::INDONESIA:   return "Indonesia";
        case Country::SOUTH_KOREA: return "South Korea";
        default:                   return "Unknown";
    }
}

std::string HeaderParser::toString(License v) {
    switch (v) {
        case License::NINTENDO: return "Nintendo";
        case License::CAPCOM: return "Capcom";
        case License::KONAMI: return "Konami";
        case License::GGSDF: return "GGS and Digital Fox";
        default: return "Unknown";
    }
}

std::string HeaderParser::mapModeToString(uint8_t value) {
    uint8_t base = value & 0x2F;

    switch (base) {
        case 0x20: return "LoROM + SlowROM";
        case 0x21: return "HiROM + SlowROM";
        case 0x23: return "SA-1";
        case 0x25: return "ExHiROM + SlowROM";
        case 0x30: return "LoROM + FastROM";
        case 0x31: return "HiROM + FastROM";
        case 0x32: return "HiROM + FastROM (extended)";
        case 0x35: return "ExHiROM + FastROM";
        default:   return "Unknown";
    }
}

std::string HeaderParser::toHexValue(License v) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase
       << static_cast<int>(v);
    return ss.str();
}

// Compute exact ROM size in bytes
size_t HeaderParser::computeRomBytes(RomSize sizeEnum) {
    if (sizeEnum == RomSize::UNKNOWN) return 0;
    // ROM Size encoding: 0x08 = 256 KB, 0x09 = 512 KB, 0x0A = 1 MB ...
    size_t kb = 256 * (1 << (static_cast<int>(sizeEnum) - 8));
    return kb * 1024;
}

// Detect special chips
std::string HeaderParser::detectChip(RomTypeField romType) {
    switch (static_cast<uint8_t>(romType)) {
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x1A:
            return "SuperFX";

        case 0x23:
            return "SA-1";

        case 0x03:
            return "DSP";

        default:
            return "None";
    }
}

// Validate header
bool HeaderParser::validateHeader(uint16_t checksum, uint16_t complement) {
    return static_cast<uint32_t>(checksum) + static_cast<uint32_t>(complement) == 0xFFFF;
}

// --------------------------------------------------
// Print
// --------------------------------------------------

void HeaderParser::printHeader(const SnesHeader& h) {
    std::cout << "Title          : " << h.title << "\n";
    
    std::cout << "Map Mode       : " << mapModeToString(h.mapMode) << " (0x" << std::hex << (int)h.mapMode << ")\n";

    std::cout << "ROM Type       : " << toString(h.romType) << "\n";

    std::cout << "ROM Size       : " << toString(h.romSize) << "\n";

    std::cout << "SRAM Size      : " << toString(h.sramSize) << "\n";

    std::cout << "Country        : " << toString(h.country) << "\n";

    std::cout << "License        : " << toString(h.license) << "\n";

    std::cout << "LicenseHex     : " << toHexValue(h.license) << "\n";

    std::cout << "Version        : 1." << std::dec << (int)h.version << "\n";

    std::cout << "Checksum       : 0x" << std::hex << h.checksum << "\n";

    std::cout << "Complement     : 0x" << std::hex << h.complement << "\n";

    std::cout << "Header Valid?  : " << (h.validHeader ? "Yes" : "No") << "\n";

    std::cout << "Special Chip   : " << h.chip << "\n";
}

void HeaderParser::print(const std::vector<uint8_t>& data) {
    RomMapping type = HeaderParser::detect(data);

    switch (type) {
        case RomMapping::LoROM:
            std::cout << "Mode           : LoROM (detected)\n";
            HeaderParser::printHeader(HeaderParser::parse(data, 0x7FC0));
            break;

        case RomMapping::HiROM:
            std::cout << "Mode           : HiROM (detected)\n";
            HeaderParser::printHeader(HeaderParser::parse(data, 0xFFC0));
            break;

        case RomMapping::Unknown:
            std::cout << "Mode           : Unknown ROM type\n";
            HeaderParser::printHeader(HeaderParser::parse(data, 0x7FC0));
            HeaderParser::printHeader(HeaderParser::parse(data, 0xFFC0));
            break;
    }
}

static std::vector<std::string> headerToLines(const SnesHeader& h) {
    std::vector<std::string> lines;

    lines.push_back("Title      : " + h.title);
    lines.push_back("Map Mode   : " + HeaderParser::mapModeToString(h.mapMode) + " (0x" + [&]() {
        std::stringstream ss;
        ss << std::hex << std::nouppercase << (int)h.mapMode;
        return ss.str();
    }() + ")");
    lines.push_back("ROM Type   : " + HeaderParser::toString(h.romType));
    lines.push_back("ROM Size   : " + HeaderParser::toString(h.romSize));
    lines.push_back("SRAM Size  : " + HeaderParser::toString(h.sramSize));
    lines.push_back("Country    : " + HeaderParser::toString(h.country));
    lines.push_back("License    : " + HeaderParser::toString(h.license));
    lines.push_back("LicenseHex : " + HeaderParser::toHexValue(h.license));

    {
        std::stringstream ss;
        ss << "Version    : 1." << std::dec << (int)h.version;
        lines.push_back(ss.str());
    }

    {
        std::stringstream ss;
        ss << "Checksum   : 0x" << std::hex << h.checksum;
        lines.push_back(ss.str());
    }

    {
        std::stringstream ss;
        ss << "Complement : 0x" << std::hex << h.complement;
        lines.push_back(ss.str());
    }

    lines.push_back(std::string("Header Valid? : ") + (h.validHeader ? "Yes" : "No"));
    lines.push_back("Special Chip  : " + h.chip);

    return lines;
}

std::vector<std::string> HeaderParser::toLines(const std::vector<uint8_t>& data) {
    std::vector<std::string> lines;
    RomMapping type = HeaderParser::detect(data);

    switch (type) {
        case RomMapping::LoROM:
            lines.push_back("=== LoROM (detected) ===");
            {
                auto parsed = HeaderParser::parse(data, 0x7FC0);
                auto info = headerToLines(parsed);
                lines.insert(lines.end(), info.begin(), info.end());
            }
            break;

        case RomMapping::HiROM:
            lines.push_back("=== HiROM (detected) ===");
            {
                auto parsed = HeaderParser::parse(data, 0xFFC0);
                auto info = headerToLines(parsed);
                lines.insert(lines.end(), info.begin(), info.end());
            }
            break;

        case RomMapping::Unknown:
            lines.push_back("=== Unknown ROM type ===");
            {
                auto parsedLo = HeaderParser::parse(data, 0x7FC0);
                auto lo = headerToLines(parsedLo);
                lines.insert(lines.end(), lo.begin(), lo.end());
            }
            lines.push_back("");
            {
                auto parsedHi = HeaderParser::parse(data, 0xFFC0);
                auto hi = headerToLines(parsedHi);
                lines.insert(lines.end(), hi.begin(), hi.end());
            }
            break;
    }

    return lines;
}
