#include "ppu.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

// -----------------------------------------------------------------------
// reset
// -----------------------------------------------------------------------
void Ppu::reset() {
    m_vram.fill(0);
    m_oam.fill(0);
    m_cgram.fill(0);

    m_forcedBlank = true;
    m_brightness  = 0x0F;
    m_obsel       = 0;

    m_oamBaseAddr = 0;
    m_oamAddr     = 0;
    m_oamPriority = false;
    m_oamLatch    = 0;

    m_bgMode      = 0;
    m_bg3Priority = false;
    m_bgTileSize  = 0;
    m_mosaic      = 0;
    m_framebuffer.fill(0xFF000000u);

    for (int i = 0; i < 4; ++i) { m_bgSC[i] = 0; m_bgHOFS[i] = 0; m_bgVOFS[i] = 0; }
    m_bgNBA[0] = m_bgNBA[1] = 0;
    m_bgOldByte = 0;

    m_vmain       = 0x80;
    m_vramAddr    = 0;
    m_vramReadBuf = 0;

    m_m7sel = 0;
    m_m7a = 0; m_m7b = 0; m_m7c = 0; m_m7d = 0x0100;
    m_m7x = 0; m_m7y = 0;
    m_m7hofs     = 0;
    m_m7vofs     = 0;
    m_m7ofsLatch = 0;
    m_m7latch           = 0;
    m_m7PendingMatAddr  = 0;
    m_m7MatAwaitHigh    = false;

    m_cgramWordAddr = 0;
    m_cgramFlip     = false;
    m_cgramBuf      = 0;

    m_w12sel = m_w34sel = m_wobjsel = 0;
    m_wh[0] = m_wh[1] = m_wh[2] = m_wh[3] = 0;
    m_wbglog = m_wobjlog = 0;

    m_tm = m_ts = m_tmw = m_tsw = 0;

    m_cgswsel = m_cgadsub = 0;
    m_fixedR = m_fixedG = m_fixedB = 0;

    m_setini     = 0;
    m_diagDone   = false;
    m_vramWrites = 0;
}

// -----------------------------------------------------------------------
// VRAM helpers
// -----------------------------------------------------------------------
uint16_t Ppu::vramStep() const {
    static const uint16_t steps[4] = { 1, 32, 128, 128 };
    return steps[m_vmain & 0x03];
}

uint16_t Ppu::vramPhysicalAddr(uint16_t logical) const {
    const uint16_t address = logical & 0x7FFFu;
    switch ((m_vmain >> 2) & 3) {
    case 0:
        return address;
    case 1:
        return static_cast<uint16_t>(
                   (address & 0xFF00u) | ((address << 3) & 0x00F8u) | ((address >> 5) & 7u))
            & 0x7FFFu;
    case 2:
        return static_cast<uint16_t>(
                   (address & 0xFE00u) | ((address << 3) & 0x01F8u) | ((address >> 6) & 7u))
            & 0x7FFFu;
    case 3:
        return static_cast<uint16_t>(
                   (address & 0xFC00u) | ((address << 3) & 0x03F8u) | ((address >> 7) & 7u))
            & 0x7FFFu;
    default:
        return address;
    }
}

void Ppu::vramPrefetch() const {
    m_vramReadBuf = m_vram[vramPhysicalAddr(m_vramAddr)];
}

// -----------------------------------------------------------------------
// OAM helpers
// -----------------------------------------------------------------------
void Ppu::writeOam(uint8_t value) {
    if (m_oamAddr < 512) {
        // Main OAM: pair writes — low byte latched, committed on high byte
        if ((m_oamAddr & 1) == 0) {
            m_oamLatch = value;
        } else {
            m_oam[m_oamAddr - 1] = m_oamLatch;
            m_oam[m_oamAddr]     = value;
        }
    } else if (m_oamAddr < 544) {
        // Extra OAM table: direct byte write
        m_oam[m_oamAddr] = value;
    }
    m_oamAddr = (m_oamAddr + 1) % 544;
}

uint8_t Ppu::readOam() const {
    uint8_t val = 0xFF;
    if (m_oamAddr < 544) val = m_oam[m_oamAddr];
    m_oamAddr = (m_oamAddr + 1) % 544;
    return val;
}

// -----------------------------------------------------------------------
// writeMode7MatrixReg — $211B-$2120 matrix / center (hardware two-write latch).
// First byte stores low; second completes int16 at the pending register port.
// HDMA perspective demos issue pairs to $211B / $211E only — corrupting Mx on the
// first byte breaks per-scanline scaling.
// -----------------------------------------------------------------------
void Ppu::writeMode7MatrixReg(uint16_t addr, uint8_t value) {
    if (!m_m7MatAwaitHigh) {
        m_m7latch          = value;
        m_m7PendingMatAddr = addr;
        m_m7MatAwaitHigh   = true;
        return;
    }
    const uint16_t word = static_cast<uint16_t>(m_m7latch) | (static_cast<uint16_t>(value) << 8);
    switch (m_m7PendingMatAddr) {
    case 0x211B: m_m7a = static_cast<int16_t>(word); break;
    case 0x211C: m_m7b = static_cast<int16_t>(word); break;
    case 0x211D: m_m7c = static_cast<int16_t>(word); break;
    case 0x211E: m_m7d = static_cast<int16_t>(word); break;
    case 0x211F: m_m7x = static_cast<int16_t>(word); break;
    case 0x2120: m_m7y = static_cast<int16_t>(word); break;
    default: break;
    }
    m_m7latch        = value;
    m_m7MatAwaitHigh = false;
}

// -----------------------------------------------------------------------
// writeReg — all PPU register writes ($2100-$213F)
// -----------------------------------------------------------------------
void Ppu::writeReg(uint16_t addr, uint8_t value) {
    switch (addr) {

    // --- $2100 INIDISP: forced blank + brightness ---
    case 0x2100:
        m_forcedBlank = (value >> 7) & 1;
        m_brightness  = value & 0x0F;
        break;

    // --- $2101 OBSEL: sprite size + CHR base ---
    case 0x2101:
        m_obsel = value;
        break;

    // --- $2102/$2103 OAMADDL/H: OAM address ---
    case 0x2102:
        m_oamBaseAddr = (m_oamBaseAddr & 0x100) | value;
        m_oamAddr     = m_oamBaseAddr * 2;
        break;
    case 0x2103:
        m_oamBaseAddr = (m_oamBaseAddr & 0x0FF) | ((value & 0x01) << 8);
        m_oamPriority = (value >> 7) & 1;
        m_oamAddr     = m_oamBaseAddr * 2;
        break;

    // --- $2104 OAMDATA ---
    case 0x2104:
        writeOam(value);
        break;

    // --- $2105 BGMODE: lower 3 bits = mode; bit3 = BG3 priority (Mode 1 only on hw);
    // bits7-4 = per-BG tile size (ignored while mode=7 fixed 8×8 layout).
    case 0x2105:
        m_bgMode      = value & 0x07;
        m_bg3Priority = (value >> 3) & 1;
        m_bgTileSize  = value >> 4;   // Mode 7 ignores these (always 8×8 via renderMode7)
        break;

    // --- $2106 MOSAIC ---
    case 0x2106:
        m_mosaic = value;
        break;

    // --- $2107-$210A BG1-4 tilemap base + mirror size ---
    case 0x2107: m_bgSC[0] = value; break;
    case 0x2108: m_bgSC[1] = value; break;
    case 0x2109: m_bgSC[2] = value; break;
    case 0x210A: m_bgSC[3] = value; break;

    // --- $210B/$210C BG CHR base addresses ---
    case 0x210B: m_bgNBA[0] = value; break;
    case 0x210C: m_bgNBA[1] = value; break;

    // --- $210D-$2114 BG scroll (shared latch) ---
    // Pattern for BG mosaic scroll: HOFS = (old_latch) | ((value & 3) << 8); latch = value.
    // $210D/$210E additionally drive Mode 7 HOFS/VOFS: each write updates a 16-bit value as
    // (written_byte << 8) | latch.mode7, then latch.mode7 = written_byte (bsnes latch.mode7).
    case 0x210D:
        m_m7hofs =
            static_cast<uint16_t>(static_cast<unsigned>(value) << 8u | static_cast<unsigned>(m_m7ofsLatch));
        m_m7ofsLatch = value;
        m_bgHOFS[0]  = (m_bgOldByte & 0xFF) | ((value & 0x03) << 8);
        m_bgOldByte  = value;
        break;
    case 0x210E:
        m_m7vofs =
            static_cast<uint16_t>(static_cast<unsigned>(value) << 8u | static_cast<unsigned>(m_m7ofsLatch));
        m_m7ofsLatch = value;
        m_bgVOFS[0]  = (m_bgOldByte & 0xFF) | ((value & 0x03) << 8);
        m_bgOldByte  = value;
        break;
    case 0x210F: m_bgHOFS[1] = (m_bgOldByte & 0xFF) | ((value & 0x03) << 8); m_bgOldByte = value; break;
    case 0x2110: m_bgVOFS[1] = (m_bgOldByte & 0xFF) | ((value & 0x03) << 8); m_bgOldByte = value; break;
    case 0x2111: m_bgHOFS[2] = (m_bgOldByte & 0xFF) | ((value & 0x03) << 8); m_bgOldByte = value; break;
    case 0x2112: m_bgVOFS[2] = (m_bgOldByte & 0xFF) | ((value & 0x03) << 8); m_bgOldByte = value; break;
    case 0x2113: m_bgHOFS[3] = (m_bgOldByte & 0xFF) | ((value & 0x03) << 8); m_bgOldByte = value; break;
    case 0x2114: m_bgVOFS[3] = (m_bgOldByte & 0xFF) | ((value & 0x03) << 8); m_bgOldByte = value; break;

    // --- $2115 VMAIN: VRAM access mode ---
    case 0x2115:
        m_vmain = value;
        break;

    // --- $2116/$2117 VMADD: VRAM word address (prefetch on write) ---
    case 0x2116:
        m_vramAddr = (m_vramAddr & 0xFF00) | value;
        vramPrefetch();
        break;
    case 0x2117:
        m_vramAddr = (m_vramAddr & 0x00FF) | (static_cast<uint16_t>(value) << 8);
        vramPrefetch();
        break;

    // --- $2118 VMDATAL: write low byte, increment if VMAIN.7=0 ---
    case 0x2118: {
        const uint16_t a = vramPhysicalAddr(m_vramAddr);
        m_vram[a] = (m_vram[a] & 0xFF00) | value;
        if ((m_vmain & 0x80) == 0) { m_vramAddr += vramStep(); ++m_vramWrites; }
        break;
    }

    // --- $2119 VMDATAH: write high byte, increment if VMAIN.7=1 ---
    case 0x2119: {
        const uint16_t a = vramPhysicalAddr(m_vramAddr);
        m_vram[a] = (m_vram[a] & 0x00FF) | (static_cast<uint16_t>(value) << 8);
        if ((m_vmain & 0x80) != 0) { m_vramAddr += vramStep(); ++m_vramWrites; }
        break;
    }

    // --- $211A M7SEL ---
    case 0x211A:
        m_m7sel = value;
        break;

    // --- $211B-$2120 Mode 7 matrix + center (two-write latch per register port) ---
    case 0x211B:
    case 0x211C:
    case 0x211D:
    case 0x211E:
    case 0x211F:
    case 0x2120:
        writeMode7MatrixReg(addr, value);
        break;

    // --- $2121 CGADD: CGRAM word address (color index) ---
    case 0x2121:
        m_cgramWordAddr = value;
        m_cgramFlip     = false;
        break;

    // --- $2122 CGDATA: two-write latch, BGR555 ---
    case 0x2122:
        if (!m_cgramFlip) {
            m_cgramBuf  = value;        // buffer low byte
            m_cgramFlip = true;
        } else {
            m_cgram[m_cgramWordAddr] =
                static_cast<uint16_t>(m_cgramBuf) |
                (static_cast<uint16_t>(value & 0x7F) << 8);
            m_cgramWordAddr++;          // wraps at 256
            m_cgramFlip = false;
        }
        break;

    // --- $2123-$212B Windowing ---
    case 0x2123: m_w12sel  = value; break;
    case 0x2124: m_w34sel  = value; break;
    case 0x2125: m_wobjsel = value; break;
    case 0x2126: m_wh[0]   = value; break;
    case 0x2127: m_wh[1]   = value; break;
    case 0x2128: m_wh[2]   = value; break;
    case 0x2129: m_wh[3]   = value; break;
    case 0x212A: m_wbglog  = value; break;
    case 0x212B: m_wobjlog = value; break;

    // --- $212C-$212F Screen designation ---
    case 0x212C: m_tm  = value & 0x1F; break;
    case 0x212D: m_ts  = value & 0x1F; break;
    case 0x212E: m_tmw = value & 0x1F; break;
    case 0x212F: m_tsw = value & 0x1F; break;

    // --- $2130 CGSWSEL / $2131 CGADSUB ---
    case 0x2130: m_cgswsel = value; break;
    case 0x2131: m_cgadsub = value; break;

    // --- $2132 COLDATA: fixed color (R/G/B components written separately) ---
    case 0x2132: {
        const uint8_t col = value & 0x1F;
        if (value & 0x20) m_fixedR = col;
        if (value & 0x40) m_fixedG = col;
        if (value & 0x80) m_fixedB = col;
        break;
    }

    // --- $2133 SETINI ---
    case 0x2133:
        m_setini = value;
        break;

    default:
        break;
    }
}

// -----------------------------------------------------------------------
// readReg — all PPU register reads ($2100-$213F)
// -----------------------------------------------------------------------
uint8_t Ppu::readReg(uint16_t addr) const {
    switch (addr) {

    // --- $2138 OAMDATAREAD ---
    case 0x2138:
        return readOam();

    // --- $2139 VMDATALREAD: return prefetch low byte ---
    case 0x2139: {
        const uint8_t val = static_cast<uint8_t>(m_vramReadBuf & 0xFF);
        if ((m_vmain & 0x80) == 0) {
            m_vramAddr += vramStep();
            vramPrefetch();
        }
        return val;
    }

    // --- $213A VMDATAHREAD: return prefetch high byte ---
    case 0x213A: {
        const uint8_t val = static_cast<uint8_t>(m_vramReadBuf >> 8);
        if ((m_vmain & 0x80) != 0) {
            m_vramAddr += vramStep();
            vramPrefetch();
        }
        return val;
    }

    // --- $213B CGDATAREAD: two-read latch ---
    case 0x213B: {
        uint8_t val;
        const uint16_t color = m_cgram[m_cgramWordAddr];
        if (!m_cgramFlip) {
            val         = static_cast<uint8_t>(color & 0xFF);
            m_cgramFlip = true;
        } else {
            val             = static_cast<uint8_t>((color >> 8) & 0x7F);
            m_cgramWordAddr++;
            m_cgramFlip = false;
        }
        return val;
    }

    // --- $213E PPU1 status: version=1 in bits 3:0; bit 6 = OBJ range overflow ---
    case 0x213E: {
        const uint8_t val = 0x01 | (m_objRangeOver ? 0x40 : 0x00);
        m_objRangeOver = false;   // cleared on read
        return val;
    }

    default:
        return 0xFF;
    }
}

// =======================================================================
// Rendering
// =======================================================================

// -----------------------------------------------------------------------
// cgramToArgb — BGR555 → 0xAARRGGBB (alpha always 0xFF)
// -----------------------------------------------------------------------
uint32_t Ppu::cgramToArgb(uint16_t bgr) const {
    const uint8_t r = static_cast<uint8_t>((bgr & 0x001F) << 3);
    const uint8_t g = static_cast<uint8_t>(((bgr >> 5)  & 0x1F) << 3);
    const uint8_t b = static_cast<uint8_t>(((bgr >> 10) & 0x1F) << 3);
    return 0xFF000000u | (static_cast<uint32_t>(r) << 16)
                       | (static_cast<uint32_t>(g) << 8)
                       | b;
}

// -----------------------------------------------------------------------
// applyInidispLuma — $2100 brightness nibble (bits 3:0) scales final RGB.
// Forced blank (bit 7) is handled in renderScanline (black scanline).
// 0 = master dim to black, F = full output (linear in 15 steps).
// -----------------------------------------------------------------------
uint32_t Ppu::applyInidispLuma(uint32_t argb) const {
    const unsigned b = static_cast<unsigned>(m_brightness & 0x0Fu);
    if (b == 0) return 0xFF000000u;

    const uint32_t rr = ((argb >> 16) & 0xFFu) * b / 15u;
    const uint32_t gg = ((argb >>  8) & 0xFFu) * b / 15u;
    const uint32_t bb = ( argb        & 0xFFu) * b / 15u;

    return 0xFF000000u | (rr << 16) | (gg << 8) | bb;
}

// -----------------------------------------------------------------------
// chrBase — VRAM word address of CHR data for bg (0-3)
// -----------------------------------------------------------------------
uint16_t Ppu::chrBase(int bg) const {
    const uint8_t nba   = m_bgNBA[bg >> 1];           // $210B for BG1/2, $210C for BG3/4
    const int     shift = (bg & 1) ? 4 : 0;
    return static_cast<uint16_t>(((nba >> shift) & 0x0F) * 0x1000);
}

// -----------------------------------------------------------------------
// tilemapEntry — fetch the 16-bit tilemap word for tile (col,row)
//   Handles 32×32, 64×32, 32×64, 64×64 tilemaps via BGxSC bits 1:0
// -----------------------------------------------------------------------
uint16_t Ppu::tilemapEntry(int bg, int tileCol, int tileRow) const {
    const uint8_t  sc   = m_bgSC[bg];
    const uint16_t base = static_cast<uint16_t>((sc >> 2) * 0x400);
    const uint8_t  size = sc & 0x03;

    uint16_t off = 0;
    int c = tileCol & 63;
    int r = tileRow & 63;

    switch (size) {
    case 0: // 32×32
        c &= 31; r &= 31;
        break;
    case 1: // 64×32 — second screen to the right
        if (c >= 32) { off = 0x400; c -= 32; }
        r &= 31;
        break;
    case 2: // 32×64 — second screen below
        c &= 31;
        if (r >= 32) { off = 0x400; r -= 32; }
        break;
    case 3: // 64×64 — four screens
        if (c >= 32) off += 0x400;
        if (r >= 32) off += 0x800;
        c &= 31; r &= 31;
        break;
    }

    return m_vram[(base + off + static_cast<uint16_t>(r * 32 + c)) & 0x7FFF];
}

// -----------------------------------------------------------------------
// getPixel — decode one pixel from VRAM tile data
//   bpp : 2, 4, or 8
//   base: VRAM word address of CHR block
//   row : pixel row within tile (0-7, already flip-adjusted)
//   col : pixel column 0=left … 7=right within tile (already flip-adjusted)
//   Returns raw color index (0 = transparent)
// -----------------------------------------------------------------------
uint8_t Ppu::getPixel(int bpp, uint16_t base, uint16_t tileNum, int row, int col) const {
    // Per SNES planar layout each row byte uses MSB=left, LSB=right (matches Krom PPU flip tests /
    // e.g. https://emudev.de/q00-snes/backgrounds-modes-and-tests/ flipping section).
    const int bit = 7 - col;

    auto plane = [&](uint16_t wordOff) -> uint16_t {
        return m_vram[(base + tileNum * static_cast<uint16_t>(bpp == 2 ? 8 : bpp == 4 ? 16 : 32) + wordOff) & 0x7FFF];
    };

    switch (bpp) {
    case 2: {
        const uint16_t w = plane(static_cast<uint16_t>(row));
        return static_cast<uint8_t>(((w >> bit) & 1) | (((w >> (8 + bit)) & 1) << 1));
    }
    case 4: {
        const uint16_t w0 = plane(static_cast<uint16_t>(row));
        const uint16_t w1 = plane(static_cast<uint16_t>(8 + row));
        const uint8_t p01 = static_cast<uint8_t>(((w0 >> bit) & 1) | (((w0 >> (8 + bit)) & 1) << 1));
        const uint8_t p23 = static_cast<uint8_t>(((w1 >> bit) & 1) | (((w1 >> (8 + bit)) & 1) << 1));
        return static_cast<uint8_t>(p01 | (p23 << 2));
    }
    case 8: {
        const uint16_t w0 = plane(static_cast<uint16_t>(row));
        const uint16_t w1 = plane(static_cast<uint16_t>(8 + row));
        const uint16_t w2 = plane(static_cast<uint16_t>(16 + row));
        const uint16_t w3 = plane(static_cast<uint16_t>(24 + row));
        const uint8_t p01 = static_cast<uint8_t>(((w0 >> bit) & 1) | (((w0 >> (8 + bit)) & 1) << 1));
        const uint8_t p23 = static_cast<uint8_t>(((w1 >> bit) & 1) | (((w1 >> (8 + bit)) & 1) << 1));
        const uint8_t p45 = static_cast<uint8_t>(((w2 >> bit) & 1) | (((w2 >> (8 + bit)) & 1) << 1));
        const uint8_t p67 = static_cast<uint8_t>(((w3 >> bit) & 1) | (((w3 >> (8 + bit)) & 1) << 1));
        return static_cast<uint8_t>(p01 | (p23 << 2) | (p45 << 4) | (p67 << 6));
    }
    default:
        return 0;
    }
}

// 8-bit Mode 7 "direct colour" -> BGR555 (then same expansion as CGRAM).
uint32_t Ppu::mode7DirectColorArgb(uint8_t pixel) const {
    const unsigned r3 = pixel & 7u;
    const unsigned g3 = (pixel >> 3) & 7u;
    const unsigned b2 = (pixel >> 6) & 3u;
    const uint16_t r5 = static_cast<uint16_t>((r3 << 2) | (r3 >> 1));
    const uint16_t g5 = static_cast<uint16_t>((g3 << 2) | (g3 >> 1));
    const uint16_t b5 = static_cast<uint16_t>((b2 << 3) | (b2 << 1) | (b2 >> 1));
    const uint16_t bgr555 = static_cast<uint16_t>(b5 | (g5 << 5) | (r5 << 10));
    return cgramToArgb(bgr555);
}

// -----------------------------------------------------------------------
// renderMode7 — affine BG1 (+ optional BG2 EXT via $2133.6 while in mode 7).
// -----------------------------------------------------------------------
void Ppu::renderMode7(int line, LayerPixel* affineOut, LayerPixel* extBgOut) const {
    auto clipM7 = [](int n) -> int { return (n & 0x2000) ? (n | ~1023) : (n & 1023); };
    auto extend13 = [](uint16_t raw) -> int {
        int v = static_cast<int>(raw & 0x1FFFu);
        if (v & 0x1000) {
            v |= ~0x1FFF;
        }
        return v;
    };

    const int Y       = line;
    const int yScreen = ((m_m7sel >> 1) & 1) ? (255 - Y) : Y;

    const int hoffset = extend13(m_m7hofs);
    const int voffset = extend13(m_m7vofs);
    const int hcenter = extend13(static_cast<uint16_t>(m_m7x));
    const int vcenter = extend13(static_cast<uint16_t>(m_m7y));

    const int a = static_cast<int16_t>(m_m7a);
    const int b = static_cast<int16_t>(m_m7b);
    const int c = static_cast<int16_t>(m_m7c);
    const int d = static_cast<int16_t>(m_m7d);

    auto mulMasked = [&](int mat, int v) -> int {
        const int64_t prod = static_cast<int64_t>(mat) * static_cast<int64_t>(v);
        return static_cast<int>(prod & ~static_cast<int64_t>(63));
    };

    const int originX = mulMasked(a, clipM7(hoffset - hcenter)) + mulMasked(b, clipM7(voffset - vcenter))
                      + mulMasked(b, yScreen) + (hcenter << 8);
    const int originY = mulMasked(c, clipM7(hoffset - hcenter)) + mulMasked(d, clipM7(voffset - vcenter))
                      + mulMasked(d, yScreen) + (vcenter << 8);

    const unsigned repeatMode = (static_cast<unsigned>(m_m7sel) >> 6) & 3u;

    // Layer fetch must rasterize TM|TS; main/sub masking happens later on copies.
    const uint8_t m7Layers = static_cast<uint8_t>(static_cast<unsigned>(m_tm) | static_cast<unsigned>(m_ts));
    const bool    drawBg1  = (m7Layers & 0x01) != 0;
    // BG2 overlay exists only when $2133 EXTBG (bit 6) enables Mode 7 extension tiles.
    const bool mode7Ext  = ((m_setini >> 6) & 1) != 0;
    const bool drawBg2   = ((m7Layers & 0x02) != 0) && mode7Ext;

    for (int X = 0; X < 256; ++X) {
        const int xWalk = ((m_m7sel & 1) == 0) ? X : (255 - X);

        // origin / matrix values are SNES Mode 7 fixed-point: origin centered in pixel space shifted
        // left by 8; each screen column advances by +(a,c) full fixed units (near/bsnes model).
        // Do NOT shrink (a*xWalk) >> 8 before combining with origin or the row collapses (~constant X).
        const int64_t lx64 = static_cast<int64_t>(originX) + static_cast<int64_t>(a) * static_cast<int64_t>(xWalk);
        const int64_t ly64 = static_cast<int64_t>(originY) + static_cast<int64_t>(c) * static_cast<int64_t>(xWalk);

        const int pixelX = static_cast<int>(lx64 >> 8);
        const int pixelY = static_cast<int>(ly64 >> 8);

        const bool outOfBounds = ((pixelX | pixelY) & ~1023) != 0;

        const unsigned tileX = static_cast<unsigned>((pixelX >> 3) & 127);
        const unsigned tileY = static_cast<unsigned>((pixelY >> 3) & 127);
        const uint16_t mapAddr = static_cast<uint16_t>((tileY << 7) | tileX);
        const uint16_t mapWord = m_vram[mapAddr & 0x7FFF];
        uint8_t        tileNum = static_cast<uint8_t>(mapWord & 0xFF);
        const unsigned inTile =
            static_cast<unsigned>(((pixelY & 7) << 3) | (pixelX & 7)) & 63u;

        if (repeatMode == 3u && outOfBounds) {
            tileNum = 0;
        }

        const uint16_t chrAddr =
            static_cast<uint16_t>((static_cast<uint16_t>(tileNum) << 6) | static_cast<uint16_t>(inTile));
        const uint16_t chrWord = m_vram[chrAddr & 0x7FFF];
        // Chunky palette is bits 15-8 at this VRAM word; bits 7-0 belong to tile-map semantics (SNES §Mode 7).
        const uint8_t palHi   = static_cast<uint8_t>(chrWord >> 8);
        uint8_t       palette = palHi;

        if (repeatMode == 2u && outOfBounds) {
            palette = 0;
        }

        if (drawBg1 && palette != 0) {
            affineOut[X] = LayerPixel{palette, 0};
        }
        if (drawBg2) {
            const unsigned priIdx = static_cast<unsigned>(palette) & 0x7Fu;
            const uint8_t priBit  = static_cast<uint8_t>((palHi >> 7) & 1);
            if (priIdx != 0) {
                extBgOut[X] = LayerPixel{static_cast<uint8_t>(priIdx), priBit};
            }
        }
    }
}

// -----------------------------------------------------------------------
// renderBg — rasterize one BG layer for a single scanline
//   Handles 8×8 and 16×16 tiles, H/V flip, all tilemap mirror sizes.
//   Writes into 'out[0..255]'.  out[x].cgramIdx == 0 → transparent.
// -----------------------------------------------------------------------
void Ppu::renderBg(int bg, int bpp, int line, LayerPixel* out) const {
    const bool     largeTiles = (m_bgTileSize >> bg) & 1;
    const int      tileSz     = largeTiles ? 16 : 8;
    const uint16_t hofs       = m_bgHOFS[bg] & 0x3FF;
    const uint16_t vofs       = m_bgVOFS[bg] & 0x3FF;
    const uint16_t chr        = chrBase(bg);

    // CGRAM palette stride per bpp
    const int palStride = (bpp == 2) ? 4 : (bpp == 4) ? 16 : 256;
    // Mode 0 — four 2bpp BGs partition CGRAM indices 0..127 into four 32-color banks
    // (BG1 slots 0..31 … BG4 96..127); OBJ uses 128..255 ($2122 uploads).
    const uint16_t mode0PalBase =
        (m_bgMode == 0 && bpp == 2) ? static_cast<uint16_t>(bg * 32) : uint16_t(0);

    const int effY    = (line + static_cast<int>(vofs)) & 0x3FF;
    const int tileRow = effY / tileSz;                  // coarse tile row in tilemap
    const int fineYt  = effY % tileSz;                  // pixel row within the tile cell

    for (int x = 0; x < 256; ++x) {
        const int effX    = (x + static_cast<int>(hofs)) & 0x3FF;
        const int tileCol = effX / tileSz;
        const int fineXt  = effX % tileSz;

        // Fetch tilemap word (hardware layout; see SNESdev "Tilemaps"):
        //   bits 15-14 → VFlip, HFlip  |  bit 13 → priority |  bits 12-10 → palette
        //   bits 9-0   → tile number (10 bits)
        const uint16_t entry = tilemapEntry(bg, tileCol, tileRow);
        const bool     vflip = (entry >> 15) & 1;
        const bool     hflip = (entry >> 14) & 1;
        const bool     pri   = (entry >> 13) & 1;
        const uint8_t  pal   = static_cast<uint8_t>((entry >> 10) & 7);
        const uint16_t baseN = entry & 0x3FF;

        // Apply flip within the tile cell
        int fx = hflip ? (tileSz - 1 - fineXt) : fineXt;
        int fy = vflip ? (tileSz - 1 - fineYt) : fineYt;

        // For 16×16 tiles: select sub-tile and reduce to 8×8 coordinates
        uint16_t tileNum = baseN;
        if (largeTiles) {
            const int subX = fx >> 3;
            const int subY = fy >> 3;
            tileNum = (baseN + static_cast<uint16_t>(subX) + static_cast<uint16_t>(subY * 16)) & 0x3FF;
            fx &= 7;
            fy &= 7;
        }

        const uint8_t color = getPixel(bpp, chr, tileNum, fy, fx);

        if (color == 0) {
            out[x] = { 0, 0 };   // transparent
        } else {
            const uint8_t idx = (bpp == 8)
                ? color
                : static_cast<uint8_t>(mode0PalBase + static_cast<uint16_t>(pal * palStride + color));
            out[x] = { idx, pri ? uint8_t(1) : uint8_t(0) };
        }
    }
}

// -----------------------------------------------------------------------
// renderSprites — rasterize all OBJ onto 'out[0..255]' for one scanline
//   OAM index 0 has highest priority (overwrites higher indices).
//   Sprite CGRAM: entries 128-255 (palette 0-7, 16 colors each).
//   Sets m_objRangeOver if >32 sprites appear on this line.
// -----------------------------------------------------------------------
void Ppu::renderSprites(int line, SpritePixel* out) const {
    for (int i = 0; i < 256; ++i) out[i] = {};

    // Size table: [OBSEL bits 7:5][small=0 / large=1] = {width, height}
    static const int kW[8][2] = {{8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{16,32},{16,32}};
    static const int kH[8][2] = {{8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{32,64},{32,32}};

    const int sizeSet = (m_obsel >> 5) & 0x07;

    // Sprite CHR addressing: first name table base + gap to second table
    // base (words) = (obsel & 7) << 13   (hardware base unit is 0x2000 words, not 0x1000 —
    //   confirmed against SplitScrolling.sfc, whose sprite tiles sit at nameBase=$6000
    //   for obsel&7=3; the <<12 form only ever coincided with fixture ROMs using base 0)
    // gap  (words) = (((obsel >> 3) & 3) + 1) << 12
    const uint16_t nameBase = static_cast<uint16_t>((m_obsel & 0x07) << 13);
    const uint16_t nameGap  = static_cast<uint16_t>((static_cast<uint16_t>((m_obsel >> 3) & 0x03) + 1u) << 12);

    // ---- Pass 1: collect up to 32 visible sprites ----
    int visIdx[32];
    int visCount = 0;
    bool overflow = false;

    for (int i = 0; i < 128; ++i) {
        const uint8_t* s = m_oam.data() + i * 4;

        // Extra OAM: 2 bits per sprite packed into bytes at offset 512
        const uint8_t extra = m_oam[512 + (i >> 2)];
        const int     shift = (i & 3) << 1;
        const uint8_t eBits = (extra >> shift) & 0x03;

        const bool largeSize = (eBits >> 1) & 1;
        const int  sprH      = kH[sizeSet][largeSize ? 1 : 0];

        // 8-bit Y comparison (handles wrap correctly for off-screen sprites)
        if (static_cast<uint8_t>(static_cast<uint8_t>(line) - s[1])
                >= static_cast<uint8_t>(sprH)) continue;

        // X bounds check: 9-bit signed X
        const int sprX = (static_cast<int>(s[0]) | (static_cast<int>(eBits & 1) << 8));
        const int sX   = (sprX >= 256) ? (sprX - 512) : sprX;
        const int sprW = kW[sizeSet][largeSize ? 1 : 0];
        if (sX + sprW <= 0 || sX >= 256) continue;

        if (visCount < 32) {
            visIdx[visCount++] = i;
        } else {
            overflow = true;
            break;
        }
    }
    if (overflow) m_objRangeOver = true;

    // ---- Pass 2: render back-to-front (high OAM index first so low index wins) ----
    for (int v = visCount - 1; v >= 0; --v) {
        const int     i     = visIdx[v];
        const uint8_t* s    = m_oam.data() + i * 4;

        const uint8_t extra = m_oam[512 + (i >> 2)];
        const int     shift = (i & 3) << 1;
        const uint8_t eBits = (extra >> shift) & 0x03;

        const bool largeSize = (eBits >> 1) & 1;
        const int  sprW      = kW[sizeSet][largeSize ? 1 : 0];
        const int  sprH      = kH[sizeSet][largeSize ? 1 : 0];

        const int rawX = static_cast<int>(s[0]) | (static_cast<int>(eBits & 1) << 8);
        const int sprX = (rawX >= 256) ? (rawX - 512) : rawX;

        const uint8_t attr    = s[3];
        const bool    vflip   = (attr >> 7) & 1;
        const bool    hflip   = (attr >> 6) & 1;
        const uint8_t pri     = (attr >> 4) & 0x03;
        const uint8_t pal     = (attr >> 1) & 0x07;
        const bool    nameBit = attr & 0x01;
        const uint8_t baseN   = s[2];

        // Local Y within sprite (already validated to be in [0, sprH))
        int ly = static_cast<int>(static_cast<uint8_t>(static_cast<uint8_t>(line) - s[1]));
        if (vflip) ly = sprH - 1 - ly;
        const int tileRow = ly >> 3;
        const int fineY   = ly & 7;

        // CHR base for this sprite
        const uint16_t tileBase = nameBase + (nameBit ? nameGap : uint16_t(0));

        for (int lxi = 0; lxi < sprW; ++lxi) {
            const int screenX = sprX + lxi;
            if (screenX < 0 || screenX >= 256) continue;

            const int lx      = hflip ? (sprW - 1 - lxi) : lxi;
            const int tileCol = lx >> 3;
            const int fineX   = lx & 7;

            // Tile index wraps within the 256-tile name table row of 16
            const uint8_t tileNum = static_cast<uint8_t>(
                baseN + static_cast<uint8_t>(tileCol) + static_cast<uint8_t>(tileRow * 16));

            const uint8_t color = getPixel(4, tileBase, tileNum, fineY, fineX);
            if (color == 0) continue;  // transparent

            out[screenX] = {
                static_cast<uint8_t>(128u + pal * 16u + color),
                pri
            };
        }
    }
}

// -----------------------------------------------------------------------
// windowMaskBg — true if pixel x is clipped by the window mask for bg (0-3)
//   Uses W12SEL/W34SEL enable+area bits + WH positions + WBGLOG combine logic.
// -----------------------------------------------------------------------
bool Ppu::windowMaskBg(int x, int bg) const {
    const uint8_t wsel  = (bg < 2) ? m_w12sel : m_w34sel;
    const int     shift = (bg & 1) ? 4 : 0;

    const bool w1_en   = (wsel >> (shift + 1)) & 1;
    const bool w1_area = (wsel >> (shift + 0)) & 1; // 0=clip inside, 1=clip outside (area describes where the layer is *shown*)
    const bool w2_en   = (wsel >> (shift + 3)) & 1;
    const bool w2_area = (wsel >> (shift + 2)) & 1;

    if (!w1_en && !w2_en) return false;

    const bool in_w1 = (m_wh[0] <= m_wh[1]) && (x >= m_wh[0] && x <= m_wh[1]);
    const bool in_w2 = (m_wh[2] <= m_wh[3]) && (x >= m_wh[2] && x <= m_wh[3]);

    // clip_wN = true means W_N says "clip this pixel"
    const bool clip_w1 = w1_area ? !in_w1 : in_w1;
    const bool clip_w2 = w2_area ? !in_w2 : in_w2;

    if (w1_en && !w2_en) return clip_w1;
    if (!w1_en)          return clip_w2;

    // Both enabled — combine with WBGLOG (2 bits per BG)
    switch ((m_wbglog >> (bg * 2)) & 0x03) {
    case 0: return clip_w1 || clip_w2;    // OR
    case 1: return clip_w1 && clip_w2;    // AND
    case 2: return clip_w1 ^  clip_w2;    // XOR
    default:return !(clip_w1 ^ clip_w2);  // XNOR
    }
}

// -----------------------------------------------------------------------
// windowMaskObj — true if pixel x is clipped for OBJ layer
// -----------------------------------------------------------------------
bool Ppu::windowMaskObj(int x) const {
    const bool w1_en   = (m_wobjsel >> 1) & 1;
    const bool w1_area = (m_wobjsel >> 0) & 1;
    const bool w2_en   = (m_wobjsel >> 3) & 1;
    const bool w2_area = (m_wobjsel >> 2) & 1;

    if (!w1_en && !w2_en) return false;

    const bool in_w1 = (m_wh[0] <= m_wh[1]) && (x >= m_wh[0] && x <= m_wh[1]);
    const bool in_w2 = (m_wh[2] <= m_wh[3]) && (x >= m_wh[2] && x <= m_wh[3]);

    const bool clip_w1 = w1_area ? !in_w1 : in_w1;
    const bool clip_w2 = w2_area ? !in_w2 : in_w2;

    if (w1_en && !w2_en) return clip_w1;
    if (!w1_en)          return clip_w2;

    switch (m_wobjlog & 0x03) { // bits 1:0 = OBJ logic
    case 0: return clip_w1 || clip_w2;
    case 1: return clip_w1 && clip_w2;
    case 2: return clip_w1 ^  clip_w2;
    default:return !(clip_w1 ^ clip_w2);
    }
}

// -----------------------------------------------------------------------
// colorWindowCombinedClip — color-window mask (WOBJSEL color bits + WOBJLOG 3:2)
// -----------------------------------------------------------------------
bool Ppu::colorWindowCombinedClip(int x) const {
    const bool w1_en   = (m_wobjsel >> 4) & 1;
    const bool w1_area = (m_wobjsel >> 5) & 1;
    const bool w2_en   = (m_wobjsel >> 6) & 1;
    const bool w2_area = (m_wobjsel >> 7) & 1;

    if (!w1_en && !w2_en) return false;

    const bool in_w1 = (m_wh[0] <= m_wh[1]) && (x >= m_wh[0] && x <= m_wh[1]);
    const bool in_w2 = (m_wh[2] <= m_wh[3]) && (x >= m_wh[2] && x <= m_wh[3]);

    const bool clip_w1 = w1_area ? !in_w1 : in_w1;
    const bool clip_w2 = w2_area ? !in_w2 : in_w2;

    if (w1_en && !w2_en) return clip_w1;
    if (!w1_en)          return clip_w2;

    switch ((m_wobjlog >> 2) & 0x03) {
    case 0: return clip_w1 || clip_w2;
    case 1: return clip_w1 && clip_w2;
    case 2: return clip_w1 ^  clip_w2;
    default:return !(clip_w1 ^ clip_w2);
    }
}

bool Ppu::forceMainBlackFromColorWindow(int x) const {
    const unsigned mm = (m_cgswsel >> 6) & 0x03u;
    if (mm == 0) return false;
    if (mm == 3) return true;

    const bool insideOpening = !colorWindowCombinedClip(x);
    // MM=1 Outside — black outside the visible color-window opening.
    // MM=2 Inside  — black inside the opening.
    return (mm == 1u) ? !insideOpening : insideOpening;
}

bool Ppu::forceSubTransparentFromColorWindow(int x) const {
    const unsigned ss = (m_cgswsel >> 4) & 0x03u;
    if (ss == 0) return false;
    if (ss == 3) return true;

    const bool insideOpening = !colorWindowCombinedClip(x);
    return (ss == 1u) ? !insideOpening : insideOpening;
}

// -----------------------------------------------------------------------
// applyWindowMask — clip layers by TMW (main) or TSW (sub) window masks
// -----------------------------------------------------------------------
void Ppu::applyWindowMask(int /*line*/,
                           LayerPixel* bg0, LayerPixel* bg1,
                           LayerPixel* bg2, LayerPixel* bg3,
                           SpritePixel* spr, bool mainScreen) const
{
    const uint8_t mask = mainScreen ? m_tmw : m_tsw;
    if (!mask) return;

    for (int x = 0; x < 256; ++x) {
        if ((mask & 0x01) && bg0[x].cgramIdx && windowMaskBg(x, 0)) bg0[x] = {0, 0};
        if ((mask & 0x02) && bg1[x].cgramIdx && windowMaskBg(x, 1)) bg1[x] = {0, 0};
        if ((mask & 0x04) && bg2[x].cgramIdx && windowMaskBg(x, 2)) bg2[x] = {0, 0};
        if ((mask & 0x08) && bg3[x].cgramIdx && windowMaskBg(x, 3)) bg3[x] = {0, 0};
        if ((mask & 0x10) && spr[x].cgramIdx  && windowMaskObj(x))  spr[x]  = {0, 0};
    }
}

// -----------------------------------------------------------------------
// compositeSample — opaque winner + backdrop for TM or TS (pre–color math).
//   Mode ordering matches SNESdev BGMODE / priority charts.
// -----------------------------------------------------------------------
auto Ppu::compositeSample(int x,
                          const LayerPixel*  bg0,
                          const LayerPixel*  bg1,
                          const LayerPixel*  bg2,
                          const LayerPixel*  bg3,
                          const SpritePixel* spr) const -> CompositeSample
{
    const LayerPixel* layers[4] = { bg0, bg1, bg2, bg3 };

    uint8_t winIdx   = 0;
    uint8_t winCmBit = 0x20;
    bool    found    = false;

    auto tryBg = [&](int bg, uint8_t pri) {
        if (found) return;
        if (layers[bg][x].cgramIdx == 0 || layers[bg][x].priority != pri) return;
        winIdx   = layers[bg][x].cgramIdx;
        winCmBit = static_cast<uint8_t>(1u << bg);   // BG1=$2131 bit0 … BG4=bit3
        found    = true;
    };
    auto trySpr = [&](uint8_t pri) {
        if (found) return;
        if (spr[x].cgramIdx == 0 || spr[x].priority != pri) return;
        winIdx   = spr[x].cgramIdx;
        winCmBit = 0x10;
        found    = true;
    };

    switch (m_bgMode) {
    case 0:
        trySpr(3);
        tryBg(0,1); tryBg(1,1);
        trySpr(2);
        tryBg(2,1); tryBg(3,1);
        trySpr(1);
        tryBg(0,0); tryBg(1,0);
        trySpr(0);
        tryBg(2,0); tryBg(3,0);
        break;

    case 1:
        if (m_bg3Priority) {
            tryBg(2,1);              // 3H
            trySpr(3);
            tryBg(0,1); tryBg(1,1); // 1H 2H
            trySpr(2);
            tryBg(0,0); tryBg(1,0); // 1L 2L
            trySpr(1);
            trySpr(0);
            tryBg(2,0);             // 3L
        } else {
            trySpr(3);
            tryBg(0,1); tryBg(1,1);
            trySpr(2);
            tryBg(0,0); tryBg(1,0);
            trySpr(1);
            tryBg(2,1);
            trySpr(0);
            tryBg(2,0);
        }
        break;

    case 2:
    case 4:
    case 5:
        trySpr(3);
        tryBg(0,1); tryBg(1,1);
        trySpr(2);
        tryBg(0,0); tryBg(1,0);
        trySpr(1);
        trySpr(0);
        break;

    case 3:
        trySpr(3);
        tryBg(0,1); tryBg(1,1);
        trySpr(2);
        tryBg(0,0); tryBg(1,0);
        trySpr(1);
        trySpr(0);
        break;

    case 6:
        trySpr(3);
        tryBg(0,1);
        trySpr(2);
        tryBg(0,0);
        trySpr(1);
        trySpr(0);
        break;

    // Mode 7 (SNESdev Backgrounds §Priority row): front → back
    //   S3  S2  2H  S1  1L  S0  2L   (2H/2L only when $2133 EXTBG = 1; BG1 is 1L only)
    case 7: {
        const bool ext = ((m_setini >> 6) & 1) != 0;
        trySpr(3);
        trySpr(2);
        if (ext) tryBg(1, 1);
        trySpr(1);
        tryBg(0, 0);  // Affine layer — tile priority is always low (matches 1L in the chart)
        trySpr(0);
        if (ext) tryBg(1, 0);
        break;
    }

    default:
        break;
    }

    CompositeSample sample{};
    sample.winCmBit = winCmBit;
    sample.winIdx   = winIdx;
    sample.rgb      = cgramToArgb(m_cgram[winIdx]);

    // Mode 7 direct colour bypasses CGRAM indexing for affine BG tiles when $2130 bit0 set.
    if ((m_bgMode == 7) && ((m_cgswsel & 1) != 0) && found && (winCmBit == 0x01 || winCmBit == 0x02)) {
        sample.rgb = mode7DirectColorArgb(winIdx);
    }
    return sample;
}

uint32_t Ppu::finalizePixelRgb(int x, const CompositeSample& main,
                               const CompositeSample* subSampleMaybe) const
{
    if (forceMainBlackFromColorWindow(x)) {
        return 0xFF000000u;
    }

    // CGADSUB MHBO4321 — backdrop bit5, OBJ bit4, BG1..4 bits0..3.
    bool doColorMath = (m_cgadsub & main.winCmBit) != 0;
    // OBJ palettes 0–3 never receive color math (main screen sprites only).
    if (main.winCmBit == 0x10) {
        if ((m_cgadsub & 0x10) == 0) {
            doColorMath = false;
        } else if (main.winIdx >= 128) {
            const unsigned pal = static_cast<unsigned>(main.winIdx - 128u) >> 4u;
            if (pal < 4u) doColorMath = false;
        }
    }

    if (!doColorMath) {
        return main.rgb;
    }

    uint32_t subR;
    uint32_t subG;
    uint32_t subB;

    const bool subFromScreen = (m_cgswsel & 0x02u) != 0;
    if (forceSubTransparentFromColorWindow(x)) {
        subR = subG = subB = 0;
    } else if (subFromScreen) {
        if (!subSampleMaybe) {
            subR = subG = subB = 0;
        } else {
            subR = (subSampleMaybe->rgb >> 16) & 0xFF;
            subG = (subSampleMaybe->rgb >> 8) & 0xFF;
            subB = subSampleMaybe->rgb & 0xFF;
        }
    } else {
        subR = static_cast<uint32_t>(m_fixedR) << 3;
        subG = static_cast<uint32_t>(m_fixedG) << 3;
        subB = static_cast<uint32_t>(m_fixedB) << 3;
    }

    const bool doSub  = ((m_cgadsub >> 7) & 1) != 0;
    const bool doHalf = ((m_cgadsub >> 6) & 1) != 0;

    uint32_t mR = (main.rgb >> 16) & 0xFF;
    uint32_t mG = (main.rgb >> 8) & 0xFF;
    uint32_t mB = main.rgb & 0xFF;

    if (doSub) {
        mR = (mR > subR) ? mR - subR : 0u;
        mG = (mG > subG) ? mG - subG : 0u;
        mB = (mB > subB) ? mB - subB : 0u;
    } else {
        mR = std::min(255u, mR + subR);
        mG = std::min(255u, mG + subG);
        mB = std::min(255u, mB + subB);
    }
    if (doHalf) {
        mR >>= 1;
        mG >>= 1;
        mB >>= 1;
    }
    return 0xFF000000u | (mR << 16) | (mG << 8) | mB;
}

// -----------------------------------------------------------------------
// renderScanline — main entry point called once per active scanline
// -----------------------------------------------------------------------
void Ppu::renderScanline(int line) {
    if (line < 0 || line >= 224) return;

    uint32_t* row = m_framebuffer.data() + line * 256;

    // Forced blank → black scanline
    if (m_forcedBlank) {
        for (int x = 0; x < 256; ++x) row[x] = 0xFF000000u;
        return;
    }

    // One-shot diagnostic: print PPU state on the very first active scanline
    if (!m_diagDone && line == 0) {
        m_diagDone = true;
        const uint16_t chr1 = static_cast<uint16_t>((m_bgNBA[0] & 0x0F) * 0x1000);
        const uint16_t tm1  = static_cast<uint16_t>((m_bgSC[0] >> 2) * 0x400);
        const uint16_t chr3 = chrBase(2);
        const uint16_t tm3  = static_cast<uint16_t>((m_bgSC[2] >> 2) * 0x400);
        std::fprintf(stderr,
            "[PPU diag] First active frame:\n"
            "  bgMode=%u  tm=$%02X  ts=$%02X  bg3pri=%u  vramWrites=%u\n"
            "  TM: BG1=%u BG2=%u BG3=%u OBJ=%u\n"
            "  bgSC=%02X %02X %02X %02X  bgNBA=%02X %02X\n"
            "  BG1 CHR@%04X: %04X %04X %04X %04X\n"
            "  BG1 TM@%04X:  %04X %04X %04X %04X\n"
            "  BG3 CHR@%04X: %04X %04X %04X %04X\n"
            "  BG3 TM@%04X:  %04X %04X %04X %04X\n",
            m_bgMode, m_tm, m_ts, (unsigned)m_bg3Priority, m_vramWrites,
            (m_tm >> 0) & 1, (m_tm >> 1) & 1, (m_tm >> 2) & 1, (m_tm >> 4) & 1,
            m_bgSC[0], m_bgSC[1], m_bgSC[2], m_bgSC[3],
            m_bgNBA[0], m_bgNBA[1],
            chr1,
            m_vram[(chr1+0)&0x7FFF], m_vram[(chr1+1)&0x7FFF],
            m_vram[(chr1+2)&0x7FFF], m_vram[(chr1+3)&0x7FFF],
            tm1,
            m_vram[(tm1+0)&0x7FFF], m_vram[(tm1+1)&0x7FFF],
            m_vram[(tm1+2)&0x7FFF], m_vram[(tm1+3)&0x7FFF],
            chr3,
            m_vram[(chr3+0)&0x7FFF], m_vram[(chr3+1)&0x7FFF],
            m_vram[(chr3+2)&0x7FFF], m_vram[(chr3+3)&0x7FFF],
            tm3,
            m_vram[(tm3+0)&0x7FFF], m_vram[(tm3+1)&0x7FFF],
            m_vram[(tm3+2)&0x7FFF], m_vram[(tm3+3)&0x7FFF]);
    }

    // Per-layer pixel buffers (TM | TS renders — split into main/sub composites below)
    LayerPixel  bg0[256]{}, bg1[256]{}, bg2[256]{}, bg3[256]{};
    SpritePixel spr[256]{};

    const uint8_t tmts = static_cast<uint8_t>(static_cast<unsigned>(m_tm) | static_cast<unsigned>(m_ts));

    switch (m_bgMode) {
    case 0:
        if (tmts & 0x01) renderBg(0, 2, line, bg0);
        if (tmts & 0x02) renderBg(1, 2, line, bg1);
        if (tmts & 0x04) renderBg(2, 2, line, bg2);
        if (tmts & 0x08) renderBg(3, 2, line, bg3);
        break;
    case 1:
        if (tmts & 0x01) renderBg(0, 4, line, bg0);
        if (tmts & 0x02) renderBg(1, 4, line, bg1);
        if (tmts & 0x04) renderBg(2, 2, line, bg2);
        break;
    case 2:
        if (tmts & 0x01) renderBg(0, 4, line, bg0);
        if (tmts & 0x02) renderBg(1, 4, line, bg1);
        break;
    case 3:
        if (tmts & 0x01) renderBg(0, 8, line, bg0);
        if (tmts & 0x02) renderBg(1, 4, line, bg1);
        break;
    case 4:
        if (tmts & 0x01) renderBg(0, 8, line, bg0);
        if (tmts & 0x02) renderBg(1, 2, line, bg1);
        break;
    case 5:
        if (tmts & 0x01) renderBg(0, 4, line, bg0);
        if (tmts & 0x02) renderBg(1, 2, line, bg1);
        break;
    case 6:
        if (tmts & 0x01) renderBg(0, 4, line, bg0);
        break;
    case 7:
        if ((tmts & 0x03) != 0) {
            renderMode7(line, bg0, bg1);
        }
        break;
    default:  // undefined bg modes — backdrop only
        for (int x = 0; x < 256; ++x) row[x] = applyInidispLuma(cgramToArgb(m_cgram[0]));
        return;
    }

    if (tmts & 0x10) renderSprites(line, spr);

    // Split raster into main/sub designation, then independent per-screen window masking.
    LayerPixel  bg0m[256]{}, bg1m[256]{}, bg2m[256]{}, bg3m[256]{};
    LayerPixel  bg0s[256]{}, bg1s[256]{}, bg2s[256]{}, bg3s[256]{};
    SpritePixel sprm[256]{};
    SpritePixel sprs[256]{};

    std::memcpy(bg0m, bg0, sizeof(bg0));
    std::memcpy(bg1m, bg1, sizeof(bg1));
    std::memcpy(bg2m, bg2, sizeof(bg2));
    std::memcpy(bg3m, bg3, sizeof(bg3));
    std::memcpy(sprm, spr, sizeof(spr));
    std::memcpy(bg0s, bg0, sizeof(bg0));
    std::memcpy(bg1s, bg1, sizeof(bg1));
    std::memcpy(bg2s, bg2, sizeof(bg2));
    std::memcpy(bg3s, bg3, sizeof(bg3));
    std::memcpy(sprs, spr, sizeof(spr));

    auto clearIfDisabledMain = [&](uint8_t tmBit, LayerPixel* b) {
        if ((m_tm & tmBit) == 0) std::memset(b, 0, sizeof(LayerPixel) * 256u);
    };
    clearIfDisabledMain(0x01, bg0m);
    clearIfDisabledMain(0x02, bg1m);
    clearIfDisabledMain(0x04, bg2m);
    clearIfDisabledMain(0x08, bg3m);
    if ((m_tm & 0x10) == 0) std::memset(sprm, 0, sizeof(sprm));

    auto clearIfDisabledSub = [&](uint8_t tsBit, LayerPixel* b) {
        if ((m_ts & tsBit) == 0) std::memset(b, 0, sizeof(LayerPixel) * 256u);
    };
    clearIfDisabledSub(0x01, bg0s);
    clearIfDisabledSub(0x02, bg1s);
    clearIfDisabledSub(0x04, bg2s);
    clearIfDisabledSub(0x08, bg3s);
    if ((m_ts & 0x10) == 0) std::memset(sprs, 0, sizeof(sprs));

    applyWindowMask(line, bg0m, bg1m, bg2m, bg3m, sprm, true);
    applyWindowMask(line, bg0s, bg1s, bg2s, bg3s, sprs, false);

    const bool useSubAddend = (m_cgswsel & 0x02u) != 0;
    for (int x = 0; x < 256; ++x) {
        const CompositeSample mains = compositeSample(x, bg0m, bg1m, bg2m, bg3m, sprm);
        if (useSubAddend) {
            const CompositeSample subs = compositeSample(x, bg0s, bg1s, bg2s, bg3s, sprs);
            row[x] = applyInidispLuma(finalizePixelRgb(x, mains, &subs));
        } else {
            row[x] = applyInidispLuma(finalizePixelRgb(x, mains, nullptr));
        }
    }
}
