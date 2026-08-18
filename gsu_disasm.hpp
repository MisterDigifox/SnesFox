#pragma once
#include <cstdint>
#include <string>

// Static GSU (Super FX) instruction disassembler — mnemonic table verified against real
// StarFox.sfc ROM bytes (see .claude/skills/emulate-gsu-starfox). `opcodeAddr` is the address of
// the opcode byte itself (NOT GSU::pc(), which per the real pipelined-fetch convention already
// points one byte past the opcode — callers must pass pc()-1).
std::string gsuDisassemble(uint16_t opcodeAddr, uint8_t opcode, bool alt1, bool alt2,
                            uint8_t operand1, uint8_t operand2);
