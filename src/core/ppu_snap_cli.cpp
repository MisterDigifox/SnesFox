#include "ppu_snap_cli.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "bus.hpp"
#include "cli_common.hpp"
#include "cpu.hpp"
#include "gsu.hpp"
#include "header.hpp"
#include "ppu.hpp"
#include "rom.hpp"
#include "sdsp.hpp"

int runPpuSnap(const std::string& romPath, uint64_t frames) {
    Rom rom(romPath);
    const auto& data = rom.data();
    if (data.size() < 0x10000) {
        throw std::runtime_error("ROM: unexpected size");
    }

    const RomMapping mapping = HeaderParser::detect(data);
    const bool isLoRom = (mapping == RomMapping::LoROM);
    const uint16_t resetVector =
        static_cast<uint16_t>(data[(isLoRom ? 0x7FFC : 0xFFFC)]
                              | (data[(isLoRom ? 0x7FFD : 0xFFFD)] << 8));

    Bus bus(data);
    bus.reset();
    CPU cpu;
    cpu.reset(bus, resetVector);

    uint64_t audioTotal = 0;
    uint64_t audioClipped = 0;
    int16_t audioPeak = 0;
    double audioSumSq = 0.0;
    std::array<Sdsp::PcmFrame, 4096> audioBuf{};

    for (uint64_t f = 0; f < frames; ++f) {
        const uint64_t frameStartCycles = cpu.cycles();

        while ((cpu.cycles() - frameStartCycles) < Bus::kCyclesPerFrame) {
            cpu.step(bus);
            advanceCpuScheduling(bus, cpu, false);
        }

        size_t n;
        while ((n = bus.apu().popAudioSamples(audioBuf.data(), audioBuf.size())) > 0) {
            for (size_t i = 0; i < n; ++i) {
                for (int16_t s : {audioBuf[i].left, audioBuf[i].right}) {
                    ++audioTotal;
                    if (s == 32767 || s == -32768) ++audioClipped;
                    if (std::abs(static_cast<int>(s)) > std::abs(static_cast<int>(audioPeak))) audioPeak = s;
                    audioSumSq += static_cast<double>(s) * static_cast<double>(s);
                }
            }
        }
    }
    if (audioTotal > 0) {
        std::cerr << "--- Audio stats over " << std::dec << frames << " frame(s)\n"
                  << "  samples=" << audioTotal << " clipped(=+-32767/32768)=" << audioClipped
                  << " (" << (100.0 * static_cast<double>(audioClipped) / static_cast<double>(audioTotal)) << "%)"
                  << " peak=" << audioPeak
                  << " rms=" << std::sqrt(audioSumSq / static_cast<double>(audioTotal)) << "\n";
    }

    const Ppu& p = bus.ppu();
    const uint16_t* v = p.vram();
    const uint8_t nba34 = p.bgNBA34();
    const uint16_t chrW = static_cast<uint16_t>((nba34 & 0x0F) * 0x1000u);
    const uint16_t tmWords = static_cast<uint16_t>((p.bgSC(2) >> 2) * 0x400u);

    auto hexW = [](uint16_t x) -> std::string {
        std::ostringstream o;
        o << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << x;
        return o.str();
    };

    std::cerr << "--- PPU snap after " << std::dec << frames << " frame(s)\n";
    std::cerr << "  forcedBlank=" << (p.forcedBlank() ? 1 : 0) << "  bgMode=" << static_cast<int>(p.bgMode())
              << "  bg3PrioBit=" << (p.bg3Priority() ? 1 : 0) << "  tm=$" << hex8(p.tm()) << " (BG1234|OBJ="
              << ((p.tm() >> 0) & 1) << ((p.tm() >> 1) & 1) << ((p.tm() >> 2) & 1) << ((p.tm() >> 3) & 1) << "|"
              << ((p.tm() >> 4) & 1) << ")\n";

    std::cerr << "  BG34NBA=$" << hex8(nba34) << " -> BG3 CHR word base $" << hexW(chrW) << '\n';

    unsigned tmPri13 = 0;
    unsigned tmNonzero = 0;
    unsigned chrNz = 0;
    const int kPeek = 32;
    std::cerr << "  BG3 tilemap @$" << hexW(tmWords) << " words[0.." << (kPeek - 1) << "]:\n   ";
    for (int i = 0; i < kPeek; ++i) {
        const uint16_t w = v[(tmWords + static_cast<uint16_t>(i)) & 0x7FFF];
        std::cerr << hexW(w) << (i + 1 == kPeek ? '\n' : ' ');
        if (w != 0) ++tmNonzero;
        if (w & 0x2000) ++tmPri13;
    }

    std::cerr << "  BG3 CHR @$" << hexW(chrW) << " first " << kPeek << " words:\n   ";
    for (int i = 0; i < kPeek; ++i) {
        const uint16_t w = v[(chrW + static_cast<uint16_t>(i)) & 0x7FFF];
        std::cerr << hexW(w) << (i + 1 == kPeek ? '\n' : ' ');
        if (w != 0) ++chrNz;
    }

    const uint32_t* fb = p.framebuffer();
    unsigned nonBlack = 0;
    unsigned uniq = 0;
    uint32_t seen[384]{};
    for (size_t i = 0; i < 256u * 224u; ++i) {
        const uint32_t c = fb[i];
        if ((c >> 24) != 0xFF) continue;
        if (c != 0xFF000000u) {
            ++nonBlack;
            size_t si = uniq;
            bool dup = false;
            while (si-- > 0) {
                if (seen[si] == c) {
                    dup = true;
                    break;
                }
            }
            if (!dup && uniq < sizeof(seen) / sizeof(seen[0])) seen[uniq++] = c;
        }
    }

    std::cerr << "  framebuffer: non-black opaque pix=" << nonBlack << " uniqColors~=" << uniq << '\n';

    if (bus.hasSuperFx()) {
        const GSU& g = bus.gsu();
        std::cerr << "  GSU: launches=" << g.launchCount() << " stops=" << g.stopCount()
                  << " running=" << (g.running() ? 1 : 0) << " plotCount=" << g.plotCount()
                  << " lastSessionPlots=" << g.lastSessionPlots()
                  << " lastSessionCycles=" << g.lastSessionCycles()
                  << " pc=$" << hex8(g.pbr()) << ":" << hexW(g.pc())
                  << " scbr=$" << hex8(g.scbr()) << " scmr=$" << hex8(g.scmr())
                  << " rombr=$" << hex8(g.rombr()) << " rambr=" << (g.rambr() ? 1 : 0) << '\n';
    }

    {
        std::ofstream ppm("/tmp/snap.ppm", std::ios::binary);
        ppm << "P6\n256 224\n255\n";
        for (size_t i = 0; i < 256u * 224u; ++i) {
            const uint32_t c = fb[i];
            const char rgb[3] = {
                static_cast<char>((c >> 16) & 0xFF),
                static_cast<char>((c >> 8) & 0xFF),
                static_cast<char>(c & 0xFF)
            };
            ppm.write(rgb, 3);
        }
    }

    std::cerr << "  heuristic: nonzero map words=" << tmNonzero << "  tile-priority(bit13) entries=" << tmPri13
              << "  nonzero CHR peek words=" << chrNz << '\n';

    // --- TEMP sprite diag ---
    std::cerr << "  OBSEL=$" << hex8(p.obsel()) << "\n";
    {
        const uint16_t nameBase = static_cast<uint16_t>((p.obsel() & 0x07) << 12);
        unsigned nz = 0;
        for (int i = 0; i < 1024; ++i) if (v[(nameBase + i) & 0x7FFF]) ++nz;
        std::cerr << "  OBJ CHR@$" << hexW(nameBase) << " nonzero(of 1024 peeked, 64 tiles)=" << nz << "\n";
        std::cerr << "  tile18 (word off 288..303): ";
        for (int i = 288; i < 304; ++i) std::cerr << hexW(v[(nameBase + i) & 0x7FFF]) << ' ';
        std::cerr << "\n";
        std::cerr << "  VRAM@$0400 (DMA target) words[0..31]: ";
        for (int i = 0; i < 32; ++i) std::cerr << hexW(v[(0x0400 + i) & 0x7FFF]) << ' ';
        std::cerr << "\n";
    }
    {
        const uint16_t* cg = p.cgram();
        std::cerr << "  CGRAM[128..143] (OBJ pal0): ";
        for (int i = 128; i < 144; ++i) std::cerr << hexW(cg[i]) << ' ';
        std::cerr << "\n  fb(48,100)=" << std::hex << fb[100*256+48]
                  << "  fb(8,10)=" << fb[10*256+8]
                  << "  fb(220,100)=" << fb[100*256+220] << std::dec << "\n";
        std::cerr << "  fb row100 x=95..120: ";
        for (int x = 95; x <= 120; ++x) std::cerr << std::hex << fb[100*256+x] << std::dec << ' ';
        std::cerr << "\n";
    }
    for (int bg = 0; bg < 2; ++bg) {
        const uint16_t tmBase = static_cast<uint16_t>((p.bgSC(bg) >> 2) * 0x400u);
        unsigned pri = 0, nz2 = 0;
        for (int i = 0; i < 1024; ++i) {
            const uint16_t w = v[(tmBase + static_cast<uint16_t>(i)) & 0x7FFF];
            if (w) ++nz2;
            if (w & 0x2000) ++pri;
        }
        std::cerr << "  BG" << (bg+1) << " tilemap@$" << hexW(tmBase) << " nonzero=" << nz2
                  << "/1024  priHigh=" << pri << "/1024  sample[0..7]: ";
        for (int i = 0; i < 8; ++i) std::cerr << hexW(v[(tmBase + static_cast<uint16_t>(i)) & 0x7FFF]) << ' ';
        std::cerr << "  around(32,88)[tile col4 row11]: ";
        const int tc = 32/8, tr = 88/8;
        const uint16_t mapWord = v[(tmBase + static_cast<uint16_t>(tr*32+tc)) & 0x7FFF];
        std::cerr << hexW(mapWord) << '\n';

        const uint16_t chrBaseBg = p.chrBase(bg);
        unsigned chrNzBg = 0;
        for (int i = 0; i < 4096; ++i) if (v[(chrBaseBg + static_cast<uint16_t>(i)) & 0x7FFF]) ++chrNzBg;
        std::cerr << "  BG" << (bg+1) << " CHR@$" << hexW(chrBaseBg) << " nonzero=" << chrNzBg
                  << "/4096 first16: ";
        for (int i = 0; i < 16; ++i) std::cerr << hexW(v[(chrBaseBg + static_cast<uint16_t>(i)) & 0x7FFF]) << ' ';
        std::cerr << '\n';

        // What does the SPECIFIC tile referenced by that sampled tilemap entry actually look
        // like at each BG's own CHR base? (bg0=BG1 $210B lo, bg1=BG2 $210B hi, bg2=BG3 $210C lo)
        const unsigned tileIdx = mapWord & 0x3FF;
        std::cerr << "  tile#" << tileIdx << " (from BG" << (bg+1) << " map) 4bpp data at each CHR base:\n";
        for (int cb = 0; cb < 3; ++cb) {
            const uint16_t base = p.chrBase(cb);
            const uint16_t off = static_cast<uint16_t>(base + tileIdx * 16u);
            std::cerr << "    @BG" << (cb+1) << "base($" << hexW(base) << ")+tile*16=$" << hexW(off) << ": ";
            for (int i = 0; i < 8; ++i) std::cerr << hexW(v[(off + static_cast<uint16_t>(i)) & 0x7FFF]) << ' ';
            std::cerr << '\n';
        }
    }
    const uint8_t* oam = p.oam();
    for (int i = 0; i < 8; ++i) {
        const uint8_t* s = oam + i * 4;
        const uint8_t extra = oam[512 + (i >> 2)];
        const uint8_t eBits = (extra >> ((i & 3) << 1)) & 0x03;
        std::cerr << "  spr[" << i << "] x=" << (int)s[0] << "(+"<<(int)(eBits&1)<<") y=" << (int)s[1]
                  << " tile=$" << hex8(s[2]) << " attr=$" << hex8(s[3]) << " large=" << (int)((eBits>>1)&1) << "\n";
    }
    // --- end TEMP sprite diag ---

    return 0;
}
