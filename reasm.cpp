#include "reasm.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "opcodes.hpp"

namespace {

struct ParsedLine {
    enum class Kind {
        Empty,
        Bank,
        Label,
        Equ,
        Db,
        Dw,
        Dl,
        Instr,
    };

    Kind kind = Kind::Empty;
    int lineNo = 0;
    std::string raw;
    std::string text;

    uint8_t bank = 0;
    std::string label;
    std::string mnemonic;
    std::string operand;
    std::vector<uint32_t> dbValues;
    std::vector<std::string> dataItems;

    uint32_t pc = 0;
    uint8_t size = 0;
    uint8_t pBefore = 0x34;
    bool definesPc = false;
    std::array<uint8_t, 4> bytes{};
};

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

std::string upper(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool parseHexUnsigned(const std::string& s, uint32_t& out) {
    if (s.empty()) return false;
    uint32_t value = 0;
    for (char c : s) {
        value <<= 4;
        if (c >= '0' && c <= '9') value |= static_cast<uint32_t>(c - '0');
        else if (c >= 'A' && c <= 'F') value |= static_cast<uint32_t>(10 + c - 'A');
        else if (c >= 'a' && c <= 'f') value |= static_cast<uint32_t>(10 + c - 'a');
        else return false;
    }
    out = value;
    return true;
}

bool parseHexDollar(const std::string& s, uint32_t& out) {
    if (s.size() < 2 || s[0] != '$') return false;
    return parseHexUnsigned(s.substr(1), out);
}

size_t hexDigitsAfterDollar(const std::string& token) {
    if (token.size() < 2 || token[0] != '$') return 0;
    size_t n = 0;
    for (size_t i = 1; i < token.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(token[i]))) return 0;
        ++n;
    }
    return n;
}

bool isLoRomArea(uint8_t /*bank*/, uint16_t addr) {
    return addr >= 0x8000;
}

uint32_t loRomToOffset(uint8_t bank, uint16_t addr) {
    return ((bank & 0x7F) << 15) | (addr - 0x8000);
}

uint8_t instrSize(const Op& op, uint8_t p) {
    switch (op.mode) {
        case AddrMode::ImmediateM:
            return (p & 0x20) ? 2 : 3;
        case AddrMode::ImmediateX:
            return (p & 0x10) ? 2 : 3;
        default:
            return op.size ? op.size : 1;
    }
}

bool labelToAddress(const std::string& s, uint32_t& out) {
    if (startsWith(s, "L_") && s.size() == 8) {
        return parseHexUnsigned(s.substr(2), out);
    }
    if (startsWith(s, "Func_") && s.size() == 11) {
        return parseHexUnsigned(s.substr(5), out);
    }
    if (startsWith(s, "PtrTable_") && s.size() == 15) {
        return parseHexUnsigned(s.substr(9), out);
    }
    if (startsWith(s, "PtrTable24_") && s.size() == 17) {
        return parseHexUnsigned(s.substr(11), out);
    }
    if (startsWith(s, "DataWords_") && s.size() == 16) {
        return parseHexUnsigned(s.substr(10), out);
    }
    return false;
}

std::string removeSpaces(std::string s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) {
        return std::isspace(c);
    }), s.end());
    return s;
}

std::optional<uint8_t> immediateSizeFromOperandText(const std::string& operand) {
    std::string t = removeSpaces(trim(operand));
    if (t.size() < 3 || t[0] != '#' || t[1] != '$') return std::nullopt;

    const std::string hex = t.substr(2);
    if (hex.empty()) return std::nullopt;

    for (char c : hex) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
    }

    if (hex.size() <= 2) return static_cast<uint8_t>(2);
    if (hex.size() <= 4) return static_cast<uint8_t>(3);
    return std::nullopt;
}

std::optional<AddrMode> detectModeFromOperand(const std::string& mnemonic,
                                              const std::string& operand) {
    const std::string op = trim(operand);
    const std::string opNoSp = removeSpaces(op);

    if (op.empty()) return AddrMode::Implied;
    if (upper(op) == "A") return AddrMode::Accumulator;

    if (op[0] == '#') {
        if (upper(mnemonic) == "REP" || upper(mnemonic) == "SEP") {
            return AddrMode::Immediate8;
        }
        return std::nullopt;
    }

    if ((upper(mnemonic) == "MVN" || upper(mnemonic) == "MVP") &&
        opNoSp.find(',') != std::string::npos) {
        return AddrMode::BlockMove;
    }

    if (startsWith(opNoSp, "($") && opNoSp.find(",S),Y") != std::string::npos) {
        return AddrMode::StackRelativeIndirectY;
    }

    if (startsWith(opNoSp, "($") && opNoSp.find(",X)") != std::string::npos) {
        const auto end = opNoSp.find(',');
        const auto token = opNoSp.substr(1, end - 1);
        const size_t digits = hexDigitsAfterDollar(token);
        if (digits <= 2) return AddrMode::DirectXIndirect;
        return AddrMode::AbsoluteXIndirect;
    }

    if (startsWith(opNoSp, "($") && opNoSp.find("),Y") != std::string::npos) {
        return AddrMode::DirectIndirectY;
    }

    if (startsWith(opNoSp, "($") && opNoSp.back() == ')') {
        const auto token = opNoSp.substr(1, opNoSp.size() - 2);
        const size_t digits = hexDigitsAfterDollar(token);
        if (digits <= 2) return AddrMode::DirectIndirect;
        return AddrMode::AbsoluteIndirect;
    }

    if (startsWith(opNoSp, "[$") && opNoSp.find("],Y") != std::string::npos) {
        return AddrMode::DirectIndirectLongY;
    }

    if (startsWith(opNoSp, "[$") && opNoSp.back() == ']') {
        const auto token = opNoSp.substr(1, opNoSp.size() - 2);
        const size_t digits = hexDigitsAfterDollar(token);
        if (digits <= 2) return AddrMode::DirectIndirectLong;
        return AddrMode::AbsoluteIndirectLong;
    }

    if (opNoSp.find(",S") != std::string::npos) return AddrMode::StackRelative;

    if (startsWith(opNoSp, "L_") || startsWith(opNoSp, "Func_") ||
        startsWith(opNoSp, "PtrTable_") || startsWith(opNoSp, "PtrTable24_")) {
        auto comma = opNoSp.find(',');
        if (comma == std::string::npos) return AddrMode::Absolute;
        const std::string suffix = upper(opNoSp.substr(comma + 1));
        if (suffix == "X") return AddrMode::AbsoluteX;
        if (suffix == "Y") return AddrMode::AbsoluteY;
        return std::nullopt;
    }

    uint32_t value = 0;
    size_t digits = 0;
    std::string base = opNoSp;
    auto comma = base.find(',');
    if (comma != std::string::npos) base = base.substr(0, comma);

    digits = hexDigitsAfterDollar(base);
    if (!parseHexDollar(base, value)) {
        // Unknown identifier: forward reference or equ symbol (hw register, RAM label, etc.)
        if (!base.empty() && std::isalpha(static_cast<unsigned char>(base[0])) &&
            upper(base) != "A") {
            const auto c2 = opNoSp.find(',');
            if (c2 == std::string::npos) return AddrMode::Absolute;
            const std::string sfx = upper(opNoSp.substr(c2 + 1));
            if (sfx == "X") return AddrMode::AbsoluteX;
            if (sfx == "Y") return AddrMode::AbsoluteY;
        }
        return std::nullopt;
    }

    comma = opNoSp.find(',');
    if (comma == std::string::npos) {
        if (digits <= 2) return AddrMode::DirectPage;
        if (digits <= 4) return AddrMode::Absolute;
        return AddrMode::AbsoluteLong;
    }

    const std::string suffix = upper(opNoSp.substr(comma + 1));
    if (suffix == "X") {
        if (digits <= 2) return AddrMode::DirectPageX;
        if (digits <= 4) return AddrMode::AbsoluteX;
        return AddrMode::AbsoluteLongX;
    }
    if (suffix == "Y") {
        if (digits <= 2) return AddrMode::DirectPageY;
        return AddrMode::AbsoluteY;
    }

    return std::nullopt;
}

bool isBranchMnemonic(const std::string& m) {
    const std::string u = upper(m);
    return u == "BPL" || u == "BMI" || u == "BVC" || u == "BVS" ||
           u == "BCC" || u == "BCS" || u == "BNE" || u == "BEQ" ||
           u == "BRA" || u == "BRL" || u == "PER";
}

std::vector<uint8_t> findOpcodesByMnemonic(const std::string& mnemonic) {
    std::vector<uint8_t> out;
    const std::string u = upper(mnemonic);

    for (int i = 0; i < 256; ++i) {
        const auto& op = OPCODES[static_cast<size_t>(i)];
        if (op.valid && u == op.name) {
            out.push_back(static_cast<uint8_t>(i));
        }
    }

    return out;
}

bool parseDbValues(const std::string& s, std::vector<uint32_t>& values) {
    std::string rest = trim(s);
    if (!startsWith(upper(rest), ".DB")) return false;

    rest = trim(rest.substr(3));
    if (rest.empty()) return true;

    std::stringstream ss(rest);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        uint32_t v = 0;
        if (!parseHexDollar(item, v) || v > 0xFF) return false;
        values.push_back(v);
    }

    return true;
}

bool parseDataDirective(const std::string& s,
                        const std::string& directive,
                        std::vector<std::string>& items) {
    std::string rest = trim(s);
    const std::string up = upper(rest);
    if (!startsWith(up, directive)) return false;

    rest = trim(rest.substr(directive.size()));
    if (rest.empty()) return true;

    std::stringstream ss(rest);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (item.empty()) return false;
        items.push_back(item);
    }
    return true;
}

bool parseBankLine(const std::string& s, uint8_t& bank) {
    const std::string t = trim(s);
    const std::string prefix = "; ===== BANK $";
    auto u = upper(t);

    if (!startsWith(u, prefix)) return false;

    auto pos = t.find('$');
    if (pos == std::string::npos || pos + 2 >= t.size()) return false;

    uint32_t v = 0;
    if (!parseHexUnsigned(t.substr(pos + 1, 2), v) || v > 0xFF) return false;

    bank = static_cast<uint8_t>(v);
    return true;
}

bool parseLine(const std::string& raw, int lineNo, ParsedLine& line, std::string& error) {
    line = {};
    line.lineNo = lineNo;
    line.raw = raw;

    uint8_t bank = 0;
    if (parseBankLine(trim(raw), bank)) {
        line.kind = ParsedLine::Kind::Bank;
        line.bank = bank;
        line.text = trim(raw);
        return true;
    }

    std::string noComment = raw;
    auto semi = noComment.find(';');
    if (semi != std::string::npos) {
        noComment = noComment.substr(0, semi);
    }
    line.text = trim(noComment);

    if (line.text.empty()) {
        line.kind = ParsedLine::Kind::Empty;
        return true;
    }

    if (parseBankLine(line.text, bank)) {
        line.kind = ParsedLine::Kind::Bank;
        line.bank = bank;
        return true;
    }

    if (line.text.back() == ':') {
        line.kind = ParsedLine::Kind::Label;
        line.label = trim(line.text.substr(0, line.text.size() - 1));
        return true;
    }

    std::vector<std::string> dataItems;
    if (parseDataDirective(line.text, ".DW", dataItems)) {
        line.kind = ParsedLine::Kind::Dw;
        line.dataItems = std::move(dataItems);
        return true;
    }
    if (parseDataDirective(line.text, ".DL", dataItems)) {
        line.kind = ParsedLine::Kind::Dl;
        line.dataItems = std::move(dataItems);
        return true;
    }

    std::vector<uint32_t> dbVals;
    if (parseDbValues(line.text, dbVals)) {
        line.kind = ParsedLine::Kind::Db;
        line.dbValues = std::move(dbVals);
        return true;
    }

    std::istringstream iss(line.text);
    std::string mnemonic;
    iss >> mnemonic;
    if (mnemonic.empty()) {
        line.kind = ParsedLine::Kind::Empty;
        return true;
    }

    // Detect "Name equ $XXXX"
    std::string word2, word3;
    iss >> word2 >> word3;
    if (upper(word2) == "EQU" && !word3.empty()) {
        uint32_t value = 0;
        if (parseHexDollar(word3, value)) {
            line.kind = ParsedLine::Kind::Equ;
            line.label = mnemonic;
            line.dbValues.push_back(value);
            return true;
        }
    }

    std::string operand = word2;
    if (!word3.empty()) operand += " " + word3;
    std::string rest;
    std::getline(iss, rest);
    if (!rest.empty()) operand += rest;

    line.kind = ParsedLine::Kind::Instr;
    line.mnemonic = upper(trim(mnemonic));
    line.operand = trim(operand);
    return true;
}

const Op* chooseOpcode(const ParsedLine& line,
                       uint8_t p,
                       const std::unordered_map<std::string, uint32_t>& labels,
                       std::string& error) {
    const auto candidates = findOpcodesByMnemonic(line.mnemonic);
    if (candidates.empty()) {
        error = "Line " + std::to_string(line.lineNo) +
                ": unknown mnemonic '" + line.mnemonic + "'";
        return nullptr;
    }

    std::vector<const Op*> matches;
    const std::string operandCompact = removeSpaces(line.operand);

    // Returns true for any label name (known prefix or present in labels map)
    auto isNamedLabel = [&](const std::string& s) {
        const std::string base = s.substr(0, s.find(','));
        if (startsWith(base, "L_") || startsWith(base, "Func_") ||
            startsWith(base, "PtrTable_") || startsWith(base, "PtrTable24_") ||
            startsWith(base, "ram_") || startsWith(base, "wram_"))
            return true;
        return labels.count(base) > 0;
    };

    // Resolve a label name to its address (returns false if not found)
    auto resolveLabel = [&](const std::string& name, uint32_t& out) -> bool {
        auto it = labels.find(name);
        if (it != labels.end()) { out = it->second; return true; }
        return labelToAddress(name, out);
    };

    const bool operandIsLabel = isNamedLabel(operandCompact);
    const bool operandLabelWithIndex =
        operandIsLabel && operandCompact.find(',') != std::string::npos;

    // Detect mode, falling back to label address lookup for unknown identifiers
    auto detectedMode = detectModeFromOperand(line.mnemonic, line.operand);
    if (!detectedMode && operandIsLabel) {
        const std::string base = operandCompact.substr(0, operandCompact.find(','));
        const bool hasIndex = operandCompact.find(',') != std::string::npos;
        const std::string suffix = hasIndex
            ? upper(operandCompact.substr(operandCompact.find(',') + 1)) : "";
        uint32_t addr = 0;
        if (resolveLabel(base, addr)) {
            const bool is24bit = addr > 0xFFFF;
            if (!hasIndex)
                detectedMode = is24bit ? AddrMode::AbsoluteLong : AddrMode::Absolute;
            else if (suffix == "X")
                detectedMode = is24bit ? AddrMode::AbsoluteLongX : AddrMode::AbsoluteX;
            else if (suffix == "Y")
                detectedMode = AddrMode::AbsoluteY;
        }
    }

    // Disasm emits PPU regs as NAME_with_bank; equ $002121 parses as $2121 (< 65536).
    // detectModeFromOperand already guessed Absolute for identifiers — force absolute-long.
    if (operandIsLabel) {
        const std::string base = operandCompact.substr(0, operandCompact.find(','));
        if (endsWith(base, "_with_bank")) {
            const bool hasIndex = operandCompact.find(',') != std::string::npos;
            const std::string suffix = hasIndex
                ? upper(operandCompact.substr(operandCompact.find(',') + 1)) : "";
            uint32_t addr = 0;
            if (resolveLabel(base, addr)) {
                if (!hasIndex)
                    detectedMode = AddrMode::AbsoluteLong;
                else if (suffix == "X")
                    detectedMode = AddrMode::AbsoluteLongX;
            }
        }
    }

    for (uint8_t opcode : candidates) {
        const Op& op = OPCODES[opcode];

        if (operandIsLabel && !operandLabelWithIndex) {
            if (isBranchMnemonic(line.mnemonic)) {
                if (op.mode == AddrMode::Relative8 || op.mode == AddrMode::Relative16 ||
                    op.mode == AddrMode::Absolute || op.mode == AddrMode::AbsoluteLong) {
                    matches.push_back(&op);
                }
            } else if ((line.mnemonic == "JSR" || line.mnemonic == "JMP") &&
                       (op.mode == AddrMode::Absolute || op.mode == AddrMode::AbsoluteLong)) {
                matches.push_back(&op);
            } else if ((line.mnemonic == "JSL" || line.mnemonic == "JML") &&
                       op.mode == AddrMode::AbsoluteLong) {
                matches.push_back(&op);
            } else if (op.mode == AddrMode::Absolute || op.mode == AddrMode::AbsoluteLong) {
                matches.push_back(&op);
            }
            continue;
        }

        if (isBranchMnemonic(line.mnemonic)) {
            if (op.mode == AddrMode::Relative8 || op.mode == AddrMode::Relative16) {
                matches.push_back(&op);
            }
            continue;
        }

        if (line.operand.empty()) {
            if (op.mode == AddrMode::Implied || op.mode == AddrMode::Accumulator) {
                matches.push_back(&op);
            }
            continue;
        }

        if (!line.operand.empty() && line.operand[0] == '#') {
            if (op.mode == AddrMode::Immediate8 ||
                op.mode == AddrMode::ImmediateM ||
                op.mode == AddrMode::ImmediateX) {
                matches.push_back(&op);
            }
            continue;
        }

        if (detectedMode && op.mode == *detectedMode) {
            matches.push_back(&op);
        }
    }

    if (matches.empty()) {
        error = "Line " + std::to_string(line.lineNo) +
                ": no opcode match for '" + line.mnemonic +
                (line.operand.empty() ? "" : (" " + line.operand)) + "'";
        return nullptr;
    }

    auto score = [&](const Op* op) {
        int s = 0;

        if (line.operand.empty() && op->mode == AddrMode::Implied) s += 10;

        if (!line.operand.empty() && line.operand[0] == '#') {
            if (op->mode == AddrMode::Immediate8) s += 20;
            if (op->mode == AddrMode::ImmediateM || op->mode == AddrMode::ImmediateX) s += 25;
        }

        if (detectedMode && op->mode == *detectedMode) s += 30;

        if (isBranchMnemonic(line.mnemonic) &&
            (op->mode == AddrMode::Relative8 || op->mode == AddrMode::Relative16)) {
            s += 40;
        }

        if (operandIsLabel &&
            (line.mnemonic == "JSR" || line.mnemonic == "JMP") &&
            op->mode == AddrMode::Absolute) {
            s += 20;
        }

        if (operandIsLabel &&
            (line.mnemonic == "JSL" || line.mnemonic == "JML") &&
            op->mode == AddrMode::AbsoluteLong) {
            s += 20;
        }

        if ((op->mode == AddrMode::ImmediateM || op->mode == AddrMode::ImmediateX) &&
            instrSize(*op, p) > 1) {
            s += 5;
        }

        return s;
    };

    return *std::max_element(matches.begin(), matches.end(),
                             [&](const Op* a, const Op* b) {
                                 return score(a) < score(b);
                             });
}

bool parseNumericOrLabel(const std::string& s,
                         const std::unordered_map<std::string, uint32_t>& labels,
                         uint32_t& out) {
    std::string t = trim(removeSpaces(s));
    if (t.empty()) return false;

    auto it = labels.find(t);
    if (it != labels.end()) {
        out = it->second;
        return true;
    }

    if (parseHexDollar(t, out)) return true;
    return labelToAddress(t, out);
}

bool encodeInstruction(const ParsedLine& line,
                       const Op& op,
                       uint8_t pBefore,
                       const std::unordered_map<std::string, uint32_t>& labels,
                       std::array<uint8_t, 4>& bytes,
                       uint8_t& size,
                       std::string& error) {
    uint8_t opcode = 0xFF;
    bool found = false;

    for (int i = 0; i < 256; ++i) {
        if (&OPCODES[static_cast<size_t>(i)] == &op) {
            opcode = static_cast<uint8_t>(i);
            found = true;
            break;
        }
    }

    if (!found) {
        error = "Line " + std::to_string(line.lineNo) +
                ": internal opcode lookup failed";
        return false;
    }

    size = instrSize(op, pBefore);
    if ((op.mode == AddrMode::ImmediateM || op.mode == AddrMode::ImmediateX)) {
        if (auto forced = immediateSizeFromOperandText(line.operand)) {
            size = *forced;
        }
    }

    bytes = {0, 0, 0, 0};
    bytes[0] = opcode;

    auto opStr = removeSpaces(line.operand);
    uint32_t value = 0;

    switch (op.mode) {
        case AddrMode::Implied:
        case AddrMode::Accumulator:
            return true;

        case AddrMode::Immediate8:
            if (opStr.empty() || opStr[0] != '#' ||
                !parseHexDollar(opStr.substr(1), value) || value > 0xFF) {
                error = "Line " + std::to_string(line.lineNo) +
                        ": invalid immediate8 operand";
                return false;
            }
            bytes[1] = static_cast<uint8_t>(value);
            return true;

        case AddrMode::ImmediateM:
        case AddrMode::ImmediateX:
            if (opStr.empty() || opStr[0] != '#' ||
                !parseHexDollar(opStr.substr(1), value)) {
                error = "Line " + std::to_string(line.lineNo) +
                        ": invalid immediate operand";
                return false;
            }

            if (size == 2) {
                if (value > 0xFF) {
                    error = "Line " + std::to_string(line.lineNo) +
                            ": immediate too large for 8-bit mode";
                    return false;
                }
                bytes[1] = static_cast<uint8_t>(value);
            } else {
                if (value > 0xFFFF) {
                    error = "Line " + std::to_string(line.lineNo) +
                            ": immediate too large for 16-bit mode";
                    return false;
                }
                bytes[1] = static_cast<uint8_t>(value & 0xFF);
                bytes[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
            }
            return true;

        case AddrMode::DirectPage:
        case AddrMode::DirectPageX:
        case AddrMode::DirectPageY:
        case AddrMode::DirectIndirect:
        case AddrMode::DirectIndirectY:
        case AddrMode::DirectIndirectLong:
        case AddrMode::DirectIndirectLongY:
        case AddrMode::DirectXIndirect:
        case AddrMode::StackRelative:
        case AddrMode::StackRelativeIndirectY: {
            std::string t = opStr;
            for (char ch : std::string{"()[]"}) {
                t.erase(std::remove(t.begin(), t.end(), ch), t.end());
            }
            auto comma = t.find(',');
            if (comma != std::string::npos) t = t.substr(0, comma);

            if (!parseHexDollar(t, value) || value > 0xFF) {
                error = "Line " + std::to_string(line.lineNo) +
                        ": invalid 8-bit operand";
                return false;
            }

            bytes[1] = static_cast<uint8_t>(value);
            return true;
        }

        case AddrMode::Absolute:
        case AddrMode::AbsoluteX:
        case AddrMode::AbsoluteY:
        case AddrMode::AbsoluteIndirect:
        case AddrMode::AbsoluteXIndirect:
        case AddrMode::AbsoluteIndirectLong: {
            std::string t = opStr;
            for (char ch : std::string{"()[]"}) {
                t.erase(std::remove(t.begin(), t.end(), ch), t.end());
            }
            auto comma = t.find(',');
            if (comma != std::string::npos) t = t.substr(0, comma);

            if (!parseNumericOrLabel(t, labels, value) || value > 0xFFFFFF) {
                error = "Line " + std::to_string(line.lineNo) +
                        ": invalid absolute operand";
                return false;
            }

            bytes[1] = static_cast<uint8_t>(value & 0xFF);
            bytes[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
            return true;
        }

        case AddrMode::AbsoluteLong:
        case AddrMode::AbsoluteLongX: {
            std::string t = opStr;
            auto comma = t.find(',');
            if (comma != std::string::npos) t = t.substr(0, comma);

            if (!parseNumericOrLabel(t, labels, value) || value > 0xFFFFFF) {
                error = "Line " + std::to_string(line.lineNo) +
                        ": invalid long operand";
                return false;
            }

            bytes[1] = static_cast<uint8_t>(value & 0xFF);
            bytes[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
            bytes[3] = static_cast<uint8_t>((value >> 16) & 0xFF);
            return true;
        }

        case AddrMode::Relative8:
        case AddrMode::Relative16: {
            if (!parseNumericOrLabel(opStr, labels, value)) {
                error = "Line " + std::to_string(line.lineNo) +
                        ": invalid branch target '" + line.operand + "'";
                return false;
            }

            const uint16_t nextAddr =
                static_cast<uint16_t>((line.pc & 0xFFFF) + instrSize(op, pBefore));
            const uint16_t targetAddr = static_cast<uint16_t>(value & 0xFFFF);
            const int32_t delta =
                static_cast<int32_t>(static_cast<int16_t>(targetAddr - nextAddr));

            if (op.mode == AddrMode::Relative8) {
                if (delta < -128 || delta > 127) {
                    error = "Line " + std::to_string(line.lineNo) +
                            ": relative8 target out of range";
                    return false;
                }
                bytes[1] = static_cast<uint8_t>(delta & 0xFF);
            } else {
                bytes[1] = static_cast<uint8_t>(delta & 0xFF);
                bytes[2] = static_cast<uint8_t>((delta >> 8) & 0xFF);
            }

            return true;
        }

        case AddrMode::BlockMove: {
            auto comma = opStr.find(',');
            if (comma == std::string::npos) {
                error = "Line " + std::to_string(line.lineNo) +
                        ": invalid block move operand";
                return false;
            }

            uint32_t src = 0;
            uint32_t dst = 0;
            if (!parseHexDollar(opStr.substr(0, comma), src) || src > 0xFF ||
                !parseHexDollar(opStr.substr(comma + 1), dst) || dst > 0xFF) {
                error = "Line " + std::to_string(line.lineNo) +
                        ": invalid block move bytes";
                return false;
            }

            bytes[1] = static_cast<uint8_t>(src);
            bytes[2] = static_cast<uint8_t>(dst);
            return true;
        }

        case AddrMode::Unknown:
        default:
            error = "Line " + std::to_string(line.lineNo) +
                    ": unsupported addressing mode for reassembly";
            return false;
    }
}

bool assignPcAndLabels(std::vector<ParsedLine>& lines,
                       std::unordered_map<std::string, uint32_t>& labels,
                       std::string& error) {
    uint8_t currentBank = 0;
    bool haveBank = false;
    uint32_t currentPc = 0;
    uint8_t p = 0x34;

    for (auto& line : lines) {
        line.pBefore = p;

        switch (line.kind) {
            case ParsedLine::Kind::Empty:
                break;

            case ParsedLine::Kind::Equ:
                labels[line.label] = line.dbValues[0];
                break;

            case ParsedLine::Kind::Bank:
                haveBank = true;
                currentBank = line.bank;
                currentPc = (static_cast<uint32_t>(currentBank) << 16) | 0x8000u;
                p = 0x34;
                line.pc = currentPc;
                break;

            case ParsedLine::Kind::Label: {
                if (!haveBank) {
                    error = "Line " + std::to_string(line.lineNo) +
                            ": label before any BANK section";
                    return false;
                }

                line.pc = currentPc;
                line.definesPc = true;

                uint32_t addr = 0;
                if (labelToAddress(line.label, addr)) {
                    currentPc = addr;
                    line.pc = currentPc;
                } else if (auto itEq = labels.find(line.label); itEq != labels.end()) {
                    // Anchor to value from a preceding `Name equ $...` (matches disasm dumps).
                    addr = itEq->second;
                    currentPc = addr;
                    line.pc = currentPc;
                } else {
                    addr = currentPc;
                }

                labels[line.label] = addr;
                break;
            }

            case ParsedLine::Kind::Db:
                if (!haveBank) {
                    error = "Line " + std::to_string(line.lineNo) +
                            ": .db before any BANK section";
                    return false;
                }
                line.pc = currentPc;
                line.size = static_cast<uint8_t>(line.dbValues.size());
                currentPc += line.size;
                break;

            case ParsedLine::Kind::Dw:
                if (!haveBank) {
                    error = "Line " + std::to_string(line.lineNo) +
                            ": .dw before any BANK section";
                    return false;
                }
                line.pc = currentPc;
                line.size = static_cast<uint8_t>(line.dataItems.size() * 2);
                currentPc += line.size;
                break;

            case ParsedLine::Kind::Dl:
                if (!haveBank) {
                    error = "Line " + std::to_string(line.lineNo) +
                            ": .dl before any BANK section";
                    return false;
                }
                line.pc = currentPc;
                line.size = static_cast<uint8_t>(line.dataItems.size() * 3);
                currentPc += line.size;
                break;

            case ParsedLine::Kind::Instr: {
                if (!haveBank) {
                    error = "Line " + std::to_string(line.lineNo) +
                            ": instruction before any BANK section";
                    return false;
                }

                line.pc = currentPc;

                const Op* op = chooseOpcode(line, p, labels, error);
                if (!op) return false;

                line.size = instrSize(*op, p);
                if ((op->mode == AddrMode::ImmediateM || op->mode == AddrMode::ImmediateX)) {
                    if (auto forced = immediateSizeFromOperandText(line.operand)) {
                        line.size = *forced;
                    }
                }

                currentPc += line.size;

                if (line.mnemonic == "REP" && !line.operand.empty()) {
                    uint32_t imm = 0;
                    std::string opText = removeSpaces(line.operand);
                    if (opText.size() < 2 || opText[0] != '#' ||
                        !parseHexDollar(opText.substr(1), imm) || imm > 0xFF) {
                        error = "Line " + std::to_string(line.lineNo) +
                                ": invalid REP immediate";
                        return false;
                    }
                    p &= static_cast<uint8_t>(~imm);
                } else if (line.mnemonic == "SEP" && !line.operand.empty()) {
                    uint32_t imm = 0;
                    std::string opText = removeSpaces(line.operand);
                    if (opText.size() < 2 || opText[0] != '#' ||
                        !parseHexDollar(opText.substr(1), imm) || imm > 0xFF) {
                        error = "Line " + std::to_string(line.lineNo) +
                                ": invalid SEP immediate";
                        return false;
                    }
                    p |= static_cast<uint8_t>(imm);
                }

                break;
            }
        }
    }

    return true;
}

} // namespace

bool reassembleDumpAsmToRom(const std::string& asmPath,
                            std::vector<unsigned char>& romOut,
                            std::string& error) {
    std::ifstream in(asmPath);
    if (!in) {
        error = "Cannot open asm file: " + asmPath;
        return false;
    }

    std::vector<ParsedLine> lines;
    std::string raw;
    int lineNo = 0;

    while (std::getline(in, raw)) {
        ParsedLine line;
        if (!parseLine(raw, ++lineNo, line, error)) return false;
        lines.push_back(std::move(line));
    }

    std::unordered_map<std::string, uint32_t> labels;
    if (!assignPcAndLabels(lines, labels, error)) return false;

    uint32_t maxOffset = 0;
    for (const auto& line : lines) {
        if (line.kind == ParsedLine::Kind::Instr ||
            line.kind == ParsedLine::Kind::Db ||
            line.kind == ParsedLine::Kind::Dw ||
            line.kind == ParsedLine::Kind::Dl) {
            const uint8_t bank = static_cast<uint8_t>(line.pc >> 16);
            const uint16_t addr = static_cast<uint16_t>(line.pc & 0xFFFF);

            if (!isLoRomArea(bank, addr)) {
                error = "Line " + std::to_string(line.lineNo) +
                        ": PC outside LoROM area";
                return false;
            }

            maxOffset = std::max(maxOffset, loRomToOffset(bank, addr) + line.size);
        }
    }

    romOut.assign(maxOffset, 0xFF);

    for (auto& line : lines) {
        if (line.kind == ParsedLine::Kind::Db) {
            const uint32_t off = loRomToOffset(
                static_cast<uint8_t>(line.pc >> 16),
                static_cast<uint16_t>(line.pc & 0xFFFF));

            for (size_t i = 0; i < line.dbValues.size(); ++i) {
                romOut[off + i] = static_cast<uint8_t>(line.dbValues[i]);
            }
            continue;
        }

        if (line.kind == ParsedLine::Kind::Dw) {
            const uint32_t off = loRomToOffset(
                static_cast<uint8_t>(line.pc >> 16),
                static_cast<uint16_t>(line.pc & 0xFFFF));

            for (size_t i = 0; i < line.dataItems.size(); ++i) {
                uint32_t value = 0;
                if (!parseNumericOrLabel(line.dataItems[i], labels, value) || value > 0xFFFFFF) {
                    error = "Line " + std::to_string(line.lineNo) +
                            ": invalid .dw item '" + line.dataItems[i] + "'";
                    return false;
                }
                romOut[off + i * 2 + 0] = static_cast<uint8_t>(value & 0xFF);
                romOut[off + i * 2 + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
            }
            continue;
        }

        if (line.kind == ParsedLine::Kind::Dl) {
            const uint32_t off = loRomToOffset(
                static_cast<uint8_t>(line.pc >> 16),
                static_cast<uint16_t>(line.pc & 0xFFFF));

            for (size_t i = 0; i < line.dataItems.size(); ++i) {
                uint32_t value = 0;
                if (!parseNumericOrLabel(line.dataItems[i], labels, value) || value > 0xFFFFFF) {
                    error = "Line " + std::to_string(line.lineNo) +
                            ": invalid .dl item '" + line.dataItems[i] + "'";
                    return false;
                }
                romOut[off + i * 3 + 0] = static_cast<uint8_t>(value & 0xFF);
                romOut[off + i * 3 + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
                romOut[off + i * 3 + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
            }
            continue;
        }

        if (line.kind != ParsedLine::Kind::Instr) continue;

        const Op* op = chooseOpcode(line, line.pBefore, labels, error);
        if (!op) return false;

        uint8_t size = 0;
        std::array<uint8_t, 4> bytes{};
        if (!encodeInstruction(line, *op, line.pBefore, labels, bytes, size, error)) {
            return false;
        }

        const uint32_t off = loRomToOffset(
            static_cast<uint8_t>(line.pc >> 16),
            static_cast<uint16_t>(line.pc & 0xFFFF));

        for (size_t i = 0; i < size; ++i) {
            romOut[off + i] = bytes[i];
        }

        line.bytes = bytes;
    }

    return true;
}

bool reassembleDumpAsmToRomFile(const std::string& asmPath,
                                const std::string& outRomPath,
                                std::string& error) {
    std::vector<unsigned char> rom;
    if (!reassembleDumpAsmToRom(asmPath, rom, error)) return false;

    std::ofstream out(outRomPath, std::ios::binary);
    if (!out) {
        error = "Cannot open output ROM file: " + outRomPath;
        return false;
    }

    out.write(reinterpret_cast<const char*>(rom.data()),
              static_cast<std::streamsize>(rom.size()));

    if (!out) {
        error = "Failed while writing output ROM file: " + outRomPath;
        return false;
    }

    return true;
}