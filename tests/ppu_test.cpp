#include "ppu_test.hpp"
#include "ppu.hpp"

#include <cstdint>
#include <cstdio>

// -----------------------------------------------------------------------
// Lightweight assertion-based regression tests for Ppu register semantics.
// Each test drives a freshly-reset Ppu purely through its public writeReg()/
// renderScanline()/framebuffer() API — no ROM, Bus, or CPU involved — so a
// wrong bit shift or inverted polarity shows up as a wrong pixel color
// without needing a fixture ROM or visual inspection.
// -----------------------------------------------------------------------

namespace {

int g_failures = 0;
int g_checks    = 0;

void expectEq(uint32_t actual, uint32_t expected, const char* what) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s (expected $%08X, got $%08X)\n", what, expected, actual);
    } else {
        std::fprintf(stderr, "PASS: %s\n", what);
    }
}

void writeVramWord(Ppu& p, uint16_t wordAddr, uint16_t value) {
    p.writeReg(0x2115, 0x80); // word-increment mode, step 1
    p.writeReg(0x2116, static_cast<uint8_t>(wordAddr & 0xFF));
    p.writeReg(0x2117, static_cast<uint8_t>(wordAddr >> 8));
    p.writeReg(0x2118, static_cast<uint8_t>(value & 0xFF));
    p.writeReg(0x2119, static_cast<uint8_t>(value >> 8));
}

// Writes one 8x8 4bpp tile at VRAM word `base`, with column `col`'s color
// given by colorForCol[col] (0-15). Passing the same color for all 8 columns
// produces a solid tile.
void writeTile(Ppu& p, uint16_t base, const uint8_t colorForCol[8]) {
    uint16_t w0 = 0, w1 = 0; // rows 0-7 (plane0/1) and 8-15 (plane2/3) — identical per row here
    for (int col = 0; col < 8; ++col) {
        const int bit = 7 - col;
        const uint8_t c = colorForCol[col];
        if (c & 0x1) w0 |= static_cast<uint16_t>(1u << bit);
        if (c & 0x2) w0 |= static_cast<uint16_t>(1u << (8 + bit));
        if (c & 0x4) w1 |= static_cast<uint16_t>(1u << bit);
        if (c & 0x8) w1 |= static_cast<uint16_t>(1u << (8 + bit));
    }
    for (int row = 0; row < 8; ++row) writeVramWord(p, static_cast<uint16_t>(base + row), w0);
    for (int row = 0; row < 8; ++row) writeVramWord(p, static_cast<uint16_t>(base + 8 + row), w1);
}

void writeSolidTile(Ppu& p, uint16_t base, uint8_t color) {
    const uint8_t cols[8] = { color, color, color, color, color, color, color, color };
    writeTile(p, base, cols);
}

void writeCgram(Ppu& p, uint8_t index, uint16_t bgr555) {
    p.writeReg(0x2121, index);
    p.writeReg(0x2122, static_cast<uint8_t>(bgr555 & 0xFF));
    p.writeReg(0x2122, static_cast<uint8_t>(bgr555 >> 8));
}

// index*4 bytes: X, Y, tile, attr — matches hardware main-OAM layout.
void writeOamSprite(Ppu& p, int index, uint8_t x, uint8_t y, uint8_t tile, uint8_t attr) {
    const uint16_t wordAddr = static_cast<uint16_t>(index * 2);
    p.writeReg(0x2102, static_cast<uint8_t>(wordAddr & 0xFF));
    p.writeReg(0x2103, static_cast<uint8_t>((wordAddr >> 8) & 0x01));
    p.writeReg(0x2104, x);
    p.writeReg(0x2104, y);
    p.writeReg(0x2104, tile);
    p.writeReg(0x2104, attr);
}

void fillTilemapWithTileZeroPal0(Ppu& p, uint16_t base, int wordCount) {
    for (int i = 0; i < wordCount; ++i) writeVramWord(p, static_cast<uint16_t>(base + i), 0x0000);
}

// -----------------------------------------------------------------------
// Regression test: OBJ Name Base Address must use the hardware unit of
// 0x2000 words (<<13), not 0x1000 words (<<12). Bug fixed in ppu.cpp,
// caught only by SplitScrolling.sfc — no other fixture used a nonzero
// OBSEL base-select index, so this exercises that distinguishing value.
// -----------------------------------------------------------------------
void testObjNameBaseAddressUnit() {
    Ppu p;
    p.reset();
    p.writeReg(0x2100, 0x0F); // forced blank off, full brightness

    // Two distinguishable solid tiles: one at the pre-fix (wrong) address,
    // one at the post-fix (correct) address for OBSEL base-select index 1.
    writeSolidTile(p, 0x1000, 5); // wrong address (old <<12 bug lands here)
    writeSolidTile(p, 0x2000, 9); // correct address (<<13)

    writeCgram(p, 128 + 5, 0x7C00); // OBJ pal0 color5 -> blue
    writeCgram(p, 128 + 9, 0x03E0); // OBJ pal0 color9 -> green

    p.writeReg(0x2101, 0x01);          // OBSEL: small size, base-select index 1
    writeOamSprite(p, 0, 10, 20, 0, 0x00); // tile 0, pal 0, small, no flip

    p.writeReg(0x212C, 0x10); // TM: OBJ on main screen only
    p.renderScanline(20);

    expectEq(p.framebuffer()[20 * 256 + 10], 0xFF00F800u,
        "OBSEL Name Base Address uses <<13 (0x2000 word unit), not <<12");
}

// -----------------------------------------------------------------------
// Regression test: W12SEL "area" bit polarity. area=1 means the layer is
// SHOWN inside the window (hidden outside) — the inverted reading hid the
// separator bar in SplitScrolling.sfc's DBZ split-screen effect.
// -----------------------------------------------------------------------
void testWindowAreaPolarity() {
    Ppu p;
    p.reset();
    p.writeReg(0x2100, 0x0F);
    p.writeReg(0x2105, 0x01); // BG mode 1

    writeSolidTile(p, 0x0000, 7);                 // BG1 CHR base 0, tile0 solid color 7
    fillTilemapWithTileZeroPal0(p, 0x1000, 1024); // tilemap base $1000 (distinct from CHR), all -> tile0/pal0
    writeCgram(p, 7, 0x001F); // BG pal0 color7 -> red

    p.writeReg(0x2107, 0x10); // BG1SC: tilemap base $1000 words (4 << 2), 32x32
    p.writeReg(0x2123, 0x03); // W12SEL: BG1 window1 area=1(inside), enable=1
    p.writeReg(0x2126, 0);    // WH0 (window1 left)
    p.writeReg(0x2127, 100);  // WH1 (window1 right) -> window1 = [0,100]

    p.writeReg(0x212C, 0x01); // TM: BG1 on main screen
    p.writeReg(0x212E, 0x01); // TMW: window-mask BG1 on main screen

    p.renderScanline(50);

    expectEq(p.framebuffer()[50 * 256 + 50], 0xFFF80000u,
        "window area=1 shows BG1 INSIDE [0,100] (x=50)");
    expectEq(p.framebuffer()[50 * 256 + 150], 0xFF000000u,
        "window area=1 hides BG1 OUTSIDE [0,100] (x=150, backdrop)");
}

// -----------------------------------------------------------------------
// Regression test: $2106 MOSAIC must actually affect rendering. It was
// previously stored but never consulted by renderBg — the demo's
// "flying into the sky" pixelation effect silently did nothing.
// -----------------------------------------------------------------------
void testMosaicBlockHold() {
    Ppu p;
    p.reset();
    p.writeReg(0x2100, 0x0F);
    p.writeReg(0x2105, 0x01); // BG mode 1

    // Column-alternating tile: even columns color1, odd columns color2 —
    // guarantees adjacent pixels differ without mosaic.
    const uint8_t cols[8] = { 1, 2, 1, 2, 1, 2, 1, 2 };
    writeTile(p, 0x0000, cols);
    fillTilemapWithTileZeroPal0(p, 0x1000, 1024); // tilemap base $1000 (distinct from CHR)
    writeCgram(p, 1, 0x001F); // color1 -> red
    writeCgram(p, 2, 0x7C00); // color2 -> blue

    p.writeReg(0x2107, 0x10); // BG1SC: tilemap base $1000 words (4 << 2), 32x32

    p.writeReg(0x212C, 0x01); // TM: BG1 on main screen

    // Without mosaic: x=0 and x=1 must differ (red vs blue).
    p.writeReg(0x2106, 0x00);
    p.renderScanline(50);
    const uint32_t noMosaicX0 = p.framebuffer()[50 * 256 + 0];
    const uint32_t noMosaicX1 = p.framebuffer()[50 * 256 + 1];
    expectEq(noMosaicX0, 0xFFF80000u, "mosaic off: x=0 is color1 (red)");
    expectEq(noMosaicX1, 0xFF0000F8u, "mosaic off: x=1 is color2 (blue), differs from x=0");

    // With BG1 mosaic size 4: x=0..3 must all sample x=0's color.
    p.writeReg(0x2106, 0x31); // size=4 (bits7-4=3), BG1 enable (bit0=1)
    p.renderScanline(50);
    expectEq(p.framebuffer()[50 * 256 + 1], 0xFFF80000u,
        "mosaic size 4: x=1 inherits x=0's color (red), not its own");
    expectEq(p.framebuffer()[50 * 256 + 3], 0xFFF80000u,
        "mosaic size 4: x=3 (last pixel in block) still inherits x=0's color");
}

// -----------------------------------------------------------------------
// Regression test: WOBJSEL's color/math window nibble (bits 4-7) must use
// the same area-low/enable-high bit order as every other window-enable
// pair in this register family (W12SEL's BG1 and BG2 nibbles, and WOBJSEL's
// own OBJ nibble in bits 0-3). It was previously read as enable-low/area-high,
// silently disabling the color window whenever a ROM set only the enable bit
// per the correct convention.
// -----------------------------------------------------------------------
void testColorWindowBitOrder() {
    Ppu p;
    p.reset();
    p.writeReg(0x2100, 0x0F);
    p.writeReg(0x2105, 0x01); // BG mode 1

    writeSolidTile(p, 0x0000, 3);
    fillTilemapWithTileZeroPal0(p, 0x1000, 1024);
    writeCgram(p, 3, 0x001F); // BG pal0 color3 -> red

    p.writeReg(0x2107, 0x10); // BG1SC: tilemap base $1000
    p.writeReg(0x212C, 0x01); // TM: BG1 on main screen

    p.writeReg(0x2126, 0);    // WH0 (window1 left)
    p.writeReg(0x2127, 100);  // WH1 (window1 right) -> window1 = [0,100]
    p.writeReg(0x2125, 0x20); // WOBJSEL: color window1 area=0(bit4), enable=1(bit5)
    p.writeReg(0x2130, 0x40); // CGWSEL: force main black OUTSIDE the color window (mm=1)

    p.renderScanline(50);

    // CGWSEL mm=1 ("NotMathWin") forces black OUTSIDE the color window,
    // so x=50 (inside [0,100]) stays unmodified and x=150 (outside) is black.
    expectEq(p.framebuffer()[50 * 256 + 50], 0xFFF80000u,
        "color window area/enable order: x=50 inside [0,100] shows BG1 unmodified");
    expectEq(p.framebuffer()[50 * 256 + 150], 0xFF000000u,
        "color window area/enable order: x=150 outside [0,100] forced black");
}

} // namespace

int runPpuSelfTests() {
    testObjNameBaseAddressUnit();
    testWindowAreaPolarity();
    testMosaicBlockHold();
    testColorWindowBitOrder();

    std::fprintf(stderr, "\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
