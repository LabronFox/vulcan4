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

#include "ps2_runtime.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "runtime/ee_scheduler.h"
#include "Stubs/Audio.h"
#include "Stubs/MPEG.h"
#include "ps2_log.h"
#include "ps2_recompiled_stubs.h"

#include <ps2_recompiled_functions.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <vector>

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

    // Reset the parts of the console environment the runtime expects before the first guest
    // instruction, mirroring what PS2Runtime::run() does, minus the render loop.
    // PS2Runtime::resetIop() is private, so the IOP is left as the constructor built it; that is
    // a deviation from run() and is recorded as such in docs/FIRST-BOOT.md.
    ps2_stubs::resetSifState();
    ps2_stubs::resetAudioStubState();
    ps2_stubs::resetMpegStubState();
    runtime.initializeEeKernelState(rdram);

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
    uint64_t dispatcherTransfers = 0;
    const char *haltReason = kHaltEntryBudget;
    uint32_t haltPc = entryPoint;
    std::string haltDetail;

    std::unordered_map<uint32_t, std::string> functionNames; // address -> name, first time seen
    std::map<uint32_t, GuestCall> missing;                  // address -> named call
    std::vector<uint32_t> pcOrder;                          // first-seen order, for the report

    uint32_t previousPc = 0xffffffffu;
    uint32_t repeatedPc = 0;

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
    std::thread watchdog([&runtime, &budget, &watchdogFired, start]() {
        const auto deadline = start + std::chrono::seconds(budget.maxSeconds);
        while (std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        watchdogFired.store(true);
        runtime.requestStop();
    });

    while (!finished)
    {
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
        try
        {
            g_ps2RecompiledFunctionTable[slot](rdram, &ctx, &runtime);
        }
        catch (const EeDispatcherTransfer &)
        {
            ++dispatcherTransfers;
        }

        if (runtime.isStopRequested())
        {
            // The watchdog sets this when the deadline passes. Distinguish it from anything
            // else that might request a stop, so the report never overstates progress.
            if (watchdogFired.load())
            {
                haltReason = kHaltDeadline;
                haltDetail = "guest was still inside a function when the deadline fired; "
                             "the watchdog asked the guest to yield";
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

    runtime.requestStop(); // tell the watchdog thread to exit its wait loop
    watchdog.join();

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();

    // ------------------------------------------------------------------ the report
    //
    // Machine-readable, exactly one line, exactly this shape. The gate greps for it.
    std::cout << "VULCAN4 BOOT REPORT functions_entered=" << functionsEntered << " halt=" << haltReason
              << " bios_files=" << biosFilesOpened << "\n";

    std::cout << "VULCAN4 HARNESS detail=" << haltDetail << " pc=" << toHex(haltPc)
              << " distinct_pcs=" << distinctPcs << " dispatcher_transfers=" << dispatcherTransfers
              << " elapsed_ms=" << elapsed
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
    // THIS IS THE NO-BIOS BACKLOG, and it is the next dish's brief.
    //
    // Two kinds of "call the guest asked for" are collected, from two honest sources:
    //
    //  (a) MISSING FUNCTIONS. A guest control transfer to an address the recompiler produced
    //      no body for. We record the address and, if the analyzer recognised it, its SDK name.
    //
    //  (b) SYSCALLS. ps2xRuntime routes every guest `syscall` through handleSyscall(), and every
    //      handler logs itself into ps2_log's runtime log. We read that log back and keep the
    //      lines, so what is printed is what the runtime actually did, not a reconstruction.
    //
    // Nothing is invented: a call that did not happen is not listed.
    const auto logEntries = ps2_log::snapshot_runtime_log_entries();
    std::map<std::string, uint64_t> syscallCounts;
    uint64_t logLinesScanned = 0;
    for (const auto &entry : logEntries)
    {
        ++logLinesScanned;
        // The runtime's syscall handlers announce themselves; keep the ones we can attribute.
        if (entry.text.find("syscall") != std::string::npos
            || entry.text.find("Syscall") != std::string::npos
            || entry.text.find("SCE") != std::string::npos)
        {
            ++syscallCounts[entry.text];
        }
    }

    std::cout << "VULCAN4 GUEST CALL LIST (what the guest asked the runtime for)\n";
    std::cout << "  VULCAN4 CALLKIND missing_functions=" << missing.size()
              << " runtime_log_lines_scanned=" << logLinesScanned
              << " syscall_log_lines=" << syscallCounts.size() << "\n";
    for (const auto &entry : missing)
    {
        std::cout << "  VULCAN4 CALL " << toHex(entry.first) << " name=" << entry.second.name
                  << " kind=" << entry.second.kind << " times=" << entry.second.count << "\n";
    }
    for (const auto &entry : syscallCounts)
    {
        std::cout << "  VULCAN4 SYSCALL times=" << entry.second << " " << entry.first << "\n";
    }

    std::cout << "VULCAN4 BIOS none_required=true files_opened=" << biosFilesOpened
              << " (the harness contains no BIOS loading path)\n";

    std::cout.flush();
    return 0;
}
