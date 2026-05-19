#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 🔍 Rom Mapping
enum class RomMapping {
    LoROM,
    HiROM,
    Unknown
};

// 🧠 Hardware
enum class RomTypeField : uint8_t {
    // Standard
    ROM_ONLY        = 0x00,
    ROM_RAM         = 0x01,
    ROM_RAM_BATTERY = 0x02,

    // DSP
    DSP             = 0x03,

    // SuperFX
    SUPERFX         = 0x13, // base
    SUPERFX2        = 0x14,
    SUPERFX3        = 0x15,
    SUPERFX4        = 0x1A,

    // SA-1
    SA1             = 0x23,

    UNKNOWN         = 0xFF
};

// 📦 Rom Size
enum class RomSize : uint8_t {
    KB_256  = 0x08,
    KB_512  = 0x09,
    MB_1    = 0x0A,
    MB_2    = 0x0B,
    MB_4    = 0x0C,
    MB_8    = 0x0D,
    UNKNOWN = 0xFF
};

// 💾 SRAM
enum class SramSize : uint8_t {
    NONE    = 0x00,
    KB_2    = 0x01,
    KB_8    = 0x02,
    KB_32   = 0x03,
    KB_128  = 0x04,
    UNKNOWN = 0xFF
};

// 🌍 Country
enum class Country : uint8_t {
    JAPAN          = 0x00,
    USA            = 0x01,
    EUROPE         = 0x02,
    SWEDEN         = 0x03,
    FINLAND        = 0x04,
    DENMARK        = 0x05,
    FRANCE         = 0x06,
    HOLLAND        = 0x07,
    SPAIN          = 0x08,
    GERMANY        = 0x09,
    ITALY          = 0x0A,
    CHINA          = 0x0B,
    INDONESIA      = 0x0C,
    SOUTH_KOREA    = 0x0D,
    UNKNOWN        = 0xFF
};

// 🏢 License
enum class License : uint8_t {
    NINTENDO = 0x01,
    CAPCOM   = 0x08,
    KONAMI   = 0xA4,
    GGSDF    = 0xFE,
    UNKNOWN  = 0xFF
};

// 📄 Header
struct SnesHeader {
    std::string title;
    uint8_t mapMode;
    RomTypeField romType;
    RomSize romSize;
    SramSize sramSize;
    Country country;
    License license;
    uint8_t version;
    uint16_t checksum;
    uint16_t complement;

    // Exact ROM size in bytes
    size_t romBytes = 0;

    // Special chip name
    std::string chip = "None";

    // Checksum validation
    bool validHeader = false;
};

class HeaderParser final {
private:
    static SnesHeader parse(const std::vector<uint8_t>& data, size_t offset);
    static void printHeader(const SnesHeader& h);
    static size_t computeRomBytes(RomSize sizeEnum);
    static bool validateHeader(uint16_t checksum, uint16_t complement);

    static std::string detectChip(RomTypeField romType);

public:
    static RomMapping detect(const std::vector<uint8_t>& data);
    static std::string toString(RomTypeField v);
    static std::string toString(RomSize v);
    static std::string toString(SramSize v);
    static std::string toString(Country v);
    static std::string toString(License v);
    static std::string toHexValue(License v);
    static std::string mapModeToString(uint8_t value);
    static void print(const std::vector<uint8_t>& data);
    static std::vector<std::string> toLines(const std::vector<uint8_t>& data);
};
