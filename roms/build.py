#!/usr/bin/env python3
"""Star3D: a minimal, real Super FX (GSU) demo - a static 3D wireframe cube,
rotated once around the Y axis, projected and drawn entirely by the GSU (not
precomputed on the host CPU side). Hand-assembles raw 65816 + GSU machine code
directly to bytes (no external assembler) and writes star3d.sfc.

ROM layout (LoROM):
  file 0x0000-0x7FFF : bank $00 - 65816 boot code + header at 0x7FC0-0x7FFF
  file 0x8000-0xFFFF : bank $01 - GSU program (PBR=1, R15 offsets from here)
  padded to 256KB total.
"""
import math
import struct

OUT_PATH = "Star3D/star3d.sfc"
ROM_SIZE = 0x40000  # 256KB


# ---------------------------------------------------------------------------
# Tiny byte-buffer assembler (no labels needed: every branch in this program
# is either absent entirely, or a simple backward spin-loop resolved inline).
# ---------------------------------------------------------------------------
class Buf:
    def __init__(self):
        self.b = bytearray()

    def db(self, *vals):
        for v in vals:
            assert 0 <= v <= 0xFF, v
            self.b.append(v)
        return self

    def dw(self, v):
        assert 0 <= v <= 0xFFFF, v
        self.b.append(v & 0xFF)
        self.b.append((v >> 8) & 0xFF)
        return self

    def here(self):
        return len(self.b)


# ---------------------------------------------------------------------------
# 65816 boot code (bank $00, CPU address $8000 + offset == file offset)
# ---------------------------------------------------------------------------
cpu = Buf()


def A_sei(): cpu.db(0x78)
def A_cld(): cpu.db(0xD8)
def A_ldx8(v): cpu.db(0xA2, v)
def A_txs(): cpu.db(0x9A)
def A_lda8(v): cpu.db(0xA9, v)
def A_sta_abs(addr): cpu.db(0x8D); cpu.dw(addr)
def A_and8(v): cpu.db(0x29, v)
def A_bne_to(target_offset):
    # relative8: target = addr_after_instr + signed(disp); addr_after_instr = here()+2
    disp = target_offset - (cpu.here() + 2)
    assert -128 <= disp <= 127, disp
    cpu.db(0xD0, disp & 0xFF)
def A_bra_self():
    # branch to its own start (2-byte instr): disp = start - (start+2) = -2
    start = cpu.here()
    disp = start - (start + 2)
    cpu.db(0x80, disp & 0xFF)


CPU_BASE = 0x8000  # CPU address of file offset 0x0000

# --- boot ---
A_sei()
A_cld()
A_ldx8(0xFF)
A_txs()

# forced blank on immediately, do all setup while the screen is off
A_lda8(0x80); A_sta_abs(0x2100)          # INIDISP = forced blank

# BG mode 1
A_lda8(0x01); A_sta_abs(0x2105)          # BGMODE = 1

# BG1 CHR base = word $0000 (NBA low nibble 0); BG1 tilemap base = word $2000
A_lda8(0x00); A_sta_abs(0x210B)          # BG12NBA: BG1 CHR base = 0*0x1000
A_lda8(0x20); A_sta_abs(0x2107)          # BG1SC: base=(0x20>>2)*0x400=$2000, size 32x32

# --- CGRAM: index0=black, index1=white ---
A_lda8(0x00); A_sta_abs(0x2121)          # CGADD = 0
A_lda8(0x00); A_sta_abs(0x2122)          # word0 lo = $00
A_lda8(0x00); A_sta_abs(0x2122)          # word0 hi = $00 -> index0 = $0000 (black)
A_lda8(0xFF); A_sta_abs(0x2122)          # word1 lo = $FF
A_lda8(0x7F); A_sta_abs(0x2122)          # word1 hi = $7F -> index1 = $7FFF (white)

# --- GSU launch registers ---
A_lda8(0x01); A_sta_abs(0x3034)          # PBR = 1
A_lda8(0x19); A_sta_abs(0x303A)          # SCMR: RON|RAN|MD=4bpp, HT=0(128)
A_lda8(0x00); A_sta_abs(0x3038)          # SCBR = 0 (buffer at GSU RAM $70:0000)
A_lda8(0x00); A_sta_abs(0x301E)          # R15 lo = 0
A_lda8(0x00); A_sta_abs(0x301F)          # R15 hi = 0 -> triggers launch

# --- wait for GSU to finish (poll SFR.GO, bit5 of $3030) ---
wait_addr = CPU_BASE + cpu.here()
cpu.db(0xAD); cpu.dw(0x3030)              # LDA $3030
A_and8(0x20)
A_bne_to(wait_addr - CPU_BASE)

# --- DMA #1: GSU RAM buffer ($70:0000, 16384 bytes) -> VRAM CHR @ word $0000 ---
A_lda8(0x80); A_sta_abs(0x2115)          # VMAIN: word access, increment after high byte
A_lda8(0x00); A_sta_abs(0x2116)          # VMADDL
A_lda8(0x00); A_sta_abs(0x2117)          # VMADDH  -> VMADD = $0000

A_lda8(0x01); A_sta_abs(0x4300)          # DMAP0: mode1 (2 regs alternating), A->B
A_lda8(0x18); A_sta_abs(0x4301)          # BBAD0 = $18 (VMDATAL/H)
A_lda8(0x00); A_sta_abs(0x4302)          # A1T0L
A_lda8(0x00); A_sta_abs(0x4303)          # A1T0H  -> source addr $0000
A_lda8(0x70); A_sta_abs(0x4304)          # A1B0 = bank $70 (GSU RAM)
A_lda8(0x00); A_sta_abs(0x4305)          # DAS0L
A_lda8(0x40); A_sta_abs(0x4306)          # DAS0H  -> 0x4000 bytes (16384)
A_lda8(0x01); A_sta_abs(0x420B)          # trigger channel 0

# --- DMA #2: ROM tilemap table (512 words / 1024 bytes) -> VRAM tilemap @ word $2000 ---
A_lda8(0x00); A_sta_abs(0x2116)
A_lda8(0x20); A_sta_abs(0x2117)          # VMADD = $2000

TILEMAP_PATCH_LO = None  # patched after we know cpu.here() for the two operand bytes
TILEMAP_PATCH_HI = None
A_lda8(0x00)
TILEMAP_PATCH_LO = cpu.here() - 1
cpu.db(0x8D); cpu.dw(0x4302)             # A1T0L (value patched below)
A_lda8(0x00)
TILEMAP_PATCH_HI = cpu.here() - 1
cpu.db(0x8D); cpu.dw(0x4303)             # A1T0H
A_lda8(0x00); A_sta_abs(0x4304)          # A1B0 = bank $00 (ROM)
A_lda8(0x00); A_sta_abs(0x4305)          # DAS0L
A_lda8(0x04); A_sta_abs(0x4306)          # DAS0H -> 0x0400 bytes (1024)
A_lda8(0x01); A_sta_abs(0x420B)          # trigger channel 0

# --- turn on display ---
A_lda8(0x01); A_sta_abs(0x212C)          # TM: enable BG1
A_lda8(0x0F); A_sta_abs(0x2100)          # INIDISP: full brightness, forced blank off

# --- spin forever ---
A_bra_self()

boot_code = bytes(cpu.b)
assert len(boot_code) < 0x7FC0, len(boot_code)


# ---------------------------------------------------------------------------
# GSU program (bank $01, GSU-local offset == file offset - 0x8000)
# ---------------------------------------------------------------------------
gsu = Buf()


def G_cache(): gsu.db(0x02)
def G_stop(): gsu.db(0x00)
def G_with(n): gsu.db(0x20 | n)
def G_to(n): gsu.db(0x10 | n)
def G_alt1(): gsu.db(0x3D)
def G_store(n):
    assert 0 <= n <= 11
    gsu.db(0x30 | n)
def G_load(n):
    assert 0 <= n <= 11
    gsu.db(0x40 | n)
def G_plot(): gsu.db(0x4C)
def G_color(): gsu.db(0x4E)
def G_add(n): gsu.db(0x50 | n)
def G_sub(n): gsu.db(0x60 | n)
def G_dec(n):
    assert 0 <= n <= 14
    gsu.db(0xE0 | n)
def G_fmult(): gsu.db(0x9F)
def G_inc(n):
    assert 0 <= n <= 14
    gsu.db(0xD0 | n)
def G_iwt(n, imm16):
    imm16 &= 0xFFFF
    gsu.db(0xF0 | n)
    gsu.dw(imm16)


def G_move(rd, rs):
    G_with(rs)
    G_to(rd)


def G_store_byte(src_reg, addr_reg):
    G_with(src_reg)
    G_alt1()
    G_store(addr_reg)


def G_load_byte(dst_reg, addr_reg):
    G_with(dst_reg)
    G_alt1()
    G_load(addr_reg)


# --- math setup ---
ANGLE_DEG = 35.0
theta = math.radians(ANGLE_DEG)
COS_Q15 = round(math.cos(theta) * 32768)
SIN_Q15 = round(math.sin(theta) * 32768)


def to_s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def fmult_top16_exact(a_s16, b_s16):
    """Replicates gsu.cpp's FMULT exactly: dr = (int16(sr)*int16(m_r[6])) >> 16.
    Python's >> on a (possibly negative) int is an arithmetic right shift (floors
    toward -inf), matching C++'s int32_t >> 16 for negative values."""
    product = a_s16 * b_s16
    top = product >> 16
    return to_s16(top & 0xFFFF)


H = 32  # cube half-width
VERTS = [
    (-H, -H, -H), (H, -H, -H), (H, H, -H), (-H, H, -H),
    (-H, -H, H), (H, -H, H), (H, H, H), (-H, H, H),
]
EDGES = [(0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6), (6, 7), (7, 4),
         (0, 4), (1, 5), (2, 6), (3, 7)]

CENTER_X = 128
CENTER_Y = 64

screenX = []
screenY = []
for (vx, vy, vz) in VERTS:
    term1 = fmult_top16_exact(to_s16(2 * vx), COS_Q15)
    term2 = fmult_top16_exact(to_s16(2 * vz), SIN_Q15)  # = sin*Vz, reused below for Yr
    xr = term1 + term2
    # Yr = Vy - k*Vz, k = sin(angle) (same coefficient as the X term, same term2
    # value) - shears Y by Z so the 4 depth-only edges stop collapsing onto the
    # top/bottom rows. screenY = CENTER_Y - Yr = CENTER_Y - Vy + term2.
    yr = vy - term2
    sx = CENTER_X + xr
    sy = CENTER_Y - yr
    assert 0 <= sx <= 255, sx
    assert 0 <= sy <= 127, sy
    screenX.append(sx)
    screenY.append(sy)

# classify each edge as horizontal (same screenY), vertical (same screenX), or
# diagonal-45 (both differ, but by construction |dx|==|dy| exactly for the 4
# depth-only "connecting" edges, since they use the identical term2 value for
# both the X-shear and the new Y-shear).
edge_plan = []
for (a, b) in EDGES:
    if screenY[a] == screenY[b] and screenX[a] != screenX[b]:
        s, e = (a, b) if screenX[a] < screenX[b] else (b, a)
        count = screenX[e] - screenX[s] + 1
        edge_plan.append(("H", s, e, count))
    elif screenX[a] == screenX[b] and screenY[a] != screenY[b]:
        s, e = (a, b) if screenY[a] < screenY[b] else (b, a)
        count = screenY[e] - screenY[s] + 1
        edge_plan.append(("V", s, e, count))
    elif screenX[a] != screenX[b] and screenY[a] != screenY[b]:
        s, e = (a, b) if screenX[a] < screenX[b] else (b, a)
        dx = screenX[e] - screenX[s]
        dy = screenY[e] - screenY[s]
        assert dx == dy, (a, b, dx, dy)  # must hold exactly, not approximately
        assert dx > 0, (a, b, dx, dy)
        edge_plan.append(("D45", s, e, dx + 1))
    else:
        raise AssertionError(f"edge {(a, b)} is degenerate: "
                              f"sx={screenX[a]},{screenX[b]} sy={screenY[a]},{screenY[b]}")

X_TABLE_BASE = 0x4000
Y_TABLE_BASE = 0x4008

# --- emit GSU program ---
G_cache()

# color = 1 (white index), set once
G_iwt(0, 1)
G_color()

# hoist constants: R3=128 (center X), R7=64 (center Y)
G_iwt(3, CENTER_X)
G_iwt(7, CENTER_Y)

# table address pointers
G_iwt(9, X_TABLE_BASE)
G_iwt(10, Y_TABLE_BASE)

for (vx, vy, vz) in VERTS:
    G_iwt(0, to_s16(2 * vx) & 0xFFFF)
    G_iwt(1, to_s16(2 * vz) & 0xFFFF)
    G_iwt(6, COS_Q15 & 0xFFFF)
    G_with(0)
    G_fmult()                    # R0 = cos*Vx
    G_iwt(6, SIN_Q15 & 0xFFFF)
    G_with(1)
    G_fmult()                    # R1 = sin*Vz
    G_add(1)                     # R0 = R0+R1 = Xr   (default sr=dr=R0)
    G_add(3)                     # R0 = Xr+128 = screenX
    G_move(5, 0)                 # R5 = screenX
    G_move(0, 7)                 # R0 = 64
    G_iwt(4, vy & 0xFFFF)
    G_sub(4)                     # R0 = 64-Vy
    G_add(1)                     # R0 = 64-Vy+term2(=sin*Vz) = screenY (R1 still
                                  # holds term2 from the FMULT above, untouched by
                                  # the ADD/SUB on R0)
    G_store_byte(5, 9)           # X_table[i] = screenX
    G_inc(9)
    G_store_byte(0, 10)          # Y_table[i] = screenY
    G_inc(10)

for (kind, s, e, count) in edge_plan:
    if kind == "H":
        G_iwt(8, Y_TABLE_BASE + s)
        G_load_byte(2, 8)                 # R2 = fixed y
        G_iwt(8, X_TABLE_BASE + s)
        G_load_byte(1, 8)                 # R1 = starting x
        for _ in range(count):
            G_plot()                      # auto-increments R1
    elif kind == "V":
        G_iwt(8, X_TABLE_BASE + s)
        G_load_byte(1, 8)                 # R1 = fixed x
        G_iwt(8, Y_TABLE_BASE + s)
        G_load_byte(2, 8)                 # R2 = starting y
        for _ in range(count):
            G_plot()                      # PLOT *always* auto-increments R1 too
            G_dec(1)                       # ...undo that, x must stay fixed here
            G_inc(2)
    else:  # D45: x and y advance together, one step per PLOT
        G_iwt(8, X_TABLE_BASE + s)
        G_load_byte(1, 8)                 # R1 = starting x
        G_iwt(8, Y_TABLE_BASE + s)
        G_load_byte(2, 8)                 # R2 = starting y
        for _ in range(count):
            G_plot()                      # auto-increments R1 (x) - exactly what we want
            G_inc(2)                      # ...and step y too, in lockstep

G_stop()

gsu_code = bytes(gsu.b)
assert len(gsu_code) < 0x8000, len(gsu_code)


# ---------------------------------------------------------------------------
# Assemble ROM
# ---------------------------------------------------------------------------
rom = bytearray(b"\x00" * ROM_SIZE)
rom[0x0000:0x0000 + len(boot_code)] = boot_code
rom[0x8000:0x8000 + len(gsu_code)] = gsu_code

# tilemap table: 512 words at 32x32 layout (rows 0-15 used), word = col*16+row
tilemap = bytearray(1024)
for row in range(16):
    for col in range(32):
        cn = col * 16 + row
        idx = (row * 32 + col) * 2
        tilemap[idx] = cn & 0xFF
        tilemap[idx + 1] = (cn >> 8) & 0xFF
TILEMAP_ROM_OFFSET = 0x7800  # inside bank0, before the header block at 0x7FC0
assert TILEMAP_ROM_OFFSET + len(tilemap) <= 0x7FC0
rom[TILEMAP_ROM_OFFSET:TILEMAP_ROM_OFFSET + len(tilemap)] = tilemap

tilemap_cpu_addr = CPU_BASE + TILEMAP_ROM_OFFSET
rom[TILEMAP_PATCH_LO] = tilemap_cpu_addr & 0xFF
rom[TILEMAP_PATCH_HI] = (tilemap_cpu_addr >> 8) & 0xFF

# --- header at file offset 0x7FC0 ---
HDR = 0x7FC0
title = b"STAR3D CUBE DEMO     "[:21].ljust(21, b" ")
rom[HDR + 0x00:HDR + 0x00 + 21] = title
rom[HDR + 0x15] = 0x20   # mapMode: LoROM, SlowROM
rom[HDR + 0x16] = 0x13   # romType: SuperFX
rom[HDR + 0x17] = 0x08   # romSize: 256KB
rom[HDR + 0x18] = 0x00   # sramSize: none
rom[HDR + 0x19] = 0x01   # country: USA
rom[HDR + 0x1A] = 0x00   # license
rom[HDR + 0x1B] = 0x00   # version

# reset vector -> boot code start ($8000)
rom[0x7FFC] = CPU_BASE & 0xFF
rom[0x7FFD] = (CPU_BASE >> 8) & 0xFF

# checksum: zero the checksum/complement fields first, sum all bytes mod 0x10000
rom[HDR + 0x1C:HDR + 0x20] = b"\x00\x00\x00\x00"
checksum = sum(rom) & 0xFFFF
complement = checksum ^ 0xFFFF
rom[HDR + 0x1C] = complement & 0xFF
rom[HDR + 0x1D] = (complement >> 8) & 0xFF
rom[HDR + 0x1E] = checksum & 0xFF
rom[HDR + 0x1F] = (checksum >> 8) & 0xFF

with open(OUT_PATH, "wb") as f:
    f.write(rom)

print(f"wrote {OUT_PATH}: {len(rom)} bytes")
print(f"boot_code={len(boot_code)} bytes, gsu_code={len(gsu_code)} bytes")
print(f"cos_q15={COS_Q15} sin_q15={SIN_Q15}")
for i, (sx, sy) in enumerate(zip(screenX, screenY)):
    print(f"  V{i}: screenX={sx} screenY={sy}")
for (kind, s, e, count) in edge_plan:
    print(f"  edge {kind} {s}->{e} count={count}")
