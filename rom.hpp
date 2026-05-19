#pragma once
#include <vector>
#include <string>
#include <cstdint>

class Rom final {
private:
    std::vector<uint8_t> m_data;
    size_t m_offset = 0;

    static bool hasCopierHeader(size_t fileSize);

public:
    explicit Rom(const std::string& path);

    const std::vector<uint8_t>& data() const;
    const uint8_t* rawData() const;
    size_t size() const;      // ROM size without copier header
    size_t fileSize() const;  // original file size
    size_t offset() const;    // 0 or 512
    bool hasHeader() const;   // true if copier header was found
};
