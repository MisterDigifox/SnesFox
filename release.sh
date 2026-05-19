#!/bin/bash

ROM_NAME="hello_world.sfc"
OUTPUT_ASM="output.asm"
OUTPUT_ROM="out.sfc"

./build.sh;
./snesfox disasm $ROM_NAME;
./snesfox reasm $OUTPUT_ASM $OUTPUT_ROM;

palettes=$(grep -c '^Palette16_' output.asm)
echo "instr=$(grep -E '^    [A-Z]{2,4}($| )' output.asm | wc -l) dw=$(grep -c '^    \.dw ' output.asm) dl=$(grep -c '^    \.dl ' output.asm) db=$(grep -c '^    \.db ' output.asm) palettes=$palettes colors=$((palettes * 16)) hw_comments=$(grep -c '^    ; .*register access' output.asm) labels=$(grep -Ec '^(L_|Func_|ResetHandler|NativeNMIHandler|NativeIRQHandler|EmuNMIHandler|EmuIRQBRKHandler|EarlyInit_)' output.asm)"

cmp $ROM_NAME $OUTPUT_ROM
