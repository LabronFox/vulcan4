// vulcan4_harness.cpp - the VULCAN 4 first-boot harness (goal G1.1).
//
// PURPOSE
//   Load a PS2 guest ELF the way the console does, start at its real entry point, and execute
//   the *recompiled* translation unit (the C++ ps2_recomp generated from that ELF) on the host.
//   Then stop, and say exactly why.
//
//   This is the dish where VULCAN 4 stops being analysis and becomes a program.
//
// RULES THIS FILE ENFORCES
//   * NO BIOS, EVER. The harness never searches for, opens, or requires a console BIOS image.
//     Any attempt would increment a counter that the report line publishes, and the gate
//     requires it to be 0. There is no code path that can increment it: see biosFilesOpened.
//   * NAMED, NEVER SILENT. Every guest call the runtime is asked for is recorded by name and
//     address and printed. A stop is always a reason token plus a PC.
//   * NO FAKING. functions_entered is counted by actually entering guest functions.
//   * IT CANNOT SPIN FOREVER. GT4's own code contains a deliberate spin trap (see
//     sub_01000558 in docs/FUNCTION-ANATOMY.md section 5), so there are three independent
//     stop conditions: an entry budget, a spin detector, and a wall-clock deadline.
//
// HOW THE EXECUTION MODEL WORKS
//   ps2_recomp translates each MIPS function into a C++ function that runs *inline* on the host.
//   It does not return normally at every branch: it RETURNS (yields) whenever it hits a guest
//   control transfer, leaving the next PC in ctx->pc. The runtime is expected to re-enter the
//   generated function at that PC. So the driver loop is simply:
//
//       while (budget remains) {
//           fn = generatedTable[pc];        // the dense table register_functions.cpp fills
//           if (!fn) { name it, stop; }
//           fn(rdram, &ctx, &runtime);      // runs until it yields
//       }
//
//   Every iteration of that loop is one entry into a guest function, which is what
//   functions_entered counts.
//
// BUILD
//   See docs/FIRST-BOOT.md for the exact command line. Needs libps2_runtime.a, libps2_iop.a,
//   raylib, ffmpeg and the generated translation unit from ps2_recomp.

#include "ps2_guest_progress.h"
#include "ps2_runtime.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "runtime/ee_scheduler.h"
#include "Stubs/Audio.h"
#include "Stubs/MPEG.h"
#include "ps2_log.h"
#include "runtime/syscall_names.h"
#include "ps2_recompiled_stubs.h"

#include <ps2_recompiled_functions.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <atomic>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <unordered_map>
#include <variant>
#include <vector>

namespace
{
// W30. Watch the guest buffer that holds the path GT4 hands sceMcOpen, and report the PC that
// writes it. The path is `//e.gt4` -- `e.gt4` matches the ELF's static string byte
// for byte, so the filename half is copied correctly and only the two directory components are
// wrong. Finding the writer names the formatting code, and that code explains the encoding. This is
// the one measurement left that does not require guessing.
//
// It rides on the store observer the recompiler already emits at every WRITE32 (159 sites), so
// nothing had to be recompiled. The observer fires on EVERY guest store, so the filter is one
// unsigned compare and the printing is capped: an uncapped print would dominate the run and change
// the very throughput it is measuring.
constexpr uint32_t kWatchLo = 0x01051A10u;
constexpr uint32_t kWatchHi = 0x01051A20u;
uint32_t g_watchStoreHits = 0;
constexpr uint32_t kWatchStoreMax = 600;

void watchGuestStoreForPath(uint32_t guestAddr, uint32_t size, uint64_t value, const R5900Context *ctx)
{
    if (guestAddr >= kWatchHi || guestAddr + size <= kWatchLo)
    {
        return;
    }
    if (g_watchStoreHits >= kWatchStoreMax)
    {
        return;
    }
    ++g_watchStoreHits;
    std::cout << "VULCAN4 W30WRITE n=" << g_watchStoreHits << " pc=0x" << std::hex
              << (ctx != nullptr ? ctx->pc : 0u) << " ra=0x" << (ctx != nullptr ? getRegU32(ctx, 31) : 0u)
              << " addr=0x" << guestAddr << std::dec << " size=" << size << " value=0x" << std::hex
              << static_cast<uint32_t>(value) << std::dec << "\n";
}
} // namespace

namespace
{
    // ---------------------------------------------------------------- stop reasons
    // These tokens are the vocabulary of the report line. They are stable: a log can be
    // grepped by halt=<token> across runs and across dishes.
    constexpr const char *kHaltEntryBudget = "entry_budget_exhausted";
    constexpr const char *kHaltMissingFunction = "missing_function";
    constexpr const char *kHaltSpinTrap = "spin_trap";
    constexpr const char *kHaltDeadline = "wallclock_deadline";
    constexpr const char *kHaltOutOfTable = "pc_outside_generated_table";
    constexpr const char *kHaltReturnedToEntry = "returned_to_entry";
    constexpr const char *kHaltInSyscall = "stuck_in_syscall";
    // G1.5: names the SPECIFIC syscall that dominates the call tally, not just the category.
    constexpr const char *kHaltStalledInSyscall = "livelocked_in_syscall";
    constexpr const char *kHaltSpinningInGuest = "waiting_on_unnamed_value";
    // A cycle of guest PCs that keeps repeating with no new PC ever reached. This is distinct
    // from kHaltSpinTrap (one PC revisited) and from kHaltInSyscall (a syscall that is executing):
    // here the guest is executing normally, over and over, and going nowhere.
    constexpr const char *kHaltCycleNoProgress = "guest_cycle_no_progress";
    // G1.8g / W8. The guest asked to sleep (or waited on a kernel object) and NOTHING is runnable:
    // no other thread to switch to, no invocation queued, no event due. On a console this is where
    // an interrupt would arrive. Here it is the honest end of the road, and re-entering the parked
    // frame to "keep going" is precisely the fake this project forbids -- it is what made
    // sce_SleepThread a no-op and left the guest spinning in the idle loop at 0x01000760.
    constexpr const char *kHaltGuestBlocked = "guest_blocked";

    // ---------------------------------------------------------------- budgets
    //
    // The three stop conditions. The spin detector is the interesting one: GT4 contains a
    // deliberate infinite loop (five NOPs then a backward branch) used as a failure trap, so a
    // guest that is alive but unhappy will sit in it forever. Without the detector that is an
    // unbounded run; with it, the trap becomes a named, reported outcome.
    struct Budget
    {
        uint64_t maxEntries = 2000000;   // total guest function entries
        uint32_t maxRepeatedPc = 1000000; // consecutive returns to the same PC
        // Repeats of one PC cycle, with no progress anywhere inside it, before the guest is
        // called hung. W7 tripped a threshold of 24 on a copy loop that ran ~193 passes per
        // guest call: the call's return is the only forward signal, so the threshold has to
        // clear one call's internal loops. 4096 clears the measured worst case by >20x while
        // still naming a real hang; beyond that the instruction and wall-clock budgets are the
        // honest backstop.
        uint64_t maxCycleRepeats = 4096;
        int maxSeconds = 120;            // wall clock
    };

    // A named call the guest asked for that we could not satisfy.
    struct GuestCall
    {
        uint32_t address = 0;
        std::string name;
        std::string kind;
        uint64_t count = 0;
    };

    // The harness has no BIOS-loading code path at all. This counter exists so the report can
    // state that as a measured fact rather than an assurance. It is only ever incremented by a
    // BIOS open, and there is no such call anywhere in this file.
    uint64_t biosFilesOpened = 0;

    // Name a numeric SCE syscall using the table transcribed from the runtime dispatcher by
    // tools/harness/gen_syscall_names.py. Unknown numbers are reported as such, never guessed.
    const char *syscallName(uint32_t id)
    {
        for (unsigned i = 0; i < kSyscallNameCount; ++i)
        {
            if (kSyscallNames[i].id == id)
            {
                return kSyscallNames[i].name;
            }
        }
        return "unnamed_syscall";
    }

    std::string toHex(uint64_t value, int width = 8)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "0x%0*llx", width, static_cast<unsigned long long>(value));
        return buffer;
    }

    // Load "name@0xADDRESS" pairs from the analyzer's gt4.toml so SCE SDK functions can be
    // reported by their real names instead of a bare address. The recompiler's own header only
    // has sub_<addr>_<addr> names, because the ELF is stripped; the analyzer recovered the SDK
    // names from its embedded SCE symbol database.
    std::unordered_map<uint32_t, std::string> loadSdkNames(const std::string &tomlPath)
    {
        std::unordered_map<uint32_t, std::string> names;
        std::ifstream toml(tomlPath);
        if (!toml)
        {
            return names;
        }

        std::string line;
        while (std::getline(toml, line))
        {
            const size_t at = line.find('@');
            if (at == std::string::npos)
            {
                continue;
            }
            // Only accept quoted "name@0xADDR" entries.
            const size_t firstQuote = line.find('"');
            if (firstQuote == std::string::npos)
            {
                continue;
            }
            std::string entry = line.substr(firstQuote + 1);
            const size_t closeQuote = entry.find('"');
            if (closeQuote != std::string::npos)
            {
                entry = entry.substr(0, closeQuote);
            }
            const size_t entryAt = entry.rfind('@');
            if (entryAt == std::string::npos)
            {
                continue;
            }
            const std::string name = entry.substr(0, entryAt);
            const std::string addressText = entry.substr(entryAt + 1);
            if (name.empty())
            {
                continue;
            }
            try
            {
                names[static_cast<uint32_t>(std::stoul(addressText, nullptr, 0))] = name;
            }
            catch (const std::exception &)
            {
                // A name we cannot parse is not worth failing a boot over.
            }
        }
        return names;
    }

    // Report the ELF's program headers independently, so the log carries evidence of how the
    // image was placed rather than an assurance that it was. The runtime's own loadELF does
    // this; we re-read the headers to show the numbers.
    void reportElfLayout(const std::string &elfPath)
    {
        std::ifstream file(elfPath, std::ios::binary);
        if (!file)
        {
            return;
        }
        unsigned char ident[16];
        file.read(reinterpret_cast<char *>(ident), sizeof(ident));
        if (file.gcount() != static_cast<std::streamsize>(sizeof(ident)) || ident[0] != 0x7f
            || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F')
        {
            std::cout << "VULCAN4 ELF LAYOUT: not an ELF file\n";
            return;
        }

        auto readU16 = [&file](std::streamoff off) -> uint32_t {
            file.seekg(off);
            uint16_t v = 0;
            file.read(reinterpret_cast<char *>(&v), sizeof(v));
            return v; // host is x86-64 little-endian, the guest is little-endian: no swap
        };
        auto readU32 = [&file](std::streamoff off) -> uint32_t {
            file.seekg(off);
            uint32_t v = 0;
            file.read(reinterpret_cast<char *>(&v), sizeof(v));
            return v; // host is x86-64 little-endian, the guest is little-endian: no swap
        };

        // ELF32 layout: e_entry@0x18, e_phoff@0x1C are 4 bytes, but e_phentsize@0x2A and
        // e_phnum@0x2C are 2 bytes each. Reading those as 4 yields a nonsense phnum and walks
        // off the end of the file.
        const uint32_t entry = readU32(0x18);
        const uint32_t phoff = readU32(0x1C);
        const uint32_t phentsize = readU16(0x2A);
        const uint32_t phnum = readU16(0x2C);

        std::cout << "VULCAN4 ELF LAYOUT ei_data=" << static_cast<int>(ident[5])
                  << " (1=ELFDATA2LSB) entry=" << toHex(entry) << " program_headers=" << phnum
                  << " bios_files=" << biosFilesOpened << "\n";

        for (uint32_t i = 0; i < phnum; ++i)
        {
            const std::streamoff base = static_cast<std::streamoff>(phoff)
                + static_cast<std::streamoff>(i) * static_cast<std::streamoff>(phentsize);
            file.seekg(base);
            uint32_t ph[8] = {0};
            file.read(reinterpret_cast<char *>(ph), sizeof(ph));
            if (ph[0] == 1u) // PT_LOAD
            {
                std::cout << "VULCAN4 ELF SEGMENT " << i << " PT_LOAD vaddr=" << toHex(ph[2])
                          << " filesz=" << toHex(ph[4]) << " memsz=" << toHex(ph[5])
                          << " flags=" << toHex(ph[1], 1) << " bss_zeroed="
                          << (ph[5] > ph[4] ? toHex(ph[5] - ph[4]) : std::string("0")) << "\n";
            }
            else
            {
                std::cout << "VULCAN4 ELF SEGMENT " << i << " type=" << toHex(ph[0])
                          << " (not PT_LOAD, not loaded into RDRAM)\n";
            }
        }
        std::cout << "VULCAN4 ELF LAYOUT entry_point_confirmed=" << toHex(entry) << "\n";
    }
} // namespace


// ---------------------------------------------------------------------------
// G1.7: name the wait.
//
// "spinning_in_guest_code" describes a symptom. This turns it into a diagnosis: find the
// repeating block of guest instructions, then decode every LOAD in it and report the address
// the guest is polling and the value it currently holds. That is the difference between
// "it is stuck" and "it is waiting on 0x1xxxxxxx, which reads 0".
//
// MIPS load opcodes we decode: lb 0x20 lh 0x21 lwl 0x22 lw 0x23 lbu 0x24 lhu 0x25
// lwr 0x26 lwu 0x30 ld 0x37.
constexpr bool decodeGpuLoad(uint32_t insn, uint32_t &outReg, uint32_t &outImm, int &outWidth, bool &outSigned)
{
    const uint32_t op = insn >> 26;
    switch (op)
    {
    case 0x20: outWidth = 1; outSigned = true;  break;  // lb
    case 0x24: outWidth = 1; outSigned = false; break;  // lbu
    case 0x21: outWidth = 2; outSigned = true;  break;  // lh
    case 0x25: outWidth = 2; outSigned = false; break;  // lhu
    case 0x23: outWidth = 4; outSigned = true;  break;  // lw
    case 0x30: outWidth = 4; outSigned = false; break;  // lwu
    case 0x37: outWidth = 8; outSigned = true;  break;  // ld
    default: return false;
    }
    outReg = (insn >> 16) & 0x1Fu;
    const uint32_t rawImm = insn & 0xFFFFu;
    outImm = (rawImm & 0x8000u) ? static_cast<uint32_t>(static_cast<int32_t>(rawImm | 0xFFFF0000u))
                                : rawImm;
    return true;
}

constexpr const char *mipsMnemonic(uint32_t insn)
{
    switch (insn >> 26)
    {
    case 0x20: return "lb";
    case 0x24: return "lbu";
    case 0x21: return "lh";
    case 0x25: return "lhu";
    case 0x23: return "lw";
    case 0x30: return "lwu";
    case 0x37: return "ld";
    case 0x28: return "sb";
    case 0x29: return "sh";
    case 0x2B: return "sw";
    case 0x3F: return "sd";
    case 0x04: return "beq";
    case 0x05: return "bne";
    case 0x06: return "blez";
    case 0x07: return "bgtz";
    case 0x0A: return "slti";
    case 0x0B: return "sltiu";
    case 0x0C: return "andi";
    case 0x0D: return "ori";
    case 0x0E: return "xori";
    case 0x09: return "addiu";
    case 0x08: return "addi";
    case 0x00: return ((insn >> 26) == 0 && ((insn >> 6) & 0xF) == 0x10) ? "bltzal" : "sll";
    case 0x0F: return "lui";
    case 0x2A: return "slt";
    case 0x2C: return "slt";
    default: return "?";
    }
}

int main(int argc, char *argv[])
{
    const std::string usage =
        "Usage: vulcan4_harness <guest.elf> [sdk_names.toml] [max_entries] [max_seconds]\n"
        "  guest.elf       the PS2 executable to load (no BIOS is ever looked for)\n"
        "  sdk_names.toml  optional: analyzer output, used to name SCE SDK calls in the report\n";
    if (argc < 2)
    {
        std::cerr << usage;
        return 2;
    }

    const std::string elfPath = argv[1];
    const std::string tomlPath = (argc > 2) ? argv[2] : std::string();
    Budget budget;
    if (argc > 3)
    {
        budget.maxEntries = std::strtoull(argv[3], nullptr, 10);
    }
    if (argc > 4)
    {
        budget.maxSeconds = static_cast<int>(std::strtol(argv[4], nullptr, 10));
    }

    std::unordered_map<uint32_t, std::string> sdkNames;
    if (!tomlPath.empty())
    {
        sdkNames = loadSdkNames(tomlPath);
        std::cout << "VULCAN4 HARNESS sdk_names_loaded=" << sdkNames.size() << "\n";
    }

    std::cout << "VULCAN4 HARNESS starting\n";
    std::cout << "VULCAN4 HARNESS bios_policy=none (no console BIOS is searched, loaded or required)\n";
    reportElfLayout(elfPath);

    PS2Runtime runtime;

    // initialize() brings up the memory model and binds the core subsystems. It also opens a
    // window via raylib, which is why this harness is run under a virtual framebuffer on a
    // headless box. Nothing here loads a BIOS.
    if (!runtime.initialize("VULCAN 4 - first boot"))
    {
        std::cout << "VULCAN4 HARNESS halt=" << kHaltMissingFunction
                  << " detail=runtime_init_failed\n";
        return 1;
    }

    if (!runtime.loadELF(elfPath))
    {
        std::cout << "VULCAN4 HARNESS halt=" << kHaltMissingFunction
                  << " detail=elf_load_failed\n";
        return 1;
    }

    uint8_t *rdram = runtime.memory().getRDRAM();
    R5900Context &ctx = runtime.cpu();

    // W30: watch the guest buffer holding the memory-card path. Installed before the first guest
    // instruction so the one-time formatting write is caught, not just the reads that follow.
    ps2SetGuestStoreObserver(&watchGuestStoreForPath);

    // Reset the parts of the console environment the runtime expects before the first guest
    // instruction, mirroring what PS2Runtime::run() does, minus the render loop.
    // PS2Runtime::resetIop() is private, so the IOP is left as the constructor built it; that is
    // a deviation from run() and is recorded as such in docs/FIRST-BOOT.md.
    ps2_stubs::resetSifState();
    ps2_stubs::resetAudioStubState();
    ps2_stubs::resetMpegStubState();
    runtime.initializeEeKernelState(rdram);

    // ---------------------------------------------------------------- G1.6 decisive diagnostic.
    //
    // The guest calls FindAddress(0x80000000, 0x80080000, 0x010285F8) and the same for
    // 0x010285C0. In the image, those two words occur exactly once each, 8 bytes apart, as one
    // 16-byte descriptor in the second PT_LOAD segment at vaddr 0x01035354 -- RAM offset 0x35354,
    // which IS inside the search window. Yet the trace reports a miss over the full window and
    // allZero=true. One of those is a lie. This prints the evidence instead of arguing about it.
    {
        constexpr uint32_t kRamOffset = 0x00035354u;
        constexpr size_t kPs2RamSize = PS2_RAM_SIZE;
        std::cout << "VULCAN4 PROBE1 ram_size=" << kPs2RamSize
                  << " window=[0x00000000,0x00080000) desc_offset=0x" << std::hex << kRamOffset
                  << std::dec << "\n";
        if (kRamOffset + 16u <= kPs2RamSize)
        {
            uint32_t words[4];
            std::memcpy(words, rdram + kRamOffset, sizeof(words));
            std::cout << "VULCAN4 PROBE2 rdram[0x35354..0x35363] =";
            for (uint32_t w : words)
            {
                std::cout << " 0x" << std::hex << w << std::dec;
            }
            std::cout << (words[0] == 0x010285F8u ? "  <-- sub_010285F8 IS PRESENT"
                                                  : "  <-- sub_010285F8 IS **MISSING**")
                      << (words[2] == 0x010285C0u ? " ; sub_010285C0 present"
                                                   : " ; sub_010285C0 MISSING")
                      << "\n";
        }

        // If the descriptor is not where the program headers say, find out where it actually is.
        uint32_t found85f8 = 0xFFFFFFFFu;
        uint32_t found85c0 = 0xFFFFFFFFu;
        for (size_t off = 0; off + 4u <= kPs2RamSize; off += 4u)
        {
            uint32_t w = 0;
            std::memcpy(&w, rdram + off, sizeof(w));
            if (w == 0x010285F8u && found85f8 == 0xFFFFFFFFu)
            {
                found85f8 = static_cast<uint32_t>(off);
            }
            if (w == 0x010285C0u && found85c0 == 0xFFFFFFFFu)
            {
                found85c0 = static_cast<uint32_t>(off);
            }
        }
        std::cout << "VULCAN4 PROBE3 rdram scan: 0x010285F8 at RAM offset 0x" << std::hex
                  << found85f8 << std::dec << ", 0x010285C0 at RAM offset 0x" << std::hex
                  << found85c0 << std::dec << "\n";
        // Also: is the searched window even zeroed?
        uint32_t nonZeroWords = 0;
        for (size_t off = 0; off + 4u <= 0x00080000u; off += 4u)
        {
            uint32_t w = 0;
            std::memcpy(&w, rdram + off, sizeof(w));
            if (w != 0u)
            {
                ++nonZeroWords;
            }
        }
        std::cout << "VULCAN4 PROBE4 search window [0,0x80000): non-zero words = " << nonZeroWords
                  << (nonZeroWords == 0u ? "  (allZero=true would be CORRECT)"
                                        : "  (so allZero=true in the trace is **FALSE**)")
                  << "\n";
    }

    // Initialise the EE scheduler.
    //
    // This is the one line PS2Runtime::run() does that a hand-rolled harness is easy to miss, and
    // missing it is invisible until the guest loops. The recompiled code calls
    // runtime->eeCheckpointDue() on every loop back-edge, and that reads
    // EeScheduler::checkpointDue(), which only starts returning true once the scheduler has been
    // reset -- it decides from a deadline cycle counter that reset() initialises. Unreset, the
    // deadline is 0, the pending flag is false, and eeCheckpointDue() returns false forever.
    //
    // The consequence measured in goal G1.4: GT4's convergence loop at 0x01028740 has a
    // conditional back-edge that is taken on every iteration, so it *would* yield to the driver
    // every time round -- but it never did, the driver regained control only 3 times for the whole
    // run, and no progress, cycle or no-progress detector placed in the driver could see the loop
    // at all. Both accessors used here are public.
    runtime.eeScheduler().reset(rdram, ctx);

    // G1.8g / W10. This loop advances EeScheduler::currentContext() every iteration, and for the
    // main thread that IS GuestThread::context -- so m_cpuContext is the STALE one and must not be
    // copied over it. Without this declaration serviceInvocations() refreshes the scheduler's copy
    // from m_cpuContext on every service call, which rewinds the caller to before its own call:
    // measured on GT4 as a guest `jal` -> a deferred syscall -> a resume left $ra holding the
    // PREVIOUS call's value and $s2/$s3 back at zero, and the guest spun forever in a convergence
    // loop that was working perfectly. $v0 always looked right, because the runtime writes it back
    // by hand, which is why this went unnoticed for eleven dishes.
    runtime.eeScheduler().setDriverAdvancesSchedulerContext(true);

    const uint32_t entryPoint = ctx.pc;
    const uint32_t tableBase = g_ps2RecompiledFunctionTableBase;
    const uint32_t tableEnd = g_ps2RecompiledFunctionTableEnd;
    const uint32_t tableSlots = g_ps2RecompiledFunctionTableSlotCount;

    // The PS2 ABI hands the entry point $sp pointing near the top of RDRAM and $a0/$a1 zeroed.
    // The entry block reads *0x01041800 into a0, so $gp and the data segment are already in
    // place; we only supply the stack the console would have supplied.
    ctx.r[4] = _mm_setzero_si128();
    ctx.r[5] = _mm_setzero_si128();
    ctx.r[29] = _mm_set_epi64x(0, static_cast<int64_t>(PS2_RAM_SIZE - 0x10u));

    std::cout << "VULCAN4 HARNESS entry_point=" << toHex(entryPoint)
              << " generated_table=[" << toHex(tableBase) << "," << toHex(tableEnd) << ")"
              << " slots=" << tableSlots << "\n";

    // ------------------------------------------------------------------ execute
    uint64_t functionsEntered = 0;
    uint64_t distinctPcs = 0;
    // W16: distinct_pcs=167 says HOW MUCH code the guest visited and nothing about WHERE it spent
    // its life. Every wall so far had to be located by reading call chains by hand out of the
    // generated unit -- error-prone, and it guesses at edges instead of sampling the loop. The
    // guest's entry PC is already in hand here, so counting it costs one hash per entry and turns
    // "the guest cycles through 4 addresses" into "and here is the top of the cycle".
    std::unordered_map<uint32_t, uint64_t> pcEntryCounts;
    // W22. WHO calls each function: the distinct $ra values seen on entry, keyed by entry PC. The
    // syscall site sets name where a syscall was issued from; this names the caller of a FUNCTION,
    // which is the missing half. GT4 spins through sub_010112E0 <-> sub_01011508 844,500 times and
    // both correctly do nothing (the mutex is free, its wait list empty), so the loop that matters
    // is in whatever calls them -- and nothing in the report could name that until now.
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> entryCallers;
    uint64_t dispatcherTransfers = 0;
    std::size_t checkpointServiced = 0;
    uint64_t servicedInvocations = 0;
    // G1.8g. servicedInvocations counted every catch as progress, which is how a boot that did
    // nothing at all could report 15/15 "serviced" next to 119 syscalls in 20 million entries.
    // These two say what the service call actually achieved.
    uint64_t servicedWithProgress = 0;
    uint64_t blockedOnServicing = 0;

    const char *haltReason = kHaltEntryBudget;
    uint32_t haltPc = entryPoint;
    std::string haltDetail;

    std::unordered_map<uint32_t, std::string> functionNames; // address -> name, first time seen
    std::map<uint32_t, GuestCall> missing;                  // address -> named call
    std::vector<uint32_t> pcOrder;                          // first-seen order, for the report

    uint32_t previousPc = 0xffffffffu;
    uint32_t repeatedPc = 0;

    // ---- progress detector
    //
    // The guest can be executing correctly and still be going nowhere: it cycles through the same
    // small set of PCs forever without ever reaching a new one. But a PC cycle repeating is NOT
    // that, because GT4's normal work IS a converging loop over one small set of addresses -- at
    // W7 this detector called a copy loop six iterations from its bound a hang and stopped the
    // boot at functions_entered=95. Only a cycle that repeats with no new code reached and no new
    // hardware touched anywhere inside it is a hang, so the tracker is fed those two signals.
    // It lives in the runtime (ps2_guest_progress.h) so the behaviour is unit-tested.
    PS2GuestProgress progress;
    progress.setHangThreshold(budget.maxCycleRepeats);
    uint64_t progressDistinctPcMark = 0;
    size_t progressMmioMark = 0;

    // ---- G1.8g: the guest frames a service call runs ON OUR BEHALF.
    //
    // EeScheduler::serviceInvocations() enters the guest itself, and a driver that only counts
    // its own loop therefore counts a FRACTION of the guest's execution. Measured: a boot in which
    // the guest's own syscall-override handler ran 59,816 times inside the service call reported
    // distinct_pcs=4, and the handler was not among the four -- so the progress detector, the
    // cycle detector, the entry count and the PC list were all blind to most of the run, and
    // "the guest cycles through 4 addresses" looked like a diagnosis when it was a blind spot.
    //
    // The observer gets the live context of each frame the service path enters, and feeds the SAME
    // counters the driver loop uses. One accounting, no second opinion.
    uint64_t serviceFrames = 0;
    runtime.eeScheduler().setServiceFrameObserver([&](const R5900Context &frame) {
        ++serviceFrames;
        if (functionNames.find(frame.pc) == functionNames.end())
        {
            std::ostringstream serviceName;
            serviceName << "func_" << toHex(frame.pc);
            functionNames.emplace(frame.pc, serviceName.str());
            pcOrder.push_back(frame.pc);
            ++distinctPcs;
            const size_t mmioAddresses = runtime.memory().mmioCounts().size();
            const bool reachedNewCode = (distinctPcs != progressDistinctPcMark)
                || (mmioAddresses != progressMmioMark);
            progressDistinctPcMark = distinctPcs;
            progressMmioMark = mmioAddresses;
            progress.observe(frame.pc, reachedNewCode);
        }
    });

    // Optional tracing, so a run that never reaches the report line can still be diagnosed
    // instead of just timing out. VULCAN4_TRACE=<n> prints the pc of the first n entries and
    // then every 100000th; VULCAN4_TRACE_ALL=1 prints every entry.
    const char *traceEnv = std::getenv("VULCAN4_TRACE");
    const char *traceAllEnv = std::getenv("VULCAN4_TRACE_ALL");
    const uint64_t traceFirst = traceEnv ? std::strtoull(traceEnv, nullptr, 10) : 0;
    const bool traceAll = traceAllEnv != nullptr;

    const auto start = std::chrono::steady_clock::now();
    bool finished = false;

    // ---- watchdog
    //
    // A guest that spins INSIDE one generated function never returns to this loop, so neither
    // the entry budget nor the deadline check can fire. The generated code emits
    // runtime->eeCheckpointDue() on unconditional back-edges, and PS2Runtime::requestStop() is
    // public and sets the flag that makes eeCheckpointDue() return true. So a watchdog thread
    // that calls requestStop() after the deadline causes the spinning guest to yield back here,
    // where the stop is detected and reported honestly. This is how the harness guarantees it
    // cannot hang, and it is why the halt is reported as a reason rather than a timeout kill.
    std::atomic<bool> watchdogFired{false};
    // G1.8g. The watchdog used to sleep out the WHOLE deadline and only then call
    // requestStop(), so the harness's own `watchdog.join()` after the loop blocked for every
    // remaining second of the budget. Measured: a 20 s budget produced a 20.3 s process for a
    // guest phase that takes 1.3 s, and a 120 s budget produced a 120.6 s process for the same
    // 1.3 s of work. Every wall-clock number in the boot report was therefore the harness
    // waiting for its own watchdog, not the guest doing anything -- which is the third time in
    // this project that the instrument, not the thing measured, produced the number.
    // The watchdog now watches a flag the driver sets when it is done, so joining is prompt.
    std::atomic<bool> driverFinished{false};
    std::thread watchdog([&runtime, &budget, &watchdogFired, &driverFinished, start]() {
        const auto deadline = start + std::chrono::seconds(budget.maxSeconds);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (driverFinished.load(std::memory_order_acquire))
            {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        watchdogFired.store(true);
        runtime.requestStop();
    });

    // G1.8d: decode the PC cycle the guest is stuck in, and NAME it.
    //
    // Before this existed the cycle detector printed a bare "guest is cycling through 5
    // addresses ... (first 0x01000760)" and stopped. Five addresses and a first PC is not a
    // diagnosis: it is the location of one, with the other four withheld. That is why W7 took
    // ten measurement passes -- the report never said what the guest was doing, only where it
    // happened to be standing.
    //
    // So the decoder that already existed for the deadline path is lifted out and reused here.
    // It walks back to the previous occurrence of `pc` (everything after that is one pass of the
    // loop), decodes every instruction in the block, and for each LOAD prints the effective
    // address it is polling and the value sitting there. A cycle that loads is waiting on a
    // memory word, and that word now has a name, an address and a current value.
    // G1.8g. `frame` is a PARAMETER, not a capture, on purpose. The decoder reports the effective
    // address a loop is polling and the value sitting there, so reading the wrong context would
    // print a plausible wrong address -- the exact failure this project keeps paying for.
    auto nameCycle = [&](const char *tag, uint32_t pc, const R5900Context &frame)
    {
        std::vector<uint32_t> loop;
        if (!progress.history().empty())
        {
            std::size_t start = progress.history().size();
            for (std::size_t i = progress.history().size(); i-- > 0;)
            {
                if (progress.history()[i] == pc)
                {
                    start = i;
                    break;
                }
            }
            for (std::size_t i = start; i < progress.history().size() && loop.size() < 64u; ++i)
            {
                if (std::find(loop.begin(), loop.end(), progress.history()[i]) == loop.end())
                {
                    loop.push_back(progress.history()[i]);
                }
            }
        }

        std::cout << "VULCAN4 " << tag << " pc=0x" << std::hex << pc << std::dec
                  << " block_instructions=" << loop.size()
                  << " history_len=" << progress.history().size() << std::endl;

        uint32_t loadCount = 0;
        uint32_t branchCount = 0;
        std::vector<uint32_t> polledAddresses;
        for (uint32_t loopPc : loop)
        {
            const uint32_t ramOffset = loopPc & 0x00FFFFFFu;
            if (ramOffset + 4u > PS2_RAM_SIZE)
            {
                continue;
            }
            uint32_t insn = 0;
            std::memcpy(&insn, rdram + ramOffset, sizeof(insn));
            const char *mn = mipsMnemonic(insn);
            const uint32_t op = insn >> 26;
            const bool isBranch = (op == 0x04 || op == 0x05 || op == 0x06 || op == 0x07
                                   || (op == 0 && (insn & 0x3F) == 0x08));
            if (isBranch)
            {
                ++branchCount;
            }
            std::cout << "VULCAN4 " << tag << "LOOP pc=0x" << std::hex << loopPc << std::dec
                      << " insn=0x" << std::hex << insn << std::dec << " " << mn;
            // G1.8d CORRECTION. A zero word here is almost certainly NOT an instruction. The
            // guest image is a file-backed PT_LOAD at vaddr 0x01000000 / file offset 0x1000, and
            // this decode indexes the harness's RDRAM by the low 24 bits of the vaddr, which does
            // not reproduce that mapping -- so it reads zeroed RDRAM for every guest address and
            // would print "nop" for all of them. That is a plausible-looking wrong answer, which is
            // the failure mode this project keeps hitting, so the zero case is LABELLED rather
            // than decoded. Measured against SCUS_973.28: guest 0x01000760 is 0x0000282d (DADDU),
            // not 0x00000000.
            if (insn == 0u)
            {
                std::cout << "  [UNVERIFIED: rdram lookup returned zero -- this is NOT evidence"
                             " that the guest executes a nop here; decode from the ELF instead]";
            }
            uint32_t rt = 0, imm = 0;
            int width = 0;
            bool isSigned = false;
            if (decodeGpuLoad(insn, rt, imm, width, isSigned))
            {
                const uint32_t base = _mm_extract_epi32(frame.r[(insn >> 21) & 0x1F], 0);
                const uint32_t addr = base + imm;
                uint32_t value = 0;
                std::memcpy(&value, rdram + (addr & 0x00FFFFFFu), sizeof(value));
                std::cout << "  -> POLLS addr=0x" << std::hex << addr << std::dec
                          << " (base=r" << ((insn >> 21) & 0x1F) << " 0x" << std::hex << base
                          << std::dec << " + 0x" << std::hex << imm << std::dec
                          << ") width=" << width << " rt=r" << rt
                          << " value=0x" << std::hex << value << std::dec;
                polledAddresses.push_back(addr);
                ++loadCount;
            }
            if (isBranch)
            {
                std::cout << "  [branch]";
            }
            std::cout << std::endl;
        }
        std::cout << "VULCAN4 " << tag << "SUM loads=" << loadCount << " branches=" << branchCount
                  << (loadCount
                          ? " -- the guest is POLLING MEMORY; the POLLS lines name the words"
                          : " -- no loads: pure arithmetic or a register-only wait, NOT memory")
                  << std::endl;
        if (!polledAddresses.empty())
        {
            std::cout << "VULCAN4 " << tag << "DEPENDS on:";
            for (uint32_t a : polledAddresses)
                std::cout << " 0x" << std::hex << a << std::dec;
            std::cout << std::endl;
        }
        return loop;
    };

    // Which kernel object is a parked thread parked on? EeScheduler::waitObjectId() is private, and
    // duplicating its logic here is the same class of mistake: two copies of one rule that drift.
    // So this only names the payload, and says "none" when there is no object to name.
    auto waitObjectName = [](const GuestThread &thread) -> std::string
    {
        return std::visit(
            [](const auto &payload) -> std::string
            {
                using T = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<T, std::monostate>) { return "none"; }
                else if constexpr (std::is_same_v<T, EeSemaphoreWait>) { return "sema" + std::to_string(payload.id); }
                else if constexpr (std::is_same_v<T, EeEventFlagWait>) { return "eventflag" + std::to_string(payload.id); }
                else if constexpr (std::is_same_v<T, EeVSyncWait>) { return "vsync"; }
                else if constexpr (std::is_same_v<T, EeExternalWait>) { return "external" + std::to_string(payload.type); }
                else { return "unknown"; }
            },
            thread.wait.payload);
    };

    while (!finished)
    {
        // ---- G1.8g: RUN THE THREAD THE SCHEDULER SAYS IS RUNNING.
        //
        // This loop used to enter the guest on `runtime.cpu()`, which is the MAIN thread's frame
        // and was correct only while the main thread was the one running. The moment the guest's
        // cooperative scheduler switches threads, the driver kept advancing the main thread's
        // registers while the scheduler believed some other thread held the CPU. The boot report
        // showed exactly that contradiction and nothing else:
        //
        //   runnable_threads=tid1@prio3:pc=0x0100d8f8(ready),tid2@prio2:pc=0x0100de58(running)
        //
        // tid2 running, tid1 ready, and the driver entering tid1's frame. GT4 is genuinely
        // multi-threaded by this point -- W8's fix is what let the worker survive at all -- and
        // the driver was reading the wrong thread's program counter the whole time.
        //
        // So the frame is re-read from the scheduler EVERY iteration, not bound once. A reference
        // captured outside the loop would be a stale frame wearing the right name, which is worse
        // than the bug.
        R5900Context *const schedulerFrame = runtime.eeScheduler().currentContext();
        R5900Context &ctx = schedulerFrame != nullptr ? *schedulerFrame : runtime.cpu();

        // ---- G1.8g: is the guest actually able to run?
        //
        // This loop advances PS2Runtime::m_cpuContext, which is the MAIN thread's frame. If that
        // thread has parked itself -- sce_SleepThread, a semaphore wait, an event-flag wait, a
        // vsync wait -- then entering the guest here would run a frame the scheduler has
        // explicitly decided must not run, and every count below would be a fiction. Before
        // G1.8f's honest detector this loop could not tell; now the scheduler can, and it says
        // so.
        //
        // The other half of the check: if the main thread is parked AND nothing else is runnable,
        // the guest is genuinely blocked. On a console an interrupt or a wakeup would break that
        // open. Here the run has to stop and say so, because the alternative -- re-entering the
        // parked frame -- is the exact thing that made sce_SleepThread a no-op (W8).
        if (!runtime.eeScheduler().canDispatchGuest())
        {
            haltReason = kHaltGuestBlocked;
            haltPc = ctx.pc;
            const GuestThread *blocked = runtime.eeScheduler().thread(EeScheduler::kMainThreadId);
            std::string reason = "none";
            if (blocked)
            {
                switch (blocked->wait.reason)
                {
                case EeWaitReason::Sleep:      reason = "Sleep"; break;
                case EeWaitReason::Semaphore:  reason = "Semaphore"; break;
                case EeWaitReason::EventFlag:  reason = "EventFlag"; break;
                case EeWaitReason::VSync:      reason = "VSync"; break;
                case EeWaitReason::External:   reason = "External"; break;
                case EeWaitReason::Mpeg:       reason = "Mpeg"; break;
                case EeWaitReason::None:       reason = "none"; break;
                }
            }
            const EeKernelSnapshot snap = runtime.eeScheduler().snapshot();
            std::string runnable;
            for (const EeThreadSnapshot &thread : snap.threads)
            {
                if (thread.status == EeThreadStatus::Ready
                    || thread.status == EeThreadStatus::Running)
                {
                    runnable += runnable.empty() ? "" : " ";
                    runnable += "tid=" + std::to_string(thread.id) + "@pc=" + toHex(thread.pc);
                }
            }
            haltDetail = "the main thread is parked in " + reason
                + " waiting on " + (blocked ? waitObjectName(*blocked) : std::string("none"))
                + ", and no other thread is runnable, so the guest cannot make progress without an "
                  "interrupt or a wakeup we do not yet deliver. Runnable frames: "
                + (runnable.empty() ? std::string("none") : runnable);
            break;
        }

        // ---- wall clock
        const auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedSeconds >= budget.maxSeconds)
        {
            haltReason = kHaltDeadline;
            haltPc = ctx.pc;
            haltDetail = "elapsed=" + std::to_string(elapsedSeconds) + "s";
            break;
        }

        // ---- entry budget
        if (functionsEntered >= budget.maxEntries)
        {
            haltReason = kHaltEntryBudget;
            haltPc = ctx.pc;
            haltDetail = "entered=" + std::to_string(functionsEntered);
            break;
        }

        // ---- spin detector: the same PC returned to over and over with no forward progress.
        // GT4 uses a five-NOP backward branch as a failure trap; this is what names it.
        if (ctx.pc == previousPc)
        {
            ++repeatedPc;
            if (repeatedPc >= budget.maxRepeatedPc)
            {
                haltReason = kHaltSpinTrap;
                haltPc = ctx.pc;
                haltDetail = "same_pc_repeated=" + std::to_string(repeatedPc)
                    + " (guest is in a loop and not making progress)";
                break;
            }
        }
        else
        {
            previousPc = ctx.pc;
            repeatedPc = 0;
        }

        // ---- progress detection
        {
            // Forward motion the PC stream cannot show: entering code this guest has not run
            // before, or touching hardware it has not touched before. Re-polling a register it
            // already polls deliberately does NOT count, so a guest genuinely waiting on hardware
            // is still caught.
            // G1.8d FIX. This used to be:
            //     reachedNewCode = (functionsEntered != progressDistinctPcMark) || (mmio != mark);
            //     progressDistinctPcMark = functionsEntered;
            // which is true on EVERY observation, because the mark is assigned the value it is
            // compared against one line earlier and functionsEntered has advanced by then.
            // reachedNewCode was therefore a constant `true`, PS2GuestProgress::observe() reset
            // m_repeats on every single PC, and the cycle detector was DEAD CODE -- it could not
            // fire no matter what the guest did. Measured: a 20,000,000-entry boot with distinct_pcs
            // frozen at 112 never produced one VULCAN4 CYCLE line.
            //
            // "Reached code it has not run before" means the DISTINCT PC count grew, which this
            // harness already tracks. Touching new hardware still counts, as before.
            const size_t mmioAddresses = runtime.memory().mmioCounts().size();
            const bool reachedNewCode = (distinctPcs != progressDistinctPcMark)
                || (mmioAddresses != progressMmioMark);
            progressDistinctPcMark = distinctPcs;
            progressMmioMark = mmioAddresses;

            if (progress.observe(ctx.pc, reachedNewCode))
            {
                haltReason = kHaltCycleNoProgress;
                haltPc = ctx.pc;
                // NAME IT. Decode the cycle before stopping, so the report carries the
                // instructions and any polled address rather than a bare count.
                const std::vector<uint32_t> cycle = nameCycle("CYCLE", ctx.pc, ctx);
                std::string decoded;
                for (uint32_t c : cycle)
                {
                    decoded += toHex(c) + " ";
                }
                haltDetail = "guest is cycling through " + std::to_string(progress.hangCycleLength())
                    + " addresses and has reached no new code or hardware in "
                    + std::to_string(progress.hangRepeats()) + " repeats: [" + decoded
                    + "] -- decoded on the VULCAN4 CYCLELOOP lines above"
                    + (progress.hangFirstPc() != ctx.pc
                           ? " (first address of the cycle " + toHex(progress.hangFirstPc()) + ")"
                           : "");
                break;
            }
        }

        // ---- resolve the current PC to a generated function
        if ((ctx.pc & 3u) != 0u || ctx.pc < tableBase || ctx.pc >= tableEnd)
        {
            haltReason = kHaltOutOfTable;
            haltPc = ctx.pc;
            haltDetail = "pc is outside the generated function table";
            break;
        }

        const uint32_t slot = (ctx.pc - tableBase) >> 2;
        if (slot >= tableSlots || g_ps2RecompiledFunctionTable[slot] == nullptr)
        {
            // NAMED, NEVER SILENT. Record the address, the analyzer's name if it has one, and
            // stop here rather than jumping into nothing.
            GuestCall &call = missing[ctx.pc];
            if (call.name.empty())
            {
                const auto known = sdkNames.find(ctx.pc);
                if (known != sdkNames.end())
                {
                    call.name = known->second;
                    call.kind = "sce_sdk";
                }
                else
                {
                    call.name = "unrecompiled";
                    call.kind = "no_generated_function";
                }
                call.address = ctx.pc;
            }
            ++call.count;

            haltReason = kHaltMissingFunction;
            haltPc = ctx.pc;
            haltDetail = "no generated function at this pc";
            break;
        }

        // ---- enter the guest function. It runs inline until it yields at a guest branch.
        if (functionNames.find(ctx.pc) == functionNames.end())
        {
            std::ostringstream name;
            name << "func_" << toHex(ctx.pc);
            functionNames.emplace(ctx.pc, name.str());
            pcOrder.push_back(ctx.pc);
            ++distinctPcs;
        }

        ++functionsEntered;
        ++pcEntryCounts[ctx.pc];
        entryCallers[ctx.pc].insert(getRegU32(&ctx, 31));

        if (traceAll || (traceFirst && (functionsEntered <= traceFirst || functionsEntered % 100000 == 0)))
        {
            std::cout << "VULCAN4 TRACE entry=" << functionsEntered << " pc=" << toHex(ctx.pc)
                      << " sp=" << toHex(static_cast<uint32_t>(_mm_extract_epi32(ctx.r[29], 0)))
                      << " ra=" << toHex(static_cast<uint32_t>(_mm_extract_epi32(ctx.r[31], 0)))
                      << std::endl;
        }
        // ps2xRuntime implements the EE's cooperative threads with C++ exceptions:
        // EeDispatcherTransfer is documented in runtime/ee_scheduler.h as "the EE equivalent of
        // a longjmp to the dispatcher. It is not an error and must only be caught at
        // EeScheduler::run()." Upstream's EeScheduler::run() is the dispatcher; in this harness
        // the loop below is. So catching it here and re-entering at ctx->pc is the faithful
        // translation, not a swallowed error. It is counted and reported so its volume is
        // visible rather than invisible.
        //
        // G1.8b: catching it was only HALF the contract, and the half that was missing. The signal
        // means the guest queued an invocation (a syscall override, an interrupt callback) and is
        // now mid-frame with its live registers held by the scheduler. Re-entering the guest
        // WITHOUT letting the scheduler run that invocation is what cost GT4 its convergence loop:
        // measured 2026-09-30, the loop's $s2/$s3 were reset to zero every third iteration, and
        // once the stranded invocation existed, hasInvocation() latched true so every later 0x83
        // silently fell through to our builtin instead of the guest's own code.
        //
        // So the obligation is discharged here: hand the signal to the scheduler, which runs the
        // invocation to completion and copies the main thread's context back into runtime.cpu() --
        // the same object this loop reads ctx.pc from.
        //
        // G1.8g: the RESULT is recorded, not just the fact of the call. "Ran" means the guest
        // moved. "Blocked" means the scheduler has nothing runnable and this frame must not be
        // re-entered -- which the top-of-loop check then names as guest_blocked instead of letting
        // the run grind on a parked thread.
        try
        {
            g_ps2RecompiledFunctionTable[slot](rdram, &ctx, &runtime);
        }
        catch (const EeDispatcherTransfer &)
        {
            ++dispatcherTransfers;
            const EeServiceResult serviced = runtime.eeScheduler().serviceInvocations();
            ++servicedInvocations;
            if (serviced == EeServiceResult::Ran || serviced == EeServiceResult::RanNothing)
            {
                ++servicedWithProgress;
            }
            else if (serviced == EeServiceResult::Blocked)
            {
                ++blockedOnServicing;
            }
        }

        // ---- G1.8h / W12: a YIELD is not a TRANSFER, and it still owes the scheduler a turn.
        //
        // The catch above is the ONLY path that called serviceInvocations(), and it fires only
        // when the guest queued an invocation. But dispatchGuestBranch() has a second way out: when
        // checkpointDue() says EE time or pending work needs attention, it returns FALSE, the
        // generated function does `return`, and NO exception is thrown. The guest has yielded --
        // it is mid-frame with its registers live and it has stopped on purpose -- and this loop
        // simply carried on into it again.
        //
        // That is why VBlank never happened. VBlank is delivered by processPendingEvents(), which
        // only serviceInvocations() calls, so on a yield-only run the event queue was never
        // drained and time never became events. Measured on SCUS_973.28: 20,000,000 guest entries
        // with dispatcher_transfers=16 -- sixteen services against twenty million entries -- and
        // vsync_tick=1 after 54 seconds, with ee_cycle=98,464,696 sitting ten VBlank periods past
        // next_event_cycle=9,830,598. GT4 waits for the next frame in its idle loop, the frame
        // never arrives, and it spins: 798,251 calls to sce_ChangeThreadPriority, 1,064,347 to
        // syscall 0x2f, against only 223 distinct guest PCs.
        //
        // So the driver asks the scheduler whether it is owed a turn after EVERY guest return, not
        // only after a transfer. checkpointDue(0) is the query the generated code already uses at
        // every guest branch, and 0 cycles makes it a pure question: accountCycles() charges
        // nothing and mutates no clock, so this cannot skew the EE timeline it is asking about.
        if (runtime.eeScheduler().checkpointDue(0u))
        {
            const EeServiceResult serviced = runtime.eeScheduler().serviceInvocations();
            ++servicedInvocations;
            ++checkpointServiced;
            if (serviced == EeServiceResult::Ran || serviced == EeServiceResult::RanNothing)
            {
                ++servicedWithProgress;
            }
            else if (serviced == EeServiceResult::Blocked)
            {
                ++blockedOnServicing;
            }
        }

        if (runtime.isStopRequested())
        {
            // The watchdog sets this when the deadline passes. Distinguish it from anything
            // else that might request a stop, so the report never overstates progress.
            if (watchdogFired.load())
            {
                // Turn "it timed out" into "it is waiting on THIS". If the runtime says the
                // guest is inside a syscall, that syscall is the wall, and we name it.
                const uint32_t active = runtime.activeSyscallId();
                if (active != PS2Runtime::kNoActiveSyscall)
                {
                    // G1.5. "stuck_in_syscall" says the guest is in a syscall but not WHICH one,
                    // or whether it is making any progress through it. On GT4 that turned out to
                    // be the whole story: sce_FindAddress (0x83) was called 155 times, each call
                    // brute-force scanning ~112,000 words of RDRAM for a function pointer that
                    // nothing had ever written, and returning 0 every time. A generic name sent
                    // G1.4 looking for a dispatch bug that did not exist. So when a single
                    // syscall dominates the call tally AND we are deadlocked inside it, name the
                    // syscall instead of the category.
                    uint64_t activeCalls = 0;
                    uint32_t dominantId = 0;
                    uint64_t dominantCalls = 0;
                    for (const auto &entry : runtime.syscallCounts())
                    {
                        activeCalls += entry.second.count;
                        if (entry.second.count > dominantCalls)
                        {
                            dominantCalls = entry.second.count;
                            dominantId = entry.first;
                        }
                    }
                    const uint64_t totalCalls = runtime.syscallCallCount();

                    if (active == dominantId && totalCalls > 0
                        && dominantCalls * 2u > totalCalls)
                    {
                        haltReason = kHaltStalledInSyscall;
                        haltDetail = std::string("livelocked in SCE syscall ") + toHex(active, 2)
                            + " (" + syscallName(active) + "): " + std::to_string(dominantCalls)
                            + " of " + std::to_string(totalCalls)
                            + " guest syscalls were this one, at guest pc " + toHex(ctx.pc)
                            + " -- it is being retried, not satisfied, so the wall is the "
                              "return value it is not getting, not the call itself";
                    }
                    else
                    {
                        haltReason = kHaltInSyscall;
                        haltDetail = std::string("blocked inside SCE syscall ") + toHex(active, 2)
                            + " (" + syscallName(active) + "), guest pc " + toHex(ctx.pc)
                            + " -- this syscall is the wall";
                    }
                }
                else
                {
                    // G1.7: do not stop at "spinning". Name the wait.
                    //
                    // Find the repeating block from the PC history, then decode every instruction
                    // in it. Any LOAD's effective address is what the guest is polling; the value
                    // in the register right now is what it got. That turns a symptom into a
                    // dependency, with an address, which is the whole point of this dish.
                    {
                        std::vector<uint32_t> loop;
                        if (!progress.history().empty())
                        {
                            // Walk back to the previous occurrence of the current PC: everything
                            // after it is one pass of the loop.
                            size_t start = progress.history().size();
                            for (size_t i = progress.history().size(); i-- > 0;)
                            {
                                if (progress.history()[i] == ctx.pc)
                                {
                                    start = i;
                                    break;
                                }
                            }
                            for (size_t i = start; i < progress.history().size() && loop.size() < 64u; ++i)
                            {
                                if (std::find(loop.begin(), loop.end(), progress.history()[i]) == loop.end())
                                {
                                    loop.push_back(progress.history()[i]);
                                }
                            }
                        }

                        std::cout << "VULCAN4 WAIT entry_pc=0x" << std::hex << ctx.pc << std::dec
                                  << " block_instructions=" << loop.size()
                                  << " history_len=" << progress.history().size()
                                  << " distinct_pcs_total=" << distinctPcs << std::endl;

                        uint32_t loadCount = 0;
                        uint32_t branchCount = 0;
                        for (uint32_t pc : loop)
                        {
                            // Guest vaddr -> RDRAM offset. The image is loaded at the PS2 user
                            // segment base 0x01000000, so the offset is the low 24 bits.
                            const uint32_t ramOffset = pc & 0x00FFFFFFu;
                            if (ramOffset + 4u > PS2_RAM_SIZE)
                            {
                                continue;
                            }
                            uint32_t insn = 0;
                            std::memcpy(&insn, rdram + ramOffset, sizeof(insn));
                            const char *mn = mipsMnemonic(insn);
                            const uint32_t op = insn >> 26;
                            const bool isBranch = (op == 0x04 || op == 0x05 || op == 0x06 || op == 0x07
                                                   || (op == 0 && (insn & 0x3F) == 0x08));
                            if (isBranch)
                            {
                                ++branchCount;
                            }
                            std::cout << "VULCAN4 WAITLOOP pc=0x" << std::hex << pc << std::dec
                                      << " insn=0x" << std::hex << insn << std::dec << " " << mn;
                            uint32_t rt = 0, imm = 0;
                            int width = 0;
                            bool isSigned = false;
                            if (decodeGpuLoad(insn, rt, imm, width, isSigned))
                            {
                                const uint32_t base = _mm_extract_epi32(ctx.r[(insn >> 21) & 0x1F], 0);
                                const uint32_t addr = base + imm;
                                uint32_t value = 0;
                                std::memcpy(&value, rdram + (addr & 0x00FFFFFFu), sizeof(value));
                                std::cout << "  -> POLLS addr=0x" << std::hex << addr << std::dec
                                          << " (base=r" << ((insn >> 21) & 0x1F) << " 0x" << std::hex
                                          << base << std::dec << " + 0x" << std::hex << imm << std::dec
                                          << ") width=" << width << " rt=r" << rt
                                          << " value=0x" << std::hex << value << std::dec;
                                ++loadCount;
                            }
                            if (isBranch)
                            {
                                std::cout << "  [branch]";
                            }
                            std::cout << std::endl;
                        }
                        std::cout << "VULCAN4 WAITSUM loads=" << loadCount
                                  << " branches=" << branchCount
                                  << " (a loop that loads is polling something; a loop with no"
                                     " loads is pure arithmetic and cannot be waiting on hardware)"
                                  << std::endl;

                        haltReason = kHaltSpinningInGuest;
                        haltDetail = "no syscall executing at the deadline; guest is waiting on "
                                     "the loop decoded above (see VULCAN4 WAIT lines) at pc "
                                     + toHex(ctx.pc);
                    }
                }
            }
            else
            {
                haltReason = kHaltReturnedToEntry;
                haltDetail = "runtime requested stop";
            }
            haltPc = ctx.pc;
            break;
        }
    }

    // Measured BEFORE the join, because the gap between this and elapsed is the harness
    // waiting on something of its own making, and a report that only prints the total invites
    // exactly the misreading that cost this project two dishes (W7's detector, W8's watchdog).
    const auto guestElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - start)
                                  .count();

    driverFinished.store(true, std::memory_order_release);
    runtime.requestStop(); // tell the watchdog thread to exit its wait loop
    watchdog.join();

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();

    // ------------------------------------------------------------------ W7 value scan
    //
    // Seven measurement passes established that W7's copy loop is innocent and that its END POINTER
    // is $fp, whose callee-saved stack slot already holds 0x01051A3F when sub_0100EDC8 is entered.
    // The writer is therefore upstream of every function looked at so far. Walking forward one
    // function per pass is how seven passes produced no writer, so this asks the question that would
    // have answered it in one: WHERE do the start and the end value first appear in RDRAM?
    //
    // Printed only when the guest actually stalled with the inverted range, so it costs nothing on a
    // run that gets past it. Both values are reported with their address, and adjacency is the thing
    // to look for: 0x01051A3F is exactly 0x01051A40 - 1, so if they sit next to each other in one
    // table, that table is the producer and the loop is only a symptom of reading it.
    {
        constexpr uint32_t kW7Start = 0x01051A40u;
        constexpr uint32_t kW7End = 0x01051A3Fu;
        constexpr int kMaxHits = 48;
        int startHits = 0;
        int endHits = 0;
        for (uint32_t off = 0; off + 4u <= PS2_RAM_SIZE; off += 4u)
        {
            uint32_t word = 0u;
            std::memcpy(&word, rdram + off, sizeof(word));
            if (word == kW7Start)
            {
                if (startHits < kMaxHits)
                {
                    std::cout << "VULCAN4 W7SCAN start 0x" << std::hex << word << " at 0x" << off
                              << std::dec << "\n";
                }
                ++startHits;
            }
            else if (word == kW7End)
            {
                if (endHits < kMaxHits)
                {
                    std::cout << "VULCAN4 W7SCAN end   0x" << std::hex << word << " at 0x" << off
                              << std::dec << "\n";
                }
                ++endHits;
            }
        }
        std::cout << "VULCAN4 W7SCAN total start=" << startHits << " end=" << endHits
                  << " (capped at " << kMaxHits << " printed each)\n";

        // W7 is a LIST WALK, not a buffer copy. 0x01051A40 is a node read as
        //   0x1000ae0  lw $v1, 0x7A8C($v0)   ; head
        //   0x1000ae4  lw $a0, 0x18($v1)    ; head->next   <- the walk
        //   0x1000aec  ld $s0, 0x20($a0)    ; next->data
        // and 0x01051A3F is head-1, which no well-formed list contains. Dump the head
        // node's fields so the shape is measured rather than assumed: a next pointer of
        // 0 or of head-1 is a terminated-or-broken list, and anything else means the
        // walk is somewhere else entirely.
        {
            constexpr uint32_t kHead = 0x01051A40u;
            auto word = [&](uint32_t a) {
                uint32_t w = 0u;
                if (a + 4u <= PS2_RAM_SIZE)
                {
                    std::memcpy(&w, rdram + a, sizeof(w));
                }
                return w;
            };
            std::cout << "VULCAN4 W7NODE head=0x" << std::hex << kHead << std::dec
                      << " [+0x0]=0x" << std::hex << word(kHead + 0x0)
                      << " [+0x4]=0x" << word(kHead + 0x4)
                      << " [+0x8]=0x" << word(kHead + 0x8)
                      << " [+0xC]=0x" << word(kHead + 0xC)
                      << " [+0x10]=0x" << word(kHead + 0x10)
                      << " [+0x14]=0x" << word(kHead + 0x14)
                      << " [+0x18]=0x" << word(kHead + 0x18) << " <-next"
                      << " [+0x1C]=0x" << word(kHead + 0x1C)
                      << " [+0x20]=0x" << word(kHead + 0x20)
                      << " [+0x24]=0x" << word(kHead + 0x24)
                      << std::dec << "\n";
            // follow up to 8 links, so a corrupt chain is visible in one line
            uint32_t node = word(kHead + 0x18);
            std::cout << "VULCAN4 W7WALK";
            for (int hop = 0; hop < 8 && node != 0u; ++hop)
            {
                std::cout << " ->0x" << std::hex << node << std::dec;
                node = word(node + 0x18);
            }
            std::cout << "\n";
        }

        // The one non-stack home of the start value is a live BSS variable. Its neighbours are
        // where the matching END should be, so print them: if the buffer is described by a pair,
        // the pair is visible here, and if the end is absent here it was never computed.
        {
            constexpr uint32_t kHome = 0x01047A8Cu;
            std::cout << "VULCAN4 W7NEIGH around 0x" << std::hex << kHome << std::dec << ":";
            for (int32_t d = -32; d <= 32; d += 4)
            {
                const uint32_t addr = static_cast<uint32_t>(static_cast<int32_t>(kHome) + d);
                if (addr + 4u > PS2_RAM_SIZE)
                {
                    continue;
                }
                uint32_t word = 0u;
                std::memcpy(&word, rdram + addr, sizeof(word));
                std::cout << " [" << std::hex << addr << "]=0x" << word << std::dec;
            }
            std::cout << "\n";
        }
    }

    // ------------------------------------------------------------------ syscall table probe
    //
    // GT4 registers two of its own syscall handlers (0x83 and 0x5A) and then scans low RDRAM for
    // them, because the console's real syscall dispatch table is a flat array of 32-bit function
    // pointers indexed by syscall number at 0x80011F80. Two slots 164 bytes apart is the proof
    // that both registrations landed. Print the two slots at the deadline, whatever the halt was.
    //
    // G1.8: this probe is what showed the map bug. Before the fix it read `handler=0x0` for 0x83
    // (the guest's override table was invisible because .data had been biased 16MB down) and after
    // it reads the guest's own handlers back at both slots. It is kept because the guest's
    // convergence loop depends on these two words and nothing else reports them.
    {
        constexpr uint32_t kTableBase = 0x00011F80u;
        for (const uint32_t num : {0x83u, 0x5Au})
        {
            const uint32_t slot = kTableBase + num * 4u;
            uint32_t word = 0u;
            if (slot + 4u <= PS2_RAM_SIZE)
            {
                std::memcpy(&word, rdram + slot, sizeof(word));
            }
            std::cout << "VULCAN4 SYSTABLE n=0x" << std::hex << num << " slot=0x" << slot
                      << " handler=0x" << word << std::dec << "\n";
        }
    }

    // ------------------------------------------------------------------ the report
    //
    // Machine-readable, exactly one line, exactly this shape. The gate greps for it.
    // The scheduler snapshot is taken ONCE here and used for the clock, the next deadline and the
    // runnable set, so the report cannot show three different instants of the same run.
    const EeKernelSnapshot kernelSnapshot = runtime.eeScheduler().snapshot();
    std::string runnableThreadNames;
    // G1.8h. Invocation depth per thread, ALWAYS in the report.
    //
    // W10 turned out to be a stranded SyscallOverride invocation: hasInvocation() then latches and
    // every later call of that syscall falls through to our builtin instead of the guest's own
    // handler, so the guest's code stops running without anything saying so. A non-zero depth at
    // the end of a run is the signature, and it costs one integer per thread to print -- which is
    // what it should have cost from the start. A latch that cannot be SEEN in the report is a
    // latch that costs a day.
    // W16: "tid2:status=2" named a symptom -- a thread is Waiting and never runs -- without saying
    // what it is Waiting FOR. The snapshot already carries waitReason, waitId and wakeupCount, so
    // the report now prints them. A blocked thread whose reason and id are named is a diagnosis; a
    // blocked thread whose reason is hidden is another day of guessing.
    const auto waitReasonName = [](EeWaitReason reason) -> const char * {
        switch (reason)
        {
        case EeWaitReason::None:
            return "none";
        case EeWaitReason::Sleep:
            return "sleep";
        case EeWaitReason::Semaphore:
            return "sema";
        case EeWaitReason::EventFlag:
            return "eventflag";
        case EeWaitReason::VSync:
            return "vsync";
        case EeWaitReason::External:
            return "external";
        case EeWaitReason::Mpeg:
            return "mpeg";
        }
        return "?";
    };

    std::string threadState;
    for (const EeThreadSnapshot &thread : kernelSnapshot.threads)
    {
        if (!threadState.empty())
        {
            threadState += " ";
        }
        threadState += "tid" + std::to_string(thread.id) + ":status="
            + std::to_string(static_cast<int>(thread.status)) + ":wait="
            + waitReasonName(thread.waitReason) + "#" + std::to_string(thread.waitId) + ":woken="
            + std::to_string(thread.wakeupCount) + ":pc=" + toHex(thread.pc) + ":ra="
            + toHex(thread.ra) + ":invocations=" + std::to_string(thread.invocationDepth);
    }
    for (const EeThreadSnapshot &thread : kernelSnapshot.threads)
    {
        if (thread.status == EeThreadStatus::Ready || thread.status == EeThreadStatus::Running)
        {
            if (!runnableThreadNames.empty())
            {
                runnableThreadNames += ",";
            }
            runnableThreadNames +=
                "tid" + std::to_string(thread.id) + "@prio" + std::to_string(thread.currentPriority)
                + ":pc=" + toHex(thread.pc) + (thread.status == EeThreadStatus::Running ? "(running)"
                                                                                        : "(ready)");
        }
    }
    std::cout << "VULCAN4 BOOT REPORT functions_entered=" << functionsEntered << " halt=" << haltReason
              << " bios_files=" << biosFilesOpened << "\n";

    // W16: where the guest actually spent its life. Top 24 by entry count, with the share of all
    // entries, so a loop that owns 99% of the run cannot hide behind a function that owns 0.1%.
    {
        std::vector<std::pair<uint32_t, uint64_t>> ranked(pcEntryCounts.begin(), pcEntryCounts.end());
        std::sort(ranked.begin(), ranked.end(),
                  [](const auto &left, const auto &right) { return left.second > right.second; });
        // W17: print EVERY distinct PC, not just a top slice. The boot settles at ~113 distinct
        // addresses, so the whole distribution is a couple of kilobytes -- and printing all of it is
        // what settles an argument the top-24 could not: sce_ChangeThreadPriority tallies 2.24M while
        // the whole run enters 2.04M functions, so either the syscall tally counts resumes as well
        // as guest calls, or the shim arrivals have to be somewhere else in the table. Reading the
        // shim's own arrival count out of the full list answers it; guessing from a slice does not.
        // W27. The guest's call graph, hottest edges first: caller -> callee x count. The only complete view
    // of guest control flow, because it covers the transfers the arrival census cannot see.
    {
        std::vector<PS2Runtime::BranchEdge> edges(runtime.branchEdges().begin(),
                                                   runtime.branchEdges().end());
        std::sort(edges.begin(), edges.end(),
                  [](const PS2Runtime::BranchEdge &l, const PS2Runtime::BranchEdge &r)
                  { return l.count > r.count; });
        std::cout << "VULCAN4 CALL GRAPH distinct_edges=" << edges.size() << " hottest:\n";
        const std::size_t limit = std::min<std::size_t>(28, edges.size());
        for (std::size_t i = 0; i < limit; ++i)
        {
            std::cout << "    " << toHex(edges[i].source) << " -> " << toHex(edges[i].target) << " x"
                      << edges[i].count << "\n";
        }
    }

// W27. Function entries made BY dispatchGuestBranch, by target PC. The third population, and the
    // decisive one: dispatchGuestBranch ends with targetFn(rdram, ctx, this), so every inter-function
    // transfer re-enters its target from inside the branch dispatcher -- invisible to both the
    // arrival census and the scheduler's step counter. A target with no `case` label in the generated
    // switch silently restarts that function from its entry instead of resuming it.
    {
        const auto &byTarget = runtime.branchTargetEntries();
        std::vector<std::pair<uint32_t, uint64_t>> targets(byTarget.begin(), byTarget.end());
        std::sort(targets.begin(), targets.end(),
                  [](const std::pair<uint32_t, uint64_t> &l, const std::pair<uint32_t, uint64_t> &r)
                  { return l.second > r.second; });
        std::cout << "VULCAN4 BRANCH ENTRIES distinct_targets=" << targets.size() << " top:";
        const std::size_t limit = std::min<std::size_t>(20, targets.size());
        for (std::size_t i = 0; i < limit; ++i)
        {
            char buf[96];
            std::snprintf(buf, sizeof(buf), " %s=%llu", toHex(targets[i].first).c_str(),
                          static_cast<unsigned long long>(targets[i].second));
            std::cout << buf;
        }
        std::cout << "\n";
    }

// W26. Guest steps the SCHEDULER ran, by the PC it re-entered at. This is the population the
    // arrival histogram structurally cannot see -- serviceInvocations() calls the generated function
    // directly -- and it is where a loop iterating behind a dispatchGuestBranch yield actually lives.
    {
        std::vector<std::pair<uint32_t, uint64_t>> steps(kernelSnapshot.schedulerEntries.begin(),
                                                          kernelSnapshot.schedulerEntries.end());
        std::sort(steps.begin(), steps.end(),
                  [](const std::pair<uint32_t, uint64_t> &l, const std::pair<uint32_t, uint64_t> &r)
                  { return l.second > r.second; });
        std::cout << "VULCAN4 SCHED STEPS total=" << kernelSnapshot.schedulerSteps
                  << " distinct_pcs=" << steps.size() << " top:";
        const std::size_t stepLimit = std::min<std::size_t>(20, steps.size());
        for (std::size_t i = 0; i < stepLimit; ++i)
        {
            char buf[96];
            std::snprintf(buf, sizeof(buf), " %s=%llu", toHex(steps[i].first).c_str(),
                          static_cast<unsigned long long>(steps[i].second));
            std::cout << buf;
        }
        std::cout << "\n";
    }

// W26. Guest control transfers BY SITE, biggest first. This is the first line that can see a loop
    // iterating internally: the arrival histogram cannot, because guest code that runs behind a
    // dispatchGuestBranch yield never passes through the harness's arrival loop.
    {
        const auto &bySite = runtime.branchDispatchCounts();
        std::vector<std::pair<uint32_t, uint64_t>> transfers(bySite.begin(), bySite.end());
        std::sort(transfers.begin(), transfers.end(),
                  [](const std::pair<uint32_t, uint64_t> &l, const std::pair<uint32_t, uint64_t> &r)
                  { return l.second > r.second; });
        std::cout << "VULCAN4 XFER SITES distinct=" << transfers.size() << " total=";
        uint64_t xferTotal = 0;
        for (const auto &entry : transfers)
        {
            xferTotal += entry.second;
        }
        std::cout << xferTotal << " top:";
        const std::size_t xferLimit = std::min<std::size_t>(24, transfers.size());
        for (std::size_t i = 0; i < xferLimit; ++i)
        {
            const double share = xferTotal == 0
                                     ? 0.0
                                     : 100.0 * static_cast<double>(transfers[i].second) /
                                           static_cast<double>(xferTotal);
            char buf[96];
            std::snprintf(buf, sizeof(buf), " %s=%llu(%.2f%%)", toHex(transfers[i].first).c_str(),
                          static_cast<unsigned long long>(transfers[i].second), share);
            std::cout << buf;
        }
        std::cout << "\n";
    }

// W22. Who calls the functions the spin runs through. General, not hardcoded: any function's
    // entry PC with its distinct return addresses. This is what names the caller of a function the
    // way the syscall site sets name the issuer of a syscall.
    {
        std::vector<std::pair<uint32_t, std::unordered_set<uint32_t>>> callers(entryCallers.begin(),
                                                                              entryCallers.end());
        // Hottest first, not most-callered-first: the functions worth naming are the ones the guest
        // actually spends its life in, and a single-caller hot function is exactly the thing a
        // "more than one caller" filter throws away.
        std::sort(callers.begin(), callers.end(),
                  [&pcEntryCounts](const std::pair<uint32_t, std::unordered_set<uint32_t>> &l,
                         const std::pair<uint32_t, std::unordered_set<uint32_t>> &r)
                  {
                      const auto li = pcEntryCounts.find(l.first);
                      const auto ri = pcEntryCounts.find(r.first);
                      const uint64_t lc = li == pcEntryCounts.end() ? 0u : li->second;
                      const uint64_t rc = ri == pcEntryCounts.end() ? 0u : ri->second;
                      if (lc != rc)
                      {
                          return lc > rc;
                      }
                      return l.second.size() > r.second.size();
                  });
        std::cout << "VULCAN4 CALLER MAP functions=" << callers.size() << "\n";
        std::size_t printed = 0;
        for (const auto &[fn, ras] : callers)
        {
            // Only multi-caller or interesting entries, and never more than 32 lines: this is a map
            // to consult, not a log to read.
            if (printed >= 40u)
            {
                continue;
            }
            std::vector<uint32_t> sorted(ras.begin(), ras.end());
            std::sort(sorted.begin(), sorted.end());
            const auto hit = pcEntryCounts.find(fn);
            std::cout << "  VULCAN4 CALLER fn=" << toHex(fn) << " entries="
                      << (hit == pcEntryCounts.end() ? 0u : hit->second) << " from=" << sorted.size()
                      << "ra[";
            const std::size_t limit = std::min<std::size_t>(sorted.size(), 8);
            for (std::size_t i = 0; i < limit; ++i)
            {
                std::cout << (i == 0 ? "" : ",") << toHex(sorted[i]);
            }
            if (sorted.size() > limit)
            {
                std::cout << ",+" << (sorted.size() - limit);
            }
            std::cout << "]\n";
            ++printed;
        }
    }

    std::cout << "VULCAN4 PC HISTOGRAM distinct=" << ranked.size() << " top:";
        const std::size_t limit = std::min<std::size_t>(256, ranked.size());
        for (std::size_t i = 0; i < limit; ++i)
        {
            const double share =
                functionsEntered == 0 ? 0.0
                                     : 100.0 * static_cast<double>(ranked[i].second) /
                                           static_cast<double>(functionsEntered);
            char buf[96];
            std::snprintf(buf, sizeof(buf), " %s=%llu(%.2f%%)", toHex(ranked[i].first).c_str(),
                          static_cast<unsigned long long>(ranked[i].second), share);
            std::cout << buf;
        }
std::cout << "\n";
        }

        // W17: the W16b histogram put ~34% of every guest entry into func_101D470, which is one
        // indirect call through *(0x1034EC0), and its target is func_101E6B8 -- a callback enqueue
        // + dispatch on the global at 0x1035270 (head at +0x148, count at +0x4, 4-byte entries from
        // +0x8). Two things must be measured, not inferred:
        //   1. is the queue growing? A count that climbs and never returns to its floor means the
        //      guest enqueues faster than it drains, and the loop is the SYMPTOM of that -- which
        //      would also explain why tid1 never reaches the code that wakes tid2.
        //   2. what does *(0x1034EC0) hold at RUNTIME? The ELF says 0x1019be8 at load time, but the
        //      guest may have reassigned it, and a pointer to something that returns without doing
        //      the work would spin inside a no-op.
        // Printed only at the halt, once, so it costs nothing on a run that gets past this.
        {
            auto qword = [&](uint32_t a) {
                uint32_t w = 0u;
                if (a + 4u <= PS2_RAM_SIZE)
                {
                    std::memcpy(&w, rdram + a, sizeof(w));
                }
                return w;
            };
            const uint32_t fnPtr = qword(0x01034EC0u);
            const uint32_t owner = qword(0x01035270u);
            std::cout << "VULCAN4 W17DISPATCH fn_ptr(0x1034EC0)=0x" << std::hex << fnPtr << std::dec
                      << " elf_initial=0x1019be8"
                      << " reassigned=" << (fnPtr != 0x01019BE8u ? "YES" : "no")
                      << " owner(0x1035270)=0x" << std::hex << owner << std::dec;
            if (owner == 0u || owner + 0x14Cu > PS2_RAM_SIZE)
            {
                std::cout << " queue=UNREADABLE(owner out of RDRAM)\n";
            }
            else
            {
                const uint32_t head = qword(owner + 0x148u);
                const uint32_t count = qword(owner + 0x04u);
                const uint32_t tailCb = qword(owner + 0x3Cu);
                std::cout << " head=0x" << std::hex << head << std::dec << " count=" << std::dec
                          << count << " tail_cb=0x" << std::hex << tailCb << std::dec << "\n";
                // The queued values are what the dispatcher jalr's. If they are 0 or 1 rather than
                // code addresses, the dispatch is calling a token, not a function -- and saying so
                // here beats guessing at the register path.
                std::cout << "VULCAN4 W17QUEUE";
                if (head == 0u)
                {
                    std::cout << " empty (head=0)";
                }
                else if (head + 8u > PS2_RAM_SIZE)
                {
                    std::cout << " head=0x" << std::hex << head << std::dec << " OUT_OF_RDRAM";
                }
                else
                {
                    const uint32_t entries = qword(head + 0x04u);
                    std::cout << " blk=0x" << std::hex << head << std::dec << " blk_count=" << std::dec
                              << entries << " slots:";
                    const uint32_t limit = std::min<uint32_t>(entries, 8u);
                    for (uint32_t i = 0; i < limit; ++i)
                    {
                        const uint32_t slot = qword(head + 0x08u + i * 4u);
                        std::cout << " [" << i << "]=0x" << std::hex << slot << std::dec;
                    }
                    if (entries > limit)
                    {
                        std::cout << " ...(" << (entries - limit) << " more)";
                    }
                }
                std::cout << "\n";
            }
            // W30. The path GT4 hands sceMcOpen begins with the 2-byte token 05 80 and I am not
            // going to guess what it encodes. A SECOND example decodes it: scan RDRAM for every
            // occurrence of the same byte pair and print each with context. If the guest stores
            // other paths and they all start the same way, it is a fixed prefix; if one of them
            // has plain ASCII where this has 05 80, then 05 80 is a handle and the ASCII one is the
            // template it was built from. One example is a puzzle; two are a specification.
            {
                static const uint8_t kToken[2] = {0x05u, 0x80u};
                int tokenHits = 0;
                // Sanity first: the scan below is worthless if this block's view of RDRAM differs
                // from the stub's, and "zero hits" is exactly what a wrong view produces.
                std::cout << "VULCAN4 W30TOKEN sanity@0x1051a10:";
                for (uint32_t k = 0; k < 16u; ++k)
                {
                    char byteText[4];
                    std::snprintf(byteText, sizeof(byteText), "%02x", rdram[0x01051A10u + k]);
                    std::cout << byteText << " ";
                }
                std::cout << " ramsize=0x" << std::hex << PS2_RAM_SIZE << std::dec << " hits:";
                for (uint32_t off = 0; off + 2u + 40u <= PS2_RAM_SIZE && tokenHits < 24; ++off)
                {
                    if (rdram[off] != kToken[0] || rdram[off + 1u] != kToken[1])
                    {
                        continue;
                    }
                    // Require it to look like a path: a '/' within the next few bytes.
                    bool pathish = false;
                    for (uint32_t k = 2; k < 12u; ++k)
                    {
                        if (rdram[off + k] == '/')
                        {
                            pathish = true;
                            break;
                        }
                    }
                    if (!pathish)
                    {
                        continue;
                    }
                    ++tokenHits;
                    std::cout << "\n  0x" << std::hex << off << std::dec << " [";
                    uint32_t printed = 0;
                    for (uint32_t k = 0; k < 40u && printed < 40u; ++k, ++printed)
                    {
                        const uint8_t b = rdram[off + k];
                        if (b == 0u)
                        {
                            break;
                        }
                        char byteText[4];
                        std::snprintf(byteText, sizeof(byteText), "%02x", b);
                        std::cout << byteText << " ";
                    }
                    std::cout << "]";
                }
                std::cout << "\n";
            }

            // W22. The two objects the wait-list broadcast is walking, 844,500 passes between them,
            // named by $s0 at the syscall. sub_01011508's layout, read straight from the generated
            // unit: +0x00 self/next, +0x04 node tid, +0x0C wait-list head, +0x18 owner-self check,
            // +0x1C saved priority, +0x20 OWNER TID, +0x24 recursion depth. +0x20 and +0x0C are the
            // two words that decide whether anybody is ever going to be woken.
            for (uint32_t obj : {0x01047B4Cu, 0x01033098u})
            {
                if (obj + 0x28u > PS2_RAM_SIZE)
                {
                    std::cout << "VULCAN4 W22OBJ obj=0x" << std::hex << obj << std::dec
                              << " OUT_OF_RDRAM\n";
                    continue;
                }
                std::cout << "VULCAN4 W22OBJ obj=0x" << std::hex << obj << std::dec
                          << " +0x0(next)=0x" << std::hex << qword(obj + 0x00u) << std::dec
                          << " +0x4(tid)=" << std::dec << qword(obj + 0x04u)
                          << " +0x8=" << qword(obj + 0x08u)
                          << " +0xC(list)=0x" << std::hex << qword(obj + 0x0Cu) << std::dec
                          << " +0x18=0x" << std::hex << qword(obj + 0x18u) << std::dec
                          << " +0x1C(savedprio)=" << std::dec << qword(obj + 0x1Cu)
                          << " +0x20(OWNER)=" << qword(obj + 0x20u)
                          << " +0x24(depth)=" << qword(obj + 0x24u) << "\n";
            }
        }

    std::cout << "VULCAN4 HARNESS detail=" << haltDetail << " pc=" << toHex(haltPc)
              << " distinct_pcs=" << distinctPcs << " checkpoint_serviced=" << checkpointServiced << " dispatcher_transfers=" << dispatcherTransfers
              << " serviced_invocations=" << servicedInvocations
              << " service_frames=" << serviceFrames
              << " serviced_with_progress=" << servicedWithProgress
              << " blocked_on_servicing=" << blockedOnServicing
              // W20: guest invocations run by kind. service_frames counts how OFTEN we serviced;
              // these count what we RAN, and on GT4 they are ~221,000 against 31 VBlanks -- three
              // million syscalls executing in here, none of them visible to functions_entered.
              << " invocations_run=" << kernelSnapshot.invocationsRun
              << " inv_by_kind=[intr=" << kernelSnapshot.invocationsRunByKind[0]
              << ",dmac=" << kernelSnapshot.invocationsRunByKind[1]
              << ",override=" << kernelSnapshot.invocationsRunByKind[2]
              << ",other=" << kernelSnapshot.invocationsRunByKind[3] << "]"
              << " elapsed_ms=" << elapsed
              << " guest_phase_ms=" << guestElapsed
              << " harness_tail_ms=" << (elapsed - guestElapsed)
              // G1.8g: the EE clock, and the next thing it is waiting for. Without these two
              // numbers "the guest stopped progressing" and "the guest's clock never reached the
              // next interrupt" look identical, and they are different walls.
              << " ee_cycle=" << kernelSnapshot.eeCycle
              << " next_event_cycle=" << kernelSnapshot.nextEventCycle
              << " vsync_tick=" << runtime.eeScheduler().currentVSyncTick()
              << " runnable_threads=" << runnableThreadNames
              << " thread_state=" << threadState
              << " entry_budget=" << budget.maxEntries << " spin_limit=" << budget.maxRepeatedPc
              << " deadline_s=" << budget.maxSeconds << "\n";

    // The human list: every guest call the runtime was asked for and could not satisfy.
    // THIS IS THE NO-BIOS BACKLOG. It is the next dish's brief.
    std::cout << "VULCAN4 GUEST FUNCTIONS ENTERED first_32=";
    for (size_t i = 0; i < pcOrder.size() && i < 32; ++i)
    {
        std::cout << (i ? "," : "") << toHex(pcOrder[i]);
    }
    std::cout << "\n";

    // ------------------------------------------------------------------ guest call list
    //
    // THIS IS THE NO-BIOS BACKLOG, and it is the next dish's brief. Everything printed here is
    // read out of the runtime's own counters -- the counts are incremented where the guest
    // actually made the call, not reconstructed by the harness.
    //
    //  (a) SYSCALLS the guest issued, by number and name, from PS2Runtime::syscallCounts().
    //  (b) MMIO the guest touched, from PS2Memory's counters in translateAddress().
    //  (c) FUNCTIONS with no generated body, which is where a guest transfer would have died.
    const auto &syscalls = runtime.syscallCounts();
    const auto &mmio = runtime.memory().mmioCounts();

    std::cout << "VULCAN4 GUEST CALL LIST (what the guest asked the runtime for)\n";
    std::cout << "  VULCAN4 CALLKIND syscalls=" << syscalls.size()
              << " total_syscall_calls=" << runtime.syscallCallCount()
              << " syscall_depth=" << runtime.syscallCallDepth()
              << " max_syscall_depth=" << runtime.maxSyscallCallDepth()
              << " distinct_mmio_addresses=" << mmio.size()
              << " total_mmio_accesses=" << runtime.memory().mmioAccessCount()
              << " missing_functions=" << missing.size() << "\n";

    for (const auto &entry : syscalls)
    {
        std::cout << "  0x" << std::hex << std::setw(2) << std::setfill('0') << entry.first
                  << std::dec << std::setfill(' ') << " sce_" << syscallName(entry.first)
                  << " calls=" << entry.second.count
                  << " last_pc=" << toHex(entry.second.lastPc);

        // W18. last_pc is ONE site and it is the last one to run, which is the least useful one when
        // a syscall is issued from several places. So print every site it was entered from.
        if (!entry.second.entryPcs.empty())
        {
            std::vector<uint32_t> sites(entry.second.entryPcs.begin(), entry.second.entryPcs.end());
            std::sort(sites.begin(), sites.end());
            std::cout << " from=" << sites.size() << "pc[";
            const std::size_t limit = std::min<std::size_t>(sites.size(), 12);
            for (std::size_t i = 0; i < limit; ++i)
            {
                std::cout << (i == 0 ? "" : ",") << toHex(sites[i]);
            }
            if (sites.size() > limit)
            {
                std::cout << ",+" << (sites.size() - limit);
            }
            std::cout << "]";
        }

        // W21. $ra at each issue, COUNTED, biggest first -- which of them is the one being issued
        // millions of times. Sorted by count so the dominant caller is first, not alphabetical.
        if (!entry.second.entryRaCounts.empty())
        {
            std::vector<std::pair<uint32_t, uint64_t>> rankedRa(entry.second.entryRaCounts.begin(),
                                                                entry.second.entryRaCounts.end());
            std::sort(rankedRa.begin(), rankedRa.end(),
                      [](const std::pair<uint32_t, uint64_t> &l, const std::pair<uint32_t, uint64_t> &r)
                      { return l.second > r.second; });
            std::cout << " ra_count=";
            const std::size_t raLimit = std::min<std::size_t>(rankedRa.size(), 8);
            for (std::size_t i = 0; i < raLimit; ++i)
            {
                std::cout << (i == 0 ? "" : ",") << toHex(rankedRa[i].first) << "x" << rankedRa[i].second;
            }
            if (rankedRa.size() > raLimit)
            {
                std::cout << ",+" << (rankedRa.size() - raLimit);
            }
        }

        // W22. The arguments each site was handed, counted, biggest first. For
        // sceChangeThreadPriority $a1 IS the priority, and GT4 feeds one call's return value into
        // the next call's priority -- so a priority walking negative here is the loop.
        if (!entry.second.callSites.empty())
        {
            std::vector<std::tuple<uint32_t, uint32_t, uint64_t>> sites(entry.second.callSites.begin(),
                                                                        entry.second.callSites.end());
            std::sort(sites.begin(), sites.end(),
                      [](const std::tuple<uint32_t, uint32_t, uint64_t> &l,
                         const std::tuple<uint32_t, uint32_t, uint64_t> &r)
                      { return std::get<2>(l) > std::get<2>(r); });
            std::cout << " arg=";
            const std::size_t siteLimit = std::min<std::size_t>(sites.size(), 6);
            for (std::size_t i = 0; i < siteLimit; ++i)
            {
                std::cout << (i == 0 ? "" : ",") << toHex(std::get<0>(sites[i])) << "/a0="
                          << toHex(std::get<1>(sites[i])) << "x" << std::get<2>(sites[i]);
            }
            if (sites.size() > siteLimit)
            {
                std::cout << ",+" << (sites.size() - siteLimit);
            }
        }

        if (!entry.second.callSiteArg1.empty())
        {
            std::vector<std::tuple<uint32_t, uint32_t, uint64_t>> a1s(entry.second.callSiteArg1.begin(),
                                                                      entry.second.callSiteArg1.end());
            std::sort(a1s.begin(), a1s.end(),
                      [](const std::tuple<uint32_t, uint32_t, uint64_t> &l,
                         const std::tuple<uint32_t, uint32_t, uint64_t> &r)
                      { return std::get<2>(l) > std::get<2>(r); });
            std::cout << " a1=";
            const std::size_t a1Limit = std::min<std::size_t>(a1s.size(), 8);
            for (std::size_t i = 0; i < a1Limit; ++i)
            {
                std::cout << (i == 0 ? "" : ",") << toHex(std::get<0>(a1s[i])) << "/a1="
                          << toHex(std::get<1>(a1s[i])) << "x" << std::get<2>(a1s[i]);
            }
            if (a1s.size() > a1Limit)
            {
                std::cout << ",+" << (a1s.size() - a1Limit);
            }
        }

        // W22. $s0 is callee-saved, so at a syscall inside the broadcast it still holds the OBJECT
        // that sub_01011508 was entered with -- the mutex whose owner tid and wait-list head decide
        // whether anybody is ever going to be woken.
        if (!entry.second.callSiteS0.empty())
        {
            std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> s0Sites(
                entry.second.callSiteS0.begin(), entry.second.callSiteS0.end());
            std::sort(s0Sites.begin(), s0Sites.end(),
                      [](const std::tuple<uint32_t, uint32_t, uint32_t> &l,
                         const std::tuple<uint32_t, uint32_t, uint32_t> &r)
                      { return std::get<2>(l) > std::get<2>(r); });
            std::cout << " s0=";
            const std::size_t s0Limit = std::min<std::size_t>(s0Sites.size(), 6);
            for (std::size_t i = 0; i < s0Limit; ++i)
            {
                std::cout << (i == 0 ? "" : ",") << toHex(std::get<0>(s0Sites[i])) << "/"
                          << toHex(std::get<1>(s0Sites[i]));
            }
        }

        std::cout << "\n";
    }

    for (const auto &entry : mmio)
    {
        std::cout << "  MMIO 0x" << std::hex << entry.first << std::dec
                  << " accesses=" << entry.second << "\n";
    }

    for (const auto &entry : missing)
    {
        std::cout << "  VULCAN4 CALL " << toHex(entry.first) << " name=" << entry.second.name
                  << " kind=" << entry.second.kind << " times=" << entry.second.count << "\n";
    }

    std::cout << "VULCAN4 BIOS none_required=true files_opened=" << biosFilesOpened
              << " (the harness contains no BIOS loading path)\n";

    std::cout.flush();
    return 0;
}
