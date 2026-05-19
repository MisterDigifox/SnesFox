#pragma once

#include <string>
#include <vector>

bool reassembleDumpAsmToRom(const std::string& asmPath,
                            std::vector<unsigned char>& romOut,
                            std::string& error);

bool reassembleDumpAsmToRomFile(const std::string& asmPath,
                                const std::string& outRomPath,
                                std::string& error);
