#pragma once
#include <array>
#include <cstdint>

class Ppu {
public:
    void    reset();
    void    writeReg(uint16_t addr, uint8_t value);
    uint8_t readReg(uint16_t addr) const;

    // -------------------------------------------------------
    // Accessors for the renderer (PPU-02 onward)
    // -------------------------------------------------------
    bool     forcedBlank()   const { return m_forcedBlank; }
    uint8_t  brightness()    const { return m_brightness; }
    uint8_t  bgMode()        const { return m_bgMode; }
    bool     bg3Priority()   const { return m_bg3Priority; }
    uint8_t  bgSC(int bg)    const { return m_bgSC[bg]; }   // tilemap base+size
    uint8_t  bgNBA12()       const { return m_bgNBA[0]; }   // BG1/2 CHR base
    uint8_t  bgNBA34()       const { return m_bgNBA[1]; }   // BG3/4 CHR base
    uint16_t bgHOFS(int bg)  const { return m_bgHOFS[bg]; }
    uint16_t bgVOFS(int bg)  const { return m_bgVOFS[bg]; }
    uint8_t  obsel()         const { return m_obsel; }
    uint8_t  tm()            const { return m_tm; }         // main screen layers
    uint8_t  ts()            const { return m_ts; }         // sub screen layers
    uint8_t  tmw()           const { return m_tmw; }
    uint8_t  tsw()           const { return m_tsw; }
    uint8_t  cgswsel()       const { return m_cgswsel; }
    uint8_t  cgadsub()       const { return m_cgadsub; }
    uint8_t  fixedColorR()   const { return m_fixedR; }
    uint8_t  fixedColorG()   const { return m_fixedG; }
    uint8_t  fixedColorB()   const { return m_fixedB; }
    uint8_t  setini()        const { return m_setini; }
    uint8_t  w12sel()        const { return m_w12sel; }
    uint8_t  w34sel()        const { return m_w34sel; }
    uint8_t  wobjsel()       const { return m_wobjsel; }
    uint8_t  wh(int n)       const { return m_wh[n]; }
    uint8_t  wbglog()        const { return m_wbglog; }
    uint8_t  wobjlog()       const { return m_wobjlog; }
    uint32_t vramWrites()   const { return m_vramWrites; }
    uint8_t  vmain()        const { return m_vmain; }
    uint16_t vramAddr()     const { return m_vramAddr; }

    const uint16_t* vram()  const { return m_vram.data(); }
    const uint8_t*  oam()   const { return m_oam.data(); }
    const uint16_t* cgram() const { return m_cgram.data(); }

    // -------------------------------------------------------
    // Rendering — called by Bus::stepPeripherals each scanline
    // -------------------------------------------------------
    void renderScanline(int line);
    const uint32_t* framebuffer() const { return m_framebuffer.data(); }

private:
    // -------------------------------------------------------
    // Memory
    // -------------------------------------------------------
    std::array<uint16_t, 32768> m_vram{};   // 64 KB, word-addressable
    std::array<uint8_t,    544> m_oam{};    // 512 + 32 extra bytes
    std::array<uint16_t,   256> m_cgram{};  // 256 × BGR555

    // -------------------------------------------------------
    // $2100 INIDISP
    // -------------------------------------------------------
    bool    m_forcedBlank = true;
    uint8_t m_brightness  = 0x0F;

    // -------------------------------------------------------
    // $2101 OBSEL — sprite size / CHR base
    // -------------------------------------------------------
    uint8_t m_obsel = 0;

    // -------------------------------------------------------
    // $2102/$2103 OAMADDL/H, $2104 OAMDATA, $2138 OAMDATAREAD
    // -------------------------------------------------------
    uint16_t m_oamBaseAddr = 0;  // base word address from $2102/$2103
    mutable uint16_t m_oamAddr = 0;  // current byte address (0-543)
    bool     m_oamPriority = false;
    uint8_t  m_oamLatch    = 0;  // first-byte buffer for paired main-OAM writes

    // -------------------------------------------------------
    // $2105 BGMODE / $2106 MOSAIC
    // -------------------------------------------------------
    uint8_t m_bgMode      = 0;
    bool    m_bg3Priority = false;
    uint8_t m_bgTileSize  = 0; // bits 3:0 → BG1/2/3/4 tile size (0=8×8, 1=16×16)
    uint8_t m_mosaic      = 0;

    // -------------------------------------------------------
    // $2107-$210A BG1-4 SC (tilemap base + mirror size)
    // -------------------------------------------------------
    uint8_t m_bgSC[4]{};

    // -------------------------------------------------------
    // $210B/$210C BG1/2 and BG3/4 CHR base
    // -------------------------------------------------------
    uint8_t m_bgNBA[2]{};

    // -------------------------------------------------------
    // $210D-$2114 BG scroll (shared write latch)
    // -------------------------------------------------------
    uint16_t m_bgHOFS[4]{};
    uint16_t m_bgVOFS[4]{};
    uint8_t  m_bgOldByte = 0;  // shared latch across all BG scroll writes

    // -------------------------------------------------------
    // $2115-$2119 / $2139-$213A  VRAM access
    // -------------------------------------------------------
    uint8_t          m_vmain       = 0x80; // default: increment after high-byte access
    mutable uint16_t m_vramAddr    = 0;
    mutable uint16_t m_vramReadBuf = 0;   // prefetch buffer

    // -------------------------------------------------------
    // $211A-$2120 Mode 7 (shared write latch)
    // -------------------------------------------------------
    uint8_t m_m7sel   = 0;
    int16_t m_m7a = 0, m_m7b = 0, m_m7c = 0, m_m7d = 0x0100;
    int16_t m_m7x = 0, m_m7y = 0;
    uint8_t m_m7latch = 0;

    // -------------------------------------------------------
    // $2121 CGADD / $2122 CGDATA / $213B CGDATAREAD
    // -------------------------------------------------------
    mutable uint8_t m_cgramWordAddr = 0;   // color index (0-255)
    mutable bool    m_cgramFlip     = false; // false=low byte next, true=high byte next
    uint8_t         m_cgramBuf      = 0;

    // -------------------------------------------------------
    // $2123-$212B windowing
    // -------------------------------------------------------
    uint8_t m_w12sel  = 0, m_w34sel  = 0, m_wobjsel = 0;
    uint8_t m_wh[4]{};
    uint8_t m_wbglog  = 0, m_wobjlog = 0;

    // -------------------------------------------------------
    // $212C-$212F screen designation
    // -------------------------------------------------------
    uint8_t m_tm = 0, m_ts = 0;
    uint8_t m_tmw = 0, m_tsw = 0;

    // -------------------------------------------------------
    // $2130-$2132 color math
    // -------------------------------------------------------
    uint8_t m_cgswsel = 0;
    uint8_t m_cgadsub = 0;
    uint8_t m_fixedR  = 0, m_fixedG = 0, m_fixedB = 0;

    // -------------------------------------------------------
    // $2133 SETINI
    // -------------------------------------------------------
    uint8_t m_setini = 0;

    // -------------------------------------------------------
    // Helpers
    // -------------------------------------------------------
    uint16_t vramStep()             const;
    // Map VRAM logical address (VMADD) to physical layout per $2115 increment mode (Bsnes-compatible).
    uint16_t vramPhysicalAddr(uint16_t logical) const;
    void     vramPrefetch()         const;
    void     writeOam(uint8_t value);
    uint8_t  readOam()              const;

    // -------------------------------------------------------
    // Rendering helpers
    // -------------------------------------------------------
    struct LayerPixel {
        uint8_t cgramIdx = 0;  // 0 = transparent
        uint8_t priority = 0;  // tile priority bit (0 or 1)
    };

    struct SpritePixel {
        uint8_t cgramIdx = 0;  // 0 = transparent; 128 + pal*16 + color otherwise
        uint8_t priority = 0;  // sprite priority 0-3 (from OAM attr bits 5:4)
    };

    std::array<uint32_t, 256 * 224> m_framebuffer{};
    mutable bool m_objRangeOver = false; // set when >32 sprites on a scanline
    bool         m_diagDone     = false; // one-shot first-active-frame diagnostic
    uint32_t     m_vramWrites   = 0;    // total VRAM word writes (incremented per $2118/$2119 pair)

    uint32_t mode7DirectColorArgb(uint8_t pixel) const;

    void renderMode7(int line, LayerPixel* affineBg /* bg0 */, LayerPixel* extBg /* bg1 */) const;

    void     renderBg(int bg, int bpp, int line, LayerPixel* out) const;
    void     renderSprites(int line, SpritePixel* out) const;
    void     applyWindowMask(int line,
                              LayerPixel* bg0, LayerPixel* bg1,
                              LayerPixel* bg2, LayerPixel* bg3,
                              SpritePixel* spr, bool mainScreen) const;
    uint16_t tilemapEntry(int bg, int tileCol, int tileRow) const;
    uint16_t chrBase(int bg) const;
    uint8_t  getPixel(int bpp, uint16_t base, uint16_t tileNum, int row, int col) const;
    uint32_t cgramToArgb(uint16_t bgr555) const;
    uint32_t applyInidispLuma(uint32_t argb) const;
    uint32_t compositePixel(int x,
                             const LayerPixel*  bg0,
                             const LayerPixel*  bg1,
                             const LayerPixel*  bg2,
                             const LayerPixel*  bg3,
                             const SpritePixel* spr) const;
    // Window helpers
    bool windowMaskBg(int x, int bg) const;
    bool windowMaskObj(int x) const;
};
