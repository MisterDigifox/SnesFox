#!/usr/bin/env python3
"""
Emit C++ initializer rows for snesfox buildApuOpcodeTable().
Requires Sony SPC700 APU programming manual (/tmp/spcmanual.txt recommended).
curl -sL -o /tmp/spcmanual.txt http://snesmusic.org/files/spc700_apu_manual.txt
"""
from __future__ import annotations

import re
import sys


def load_entries(source: str) -> dict[int, tuple[str, int, int]]:
    entries: dict[int, tuple[str, int, int]] = {}
    pat = re.compile(r"^\s{2}(.+?)\s+([0-9A-F]{2})\s+(\d+)\s+(\d+)")
    for line in open(source, encoding="utf-8", errors="ignore"):
        m = pat.match(line)
        if not m:
            continue
        opc = int(m.group(2), 16)
        entries[opc] = (m.group(1).strip(), int(m.group(3)), int(m.group(4)))

    entries[0x0C] = ("ASL labs", 3, 5)  #_opcode 0C; Sony table duplicates CC
    entries[0xCC] = ("MOV labs,Y", 3, 5)

    for n in range(16):
        entries[(n << 4) | 1] = (f"TCALL{n}", 1, 8)
    for _b in range(8):
        entries[0x02 + (_b << 5)] = ("SET1", 2, 4)
        entries[0x12 + (_b << 5)] = ("CLR1", 2, 4)
        entries[0x03 + (_b << 5)] = ("BBC", 3, 5)
        entries[0x13 + (_b << 5)] = ("BBS", 3, 5)

    if len(entries) != 256:
        raise SystemExit(f"expected 256 opcodes got {len(entries)}")
    return entries


def name_for(full: str) -> str:
    parts = full.split()
    mnemonic = parts[0].upper().replace(".", "")
    if mnemonic == "OR":
        return "OR"
    if mnemonic.startswith("TCALL"):
        return mnemonic  # TCALLn
    return mnemonic


def classify_mode(full: str) -> str:
    """CpuOpcode AddrMode enumerator (with AddrMode:: prefix in output)."""
    parts = full.split()
    mnemonic = parts[0].upper().replace(".", "")
    # Operand text with spaces removed — keeps commas ("MOV A, dp" -> "A,DP").
    operands = "".join(full.split(None, 1)[1].split()).upper() if len(full.strip().split(None, 1)) > 1 else ""

    ru = "".join(parts).upper()
    urest = "".join(parts[1:]).upper() if len(parts) > 1 else ""
    uf = "".join(parts).upper()

    if mnemonic.startswith("TCALL"):
        return "Implied"

    if mnemonic in {
        "NOP",
        "BRK",
        "RET",
        "RETI",
        "RET1",
        "MUL",
        "DIV",
        "POP",
        "PUSH",
        "CLRC",
        "SETC",
        "NOTC",
        "CLRV",
        "CLRP",
        "SETP",
        "EI",
        "DI",
        "XCN",
        "DAA",
        "DAS",
        "SLEEP",
        "STOP",
    }:
        return "Implied"

    if mnemonic == "PCALL":
        return "Immediate8"

    if mnemonic in {"BRA", "BEQ", "BNE", "BCS", "BCC", "BVS", "BVC", "BMI", "BPL"}:
        return "Relative8"

    if mnemonic in {"BBC", "BBS"}:
        return "SpcDpRel8"

    if mnemonic == "CBNE":
        return "SpcDpRel8"

    if mnemonic == "DBNZ":
        if operands.startswith("Y"):
            return "Relative8"
        return "SpcDpRel8"

    if mnemonic in {"MOVW", "INCW", "DECW", "ADDW", "SUBW", "CMPW"}:
        return "DirectPage"

    if mnemonic == "CALL" or mnemonic == "JMP":
        if "+X" in full.upper() and "(" in full.upper():
            return "AbsoluteXIndirect"
        return "Absolute"

    if mnemonic in {"TSET1", "TCLR1", "NOT1", "AND1", "OR1", "EOR1", "MOV1"}:
        return "Absolute"

    if mnemonic in {"SET1", "CLR1"}:
        return "DirectPage"

    if "A,#INM" in operands or "A,#IMM" in operands:
        return "Immediate8"
    if "X,#INM" in operands or "X,#IMM" in operands:
        return "Immediate8"
    if "Y,#INM" in operands or "Y,#IMM" in operands:
        return "Immediate8"

    if "DP,#INM" in operands.upper().replace("IMM", "INM"):
        return "SpcDpImm8"

    if "(X)+(Y)" in ru or "(X)," in uf and "(Y)" in uf and mnemonic in {"ADC", "SBC", "CMP", "AND", "OR", "EOR"}:
        return "Implied"

    if "(X)+,A" in ru or "),A+,X" not in uf and mnemonic == "MOV" and uf.endswith("(X)+,A"):
        return "SpcIndirectXInc"

    if "A,(X)+" in uf or ",(X)+" in "".join(parts[2:]):
        if mnemonic == "MOV" and uf.startswith("MOV") and uf.endswith("+)"):
            return "SpcIndirectXInc"

    if mnemonic == "MOV" and urest.startswith("(X)+"):
        return "SpcIndirectXInc"

    if mnemonic == "MOV" and ",(X)+" in "".join(parts).replace(" ", ""):
        if "A,(X)+" in "".join(parts).upper().replace(" ", ""):
            return "SpcIndirectXInc"

    if uf == "MOV(X)+,A":
        return "SpcIndirectXInc"

    if uf == "MOVA,(X)+" or "A,(X)+" in "".join(parts).upper().replace(" ", ""):
        return "SpcIndirectXInc"

    if mnemonic in {"ADC", "SBC", "CMP", "AND", "OR", "EOR"}:
        op = operands
        if op.startswith("A,#") or op.startswith("X,#") or op.startswith("Y,#"):
            return "Immediate8"
        if op.startswith("DP,#"):
            return "SpcDpImm8"
        if op.startswith("(X),(Y)"):
            return "Implied"
        if op.startswith("DP(D),DP(S)"):
            return "SpcDpPair"
        if op.startswith("A,(X)") and "(DP" not in op[:8]:
            return "SpcIndirectX"
        if op.startswith("A,(DP+X)"):
            return "DirectXIndirect"
        if op.startswith("A,(DP)+Y"):
            return "DirectIndirectY"
        if op.startswith("Y,DP+X"):
            return "DirectPageX"
        if op.startswith("Y,DP"):
            return "DirectPage"
        if op.startswith("X,DP+X"):
            return "DirectPageX"
        if op.startswith("X,DP"):
            return "DirectPage"
        if op.startswith("X,LABS"):
            return "Absolute"
        if op.startswith("Y,LABS"):
            return "Absolute"
        if op.startswith("A,DP+X"):
            return "DirectPageX"
        if op.startswith("A,DP"):
            return "DirectPage"
        if op.startswith("A,LABS+X"):
            return "AbsoluteX"
        if op.startswith("A,LABS+Y"):
            return "AbsoluteY"
        if op.startswith("A,LABS"):
            return "Absolute"

    # MOV exhaustive
    if mnemonic == "MOV":
        lhs = operands
        pairs = (
            ("A,X", "Implied"),
            ("X,A", "Implied"),
            ("A,Y", "Implied"),
            ("Y,A", "Implied"),
            ("X,SP", "Implied"),
            ("SP,X", "Implied"),
            ("DP(D),DP(S)", "SpcDpPair"),
        )
        for pat, md in pairs:
            if lhs.replace(",", ",") == pat.replace(",", "").replace("()", ""):
                pass
        if lhs == "A,X" or lhs == "X,A" or lhs == "A,Y" or lhs == "Y,A" or lhs == "X,SP" or lhs == "SP,X":
            return "Implied"
        if lhs == "DP(D),DP(S)":
            return "SpcDpPair"
        if lhs.startswith("(X),A") and "+" not in lhs:
            return "SpcIndirectX"
        if lhs.startswith("(DP+X),A"):
            return "DirectXIndirect"
        if lhs.startswith("(DP)+Y,A"):
            return "DirectIndirectY"
        if lhs.startswith("LABS+X,A"):
            return "AbsoluteX"
        if lhs.startswith("LABS+Y,A"):
            return "AbsoluteY"
        if lhs.startswith("LABS,A"):
            return "Absolute"
        if lhs.startswith("LABS+X,Y"):
            return "AbsoluteY"
        if lhs.startswith("DP+X,Y"):
            return "DirectPageX"
        if lhs.startswith("DP+X,A"):
            return "DirectPageX"
        if lhs.startswith("LABS,X"):
            return "Absolute"
        if lhs.startswith("LABS,Y"):
            return "Absolute"
        if lhs.startswith("LABS,Y"):  # duplicate
            return "Absolute"
        if lhs.startswith("LABS+X,Y"):
            return "AbsoluteY"
        if lhs.startswith("DP+X,Y"):  # already
            return "DirectPageX"
        if lhs.startswith("DP+Y,X"):
            return "DirectPageY"
        if lhs.startswith("DP,X"):
            return "DirectPage"
        if lhs.startswith("DP,Y"):
            return "DirectPage"
        if lhs.startswith("DP+X,A"):
            return "DirectPageX"
        if lhs.startswith("DP,A"):
            return "DirectPage"
        if lhs.startswith("(X)+,A"):
            return "SpcIndirectXInc"
        if lhs.startswith("A,(X)+"):
            return "SpcIndirectXInc"
        if lhs.startswith("A,(DP+X)"):
            return "DirectXIndirect"
        if lhs.startswith("A,(DP)+Y"):
            return "DirectIndirectY"
        if lhs.startswith("A,DP+X"):
            return "DirectPageX"
        if lhs.startswith("A,DP"):
            return "DirectPage"
        if lhs.startswith("A,LABS+X"):
            return "AbsoluteX"
        if lhs.startswith("A,LABS+Y"):
            return "AbsoluteY"
        if lhs.startswith("A,LABS"):
            return "Absolute"
        if lhs.startswith("A,(X)") and "+-" not in lhs:
            return "SpcIndirectX"
        if lhs.startswith("X,LABS"):
            return "Absolute"
        if lhs.startswith("X,DP+Y"):
            return "DirectPageY"
        if lhs.startswith("X,DP"):
            return "DirectPage"
        if lhs.startswith("Y,DP+X"):
            return "DirectPageX"
        if lhs.startswith("Y,DP"):
            return "DirectPage"
        if lhs.startswith("Y,LABS"):
            return "Absolute"
        if lhs.startswith("DP+#INM"):
            return "SpcDpImm8"
        # MOV dp,#inm typography
        if "DP#" in lhs or lhs.startswith("DP,#"):
            return "SpcDpImm8"

    if mnemonic in {"INC", "DEC", "ASL", "LSR", "ROL", "ROR"}:
        targ = operands
        if targ in {"A", "X", "Y"}:
            return "Implied"
        if targ == "LABS":
            return "Absolute"
        if targ == "DP+X":
            return "DirectPageX"
        if targ == "DP":
            return "DirectPage"

    raise RuntimeError(f"classify_mode unresolved: opc text={full}")


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/spcmanual.txt"
    entries = load_entries(path)
    for opc in range(256):
        full, sz, cy = entries[opc]
        try:
            m = classify_mode(full)
        except RuntimeError:
            sys.stderr.write(f"FAIL opc=0x{opc:02X} [{full}] size={sz} cy={cy}\n")
            raise

        nm = name_for(full)
        print(f"    setOp(ops, 0x{opc:02X}, \"{nm}\", {sz}, AddrMode::{m}, {cy});")


if __name__ == "__main__":
    main()
