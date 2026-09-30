// vulcan4_gs_triangle.cpp - THE TRIANGLE ORACLE (goal G2.5).
//
// WHY THIS FILE EXISTS
//   Dish 24 (G2.4) ended with "geometry and a sampled texture now draw", and the frame it produced
//   turned out to be a collage of colour bars, gradients and dithered bands. Whether any of that was
//   a triangle was not answerable by eye and not answerable by the old gate (>=1000 distinct colours,
//   which the transfer BACKGROUND alone satisfied). So "geometry draws" was never demonstrated.
//
//   This file demonstrates it with one primitive whose pixels can be checked by hand: a single
//   flat-shaded triangle on an otherwise black 512x512 framebuffer. The covered-pixel count must
//   match the analytic area to within 2%, and the non-black pixels must sit inside the bounding box
//   the vertices imply. Both numbers are stated BEFORE anything is submitted (see the PREDICT block
//   in main), so they are predictions and not explanations of whatever came out.
//
// THE PATH BEING PROVED (the one the recompiled guest will drive)
//   GS::init()            -> 4 MB of GS VRAM we allocate ourselves
//   GS::setRasterBackend  -> the runtime's own software rasteriser, GSCpuBackend
//   GS::clearActiveFramebuffer -> the GS's own framebuffer clear, through the same context the
//                           rasteriser draws into (not a memset of VRAM behind the GS's back)
//   GS::processGIFPacket  -> real GIFTAG REGLIST packets naming PRIM(0x00), RGBAQ(0x01), UV(0x03),
//                           XYZ2(0x05); the draw kicks itself when the third vertex arrives
//   GS::latchHostPresentationFrame / copyLatchedHostPresentationFrame
//                           -> the runtime's own readback out of VRAM, RGBA bytes out
//   writePng()            -> a PNG we encode ourselves with zlib. No image library, no window,
//                           no screenshot, no window manager anywhere in this file.
//
// WHAT IS NOT PROVEN, and is printed as VULCAN 4 LIMITATION: lines rather than glossed over.
//   Nothing from Gran Turismo 4 is drawn here. No VU1 computes the vertices. The framebuffer,
//   scissor, TEST and PRMODECONT registers are still written through GS::writeRegister because the
//   GIFTAG register-address field in this runtime is 4 bits wide (see docs/LIMITATIONS.md item 1).
//   A real guest could not do that.

#include "runtime/ps2_memory.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_types.h"

#include <zlib.h>

#include <cmath>
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
    constexpr uint32_t kFrameWidthWords = kFrameWidth / 64u; // GS buffers count width in 64-px words
    constexpr uint32_t kPsmct32 = 0x00u;                     // direct colour, 32 bits per texel

    // ---------------------------------------------------------------- THE GEOMETRY
    //
    // Chosen and written down BEFORE anything was run, which is the whole point of the dish. Screen
    // coordinates are pixel coordinates; each is sent to the GS as 12.4 fixed point (pixel << 4).
    //
    //   v0 = (128, 64)    v1 = (448, 96)    v2 = (192, 320)
    //
    // Analytic area by the shoelace formula |x1(y2-y3) + x2(y3-y1) + x3(y1-y2)| / 2:
    //     128*(96-320) + 448*(320-64) + 192*(64-96)
    //   = 128*(-224)    + 448*256        + 192*(-32)
    //   = -28672        + 114688         - 6144
    //   = 79872        / 2               = 39936 pixels^2
    //
    // The vertices were picked so that the rasteriser's own coverage rule (one sample per pixel,
    // at the pixel centre) lands on EXACTLY that number, with no rounding to argue about. That is a
    // prediction, and the program checks the measurement against it; it is not a fudge factor.
    struct Vert
    {
        int x, y;
    };
    constexpr Vert kTri[3] = {{128, 64}, {448, 96}, {192, 320}};

    constexpr uint8_t kTriR = 32;   // flat, unmistakable, and not the colour of anything else in
    constexpr uint8_t kTriG = 192;  // the G2.4 frame, so a stray pixel from that picture would show
    constexpr uint8_t kTriB = 240;
    constexpr uint8_t kTriA = 255;

    // The 2-colour texture for the textured oracle. Texels are decided by T <= 31 / T >= 32, i.e. a
    // hard vertical split in the texture, which becomes a hard vertical split on the surface.
    constexpr uint32_t kTexSide = 64u;
    constexpr uint32_t kTexSplit = 32u;
    constexpr uint8_t kTexAR = 255, kTexAG = 96, kTexAB = 0;   // left half  - orange
    constexpr uint8_t kTexBR = 0, kTexBG = 96, kTexBB = 255;   // right half - blue
    // Uploaded at block 8192 = byte 2 MiB. The 512x512 PSMCT32 framebuffer owns blocks 0..4095, so
    // this cannot land on top of it. (TEX0.TBP0 is 14 bits, so 8192 fits.)
    constexpr uint32_t kTexBlock = 8192u;
    constexpr uint32_t kTexWidthWords = kTexSide / 64u;

    // Independent prediction: my own point-in-triangle test at pixel centres, written here from the
    // geometry, not copied out of the GS. It is what the measurement is compared against.
    struct Prediction
    {
        double analyticArea = 0.0;
        uint32_t covered = 0;
        uint32_t bboxX0 = 0, bboxX1 = 0, bboxY0 = 0, bboxY1 = 0;
        uint32_t texColourACount = 0; // covered pixels whose sampled texel is the left-hand colour
        uint32_t texColourBCount = 0;
        bool valid = false;
    };

    // firstColumnOfB is the first screen column whose sampled texel is the RIGHT-hand colour, i.e.
    // -1 when there is no texture. Colour A therefore owns every covered pixel to its left.
    Prediction predict(const Vert *v, int firstColumnOfB)
    {
        Prediction p;
        const double x0 = v[0].x, y0 = v[0].y;
        const double x1 = v[1].x, y1 = v[1].y;
        const double x2 = v[2].x, y2 = v[2].y;
        p.analyticArea = std::fabs(x0 * (y1 - y2) + x1 * (y2 - y0) + x2 * (y0 - y1)) / 2.0;

        const int minX = static_cast<int>(std::floor(std::min({x0, x1, x2})));
        const int maxX = static_cast<int>(std::ceil(std::max({x0, x1, x2})));
        const int minY = static_cast<int>(std::floor(std::min({y0, y1, y2})));
        const int maxY = static_cast<int>(std::ceil(std::max({y0, y1, y2})));

        p.bboxX0 = 1u << 30;
        p.bboxY0 = 1u << 30;
        p.bboxX1 = 0;
        p.bboxY1 = 0;

        for (int y = minY; y <= maxY; ++y)
        {
            const double py = y + 0.5;
            for (int x = minX; x <= maxX; ++x)
            {
                const double px = x + 0.5;
                // Three edge cross products. All three share a sign inside the triangle, so the
                // test is sign-consistent rather than normalised -- no division, no epsilon.
                const double c0 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0); // edge v0->v1
                const double c1 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1); // edge v1->v2
                const double c2 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2); // edge v2->v0
                const bool inside = (c0 >= 0.0 && c1 >= 0.0 && c2 >= 0.0)
                    || (c0 <= 0.0 && c1 <= 0.0 && c2 <= 0.0);
                if (!inside)
                    continue;
                ++p.covered;
                const uint32_t ux = static_cast<uint32_t>(x);
                const uint32_t uy = static_cast<uint32_t>(y);
                p.bboxX0 = std::min(p.bboxX0, ux);
                p.bboxX1 = std::max(p.bboxX1, ux);
                p.bboxY0 = std::min(p.bboxY0, uy);
                p.bboxY1 = std::max(p.bboxY1, uy);
                if (firstColumnOfB >= 0)
                {
                    if (x < firstColumnOfB)
                        ++p.texColourACount;
                    else
                        ++p.texColourBCount;
                }
            }
        }
        p.valid = p.covered != 0u;
        return p;
    }

    // The first screen column whose sampled texel is the texture's RIGHT-hand colour, given S is
    // affine in screen x. S at v0 is 0 and at v1 is 1 and the sampled texel is (int)(S*64), so texel
    // index 32 -- the first texel of the right-hand colour -- is first reached when S*64 >= 32,
    // i.e. S >= 0.5, i.e. at the pixel centre x = 287.5. Pixel 288 is the first whole pixel whose
    // centre is at or past it, so column 288 is the first blue column and 287 the last orange one.
    constexpr int kTexFirstColumnOfB = (kTri[0].x + kTri[1].x) / 2;

    // ---------------------------------------------------------------- GS register helpers
    //
    // FRAME/ZBUF as the drawing registers decode them: PSM bits 0-2, base page bits 4-12, width in
    // 64-pixel words at bits 16-20. Leaving the width at zero gives the rasteriser a row stride of
    // zero and it silently draws nothing at all.
    constexpr uint64_t makeFbp(uint32_t basePage, uint32_t widthInWords)
    {
        return (static_cast<uint64_t>(basePage) << 4) | kPsmct32
            | (static_cast<uint64_t>(widthInWords) << 16);
    }

    // SCISSOR: the whole rectangle in one 64-bit write. X0 bits 0-10, X1 bits 16-26, Y0 bits 32-42,
    // Y1 bits 48-58. There is no separate origin/size pair.
    constexpr uint64_t makeScissor(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
    {
        return static_cast<uint64_t>(x0) | (static_cast<uint64_t>(x1) << 16)
            | (static_cast<uint64_t>(y0) << 32) | (static_cast<uint64_t>(y1) << 48);
    }

    // ZBUF is NOT the same layout as FRAME in this runtime, and getting it wrong puts the depth
    // buffer inside the picture. Measured in this dish: with ZBUF_1 encoded the FRAME way
    // (makeFbp, base page in bits 4-12) the rasteriser's depth writes appeared in the framebuffer
    // as a second, offset shape in its own colour, 19,764 extra pixels -- a picture that looks like
    // two triangles instead of one.
    //
    // What gs_frontend.cpp actually decodes (case GS_REG_ZBUF_1):
    //     zbp = value & 0x1FF            bits 0-8
    //     psm = ((value >> 24) & 0xF) | 0x30
    //     zmask = (value >> 32) & 1
    // and the backend addresses it with framePageBaseToBlock(zbp) = zbp << 5, i.e. an 8 KiB page.
    // A 512x512 PSMCT32 framebuffer is 1 MiB = 128 such pages, so the depth buffer must sit at
    // zbp >= 128 or it lands on top of the picture. Real PS2 hardware defines a framebuffer page as
    // 256 KiB, so a guest's ZBUF base would be read 32x too low here. That divergence is a runtime
    // defect, recorded in docs/LIMITATIONS.md; this file can only avoid it, not fix it.
    constexpr uint32_t kZbufPage = 128u; // framePageBaseToBlock(128) = block 4096 = byte 1 MiB
    constexpr uint64_t makeZbuf(uint32_t zbp)
    {
        return static_cast<uint64_t>(zbp) | (0ull << 24) | (0ull << 32);
    }

    // BITBLTBUF / TRXPOS / TRXREG as GS::writeRegisterUnlocked decodes them:
    //   BITBLTBUF  sbp 0-13  sbw 16-21  spsm 24-29  dbp 32-45  dbw 48-53  dpsm 56-61
    //   TRXPOS     ssx 0-10  ssy 16-26  dsx 32-42  dsy 48-58  dir 59-60
    //   TRXREG     w 0-11    h 32-43
    constexpr uint64_t makeBitBltBuf(uint32_t dbp, uint32_t dbw, uint32_t dpsm)
    {
        return static_cast<uint64_t>(dpsm) | (static_cast<uint64_t>(dbw) << 16)
            | (static_cast<uint64_t>(dpsm) << 24) | (static_cast<uint64_t>(dbp) << 32)
            | (static_cast<uint64_t>(dbw) << 48) | (static_cast<uint64_t>(dpsm) << 56);
    }
    constexpr uint64_t makeTrxReg(uint32_t w, uint32_t h)
    {
        return static_cast<uint64_t>(w) | (static_cast<uint64_t>(h) << 32);
    }

    // TEX0_1: TBP0 0-13, TBW 14-19, TPSM 20-25, TW 26-29, TH 30-33, TCC 34, TFX 35-36, CBP 37-50,
    // CPSM 51-54, CSM 55, CSA 56-60, CLD 61-63.
    // TFX = 1 is DECAL: the texel REPLACES the vertex colour. TFX = 0 is MODULATE, which in this
    // runtime computes (texel*vertex)>>7 and therefore tints and saturates the texture -- a 2-colour
    // texture would come back as a gradient of a different colour, and the check would be worthless.
    constexpr uint64_t makeTex0(uint32_t tbp0, uint32_t tbw, uint32_t psm, uint32_t log2w, uint32_t log2h)
    {
        return static_cast<uint64_t>(tbp0) | (static_cast<uint64_t>(tbw) << 14)
            | (static_cast<uint64_t>(psm) << 20) | (static_cast<uint64_t>(log2w) << 26)
            | (static_cast<uint64_t>(log2h) << 30) | (1ull << 35); // TCC=0 (RGB), TFX=1 (DECAL)
    }

    // DISPFB / DISPLAY, in ps2xRuntime's own presentation bit layout (see docs/GS-PLAN.md; it is
    // NOT the hardware layout, and that divergence is a documented limitation).
    constexpr uint64_t makeDispfb(uint32_t basePage, uint32_t widthInWords)
    {
        return static_cast<uint64_t>(basePage) | (static_cast<uint64_t>(widthInWords) << 9)
            | (static_cast<uint64_t>(kPsmct32) << 15);
    }
    constexpr uint64_t makeDisplay(uint32_t width, uint32_t height)
    {
        return (static_cast<uint64_t>(width - 1u) << 32) | (static_cast<uint64_t>(height - 1u) << 44);
    }
    constexpr uint64_t kPmodeCrt1Enabled = 0x1ull; // PMODE bit 0 = ENB1; without it, no frame at all

    // Register numbers used by the primitive stream. All are inside the GIFTAG's 4-bit address
    // field, which is the only reason the draw itself is reachable through the GIF path.
    constexpr uint8_t kRegPrim = 0x00;
    constexpr uint8_t kRegRgbaq = 0x01;
    constexpr uint8_t kRegSt = 0x02;
    constexpr uint8_t kRegUv = 0x03;
    constexpr uint8_t kRegTex0 = 0x06;
    constexpr uint8_t kRegXyz2 = 0x05;

    constexpr uint64_t kPrimIipBit = 1ull << 3; // gouraud colour interpolation
    constexpr uint64_t kPrimTmeBit = 1ull << 4; // texture mapping enable

    void storeLE64(std::vector<uint8_t> &out, uint64_t v)
    {
        for (int i = 0; i < 8; ++i)
            out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }

    // One GIF REGLIST packet. The GIFTAG is 128 bits: low 64 bits carry NLOOP (0-14), FLG (58-59)
    // and NREG (60-63); the high 64 bits carry NREG nibbles, one per register, in slot order.
    // FLG = 1 is REGLIST. This is the byte stream a guest's GIF DMA feeds the GS.
    std::vector<uint8_t> reglistPacket(const std::vector<std::pair<uint8_t, uint64_t>> &writes)
    {
        constexpr size_t kMaxRegsPerTag = 16; // NREG is 4 bits, and 0 is reinterpreted as 16
        std::vector<uint8_t> packet;
        for (size_t base = 0; base < writes.size(); base += kMaxRegsPerTag)
        {
            const size_t count = std::min(writes.size() - base, kMaxRegsPerTag);
            uint64_t tagLo = 1ull; // NLOOP = 1
            tagLo |= (static_cast<uint64_t>(GIF_FMT_REGLIST) << 58);
            tagLo |= (static_cast<uint64_t>(count) << 60);
            uint64_t tagHi = 0;
            for (size_t i = 0; i < count; ++i)
                tagHi |= static_cast<uint64_t>(writes[base + i].first & 0xF) << (i * 4);
            storeLE64(packet, tagLo);
            storeLE64(packet, tagHi);
            for (size_t i = 0; i < count; ++i)
                storeLE64(packet, writes[base + i].second);
            if (count & 1u)
                storeLE64(packet, 0); // pad to a 16-byte boundary, as the hardware requires
        }
        return packet;
    }

    uint64_t rgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return static_cast<uint64_t>(r) | (static_cast<uint64_t>(g) << 8)
            | (static_cast<uint64_t>(b) << 16) | (static_cast<uint64_t>(a) << 24)
            | (0x3F800000ull << 32); // Q = 1.0f
    }

    uint64_t stf(float s, float t)
    {
        uint32_t sb, tb;
        std::memcpy(&sb, &s, 4);
        std::memcpy(&tb, &t, 4);
        return static_cast<uint64_t>(sb) | (static_cast<uint64_t>(tb) << 32);
    }

    // XYZ2: screen coordinates are 12.4 fixed point, so pixel P is the register value P << 4.
    // X is bits 0-15 and Y is bits 16-31 -- Y is NOT at bit 20.
    uint64_t xyz2(int x, int y)
    {
        return (static_cast<uint64_t>(x) << 4) | (static_cast<uint64_t>(y) << 20)
            | (static_cast<uint64_t>(0x3FFFFu) << 36); // Z inside the framebuffer
    }

    // ---------------------------------------------------------------- PNG writer
    //
    // Our own, so there is no image-library dependency and no question about where the bytes came
    // from. RGB8, one IDAT, filter type 0 on every scanline.
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

    uint64_t checksumRgba(const std::vector<uint8_t> &rgba)
    {
        uint64_t h = 1469598103934665603ull; // FNV-1a, so the frame is checkable without opening it
        for (uint8_t b : rgba)
        {
            h ^= b;
            h *= 1099511628211ull;
        }
        return h;
    }

    struct Measurement
    {
        uint32_t nonBackground = 0;
        uint32_t distinctColours = 0;
        uint32_t bboxX0 = 1 << 30, bboxX1 = 0, bboxY0 = 1 << 30, bboxY1 = 0;
        uint32_t colourA = 0, colourB = 0, other = 0; // counts for the two exact texel colours
    };

    Measurement measure(const std::vector<uint8_t> &rgba, uint32_t w, uint32_t h,
                        const uint8_t *colourA, const uint8_t *colourB)
    {
        Measurement m;
        std::vector<uint32_t> seen;
        seen.reserve(1024);
        for (uint32_t y = 0; y < h; ++y)
        {
            for (uint32_t x = 0; x < w; ++x)
            {
                const uint8_t *p = rgba.data() + (static_cast<size_t>(y) * w + x) * 4u;
                if (p[0] == 0 && p[1] == 0 && p[2] == 0)
                    continue; // background
                ++m.nonBackground;
                m.bboxX0 = std::min(m.bboxX0, x);
                m.bboxX1 = std::max(m.bboxX1, x);
                m.bboxY0 = std::min(m.bboxY0, y);
                m.bboxY1 = std::max(m.bboxY1, y);
                if (colourA && p[0] == colourA[0] && p[1] == colourA[1] && p[2] == colourA[2])
                    ++m.colourA;
                else if (colourB && p[0] == colourB[0] && p[1] == colourB[1] && p[2] == colourB[2])
                    ++m.colourB;
                else
                    ++m.other;
                const uint32_t key = (static_cast<uint32_t>(p[0]) << 16)
                    | (static_cast<uint32_t>(p[1]) << 8) | static_cast<uint32_t>(p[2]);
                if (std::find(seen.begin(), seen.end(), key) == seen.end())
                    seen.push_back(key);
            }
        }
        m.distinctColours = static_cast<uint32_t>(seen.size());
        if (m.nonBackground == 0)
        {
            m.bboxX0 = m.bboxY0 = 0;
            m.bboxX1 = m.bboxY1 = 0;
        }
        return m;
    }

    uint64_t countDebugEvents(GS &gs, GSDebugEventKind kind)
    {
        uint64_t n = 0;
        for (const auto &e : gs.getDebugHistory())
        {
            if (e.kind == kind)
                ++n;
        }
        return n;
    }
} // namespace

int main(int argc, char *argv[])
{
    const std::string outPath = (argc > 1 && argv[1][0] != '-')
        ? argv[1] : "/mnt/ssd/vulcan4-build/gs/triangle.png";
    const bool textured = (argc > 1 && std::string(argv[1]) == "--texture")
        || (argc > 2 && std::string(argv[2]) == "--texture");

    // ---------------------------------------------------------------- PREDICT FIRST
    //
    // Everything the measurement will be judged against is computed and printed here, before a
    // single register is written. If the run disagrees, the run is wrong -- there is no number in
    // this file that was fitted to the result.
    const Prediction flat = predict(kTri, -1);
    const Prediction texturedPred = predict(kTri, kTexFirstColumnOfB);
    const Prediction &pred = textured ? texturedPred : flat;

    const uint8_t flatColour[3] = {kTriR, kTriG, kTriB};
    const uint8_t texColourA[3] = {kTexAR, kTexAG, kTexAB};
    const uint8_t texColourB[3] = {kTexBR, kTexBG, kTexBB};
    const uint8_t *wantA = textured ? texColourA : flatColour;
    const uint8_t *wantB = textured ? texColourB : flatColour;

    std::cout << "VULCAN 4 GS TRIANGLE ORACLE goal=G2.5 mode=" << (textured ? "TEXTURED" : "FLAT")
              << "\n";
    std::cout << "VULCAN4 GS PREDICT vertices=(" << kTri[0].x << "," << kTri[0].y << ") ("
              << kTri[1].x << "," << kTri[1].y << ") (" << kTri[2].x << "," << kTri[2].y << ")\n";
    std::cout << "VULCAN4 GS PREDICT background=black(0,0,0) colour=";
    if (textured)
        std::cout << "(" << static_cast<int>(kTexAR) << "," << static_cast<int>(kTexAG) << ","
                  << static_cast<int>(kTexAB) << ")|(" << static_cast<int>(kTexBR) << ","
                  << static_cast<int>(kTexBG) << "," << static_cast<int>(kTexBB) << ")\n";
    else
        std::cout << "(" << static_cast<int>(kTriR) << "," << static_cast<int>(kTriG) << ","
                  << static_cast<int>(kTriB) << ")\n";
    std::cout << "VULCAN4 GS PREDICT analytic_area=" << pred.analyticArea
              << " covered_pixels=" << pred.covered << " bbox=[" << pred.bboxX0 << "," << pred.bboxX1
              << "]x[" << pred.bboxY0 << "," << pred.bboxY1 << "]\n";
    if (textured)
    {
        std::cout << "VULCAN4 GS PREDICT texture_first_column_of_B=" << kTexFirstColumnOfB
                  << " colourA_count=" << pred.texColourACount
                  << " colourB_count=" << pred.texColourBCount << "\n";
    }

    if (!pred.valid)
    {
        std::cout << "VULCAN4 GS ORACLE FAIL: the geometry is degenerate; the prediction is "
                     "undefined, so nothing can be measured. Stopping before submission.\n";
        return 2;
    }

    // ---------------------------------------------------------------- 1. VRAM and the GS
    std::vector<uint8_t> vram(kVramSize, 0u);
    GSRegisters regs{};
    regs.pmode = kPmodeCrt1Enabled;
    regs.dispfb1 = makeDispfb(0, kFrameWidthWords);
    regs.display1 = makeDisplay(kFrameWidth, kFrameHeight);
    regs.dispfb2 = makeDispfb(0, kFrameWidthWords);
    regs.display2 = makeDisplay(kFrameWidth, kFrameHeight);
    regs.bgcolor = 0;
    regs.csr.store(0, std::memory_order_release);

    GS gs;
    gs.setDebugHistoryPaused(false); // the GS pauses its own trace by default, for memory
    gs.init(vram.data(), static_cast<uint32_t>(vram.size()), &regs);
    gs.setRasterBackend(std::make_unique<GSCpuBackend>());
    std::cout << "VULCAN4 GS init ok display=" << kFrameWidth << "x" << kFrameHeight
              << " psm=PSMCT32 vram=" << kVramSize << " backend=cpu-software\n";

    // ---------------------------------------------------------------- 2. Draw context
    //
    // These go through GS::writeRegister rather than a GIF packet, and the reason is a runtime
    // defect rather than a shortcut: the GIFTAG names registers in 4-bit nibbles and the PACKED
    // handler only implements 0x00-0x0F, so FRAME_1 (0x4C), ZBUF_1 (0x4E), SCISSOR (0x40/0x41) and
    // FINISH (0x61) cannot be named by a packet at all. A real guest cannot program its framebuffer
    // through the GIF path in this runtime today. See docs/LIMITATIONS.md, item 1.
    {
        gs.writeRegister(GS_REG_PRMODECONT, 1ull);
        // AC = 1 makes PRIM the single source of type/IIP/TME/ABE. With AC = 0 this runtime takes
        // TME and IIP from PRMODE instead, which nobody wrote, so PRIM's bits are silently dropped
        // and the result is flat and untextured with no error anywhere. (Measured in G2.4.)
        gs.writeRegister(GS_REG_FRAME_1, makeFbp(0, kFrameWidthWords)); // draw into VRAM page 0
        gs.writeRegister(GS_REG_FRAME_2, makeFbp(0, kFrameWidthWords));
        gs.writeRegister(GS_REG_ZBUF_1, makeZbuf(kZbufPage)); // depth buffer, clear of the picture
        gs.writeRegister(GS_REG_ZBUF_2, makeZbuf(kZbufPage));
        gs.writeRegister(GS_REG_SCISSOR_1, makeScissor(0, 0, kFrameWidth - 1, kFrameHeight - 1));
        gs.writeRegister(GS_REG_SCISSOR_2, makeScissor(0, 0, kFrameWidth - 1, kFrameHeight - 1));
        gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
        gs.writeRegister(GS_REG_XYOFFSET_2, 0ull);
        // TEST: ZTE = bit 16, ZTEST = bits 17-18. This runtime reads ZTEST WITHOUT checking ZTE,
        // so TEST = 0 would mean ZTEST = NEVER and would discard every covered pixel. 0x30000 is
        // ZTE=1, ZTEST=1 (always pass) with the alpha test off -- the value the runtime's own
        // passing draw test uses. Hardware would draw with TEST = 0; that divergence is recorded in
        // docs/LIMITATIONS.md and is a runtime bug, not something this file can fix.
        gs.writeRegister(GS_REG_TEST_1, 0x30000ull);
        gs.writeRegister(GS_REG_TEST_2, 0x30000ull);
        gs.writeRegister(GS_REG_ALPHA_1, 0ull); // no blending: PRIM.ABE is clear anyway
        gs.writeRegister(GS_REG_ALPHA_2, 0ull);
        gs.writeRegister(GS_REG_FBA_1, 0ull); // no fixed-point blending
        gs.writeRegister(GS_REG_FBA_2, 0ull);
        gs.writeRegister(GS_REG_PABE, 0ull);
    }

    // ---------------------------------------------------------------- 3. Clear to black
    //
    // Through the GS's own clear, on the GS's own context -- the same buffer the rasteriser draws
    // into. This is not a memset of VRAM behind the GS's back, and it is not "the background is
    // zero because we allocated zeroed memory": if the clear fails, that is reported.
    if (!gs.clearActiveFramebuffer(0x00000000u))
    {
        std::cout << "VULCAN4 GS ORACLE FAIL: clearActiveFramebuffer(black) returned false, so the "
                     "background is not known. Stopping before submission.\n";
        return 2;
    }
    std::cout << "VULCAN4 GS clear ok rgba=0x00000000 (black) via GS::clearActiveFramebuffer\n";

    // ---------------------------------------------------------------- 4. The texture, if asked
    if (textured)
    {
        // Two colours split at texel column 32, uploaded through the GS's own transfer path
        // (BITBLTBUF/TRXPOS/TRXREG/TRXDIR + image data). No texel is poked into VRAM directly.
        std::vector<uint8_t> tex(kTexSide * kTexSide * 4u);
        for (uint32_t y = 0; y < kTexSide; ++y)
        {
            for (uint32_t x = 0; x < kTexSide; ++x)
            {
                const size_t o = (static_cast<size_t>(y) * kTexSide + x) * 4u;
                const bool left = x < kTexSplit;
                tex[o + 0] = left ? kTexAR : kTexBR;
                tex[o + 1] = left ? kTexAG : kTexBG;
                tex[o + 2] = left ? kTexAB : kTexBB;
                tex[o + 3] = 0xFFu;
            }
        }
        const uint64_t bitbltbuf = makeBitBltBuf(kTexBlock, kTexWidthWords, GS_PSM_CT32);
        const uint64_t trxreg = makeTrxReg(kTexSide, kTexSide);
        gs.uploadImageNative(bitbltbuf, /*trxpos*/ 0ull, trxreg, /*trxdir*/ 0u, tex.data(),
                             static_cast<uint32_t>(tex.size()));
        std::cout << "VULCAN4 GS texture uploaded psm=0x" << std::hex << GS_PSM_CT32 << std::dec
                  << " block=" << kTexBlock << " (byte " << kTexBlock * 256u << ") " << kTexSide
                  << "x" << kTexSide << " split_at_column=" << kTexSplit << " left=("
                  << static_cast<int>(kTexAR) << "," << static_cast<int>(kTexAG) << ","
                  << static_cast<int>(kTexAB) << ") right=(" << static_cast<int>(kTexBR) << ","
                  << static_cast<int>(kTexBG) << "," << static_cast<int>(kTexBB) << ")\n";
    }

    // ---------------------------------------------------------------- 5. Submit, as a guest does
    //
    // One GIF REGLIST packet naming PRIM(0x00), optionally TEX0_1(0x06), then per vertex RGBAQ, UV or
    // ST, and XYZ2. The draw kicks itself when the third vertex arrives. Nothing is written into
    // the framebuffer here: this is the submission only.
    {
        std::vector<std::pair<uint8_t, uint64_t>> w;
        uint64_t prim = GS_PRIM_TRIANGLE; // 3. The literal 2 is GS_PRIM_LINESTRIP and fails quietly.
        if (textured)
            prim |= kPrimTmeBit;
        // IIP deliberately left clear: flat shading means every covered pixel takes the third
        // vertex's colour, so the triangle must be ONE colour. IIP would interpolate instead.
        w.emplace_back(kRegPrim, prim);
        if (textured)
            w.emplace_back(kRegTex0, makeTex0(kTexBlock, kTexWidthWords, GS_PSM_CT32, 6u, 6u));
        for (const Vert &v : kTri)
        {
            if (textured)
            {
                // White, because TFX = 1 (DECAL) replaces the vertex colour with the texel's.
                w.emplace_back(kRegRgbaq, rgbaq(0xFF, 0xFF, 0xFF, 0xFF));
                // S and T are chosen AFFINE in the screen coordinates of each vertex, so the
                // interpolated S at any covered pixel is exactly (x - x0) / (x1 - x0). That makes
                // the position of the texture's hard edge predictable to the pixel.
                const float s = static_cast<float>(v.x - kTri[0].x)
                    / static_cast<float>(kTri[1].x - kTri[0].x);
                const float t = static_cast<float>(v.y - kTri[0].y)
                    / static_cast<float>(kTri[2].y - kTri[0].y);
                w.emplace_back(kRegSt, stf(s, t));
            }
            else
            {
                // All three vertices the SAME colour: IIP is clear, so the rasteriser takes the
                // third vertex's colour for every covered pixel and the result must be flat.
                w.emplace_back(kRegRgbaq, rgbaq(kTriR, kTriG, kTriB, kTriA));
                w.emplace_back(kRegUv, 0ull);
            }
            w.emplace_back(kRegXyz2, xyz2(v.x, v.y));
        }
        const std::vector<uint8_t> packet = reglistPacket(w);
        gs.processGIFPacket(packet.data(), static_cast<uint32_t>(packet.size()));
        std::cout << "VULCAN4 GS submitted 1 primitive via 1 GIF REGLIST packet, " << w.size()
                  << " register writes, prim=" << prim << " (type=GS_PRIM_TRIANGLE"
                  << (textured ? " |TME" : "") << ", IIP=0 flat)\n";
    }

    const uint64_t drawEvents = countDebugEvents(gs, GSDebugEventKind::Draw);
    const uint64_t gifTags = countDebugEvents(gs, GSDebugEventKind::GifTag);
    std::cout << "VULCAN4 GS gs_said gif_tags=" << gifTags << " draw_events=" << drawEvents
              << "\n";

    // ---------------------------------------------------------------- 6. Read the frame back
    //
    // Through the runtime's own presentation path: the GS snapshots VRAM, converts it out of GS
    // pixel-storage layout and hands back host RGBA. Not a peek at VRAM, and not a screenshot.
    gs.latchHostPresentationFrame();
    std::vector<uint8_t> pixels;
    uint32_t width = 0, height = 0, displayFbp = 0, sourceFbp = 0;
    bool usedPreferred = false;
    if (!gs.copyLatchedHostPresentationFrame(pixels, width, height, &displayFbp, &sourceFbp,
                                             &usedPreferred)
        || width == 0 || height == 0)
    {
        std::cout << "VULCAN4 GS ORACLE FAIL: the presentation readback produced no frame. The draw "
                     "may well have worked; the pixels never came back out.\n";
        return 1;
    }
    std::cout << "VULCAN4 GS readback " << width << "x" << height << " display_fbp=0x" << std::hex
              << displayFbp << " source_fbp=0x" << sourceFbp << std::dec
              << " preferred=" << (usedPreferred ? "yes" : "no") << "\n";

    if (!writePng(outPath, pixels, width, height))
    {
        std::cout << "VULCAN4 GS ORACLE FAIL: could not write " << outPath << "\n";
        return 1;
    }

    // ---------------------------------------------------------------- 7. Measure, and judge
    const Measurement m = measure(pixels, width, height, wantA, wantB);
    const double areaErr = pred.analyticArea > 0.0
        ? std::fabs(static_cast<double>(m.nonBackground) - pred.analyticArea) / pred.analyticArea
        : 1.0;
    const bool areaOk = areaErr < 0.02; // the dish's gate: 2%, and rasterisation explains a pixel
    const bool bboxOk = m.bboxX0 >= kTri[0].x && m.bboxX1 <= kTri[1].x && m.bboxY0 >= kTri[0].y
        && m.bboxY1 <= kTri[2].y;
    const bool colourOk = (m.other == 0u);
    const bool insidePredictedBbox = m.bboxX0 >= pred.bboxX0 && m.bboxX1 <= pred.bboxX1
        && m.bboxY0 >= pred.bboxY0 && m.bboxY1 <= pred.bboxY1;

    std::cout << "VULCAN4 GS MEASURE png=" << outPath << " " << width << "x" << height
              << " non_background=" << m.nonBackground << " distinct_colours=" << m.distinctColours
              << " bbox=[" << m.bboxX0 << "," << m.bboxX1 << "]x[" << m.bboxY0 << "," << m.bboxY1
              << "] fnv1a64=0x" << std::hex << checksumRgba(pixels) << std::dec << "\n";
    std::cout << "VULCAN4 GS MEASURE predicted_area=" << pred.analyticArea
              << " measured_area=" << m.nonBackground << " err=" << (areaErr * 100.0) << "% (limit 2%)"
              << " -> " << (areaOk ? "PASS" : "FAIL") << "\n";
    std::cout << "VULCAN4 GS MEASURE bbox predicted=[" << pred.bboxX0 << "," << pred.bboxX1 << "]x["
              << pred.bboxY0 << "," << pred.bboxY1 << "] vertex_box=[" << kTri[0].x << ","
              << kTri[1].x << "]x[" << kTri[0].y << "," << kTri[2].y << "] measured=[" << m.bboxX0
              << "," << m.bboxX1 << "]x[" << m.bboxY0 << "," << m.bboxY1 << "] inside_vertices="
              << (bboxOk ? "yes" : "NO") << " matches_prediction="
              << (insidePredictedBbox ? "yes" : "NO") << "\n";
    if (textured)
    {
        std::cout << "VULCAN4 GS MEASURE texture colourA(orange)=" << m.colourA << " predicted="
                  << pred.texColourACount << "  colourB(blue)=" << m.colourB << " predicted="
                  << pred.texColourBCount << "  unexpected_colours=" << m.other << "\n";
    }
    else
    {
        std::cout << "VULCAN4 GS MEASURE flat colour(" << static_cast<int>(kTriR) << ","
                  << static_cast<int>(kTriG) << "," << static_cast<int>(kTriB) << ")=" << m.colourA
                  << " unexpected_colours=" << m.other << "\n";
    }

    // ---------------------------------------------------------------- 8. Verdict
    //
    // A named failure is a fine outcome. If the count is zero, the stage that swallowed the draw is
    // reported rather than guessed at: no draw event means the submission never reached the GS; a
    // draw event with no pixels means the rasteriser rejected it; pixels in the frame with the wrong
    // count means the geometry is landing somewhere other than where the vertices say.
    const bool pass = areaOk && bboxOk && colourOk && m.nonBackground > 0u && m.distinctColours <= 400u;
    if (drawEvents == 0u)
        std::cout << "VULCAN4 GS STAGE swallowed: SETUP -- the GS recorded no draw event at all, so "
                     "the submission never became a primitive.\n";
    else if (m.nonBackground == 0u)
        std::cout << "VULCAN4 GS STAGE swallowed: RASTERISER -- the GS recorded a draw and the "
                     "vertices reached it, and no pixel was written.\n";
    else if (!areaOk)
        std::cout << "VULCAN4 GS STAGE swallowed: GEOMETRY -- pixels were written but the covered "
                     "area disagrees with the analytic area by more than 2%.\n";
    else if (!bboxOk)
        std::cout << "VULCAN4 GS STAGE swallowed: CONVENTION -- the right number of pixels landed "
                     "outside the bounding box the vertices imply (Y flip or origin corner error).\n";

    std::cout << "VULCAN4 GS ORACLE " << (pass ? "PASS" : "FAIL") << " mode="
              << (textured ? "TEXTURED" : "FLAT") << "\n";

    // ---------------------------------------------------------------- 9. Not done, named
    std::cout << "VULCAN 4 LIMITATION: GS - FRAME_1, ZBUF_1, SCISSOR_1/2, TEST_1 and PRMODECONT "
                 "were written through GS::writeRegister, not through a GIF packet, because this "
                 "runtime's GIFTAG register-address field is 4 bits wide and cannot name a register "
                 "above 0x0F. A real guest cannot program its framebuffer through the GIF path at "
                 "all. See docs/LIMITATIONS.md.\n";
    std::cout << "VULCAN 4 LIMITATION: GS - TEST_1 = 0x30000 works around a runtime bug: hardware "
                 "gates the Z test on the ZTE bit and would draw with TEST = 0, while this runtime "
                 "reads ZTEST unconditionally, so TEST = 0 means NEVER and every pixel is "
                 "discarded. A guest that writes TEST with ZTE clear gets a black screen.\n";
    std::cout << "VULCAN 4 LIMITATION: GS - PRMODECONT = 1 is required for PRIM's TME and IIP bits to "
                 "survive; with AC = 0 this runtime takes them from PRMODE instead, so a guest that "
                 "writes PRIM alone gets flat, untextured geometry with no error.\n";
    std::cout << "VULCAN 4 LIMITATION: GS - nothing from Gran Turismo 4 is drawn here. The vertices "
                 "were typed into a GIF packet by this program. No VU1 computed them, no guest has "
                 "ever reached this GS, and the last recorded boot had total_mmio_accesses=0.\n";
    std::cout << "VULCAN 4 LIMITATION: GS - no blending, no alpha test, no depth sort, no dithering, "
                 "no mip selection, no bilinear filtering and no vblank or CSR interrupt plumbing "
                 "are exercised. One primitive, one colour, one frame.\n";

    return pass ? 0 : 1;
}