#include "rom.hpp"
#include <fstream>
#include <iterator>
#include <stdexcept>

bool Rom::hasCopierHeader(size_t fileSize) {
    return (fileSize % 0x8000) == 512;
}

Rom::Rom(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to open ROM file: " + path);
    }

    const std::vector<uint8_t> fileBytes(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );

    if (fileBytes.empty()) {
        throw std::runtime_error("ROM file is empty");
    }

    m_offset = hasCopierHeader(fileBytes.size()) ? 512u : 0u;

    if (fileBytes.size() <= m_offset) {
        throw std::runtime_error("ROM file is too small after removing copier header");
    }

    m_data.assign(fileBytes.begin() + static_cast<std::ptrdiff_t>(m_offset), fileBytes.end());
}

const std::vector<uint8_t>& Rom::data() const {
    return m_data;
}

const uint8_t* Rom::rawData() const {
    return m_data.empty() ? nullptr : m_data.data();
}

size_t Rom::size() const {
    return m_data.size();
}

size_t Rom::fileSize() const {
    return m_data.size() + m_offset;
}

size_t Rom::offset() const {
    return m_offset;
}

bool Rom::hasHeader() const {
    return m_offset != 0;
}
