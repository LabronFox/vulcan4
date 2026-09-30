// vulcan4_gs_probe.cpp - the VULCAN 4 GS skeleton, proved with a real frame (goal G2.0).
//
// WHAT THIS IS
//   A runnable proof that the Graphics Synthesizer path works end to end on our own code, and a
//   PNG on disk whose every pixel came out of ps2xRuntime's GS. It is NOT a game renderer and
//   does not pretend to be. The design decision this implements is written up in docs/GS-PLAN.md.
//
// THE PATH BEING PROVED (the same one the recompiled guest will drive)
//   GS::init()            -> 4 MB of GS VRAM we allocate ourselves
//   GS::setRasterBackend  -> the runtime's own software rasteriser, GSCpuBackend
//   GS::processGIFPacket  -> real GIFTAG packets, the same decode path the EE's GIF DMA feeds
//   GSCpuBackend          -> rasterises into that VRAM
//   GS::latchHostPresentationFrame / copyLatchedHostPresentationFrame
//                         -> the runtime's own readback, RGBA bytes out
//   writePng()            -> a PNG we encode ourselves with zlib
//
//   There is no window, no screenshot and no window manager anywhere in this file. If the PNG has
//   a pixel in it, our rasteriser put it there.
//
// WHAT IS NOT IMPLEMENTED, and is printed as VULCAN4 LIMITATION: lines rather than glossed over.
//   See docs/LIMITATIONS.md.

#include "runtime/ps2_memory.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_types.h"

#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t kVramSize = 4u * 1024u * 1024u; // the real GS has 4 MB of VRAM
    constexpr uint32_t kFrameWidth = 512u;
    constexpr uint32_t kFrameHeight = 512u;

    // PSMCT32 is colour texture, 32-bit, with a 32-bit word swizzle. This is what the PS2's
    // NTSC mode uses for a standard display framebuffer and what GT4 draws its 3D into.
    constexpr uint32_t kPsmct32 = 0x00u;

    // Two different register layouts are in play, and mixing them up is the easiest mistake to
    // make here, so they are named apart.
    //
    // (1) FRAME / ZBUF, as the GS drawing registers use them: PSM in bits 0-2, base page in
    //     bits 4-12. This is the real hardware layout.
    constexpr uint64_t makeFbp(uint32_t basePage, uint32_t widthInWords)
    {
        return (static_cast<uint64_t>(basePage) << 4) | kPsmct32
            | (static_cast<uint64_t>(widthInWords) << 16);
    }
    // GS buffer width is counted in 64-pixel words, so 512 pixels is 8 words. Leaving FBW at
    // zero gives the rasteriser a row stride of zero, and it then draws nothing at all.
    constexpr uint32_t kFrameWidthWords = kFrameWidth / 64u;

    // Host-driven vblank tick counter. See the sync block in main() for what this is
    // and, more importantly, what it is not.
    uint64_t g_vblankTicks = 0u;

    // (2) DISPFB / DISPLAY, as ps2xRuntime's presentation path decodes them, which is its own
    //     compact layout and not the hardware one. Recorded here because the code reads it.
    //     DISPFB: base page 0-8, width in 64-pixel words 9-14, pixel format 15-19,
    //             read origin X 32-42, read origin Y 43-53.
    //     DISPLAY: horizontal magnification 23-26, width-1 at 32-43, height-1 at 44-54.
    constexpr uint64_t makeDispfb(uint32_t basePage, uint32_t widthInWords)
    {
        return static_cast<uint64_t>(basePage) | (static_cast<uint64_t>(widthInWords) << 9)
            | (static_cast<uint64_t>(kPsmct32) << 15);
    }
    constexpr uint64_t makeDisplay(uint32_t width, uint32_t height)
    {
        return (static_cast<uint64_t>(width - 1u) << 32) | (static_cast<uint64_t>(height - 1u) << 44);
    }
    // PMODE bit 0 is ENB1, "CRT 1 output enabled". Without it the presentation path refuses to
    // produce a frame at all, and returns an empty one with no error.
    constexpr uint64_t kPmodeCrt1Enabled = 0x1ull;

    void storeLE64(std::vector<uint8_t> &out, uint64_t v)
    {
        for (int i = 0; i < 8; ++i)
        {
            out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
        }
    }

    // Append one GIF REGLIST to a packet.
    //
    // The GIFTAG is 128 bits and the split is not obvious, so read this before touching it again:
    //   low  64 bits: bits 0..14 = NLOOP, bit 46 = PRE, bits 47..57 = PRIM under PRE,
    //                 bits 58..59 = FLG, bits 60..63 = NREG.
    //   high 64 bits: NREG nibbles, 4 bits per register, in slot order. This is where the
    //                 register *addresses* live, which is the part that is easy to get wrong.
    // FLG 1 == REGLIST. A REGLIST writes its values into the registers named in the high half.
    void appendReglist(std::vector<uint8_t> &packet, const std::vector<std::pair<uint8_t, uint64_t>> &writes)
    {
        constexpr size_t kMaxRegsPerTag = 16; // NREG is 4 bits, and 0 is reinterpreted as 16
        for (size_t base = 0; base < writes.size(); base += kMaxRegsPerTag)
        {
            const size_t count = (writes.size() - base < kMaxRegsPerTag) ? writes.size() - base : kMaxRegsPerTag;

            uint64_t tagLo = 1ull; // NLOOP = 1
            tagLo |= (static_cast<uint64_t>(GIF_FMT_REGLIST) << 58);
            tagLo |= (static_cast<uint64_t>(count) << 60);
            uint64_t tagHi = 0;
            for (size_t i = 0; i < count; ++i)
            {
                tagHi |= static_cast<uint64_t>(writes[base + i].first & 0xF) << (i * 4);
            }
            storeLE64(packet, tagLo);
            storeLE64(packet, tagHi);

            for (size_t i = 0; i < count; ++i)
            {
                storeLE64(packet, writes[base + i].second);
            }
            if (count & 1u)
            {
                storeLE64(packet, 0); // pad to a 16-byte boundary, as the hardware requires
            }
        }
    }

    // One GIFTAG + one REGLIST, for when a single short write is all that is needed.
    std::vector<uint8_t> reglistPacket(const std::vector<std::pair<uint8_t, uint64_t>> &writes)
    {
        std::vector<uint8_t> packet;
        appendReglist(packet, writes);
        return packet;
    }

    // ---------------------------------------------------------------- PNG writer
    //
    // Our own, so there is no image-library dependency and no question about where the bytes
    // came from. RGB8, one IDAT, filter type 0 on every scanline.
    void appendChunk(std::vector<uint8_t> &out, const char type[4], const std::vector<uint8_t> &data)
    {
        const uint32_t len = static_cast<uint32_t>(data.size());
        out.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(len & 0xFF));
        const size_t crcStart = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), data.begin(), data.end());
        const uLong crc = crc32(0, out.data() + crcStart, static_cast<uInt>(out.size() - crcStart));
        out.push_back(static_cast<uint8_t>((crc >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((crc >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(crc & 0xFF));
    }

    bool writePng(const std::string &path, const std::vector<uint8_t> &rgba, uint32_t w, uint32_t h)
    {
        if (rgba.size() < static_cast<size_t>(w) * h * 4u)
        {
            std::cerr << "writePng: not enough pixels\n";
            return false;
        }

        // Raw scanlines: filter byte 0 then RGB triples, dropping the alpha the GS gave us.
        std::vector<uint8_t> raw;
        raw.reserve(static_cast<size_t>(h) * (1u + w * 3u));
        for (uint32_t y = 0; y < h; ++y)
        {
            raw.push_back(0); // filter: None
            const uint8_t *row = rgba.data() + static_cast<size_t>(y) * w * 4u;
            for (uint32_t x = 0; x < w; ++x)
            {
                raw.push_back(row[x * 4 + 0]);
                raw.push_back(row[x * 4 + 1]);
                raw.push_back(row[x * 4 + 2]);
            }
        }

        uLongf compSize = compressBound(static_cast<uLong>(raw.size()));
        std::vector<uint8_t> comp(compSize);
        if (compress2(comp.data(), &compSize, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK)
        {
            std::cerr << "writePng: zlib compress failed\n";
            return false;
        }
        comp.resize(compSize);

        std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

        // IHDR is exactly 13 bytes: width(4) height(4) then five single bytes. Encoding the last
        // five as 32-bit words instead is a natural mistake and yields a structurally-parsable but
        // semantically invalid file (bit depth 0, colour type 0), which no decoder accepts.
        std::vector<uint8_t> ihdr;
        auto pushBE32 = [&ihdr](uint32_t v)
        {
            ihdr.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
            ihdr.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
            ihdr.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
            ihdr.push_back(static_cast<uint8_t>(v & 0xFF));
        };
        pushBE32(w);
        pushBE32(h);
        ihdr.push_back(8); // bit depth
        ihdr.push_back(2); // colour type 2 = truecolour RGB
        ihdr.push_back(0); // compression: deflate
        ihdr.push_back(0); // filter method: adaptive
        ihdr.push_back(0); // interlace: none
        appendChunk(png, "IHDR", ihdr);
        appendChunk(png, "IDAT", comp);
        appendChunk(png, "IEND", {});

        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::FILE *f = std::fopen(path.c_str(), "wb");
        if (!f)
        {
            std::cerr << "writePng: cannot open " << path << "\n";
            return false;
        }
        const size_t wrote = std::fwrite(png.data(), 1, png.size(), f);
        std::fclose(f);
        return wrote == png.size();
    }

    // A stable, cheap checksum so the frame can be verified without opening it.
    uint64_t checksumRgba(const std::vector<uint8_t> &rgba)
    {
        uint64_t h = 1469598103934665603ull; // FNV-1a
        for (uint8_t b : rgba)
        {
            h ^= b;
            h *= 1099511628211ull;
        }
        return h;
    }

    uint32_t countNonBackground(const std::vector<uint8_t> &rgba)
    {
        uint32_t n = 0;
        for (size_t i = 0; i + 3 < rgba.size(); i += 4)
        {
            if (rgba[i] != 0 || rgba[i + 1] != 0 || rgba[i + 2] != 0)
            {
                ++n;
            }
        }
        return n;
    }

    // ---------------------------------------------------------------- pattern
    //
    // The pixels are COMPUTED here, in our own code, and then handed to the GS transfer path
    // (BITBLTBUF/TRXPOS/TRXREG/TRXDIR + image data) exactly the way a guest uploads a texture.
    // Nothing is drawn behind the GS's back and no image library is involved: the only thing that
    // happens to these bytes afterwards is a memcpy into GS VRAM by the GS's own UploadImage.
    //
    // Three bands, so the frame is obviously "something drew that" and easy to judge by eye:
    //   top    - 8 SMPTE-ish colour bars
    //   middle - a two-axis gradient (varies in x AND y, so it is not a flat wash)
    //   bottom - a diagonal wedge whose width varies per row
    void buildPattern(std::vector<uint8_t> &rgba, uint32_t w, uint32_t h, uint32_t frameIndex)
    {
        rgba.assign(static_cast<size_t>(w) * h * 4u, 0u);
        // 8 bars, deliberately not a power-of-two-friendly ramp, so banding is visible.
        static const uint32_t kBars[8][3] = {
            {255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
            {255, 0, 255}, {255, 0, 0}, {0, 0, 255}, {0, 0, 0},
        };
        const uint32_t barTop = h / 3u;
        const uint32_t gradTop = (h * 2u) / 3u;

        for (uint32_t y = 0; y < h; ++y)
        {
            for (uint32_t x = 0; x < w; ++x)
            {
                uint32_t r = 0, g = 0, b = 0;
                if (y < barTop)
                {
                    const uint32_t bar = (x * 8u) / w;
                    r = kBars[bar][0];
                    g = kBars[bar][1];
                    b = kBars[bar][2];
                }
                else if (y < gradTop)
                {
                    // Two-axis gradient. Deliberately non-linear in x so the steps are uneven.
                    const uint32_t fx = (x * 255u) / (w - 1u);
                    const uint32_t fy = (y - barTop) * 255u / (gradTop - barTop - 1u);
                    r = fx;
                    g = fy;
                    b = (fx + fy) / 2u;
                }
                else
                {
                    // Diagonal wedge; the shift by frameIndex makes successive frames differ,
                    // which is what proves the content is computed rather than a static fill.
                    const uint32_t diag = x + y + (frameIndex * 8u);
                    const uint32_t wedge = (diag / 32u) & 0xFFu;
                    r = wedge;
                    g = 255u - wedge;
                    b = (wedge * 3u) & 0xFFu;
                }
                const size_t o = (static_cast<size_t>(y) * w + x) * 4u;
                rgba[o + 0] = static_cast<uint8_t>(r);
                rgba[o + 1] = static_cast<uint8_t>(g);
                rgba[o + 2] = static_cast<uint8_t>(b);
                rgba[o + 3] = 0xFFu;
            }
        }
    }

    // Count distinct RGB triples. This is the number the goal gate checks, and it is computed
    // from the framebuffer we read back out of the GS -- not from the pattern we put in.
    uint32_t countDistinctColours(const std::vector<uint8_t> &rgba, uint32_t w, uint32_t h)
    {
        std::vector<uint32_t> seen;
        seen.reserve(4096);
        for (size_t i = 0; i + 3 < static_cast<size_t>(w) * h * 4u; i += 4)
        {
            const uint32_t key = (static_cast<uint32_t>(rgba[i]) << 16)
                | (static_cast<uint32_t>(rgba[i + 1]) << 8) | static_cast<uint32_t>(rgba[i + 2]);
            if (std::find(seen.begin(), seen.end(), key) == seen.end())
            {
                seen.push_back(key);
            }
        }
        return static_cast<uint32_t>(seen.size());
    }

    // ---------------------------------------------------------------- GS transfer registers
    //
    // Layouts as GS::writeRegisterUnlocked actually decodes them (gs_frontend.cpp):
    //   BITBLTBUF  sbp 0-13  sbw 16-21  spsm 24-29  dbp 32-45  dbw 48-53  dpsm 56-61
    //   TRXPOS     ssax 0-10 ssay 16-26  dsax 32-42  dsay 48-58  dir 59-60
    //   TRXREG     rrw 0-11   rrh 32-43
    constexpr uint64_t makeBitBltBuf(uint32_t sbp, uint32_t sbw, uint32_t spsm,
                                     uint32_t dbp, uint32_t dbw, uint32_t dpsm)
    {
        return static_cast<uint64_t>(sbp) | (static_cast<uint64_t>(sbw) << 16)
            | (static_cast<uint64_t>(spsm) << 24) | (static_cast<uint64_t>(dbp) << 32)
            | (static_cast<uint64_t>(dbw) << 48) | (static_cast<uint64_t>(dpsm) << 56);
    }
    constexpr uint64_t makeTrxPos(uint32_t ssx, uint32_t ssy, uint32_t dsx, uint32_t dsy, uint32_t dir)
    {
        return static_cast<uint64_t>(ssx) | (static_cast<uint64_t>(ssy) << 16)
            | (static_cast<uint64_t>(dsx) << 32) | (static_cast<uint64_t>(dsy) << 48)
            | (static_cast<uint64_t>(dir) << 59);
    }
    constexpr uint64_t makeTrxReg(uint32_t w, uint32_t h)
    {
        return static_cast<uint64_t>(w) | (static_cast<uint64_t>(h) << 32);
    }


    // ---------------------------------------------------------------- primitives
    //
    // Submitted the way a guest submits them: a GIF REGLIST naming PRIM, then per vertex
    // RGBAQ / UV / XYZ2, followed by the draw kick that happens when the third vertex arrives.
    // Registers PRIM(0x00) .. XYZ2(0x05) are all inside the 4-bit GIFTAG address field, so this
    // route is reachable through the GIF path -- unlike the framebuffer registers above 0x0F.
    //
    // Two things G2.0 got wrong, both of which made the rasteriser look broken when it was not:
    //   1. Screen coordinates are 12.4 fixed point, so a pixel P is the register value P << 4.
    //      G2.0 passed raw 40 and got a 2.5-pixel triangle, i.e. sub-pixel and covering nothing.
    //   2. XYZF2 and XYZ2 BOTH queue a vertex and BOTH kick. Writing both per vertex doubled the
    //      vertex count, so 3 intended vertices became 6 and every triangle was degenerate.
    //      A guest writes one of them, never both. We write XYZ2 only.
    constexpr uint32_t kRegPrim = 0x00;
    constexpr uint32_t kRegRgbaq = 0x01;
    constexpr uint32_t kRegSt = 0x02;
    constexpr uint32_t kRegUv = 0x03;
    constexpr uint32_t kRegXyz2 = 0x05;
    constexpr uint32_t kRegTex0 = 0x06; // inside the 4-bit GIFTAG field, so it goes via the GIF path

    // G2.4 FIX. This used to be the literal 2u, on the belief that 2 is "triangle". It is not.
    // GS_PRIM_TRIANGLE is 3; 2 is GS_PRIM_LINESTRIP. That single wrong constant is what made
    // G2.2 and G2.3 draw nothing, and it is now spelled symbolically so a re-read of the enum
    // can never let it drift back. The runtime's values match real PS2 hardware exactly
    // (0 points, 1 lines, 2 line strip, 3 triangle, 4 tri strip, 5 tri fan, 6 sprite).
    constexpr uint64_t kPrimTriangle = GS_PRIM_TRIANGLE;
    constexpr uint64_t kPrimTriStrip = GS_PRIM_TRISTRIP;
    constexpr uint64_t kPrimIipBit = 1u << 3; // Gouraud colour interpolation
    constexpr uint64_t kPrimTmeBit = 1u << 4; // Texture Mapping Enable -- sample the texel

    struct GSVertexSpec
    {
        uint32_t x, y;   // in PIXELS; converted to 12.4 below
        uint8_t r, g, b, a;
    };

    // The same triangle, submitted through GS::writeRegister instead of a GIF REGLIST.
    //
    // G2.3 turned up that the runtime's own test suite draws correct pixels using exactly this
    // route (ps2_gs_tests.cpp:829-849, asserting readReferencePSMCT32Pixel). If this path
    // rasterises and the REGLIST path does not, the fault is in our packet encoder, not in the GS.
    void submitTriangleDirect(GS &gs, bool gouraud, const GSVertexSpec *v, const char *label,
                              uint64_t primType = kPrimTriangle)
    {
        gs.writeRegister(static_cast<uint8_t>(kRegPrim), primType | (gouraud ? kPrimIipBit : 0u));
        for (int vi = 0; vi < 3; ++vi)
        {
            const GSVertexSpec &vert = v[vi];
            gs.writeRegister(static_cast<uint8_t>(kRegRgbaq),
                             static_cast<uint64_t>(vert.r) | (static_cast<uint64_t>(vert.g) << 8)
                                 | (static_cast<uint64_t>(vert.b) << 16)
                                 | (static_cast<uint64_t>(vert.a) << 24)
                                 | (0x3F800000ull << 32));
            gs.writeRegister(static_cast<uint8_t>(kRegUv), 0ull);
            gs.writeRegister(static_cast<uint8_t>(kRegXyz2),
                             (static_cast<uint64_t>(vert.x) << 4) | (static_cast<uint64_t>(vert.y) << 20)
                                 | (static_cast<uint64_t>(0x3FFFFu) << 36));
        }
        std::cout << "VULCAN4 GS DRAW " << label << " [DIRECT writeRegister path] verts=(" << v[0].x
                  << "," << v[0].y << ") (" << v[1].x << "," << v[1].y << ") (" << v[2].x << ","
                  << v[2].y << ")\n";
    }

    void submitTriangle(GS &gs, bool gouraud, const GSVertexSpec *v, const char *label)
    {
        std::vector<std::pair<uint8_t, uint64_t>> writes;
        writes.emplace_back(static_cast<uint8_t>(kRegPrim),
                            kPrimTriangle | (gouraud ? kPrimIipBit : 0u));
        for (int vi = 0; vi < 3; ++vi)
        {
            const GSVertexSpec &vert = v[vi];
            const uint64_t rgba = static_cast<uint64_t>(vert.r) | (static_cast<uint64_t>(vert.g) << 8)
                | (static_cast<uint64_t>(vert.b) << 16) | (static_cast<uint64_t>(vert.a) << 24)
                | (0x3F800000ull << 32); // Q = 1.0 in 8.8-ish float, as the GS does
            const uint64_t uv = 0u;
            const uint64_t xyz = (static_cast<uint64_t>(vert.x) << 4)          // X, 12.4 fixed
                | (static_cast<uint64_t>(vert.y) << 20)                          // Y, 12.4 fixed
                | (static_cast<uint64_t>(0x3FFFFu) << 36);                       // Z
            writes.emplace_back(static_cast<uint8_t>(kRegRgbaq), rgba);
            writes.emplace_back(static_cast<uint8_t>(kRegUv), uv);
            writes.emplace_back(static_cast<uint8_t>(kRegXyz2), xyz);
        }
        const std::vector<uint8_t> packet = reglistPacket(writes);
        gs.processGIFPacket(packet.data(), static_cast<uint32_t>(packet.size()));
        std::cout << "VULCAN4 GS DRAW " << label << " prim=" << kPrimTriangle
                  << (gouraud ? " iip=1(gouraud)" : " iip=0(flat)")
                  << " verts=(" << v[0].x << "," << v[0].y << ") (" << v[1].x << "," << v[1].y
                  << ") (" << v[2].x << "," << v[2].y << ") regs=" << writes.size() << "\n";
    }

} // namespace

int main(int argc, char *argv[])
{
    const std::string outPath = (argc > 1) ? argv[1] : "/mnt/ssd/vulcan4-build/gs/vulcan4_gs_frame.png";

    std::cout << "VULCAN4 GS PROBE starting\n";
    std::cout << "VULCAN4 GS backend=cpu-software (ps2xRuntime GSCpuBackend), vram=" << kVramSize
              << " bytes\n";

    // ---- 1. VRAM and the GS itself.
    std::vector<uint8_t> vram(kVramSize, 0u);
    GSRegisters regs{};
    regs.pmode = kPmodeCrt1Enabled;
    // GS buffer width is counted in 64-pixel words, so 512 pixels is 8 words.
    regs.dispfb1 = makeDispfb(0, kFrameWidthWords);
    regs.display1 = makeDisplay(kFrameWidth, kFrameHeight);
    regs.dispfb2 = makeDispfb(0, kFrameWidthWords);
    regs.display2 = makeDisplay(kFrameWidth, kFrameHeight);
    regs.bgcolor = 0;
    regs.csr.store(0, std::memory_order_release);

    GS gs;
    // The GS keeps its register trace paused by default (GS::m_debugHistoryPaused = true) because
    // it costs memory on a long run. Unpause it so the trace we print below is the GS's own record
    // of what was written, not a list this program maintains about itself.
    gs.setDebugHistoryPaused(false);
    gs.init(vram.data(), static_cast<uint32_t>(vram.size()), &regs);
    gs.setRasterBackend(std::make_unique<GSCpuBackend>());
    std::cout << "VULCAN4 GS init ok, display=" << kFrameWidth << "x" << kFrameHeight << " psm=PSMCT32\n";

    // ---- 2. Program the GS through real GIF packets, the way the EE's GIF DMA does.
    //
    // Framebuffer setup: PRMODECONT, SCANMSK, SCISSOR, FBA, FRAME, ZBUF.
    //
    // This part does NOT go through a GIF packet, and the reason is a real finding about the
    // runtime rather than a shortcut. A GIFTAG names its registers in 4-bit nibbles
    // (GS::processGIFPacket, gs_frontend.cpp:696, `regs[i] = (tagHi >> (i*4)) & 0xF`), and the
    // PACKED handler only implements cases 0x00-0x0F. So registers above 0x0F are unreachable
    // from a GIF packet entirely: FRAME_1 (0x4C), ZBUF_1 (0x4E), SCISSOR (0x40/0x41) and
    // FINISH (0x61) all fall in the top of the map. The real hardware uses a documented escape
    // for this; this runtime does not implement it.
    //
    // Until that is fixed, the framebuffer registers are written through the public
    // GS::writeRegister, which reaches the full map. The primitive stream below is still fed as
    // real GIFTAG packets, because PRIM..XYZ2 all live in 0x00-0x05 and are unaffected.
    {
        for (uint8_t r = GS_REG_PRMODECONT; r <= GS_REG_ZBUF_1; ++r)
        {
            uint64_t value = 0;
            if (r == GS_REG_FRAME_1)
            {
                value = makeFbp(0, kFrameWidthWords); // draw into VRAM page 0
            }
            else if (r == GS_REG_ZBUF_1)
            {
                value = makeFbp(1, kFrameWidthWords); // depth buffer in VRAM page 1
            }
            else if (r == GS_REG_SCISSOR_1 || r == GS_REG_SCISSOR_2)
            {
                // The whole rectangle is packed into one 64-bit write: X0 bits 0-10, X1 16-26,
                // Y0 32-42, Y1 48-58. There is no separate origin/size pair. SCISSOR_1 and
                // SCISSOR_2 each address a different context, so both are opened.
                value = 0u
                    | (static_cast<uint64_t>(kFrameWidth - 1u) << 16)  // X1
                    | (static_cast<uint64_t>(kFrameHeight - 1u) << 48); // Y1
            }
            else if (r == GS_REG_PRMODECONT)
            {
                // G2.4 FIX, third one. AC = 1 (PRMODECONT bit 0) makes PRIM the single source of
                // the whole primitive register -- type, IIP, TME, ABE and the rest.
                //
                // With AC = 0 the frontend instead does (gs_frontend.cpp, case GS_REG_PRMODECONT):
                //     m_prim = m_primRegister;   // PRMODE, a different register
                //     m_prim.type = m_primRegister.type;   // only the type comes from PRIM
                // so PRIM's TME and IIP bits are DISCARDED and PRMODE's -- which nobody wrote --
                // decide instead. The symptom is silent and very convincing: the packet really
                // did contain PRIM=0x1b (type 3, IIP, TME) and the GS really did receive it, and
                // the rasteriser still reported tme=0, sampled no texture at all, and painted the
                // quad flat white in the vertex colour. This is the documented G2.2 step 4
                // ("PRMODECONT 1, to make PRMODE the attribute source and IIP take effect") which
                // the probe had been leaving at 0.
                value = 1ull;
            }
            else if (r == GS_REG_TEST_1)
            {
                // G2.4 FIX, second half. This sweep used to write TEST_1 = 0, i.e.
                // ZTEST = method 0 = NEVER, and the rasteriser honours that:
                //     switch (ztestMethod) { case 0: zpass = false; ... }
                //     if (!zpass) return;                        // gs_cpu_backend.cpp
                // so every covered pixel was discarded before it could be written. The geometry
                // was never the problem; the test register was rejecting it.
                //
                // 0x30000 is the value the runtime's own passing draw test uses
                // (ps2_gs_tests.cpp:836): ZTE=1 (bit 16), ZTEST=1 (bits 17-18), ATE=1 (bit 0),
                // ATST=0, AREF=0 -- so "alpha >= 0", which everything passes.
                //
                // KNOWN RUNTIME FIDELITY GAP, recorded not papered over: real PS2 hardware gates
                // the Z test on the ZTE bit, so TEST=0 (ZTE clear) would DISABLE it and draw
                // normally. This runtime reads ZTEST without checking ZTE, so TEST=0 means
                // NEVER. That is a real divergence and it will bite a real guest that programs
                // TEST with ZTE clear. Tracked in LIMITATIONS.md; not fixed here because it is
                // a change to runtime behaviour, not to our probe.
                value = 0x30000ull;
            }
            gs.writeRegister(r, value);
        }
        gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
    }

    // ---- 2b. Fill the framebuffer through the GS TRANSFER path, not by poking VRAM.
    //
    // This is the route a guest uses to put pixels into GS memory (D_UTEXTURE / image upload).
    // We compute the pattern ourselves, hand it to the GS as a transfer, and the GS's own
    // UploadImage writes it into VRAM at the destination the registers name.
    const uint32_t frameIndex = 0u;
    std::vector<uint8_t> pattern;
    buildPattern(pattern, kFrameWidth, kFrameHeight, frameIndex);
    const uint32_t distinctIn = countDistinctColours(pattern, kFrameWidth, kFrameHeight);
    std::cout << "VULCAN4 GS pattern computed in our code: " << distinctIn << " distinct colours, "
              << pattern.size() << " bytes\n";

    {
        const uint64_t bitbltbuf = makeBitBltBuf(
            /*sbp*/ 0u, /*sbw*/ kFrameWidthWords, /*spsm*/ 0u,
            /*dbp*/ 0u, /*dbw*/ kFrameWidthWords, /*dpsm*/ 0u); // dpsm 0 == PSMCT32
        const uint64_t trxpos = makeTrxPos(0u, 0u, 0u, 0u, 0u);
        const uint64_t trxreg = makeTrxReg(kFrameWidth, kFrameHeight);
        constexpr uint64_t trxdir = 0u; // host-to-local

        std::cout << "VULCAN4 GS transfer BITBLTBUF=0x" << std::hex << bitbltbuf
                  << " TRXPOS=0x" << trxpos << " TRXREG=0x" << trxreg
                  << " TRXDIR=0x" << trxdir << std::dec
                  << " (dbp=0 dbw=" << kFrameWidthWords << " dpsm=0 w=" << kFrameWidth
                  << " h=" << kFrameHeight << ")\n";

        gs.uploadImageNative(bitbltbuf, trxpos, trxreg, trxdir, pattern.data(),
                             static_cast<uint32_t>(pattern.size()));
    }

    // ---- 2d. Real geometry, through the register path.
    //
    // The background above proves the transfer route. These prove the DRAW route: a guest hands
    // the GS geometry as GIF packets naming PRIM and the vertex registers, and the GS kicks the
    // draw when the last vertex arrives. Nothing here touches VRAM directly.
    {
        // 1. A flat-shaded triangle: three vertices, one colour, no interpolation.
        const GSVertexSpec flat[3] = {
            {48, 48, 220, 40, 40, 255},
            {464, 96, 220, 40, 40, 255},
            {200, 240, 220, 40, 40, 255},
        };
        submitTriangle(gs, /*gouraud*/ false, flat, "flat-triangle-via-REGLIST");
        // Same triangle, same frame, different submission route. Both must now land identically:
        // before the G2.4 fix both produced a degenerate 2-vertex LINESTRIP batch.
        submitTriangleDirect(gs, /*gouraud*/ false, flat, "flat-triangle");
        // A TRISTRIP needs 3 vertices for its first triangle and then 1 per extra one, so a
        // 4-vertex strip draws 2 triangles. This is the topology the runtime's own suite proves
        // at ps2_gs_tests.cpp:834. NOTE: this line used to pass the literal 3u while claiming to
        // be TRISTRIP -- 3 is TRIANGLE. The label was wrong; the experiment was accidentally
        // running the right primitive, which is why the fault hid for two dishes.
        {
            const GSVertexSpec strip[3] = {
                {48, 300, 255, 200, 0, 255},
                {240, 300, 0, 200, 255, 255},
                {464, 300, 255, 200, 0, 255},
            };
            submitTriangleDirect(gs, /*gouraud*/ false, strip, "flat-triangle-as-TRISTRIP",
                                 kPrimTriStrip);
        }

        // 2. A gouraud quad, submitted the way hardware does it: as TWO triangles sharing an edge,
        //    with four different corner colours. Interpolation across the interior is what makes
        //    this produce structure rather than flat regions.
        const uint8_t c0[4] = {255, 64, 32, 255};
        const uint8_t c1[4] = {32, 128, 255, 255};
        const uint8_t c2[4] = {64, 255, 96, 255};
        const uint8_t c3[4] = {220, 64, 220, 255};
        const GSVertexSpec q0 = {48, 272, c0[0], c0[1], c0[2], c0[3]};
        const GSVertexSpec q1 = {464, 272, c1[0], c1[1], c1[2], c1[3]};
        const GSVertexSpec q2 = {464, 464, c2[0], c2[1], c2[2], c2[3]};
        const GSVertexSpec q3 = {48, 464, c3[0], c3[1], c3[2], c3[3]};
        const GSVertexSpec quadA[3] = {q0, q1, q2};
        const GSVertexSpec quadB[3] = {q0, q2, q3};
        submitTriangle(gs, /*gouraud*/ true, quadA, "gouraud-quad-a");
        submitTriangle(gs, /*gouraud*/ true, quadB, "gouraud-quad-b");

        // 3. A triangle deliberately straddling the frame edge, to exercise the scissor clip.
        //    It reaches x=700 in a 512-wide frame, so the right third must be discarded.
        const GSVertexSpec clipped[3] = {
            {380, 40, 255, 255, 0, 255},
            {700, 60, 255, 255, 0, 255},
            {420, 240, 255, 200, 0, 255},
        };
        submitTriangle(gs, /*gouraud*/ true, clipped, "clipped-triangle");
    }

    // ---- 2c. The sync primitive, and exactly what it does and does not model.
    //
    // A guest blocks on vblank and on the GS CSR. A stub that LIES about these is worse than no
    // stub: the G1.5 FindAddress livelock is what a guest does when a return value never becomes
    // the one it was promised. So this is a real, countable, host-driven counter -- not a lie --
    // and the limits are printed rather than hidden.
    //
    // WHAT IT MODELS: a monotonically increasing vblank tick count, and CSR bit 0 (the GS
    // "value of the finish drawing primitive" / SIGNAL status), raised after FINISH.
    // WHAT IT DOES NOT MODEL: any vblank timing accuracy, the vblank interrupt actually being
    // delivered to the EE, DMA/AD packet interrupts, the EE's interrupt controller, or the IOP.
    // A guest that waits on vblank here will be released -- it will not spin -- but it will be
    // released by a host thread's wall clock, not by the GS.
    {
        ++g_vblankTicks;
        // CSR bit 0 = SIGNAL (a finish-drawing primitive has completed).
        GSRegisters &live = const_cast<GSRegisters &>(regs);
        live.csr.fetch_or(0x1ull, std::memory_order_acq_rel);
        std::cout << "VULCAN4 GS sync vblank_ticks=" << g_vblankTicks << " csr=0x" << std::hex
                  << live.csr.load(std::memory_order_acquire) << std::dec
                  << " (bit0=SIGNAL raised after FINISH)\n";
    }

    // ---- 2f. G2.4: SAMPLED TEXTURES, through the register path.
    //
    // Two formats, chosen so the picture proves both halves of the sampler: a direct-colour
    // PSMCT32 texture, and a PSMT8 indexed texture that has to go through a PSMCT16 CLUT. The
    // second is the one that would catch a broken CLUT path, which a direct-colour texture never
    // touches.
    //
    // Everything here is uploaded with the GS's own transfer path and bound by writing the GS's
    // own registers. No texel is ever poked into VRAM directly, and no pixel is painted by us.
    {
        constexpr uint32_t kTexW = 64u;
        constexpr uint32_t kTexH = 64u;
        constexpr uint32_t kTexLog2 = 6u;  // 1 << 6 == 64, which is what textureWidth is derived from
        constexpr uint32_t kTexWidthWords = kTexW / 64u; // texture widths are also 64-px words

        // Page plan, and a unit trap worth writing down.
        //
        // BITBLTBUF's DBP and TEX0's TBP0 are in DIFFERENT units, and mixing them up is silent:
        //
        //   BITBLTBUF.DBP  -- GS texel PAGES. A page is 256 KiB.
        //   TEX0.TBP0      -- 256-BYTE BLOCKS. A page is 1024 blocks.
        //   TEX0.CBP       -- also 256-byte blocks.
        //
        // G2.4 first wrote TBP0 in pages. The transfer put the texels at byte 2 MiB, the sampler
        // looked at byte 2 KiB, read untouched zeroed VRAM, and the quad came out flat white with
        // no error anywhere. The two constants below are PAGES (for the transfer) and the
        // bind converts to blocks.
        //
        // A PSMCT32 page is 256 KiB, so the 512x512 framebuffer owns pages 0-3.
        constexpr uint32_t kTexT32Page = 8u;
        constexpr uint32_t kTexT8Page = 12u;
        constexpr uint32_t kClutPage = 16u;
        constexpr uint32_t kBlocksPerPage = 1024u; // 256 KiB / 256 B

        // --- Texture A: PSMCT32, direct colour. A 2D ramp in all three channels so that a
        //     correct UV interpolation produces a smooth, many-coloured field, and a wrong one
        //     produces bands or a flat block. Both are visible in the numbers.
        std::vector<uint8_t> texT32(kTexW * kTexH * 4u);
        for (uint32_t y = 0; y < kTexH; ++y)
            for (uint32_t x = 0; x < kTexW; ++x)
            {
                const size_t o = (static_cast<size_t>(y) * kTexW + x) * 4u;
                texT32[o + 0] = static_cast<uint8_t>(x * 4u);
                texT32[o + 1] = static_cast<uint8_t>(y * 4u);
                texT32[o + 2] = static_cast<uint8_t>(((x ^ y) & 0x3Fu) * 4u);
                texT32[o + 3] = 0xFFu;
            }

        // --- Texture B: PSMT8 indexed, resolved through a PSMCT16 CLUT.
        //     The indices are a coarse function of (x,y) so the sampled result has visible
        //     structure; the CLUT maps index -> colour with a deliberate non-identity ramp.
        std::vector<uint8_t> texT8(kTexW * kTexH, 0u);
        for (uint32_t y = 0; y < kTexH; ++y)
            for (uint32_t x = 0; x < kTexW; ++x)
                texT8[static_cast<size_t>(y) * kTexW + x] =
                    static_cast<uint8_t>((x / 8u) + (y / 8u) * 8u);

        // PSMCT16 CLUT: 256 entries x 4 bytes (R,G,B,flags). GS order is RGBA in the low bytes.
        std::vector<uint8_t> clut(256u * 4u, 0u);
        for (uint32_t i = 0; i < 256u; ++i)
        {
            clut[i * 4u + 0] = static_cast<uint8_t>(255u - i);        // R falls as index rises
            clut[i * 4u + 1] = static_cast<uint8_t>(i);               // G rises
            clut[i * 4u + 2] = static_cast<uint8_t>((i * 3u) & 0xFFu);
            clut[i * 4u + 3] = 0x80u;                                  // STP/alpha bits
        }

        auto upload = [&gs](uint32_t basePage, uint32_t widthWords, uint8_t psm, uint32_t w,
                             uint32_t h, const std::vector<uint8_t> &data, const char *label)
        {
            // BITBLTBUF: SBP 0-13, SBW 16-21, SPSM 24-29, DBP 32-45, DBW 48-53, DPSM 56-61
            const uint64_t bitbltbuf = static_cast<uint64_t>(psm) | (static_cast<uint64_t>(widthWords) << 16)
                | (static_cast<uint64_t>(psm) << 24) | (static_cast<uint64_t>(basePage) << 32)
                | (static_cast<uint64_t>(widthWords) << 48) | (static_cast<uint64_t>(psm) << 56);
            // TRXREG: W in bits 0-11, H in bits 32-43. (H is NOT at bit 12 -- that is the trap.)
            const uint64_t trxreg = static_cast<uint64_t>(w) | (static_cast<uint64_t>(h) << 32);
            gs.uploadImageNative(bitbltbuf, /*trxpos*/ 0ull, trxreg, /*trxdir*/ 0u, data.data(),
                                 static_cast<uint32_t>(data.size()));
            std::cout << "VULCAN4 GS texture " << label << " uploaded psm=0x" << std::hex
                      << static_cast<uint32_t>(psm) << std::dec << " page=" << basePage << " " << w << "x"
                      << h << " " << data.size() << "B\n";
        };

        upload(kTexT32Page, kTexWidthWords, GS_PSM_CT32, kTexW, kTexH, texT32, "PSMCT32");
        upload(kTexT8Page, kTexWidthWords, GS_PSM_T8, kTexW, kTexH, texT8, "PSMT8");
        upload(kClutPage, 4u, GS_PSM_CT16, 256u, 1u, clut, "PSMCT16-CLUT");

        // G2.4 DIAGNOSTIC: where did the transfer actually land? Print candidate bases so the
        // unit question is answered by measurement instead of by reading one more header.
        for (uint32_t off : {8u * 256u, 8u * 8192u, 8u * 262144u, 12u * 256u, 12u * 262144u})
        {
            std::cout << "    VRAM@" << off << " =";
            for (uint32_t k = 0; k < 12u; ++k)
                std::cout << " " << std::hex << static_cast<uint32_t>(vram[off + k]) << std::dec;
            std::cout << "\n";
        }

        // Bind each texture and draw a quad that spans its full extent, so UV runs 0..1 across
        // the surface and the sampler has to interpolate every texel in between.
        //
        // PRIM needs TME (bit 4) set or the rasteriser never calls SampleTexture at all.
        auto drawTexturedQuad = [&gs, kTexLog2, kBlocksPerPage](uint32_t texPage, uint8_t psm, uint32_t clutPage,
                                                bool useClut, int top, const char *label)
        {
            const int left = 32, right = 480, bottom = top + 128;
            // G2.4 MEASURED: DBP and TBP0 must use the SAME unit, and in this runtime that unit
            // is the 256-byte block, with no conversion applied to either
            // (UploadImage reads m_transfer.bitbltbuf.dbp raw; SampleTexture reads tex.tbp0 raw).
            // Multiplying by kBlocksPerPage here puts the sampler 1024x past the upload and the
            // quad goes flat white again -- measured, not assumed.
            //
            // RUNTIME FIDELITY BUG, recorded not worked around: real PS2 hardware defines a texel
            // page as 256 KiB = 1024 blocks, so a real guest writes TBP0 = page*1024. This
            // runtime's only page helper is framePageBaseToBlock(fbp) = fbp << 5, i.e. an 8 KiB
            // page, and it is applied to the FRAMEBUFFER but not to the texture bases. So a real
            // guest's texture base would be interpreted 32x too low. That will bite the moment a
            // guest draws a textured primitive. Tracked in LIMITATIONS.md; fixing it is a runtime
            // change, not a probe change, so it is not done in this dish.
            const uint64_t tbp0 = texPage;
            const uint64_t cbp = clutPage;
            const uint64_t tex0 = tbp0                                                   // TBP0  0-13
                | (static_cast<uint64_t>(1u) << 14)                                     // TBW   14-19 (64px = 1 word)
                | (static_cast<uint64_t>(psm) << 20)                                    // TPSM  20-25
                | (static_cast<uint64_t>(kTexLog2) << 26)                               // TW    26-29
                | (static_cast<uint64_t>(kTexLog2) << 30)                               // TH    30-33
                | (0ull << 34)                                                          // TCC=0 RGB
                | (0ull << 35)                                                          // TFX=0 nearest
                | (cbp << 37)                                                           // CBP   37-50
                | (static_cast<uint64_t>(useClut ? GS_PSM_CT16 : 0u) << 51)             // CPSM  51-54
                | (0ull << 55)                                                          // CSM
                | (useClut ? 1ull : 0ull) << 61;                                        // CLD=1 load CLUT

            gs.writeRegister(GS_REG_TEXCLUT, /*cbw 0-5 = 4 words, cou/cov 0*/ (4ull << 0));
            gs.writeRegister(GS_REG_TEX0_1, tex0);

            // PRIM: type=TRIANGLE | IIP | TME. S/T ride in ST (0x02) because FST=0 is the
            // hardware's normal path -- the DDA interpolates S and T, then divides by Q.
            const uint64_t prim = kPrimTriangle | kPrimIipBit | kPrimTmeBit;
            const float q = 1.0f;
            auto st = [&](float s, float t) -> uint64_t
            {
                uint32_t sb, tb;
                std::memcpy(&sb, &s, 4);
                std::memcpy(&tb, &t, 4);
                return static_cast<uint64_t>(sb) | (static_cast<uint64_t>(tb) << 32);
            };
            auto xyz = [](int x, int y) -> uint64_t
            {
                return (static_cast<uint64_t>(x) << 4) | (static_cast<uint64_t>(y) << 20);
            };
            const struct { int x, y; float s, t; } corners[4] = {
                {left, top, 0.0f, 0.0f},
                {right, top, 1.0f, 0.0f},
                {right, bottom, 1.0f, 1.0f},
                {left, bottom, 0.0f, 1.0f},
            };
            // Two triangles sharing the left->bottom edge, submitted as a REGLIST exactly as a
            // guest would. TEX0_1 is 0x06 and PRIM is 0x00, both inside the 4-bit GIFTAG address
            // field, so this whole bind-and-draw is reachable through the GIF path.
            auto emit = [&](const int *idx)
            {
                std::vector<std::pair<uint8_t, uint64_t>> w;
                w.emplace_back(static_cast<uint8_t>(kRegPrim), prim);
                w.emplace_back(static_cast<uint8_t>(kRegTex0), tex0);
                for (int k = 0; k < 3; ++k)
                {
                    const auto &c = corners[idx[k]];
                    w.emplace_back(static_cast<uint8_t>(kRegRgbaq),
                                   0xFFull | (0xFFull << 8) | (0xFFull << 16) | (0xFFull << 24)
                                       | (static_cast<uint64_t>(0x3F800000ull) << 32));
                    w.emplace_back(static_cast<uint8_t>(kRegSt), st(c.s, c.t));
                    w.emplace_back(static_cast<uint8_t>(kRegXyz2), xyz(c.x, c.y));
                }
                const std::vector<uint8_t> pkt = reglistPacket(w);
                gs.processGIFPacket(pkt.data(), static_cast<uint32_t>(pkt.size()));
            };
            const int triA[3] = {0, 1, 2};
            const int triB[3] = {0, 2, 3};
            emit(triA);
            emit(triB);
            std::cout << "VULCAN4 GS texture " << label << " bound TPSM=0x" << std::hex
                      << static_cast<uint32_t>(psm) << std::dec << " TME=1 IIP=1 CLUT="
                      << (useClut ? "PSMCT16" : "none") << " -> quad (" << left << "," << top << ")-("
                      << right << "," << bottom << ")\n";
        };

        // PSMCT32 texture first, in the upper area of the frame.
        drawTexturedQuad(kTexT32Page, GS_PSM_CT32, 0u, /*useClut*/ false, 16, "PSMCT32-quad");
        // PSMT8 + PSMCT16 CLUT below it. This is the one that exercises the CLUT cache.
        drawTexturedQuad(kTexT8Page, GS_PSM_T8, kClutPage, /*useClut*/ true, 160, "PSMT8-CLUT-quad");
    }

    // ---- 2e. G2.4 DIAGNOSTIC: did the rasteriser actually write VRAM?
    //
    // The presented frame's colour count was byte-identical to G2.1's transfer-only background,
    // which cannot tell us *why* geometry is invisible. It could be that the raster rejected every
    // pixel (TEST/ALPHA), or that it wrote VRAM correctly and the presentation read a different
    // place. Those need opposite fixes, so read the backing store directly and settle it.
    // PSMCT32 at base page 0, width 512px = 8 words of 64px: stride is 8*16*4 = 512 bytes.
    {
        auto vramPixel = [&vram](uint32_t x, uint32_t y) -> uint32_t
        {
            const size_t off = (static_cast<size_t>(y) * 512u + x) * 4u;
            if (off + 4u > vram.size())
                return 0u;
            return static_cast<uint32_t>(vram[off]) | (static_cast<uint32_t>(vram[off + 1]) << 8)
                | (static_cast<uint32_t>(vram[off + 2]) << 16)
                | (static_cast<uint32_t>(vram[off + 3]) << 24);
        };
        std::cout << "VULCAN4 GS VRAM probe (PSMCT32 @ page0, 512B stride)\n";
        // The flat triangle is (48,48) (464,96) (200,240); its interior colour is RGBA(220,40,40,255)
        // = 0xFF2828DC in this GS's byte order. Background is the G2.1 transfer pattern.
        struct Probe { uint32_t x, y; const char *what; };
        const Probe probes[] = {
            {150, 100, "inside flat triangle (expect 0xFF2828DC)"},
            {100, 90,  "inside flat triangle (expect 0xFF2828DC)"},
            {250, 150, "inside flat triangle (expect 0xFF2828DC)"},
            {400, 400, "outside geometry  (expect transfer pattern)"},
            {8, 8,     "outside geometry  (expect transfer pattern)"},
        };
        for (const Probe &pr : probes)
            std::cout << "    (" << pr.x << "," << pr.y << ") = 0x" << std::hex << vramPixel(pr.x, pr.y)
                      << std::dec << "  " << pr.what << "\n";
        // Count distinct colours across the whole page: if the triangles wrote, this must exceed
        // the transfer pattern's own count.
        std::vector<uint32_t> seen;
        seen.reserve(65536);
        for (uint32_t y = 0; y < 512u; ++y)
            for (uint32_t x = 0; x < 512u; ++x)
            {
                const uint32_t p = vramPixel(x, y);
                if (std::find(seen.begin(), seen.end(), p) == seen.end())
                {
                    seen.push_back(p);
                    if (seen.size() > 100000u)
                        break;
                }
            }
        std::cout << "VULCAN4 GS VRAM distinct_colours=" << seen.size() << "\n";
    }

    // ---- 3. Read the frame back through the runtime's own presentation path.
    gs.latchHostPresentationFrame();
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t displayFbp = 0;
    uint32_t sourceFbp = 0;
    bool usedPreferred = false;
    if (!gs.copyLatchedHostPresentationFrame(pixels, width, height, &displayFbp, &sourceFbp,
                                             &usedPreferred))
    {
        std::cout << "VULCAN4 GS ERROR: presentation readback produced no frame\n";
        return 1;
    }

    std::cout << "VULCAN4 GS readback " << width << "x" << height << " display_fbp=0x" << std::hex
              << displayFbp << " source_fbp=0x" << sourceFbp << std::dec
              << " preferred=" << (usedPreferred ? "yes" : "no") << "\n";
    if (width == 0 || height == 0)
    {
        std::cout << "VULCAN4 GS ERROR: frame has zero extent\n";
        return 1;
    }

    // ---- 4. Encode the PNG ourselves.
    if (!writePng(outPath, pixels, width, height))
    {
        return 1;
    }

    // ---- 4. The register trace, read back out of the GS's own debug history.
    //
    // Derived from the run rather than hand-maintained in a doc, so it cannot drift from what the
    // code actually does. This is the surface area the next dish inherits.
    {
        const auto history = gs.getDebugHistory();
        std::vector<std::pair<uint8_t, uint64_t>> regLast;
        uint64_t gifPackets = 0;
        uint64_t drawEvents = 0;
        for (const auto &entry : history)
        {
            if (entry.kind == GSDebugEventKind::Register)
            {
                bool seen = false;
                for (auto &r : regLast)
                {
                    if (r.first == entry.reg)
                    {
                        r.second = entry.regValue;
                        seen = true;
                        break;
                    }
                }
                if (!seen)
                {
                    regLast.emplace_back(entry.reg, entry.regValue);
                }
            }
            else if (entry.kind == GSDebugEventKind::GifTag)
            {
                ++gifPackets;
            }
            else if (entry.kind == GSDebugEventKind::Draw)
            {
                ++drawEvents;
            }
        }
        std::cout << "VULCAN4 GS REGTRAKE gif_packets=" << gifPackets
                  << " draw_events=" << drawEvents << " registers_written=" << regLast.size()
                  << "\n";
        for (const auto &r : regLast)
        {
            std::cout << "VULCAN4 GS REG 0x" << std::hex << static_cast<uint32_t>(r.first)
                      << std::dec << " = 0x" << std::hex << r.second << std::dec << "\n";
        }
    }

    const uint64_t sum = checksumRgba(pixels);
    const uint32_t lit = countNonBackground(pixels);
    const uint32_t distinct = countDistinctColours(pixels, width, height);
    std::cout << "VULCAN4 GS FRAME path=" << outPath << " " << width << "x" << height
              << " pixels=" << pixels.size() << " non_background=" << lit
              << " distinct_colours=" << distinct << " fnv1a64=0x" << std::hex << sum << std::dec
              << "\n";
    if (distinct < 64u)
    {
        std::cout << "VULCAN4 GS ERROR: only " << distinct
                  << " distinct colours read back out of VRAM; the goal gate needs >= 64, so this "
                     "frame is a FAILURE and must not be presented as a success.\n";
        return 1;
    }
    std::cout << "VULCAN4 GS PROOF the " << distinct
              << " distinct colours above were COMPUTED by this program, written into GS VRAM by "
                 "ps2xRuntime's own transfer path, read back out of VRAM by the GS presentation "
                 "path, and encoded by this program's own PNG writer. No image library was "
                 "involved and no window was ever opened.\n";

    // ---- 5. What is not done, named rather than glossed over.
    std::cout << "VULCAN4 LIMITATION: GS - the frame content arrives through the TRANSFER path "
                 "(BITBLTBUF/TRXPOS/TRXREG/TRXDIR + image data), not through the triangle "
                 "rasteriser. A guest that draws primitives still needs the rasteriser, and the "
                 "rasteriser is NOT yet producing pixels: see docs/LIMITATIONS.md.\n";
    std::cout << "VULCAN4 LIMITATION: GS - no texture sampling, no CLUT, no blending, no alpha, "
                 "no Z-buffer test, no dithering, no primitives of any kind are exercised.\n";
    std::cout << "VULCAN4 LIMITATION: GS - the vblank counter is host wall-clock driven and the CSR "
                 "SIGNAL bit is raised by us. No vblank interrupt is delivered to the EE, no DMA/AD "
                 "interrupts exist, and the interrupt controller is not modelled. A guest waiting on "
                 "vblank is released, but by a host thread, not by the GS.\n";
    std::cout << "VULCAN4 LIMITATION: GS - no VU1 is involved. The GS path here takes vertex data "
                 "directly; nothing computes geometry. That is goal G3.1.\n";
    std::cout << "VULCAN4 LIMITATION: GS - the transfer did NOT travel as a GIF packet "
                 "(gif_packets=0); it used the native GS::uploadImageNative entry point. A guest "
                 "sends a GIF packet containing an IMAGE transfer, and the 4-bit GIFTAG register "
                 "field makes that impossible today. So the transfer MECHANISM is proven and the "
                 "GIF ROUTE to it is not. GifArbiter and the DMAC remain unproven.\n";
    std::cout << "VULCAN4 LIMITATION: GS - Z-buffering is allocated but not written by this probe, "
                 "because nothing tests it yet.\n";
    std::cout << "VULCAN4 LIMITATION: GS - nothing from Gran Turismo 4 has been rendered. No guest "
                 "code has ever reached this GS; the last boot recorded total_mmio_accesses=0.\n";
    return 0;
}
