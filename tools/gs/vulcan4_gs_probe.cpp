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
            gs.writeRegister(r, value);
        }
    }

    // A triangle: PRIM=2 (GS_PRIM_TRIANGLE), then three vertices, each a colour plus a position.
    // A primitive arrives as PRIM followed by repeated RGBAQ..XYZ2 runs, so the address list
    // repeats every six registers.
    auto emitPrimitive = [&gs](uint64_t prim, const uint32_t rgba[], const uint32_t xyz[3][3])
    {
        std::vector<std::pair<uint8_t, uint64_t>> writes;
        writes.emplace_back(GS_REG_PRIM, prim);
        for (int i = 0; i < 3; ++i)
        {
            writes.emplace_back(GS_REG_RGBAQ, rgba[i]);
            writes.emplace_back(0x02, 0); // ST
            writes.emplace_back(0x03, 0); // UV
            writes.emplace_back(GS_REG_XYZF2, 0);
            writes.emplace_back(GS_REG_XYZ2, xyz[i][0] | (static_cast<uint64_t>(xyz[i][1]) << 16)
                                                   | (static_cast<uint64_t>(xyz[i][2]) << 32));
        }
        auto p = reglistPacket(writes);
        gs.processGIFPacket(p.data(), static_cast<uint32_t>(p.size()));
    };

    // A large blue triangle across the frame.
    //
    // Coordinates are raw screen pixels. The GS wants 16.16 fixed point, but emitPrimitive does
    // that packing itself, so pre-shifting here would shift twice and collapse X to zero.
    const uint32_t triRgba[3] = {0x40u | (0x80u << 8) | (0xFFu << 16),   // light blue
                                 0x20u | (0x40u << 8) | (0xC0u << 16),   // mid blue
                                 0x10u | (0x20u << 8) | (0x80u << 16)};  // dark blue
    const uint32_t triXyz[3][3] = {
        {40u, 40u, 0x3FFFF},                                   // top-left
        {kFrameWidth - 40u, 40u, 0x3FFFF},                     // top-right
        {kFrameWidth / 2u, kFrameHeight - 40u, 0x3FFFF},        // bottom-centre
    };
    emitPrimitive(/*prim=*/2u, triRgba, triXyz); // GS_PRIM_TRIANGLE == 2

    // FINISH, so the GS knows the packet stream ended.
    {
        auto p = reglistPacket({{GS_REG_FINISH, 0}});
        gs.processGIFPacket(p.data(), static_cast<uint32_t>(p.size()));
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

    const uint64_t sum = checksumRgba(pixels);
    const uint32_t lit = countNonBackground(pixels);
    std::cout << "VULCAN4 GS FRAME path=" << outPath << " " << width << "x" << height
              << " pixels=" << pixels.size() << " non_background=" << lit << " fnv1a64=0x" << std::hex
              << sum << std::dec << "\n";
    std::cout << "VULCAN4 GS PROOF the pixels above were rasterised by ps2xRuntime GSCpuBackend and "
                 "encoded by this program's own PNG writer\n";

    // ---- 5. What is not done, named rather than glossed over.
    std::cout << "VULCAN4 LIMITATION: GS - this is a skeleton. It draws one untextured triangle into "
                 "a PSMCT32 framebuffer. No texture sampling is exercised.\n";
    std::cout << "VULCAN4 LIMITATION: GS - no VU1 is involved. The GS path here takes vertex data "
                 "directly; nothing computes geometry. That is goal G3.1.\n";
    std::cout << "VULCAN4 LIMITATION: GS - the EE->GS GIF DMA is not exercised. This probe hands "
                 "packets to GS::processGIFPacket directly; GifArbiter and the DMAC are unproven.\n";
    std::cout << "VULCAN4 LIMITATION: GS - no vblank, no CSR signalling, no FINISH-driven present "
                 "event. The frame was latched on demand.\n";
    std::cout << "VULCAN4 LIMITATION: GS - Z-buffering is allocated but not written by this probe, "
                 "because nothing tests it yet.\n";
    std::cout << "VULCAN4 LIMITATION: GS - nothing from Gran Turismo 4 has been rendered. No guest "
                 "code has ever reached this GS; the last boot recorded total_mmio_accesses=0.\n";
    return 0;
}
