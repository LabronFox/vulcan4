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

#include <set>
#include "ps2_guest_progress.h"
#include "ps2_runtime.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "Stubs/LibC.h"
#include "runtime/ee_scheduler.h"
#include "Stubs/Audio.h"
#include "Stubs/MPEG.h"
#include "ps2_log.h"
#include "runtime/syscall_names.h"
// W101: SetWindowTitle(), so the game window can carry its own live vitals.
#include "raylib.h"

// W101. The north star's frame counter, defined in ps2xRuntime's gs_frontend.cpp inside an
// anonymous namespace, so it cannot be included -- only declared. This binds the harness to a
// private symbol; if a runtime refactor renames it, the LINK FAILS LOUDLY AT BUILD TIME, which is
// the only acceptable failure mode here. A silent 0 would read as "the guest drew nothing".
extern std::atomic<uint64_t> &guestFrameCounter() noexcept;
#include "ps2_recompiled_stubs.h"

#include <ps2_recompiled_functions.h>

// W240. The runtime-loaded ENGINE image has its own function table with DISTINCT symbols
// (emitted via PS2RECOMP_TABLE_SYMBOL). The driver switches to it on the ExecPS2 relaunch.
extern const uint32_t g_ps2EngineFunctionTableBase;
extern const uint32_t g_ps2EngineFunctionTableEnd;
extern const uint32_t g_ps2EngineFunctionTableSlotCount;
extern PS2Runtime::RecompiledFunction g_ps2EngineFunctionTable[];

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <execinfo.h>
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>
#include <cxxabi.h>
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
// W36. The boot path's components come from 0x010519C0, whose contents are
//     05 80 00 00 00 00 00 00 41 00 00 00 -- a 16-bit array, never ASCII. This address sits 16 bytes
//     past the destination of the one-time 16,410,192-byte copy (0x10519AC -> 0x10519B0), so it may be
//     ring-buffer content rather than a decoded field. Those two explanations need different fixes,
//     and the writer tells them apart: if the bytes arrive through the big copy the ring buffer's
//     stream is mis-decoded, and if our own decoder writes here then a field offset or width on our
//     side is wrong. Watch 12 bytes so a 32-bit store is not split across a boundary we chose blind.
// W49. There are TWO guest path buffers, and the window has to cover both -- I got this wrong at
// W48 and am correcting it.
//
//   0x010519C0  the guest BUILDS "/BASCUS-97328"          (13 bytes, then padding)
//   0x01051A10  the path sceMcOpen is HANDED: 05 80 2f 05 80 2f "e.gt4"
//
// W48 claimed this window was "watching the wrong address" and re-aimed it at 0x01051A10 alone.
// That was wrong: 0x010519C0 is where the readable path is, which is why W45 saw
// "/BA/SCUS-97328GAMEDATA" written there. Both are now inside one window. Keeping them together
// also means a single run shows the guest building one string and passing a DIFFERENT one, which is
// the actual finding: the string it passes is not the string it built.
constexpr uint32_t kWatchBuiltPathAddr = 0x010519C0u;  // where the guest writes "/BASCUS-97328"
constexpr uint32_t kWatchPassedPathAddr = 0x01051A10u; // the buffer sceMcOpen is handed
constexpr uint32_t kWatchLoDefault = kWatchBuiltPathAddr;
constexpr uint32_t kWatchHiDefault = kWatchPassedPathAddr + 0x70u;
uint32_t watchEnv(const char *name, uint32_t fallback)
{
    const char *raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0')
    {
        return fallback;
    }
    char *end = nullptr;
    const unsigned long value = std::strtoul(raw, &end, 0);
    return (end != nullptr && *end == '\0') ? static_cast<uint32_t>(value) : fallback;
}
// W93. A SAMPLING PROFILER WE OWN, because perf will not.
//
// `perf record` is installed and refused: perf_event_paranoid is 4, so hardware events and call graphs
// are both out, and changing a system sysctl is not a thing to do to somebody's box unasked. That
// leaves the only question left on the table unanswerable with what is on it: the guest runs at 3.6%
// of real PS2 speed, 28x slower than the hardware, and nothing knows where those 28x go.
//
// So: SIGPROF on a timer, take the interrupted PC out of the ucontext, and histogram it. One signal
// handler, one atomic index, one fixed array. No allocation in the handler, because a profiler that
// allocates in its own signal handler is a profiler that lies to itself. The raw addresses are printed
// at the end and symbolised with addr2line against this binary, which is the same trick W84 used to
// turn a memcpy into a named function.
namespace W93Sampler
{
constexpr size_t kBuckets = 1u << 16;
std::atomic<size_t> g_next{0};
uintptr_t g_pcs[kBuckets];
// W94. The caller as well as the leaf. 75% of every sample lands on one address inside libc, and
// glibc's memcpy/memset variants are LOCAL symbols -- not in .dynsym, so nm -D names the nearest
// EXPORTED neighbour instead, which for that address is getgroups, a syscall wrapper that cannot
// possibly be three quarters of a recompiler. The address is therefore not the interesting part; the
// interesting part is who called it.
//
// Those variants are leaf functions: no frame is pushed, so the return address is still at the top of
// the stack when the sample lands. REG_RSP + one dereference gives the caller, which IS in our binary
// and does symbolise. One extra load in the handler.
uintptr_t g_callers[kBuckets];
std::atomic<uint64_t> g_samples{0};

void handler(int, siginfo_t *, void *ctx)
{
    auto *uc = static_cast<ucontext_t *>(ctx);
    if (uc == nullptr)
    {
        return;
    }
    const uintptr_t pc = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
    uintptr_t caller = 0;
    const uintptr_t sp = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RSP]);
    if (sp != 0)
    {
        // Read-only copy, no reinterpret_cast through a volatile pointer into unknown memory.
        uintptr_t probed = 0;
        // No condition on the result: __builtin_memcpy returns the destination, which is never null,
        // so testing it for == 0 made every caller read back as zero. That is the whole bug, and it
        // is the same shape as the "callerPc was a lie" note in W74 -- an instrument field that is
        // confidently wrong is worse than one that is obviously missing.
        __builtin_memcpy(&probed, reinterpret_cast<const void *>(sp), sizeof(probed));
        caller = probed;
    }
    const size_t i = g_next.fetch_add(1u, std::memory_order_relaxed);
    if (i < kBuckets)
    {
        g_pcs[i] = pc;
        g_callers[i] = caller;
    }
    g_samples.fetch_add(1u, std::memory_order_relaxed);
}

void start(uint32_t hz)
{
    struct sigaction sa{};
    sa.sa_sigaction = &handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPROF, &sa, nullptr) != 0)
    {
        std::cerr << "VULCAN4 PROF sigaction(SIGPROF) failed; no profile will be produced" << std::endl;
        return;
    }
    struct itimerval tv{};
    const long period = 1000000L / static_cast<long>(hz == 0u ? 1u : hz);
    tv.it_interval.tv_usec = period;
    tv.it_value.tv_usec = period;
    if (setitimer(ITIMER_PROF, &tv, nullptr) != 0)
    {
        std::cerr << "VULCAN4 PROF setitimer(ITIMER_PROF) failed; no profile will be produced" << std::endl;
    }
}

void stop()
{
    struct itimerval tv{};
    setitimer(ITIMER_PROF, &tv, nullptr);

    const size_t n = std::min(g_next.load(std::memory_order_relaxed), kBuckets);
    std::vector<uintptr_t> all;
    all.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        if (g_pcs[i] != 0)
        {
            all.push_back(g_pcs[i]);
        }
    }
    std::sort(all.begin(), all.end());

    // The binary is PIE, so a raw RIP means nothing without the slide. Take the lowest mapping of our
    // own executable out of /proc/self/maps and print offsets from there, which is what addr2line
    // wants. Reading a map file at profile time is not something to do in the signal handler, and this
    // is not the signal handler.
    uintptr_t base = 0;
    {
        char selfPath[4096] = {};
        const ssize_t len = ::readlink("/proc/self/exe", selfPath, sizeof(selfPath) - 1);
        std::ifstream maps("/proc/self/maps");
        std::string line;
        while (std::getline(maps, line))
        {
            if (len > 0 && line.find(selfPath) == std::string::npos)
            {
                continue;
            }
            const size_t dash = line.find('-');
            if (dash == std::string::npos)
            {
                continue;
            }
            const uintptr_t candidate = std::strtoull(line.substr(0, dash).c_str(), nullptr, 16);
            if (base == 0 || candidate < base)
            {
                base = candidate;
            }
        }
    }

    std::cout << "VULCAN4 PROF samples=" << g_samples.load() << " collected=" << all.size()
              << " exe_base=0x" << std::hex << base << std::dec << "\n";

    // Rank by COUNT, not by address. The first cut of this printed the first 40 distinct addresses it
    // found and they were all single-digit counts, which told us nothing: with 59,180 samples over a
    // large address range, address order and count order are not remotely the same list.
    // 74% of the samples were landing on one address that is NOT in this binary, so name the mapping
    // rather than printing an offset that resolves to nothing. Read the maps once and keep the lines.
    std::vector<std::pair<uintptr_t, std::pair<uintptr_t, std::string>>> regions;
    {
        std::ifstream maps("/proc/self/maps");
        std::string line;
        while (std::getline(maps, line))
        {
            const size_t dash = line.find('-');
            if (dash == std::string::npos)
            {
                continue;
            }
            const uintptr_t lo = std::strtoull(line.substr(0, dash).c_str(), nullptr, 16);
            const size_t sp = line.find(' ');
            if (sp == std::string::npos)
            {
                continue;
            }
            const uintptr_t hi = std::strtoull(line.substr(dash + 1, sp - dash - 1).c_str(), nullptr, 16);
            std::string rest = line.substr(sp);
            const size_t p = rest.find('/');
            regions.emplace_back(lo, std::make_pair(hi, p == std::string::npos ? rest : rest.substr(p)));
        }
        std::sort(regions.begin(), regions.end());
    }
    // Name the mapping AND the offset inside it: 75% of the samples were one address in libc, and an
    // address alone cannot be looked up in a library that is mapped somewhere different every run.
    std::string g_topRegion;
    uintptr_t g_topOffset = 0;
    auto regionOf = [&regions, &g_topRegion, &g_topOffset](uintptr_t pc) -> std::string
    {
        for (const auto &[lo, hi_name] : regions)
        {
            if (pc >= lo && pc < hi_name.first)
            {
                std::string nm = hi_name.second;
                const size_t sp = nm.find(' ');
                if (sp != std::string::npos)
                {
                    nm = nm.substr(0, sp);
                }
                g_topRegion = nm;
                g_topOffset = pc - lo;
                return nm;
            }
        }
        return "?";
    };

    // Re-walk the sample array so each leaf address carries the caller that was recorded with it.
    // A std::map would be tidier; this runs once, at exit, on a few hundred thousand entries.
    std::vector<std::tuple<size_t, uintptr_t, uintptr_t>> ranked;
    {
        std::vector<std::pair<uintptr_t, size_t>> counts;
        std::unordered_map<uintptr_t, size_t> byLeaf;
        for (size_t i = 0; i < n; ++i)
        {
            if (g_pcs[i] != 0)
            {
                byLeaf[g_pcs[i]] += 1u;
            }
        }
        counts.reserve(byLeaf.size());
        for (const auto &[leaf, c] : byLeaf)
        {
            uintptr_t bestCaller = 0;
            size_t bestCount = 0;
            std::unordered_map<uintptr_t, size_t> callerTally;
            for (size_t i = 0; i < n; ++i)
            {
                if (g_pcs[i] == leaf && g_callers[i] != 0)
                {
                    callerTally[g_callers[i]] += 1u;
                }
            }
            for (const auto &[cal, cc] : callerTally)
            {
                if (cc > bestCount)
                {
                    bestCount = cc;
                    bestCaller = cal;
                }
            }
            counts.emplace_back(leaf, c);
            ranked.emplace_back(c, leaf, bestCaller);
        }
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const std::tuple<size_t, uintptr_t, uintptr_t> &a,
                 const std::tuple<size_t, uintptr_t, uintptr_t> &b)
              { return std::get<0>(a) > std::get<0>(b); });

    const size_t total = all.size();
    int rank = 0;
    for (const auto &entry : ranked)
    {
        const size_t count = std::get<0>(entry);
        const uintptr_t pc = std::get<1>(entry);
        const uintptr_t caller = std::get<2>(entry);
        if (rank >= 30)
        {
            break;
        }
        std::cout << "    PROFPC " << rank << " count=" << count << " pct="
                  << ((total != 0) ? (count * 100 / total) : 0) << "%"
                  << " off=0x" << std::hex << (pc >= base ? pc - base : pc)
                  << " in=" << regionOf(pc) << "+0x" << std::hex << g_topOffset << std::dec
                  << " fileoff=0x" << pc
                  << " caller=" << (caller != 0 ? regionOf(caller) : std::string("?"))
                  << "+0x" << std::hex << (caller != 0 && caller >= base ? caller - base : caller)
                  << std::dec << "\n";
        ++rank;
    }
}
} // namespace W93Sampler

const uint32_t kWatchLo = watchEnv("VULCAN4_WATCH_LO", kWatchLoDefault);
const uint32_t kWatchHi = watchEnv("VULCAN4_WATCH_HI", kWatchHiDefault);
uint32_t g_watchStoreHits = 0;

// W45. Every "the guest did NOT write this" conclusion since W33 has been read off stdout
// position, and that ordering is unproven: the guest executor and the IOP/RPC side are two
// producers into one unsynchronised stream, and a sample log shows them interleaving
// mid-line. So the observers now carry their own monotonic sequence number and the guest
// thread id. Ordering comes from `seq`, which is causal, not from where a line landed.
int g_guestThreadId = -1;
// W48. W45 capped observers at 4-5 hits, which they burned in the first moments of boot --
// thousands of functions before the event under investigation. Env-overridable so the
// instrument can go deep on purpose; the default stays small so an ordinary run is quiet.
const uint32_t kWatchStoreMax = watchEnv("VULCAN4_WATCH_MAX", 120u);
// W30: 1 MiB. Anything at least this big is a copy, not a field write.
constexpr uint32_t kWatchBigCopy = 1024u * 1024u;
uint32_t g_watchBigCopyHits = 0;
// W33: sub_01003E10's `sw $s1, 0x4($s4)` -- the store that caches the guest's built MC path.
constexpr uint32_t kWatchPathStorePc = 0x01003E98u;
uint32_t g_watchPathHits = 0;
// W33: the observer needs RDRAM to read the candidate path strings. Set once, before boot.
uint8_t *g_rdramForWatch = nullptr;

// W96. GS kick counter for the progress line. The GS layer prints [gs:kick] when diagnostics are on,
// but the default run needs a progress signal and "kicks" is the one that means the game is drawing.
uint64_t g_gsKickCount = 0;

// W90. A SCRATCHPAD WATCH WINDOW, because the RDRAM one cannot see the thing we are looking for.
//
// W89 measured the livelock: the guest resumes from sce_SleepThread at 0x0100AFA8 and runs
//     lbu   $v1, 0x0($s0)
//     bnel $v1, $zero, ...            (branch back 0x60 bytes -- a poll loop)
// with $s0 = 0x70002085 / 0x70002050 / 0x70002079, all inside the scratchpad. The flag the guest is
// polling lives in the scratchpad, and the observer above DELIBERATELY EXCLUDES scratchpad stores:
// `isScratchStore` forces resolvedAddr to 0 and overlapsWatch to false, so a store to 0x70002085
// produces no line at all. The shadow is over g_rdramForWatch, and the scratchpad is a separate 16 KB
// allocation (ps2_memory.cpp:344). So the one byte the boot is stuck on was invisible by construction,
// and no amount of waiting would have shown it.
//
// Off unless VULCAN4_SCRATCH_LO is set, so nothing changes for a normal run.
const uint32_t kScratchLo = watchEnv("VULCAN4_SCRATCH_LO", 0u);
const uint32_t kScratchHi = watchEnv("VULCAN4_SCRATCH_HI", 0u);
const bool kScratchWatch = (kScratchHi > kScratchLo);

// W77. The live runtime, so the shadow can ask whether the buffer it is watching is still the buffer
// the code reads. RDRAMPROBE printed this ONCE at startup, where the two pointers necessarily agree.
PS2Runtime *g_runtimeForShadowProbe = nullptr;

// W45. Exposed so a probe inside the runtime can compare the buffer IT was handed against the buffer
// the store observer is watching. Every internal consistency check so far (TLB vs flat index) proved
// the two AGREE; none of them proved they agree with the OBSERVER. This is the only way to settle it,
// and it costs one exported pointer.
void *vulcan4HarnessWatchedRdram()
{
    return static_cast<void *>(g_rdramForWatch);
}
uint32_t g_watchPeek(uint32_t guestAddr)
{
    if (g_rdramForWatch == nullptr || guestAddr + 4u > PS2_RAM_SIZE)
    {
        return 0u;
    }
    uint32_t v = 0u;
    std::memcpy(&v, g_rdramForWatch + guestAddr, sizeof(v));
    return v;
}

// W65. THE MEASUREMENT W61 PROMISED AND NEVER MADE. sub_01003E10 builds the memory-card path with
//   0x1003e40  lw    $s2, -0x2338($v0)   $s2 = *(0x0102DCC8)
//   0x1003e44  jal   func_1013D68        func_1013D68 IS the guest's SIMD strlen
//   0x1003e48  daddu $a0, $s2, $zero
//   0x1003e4c  lw    $a0, 0x0($s4)
// so the question "what does the guest's strlen return for 0x010519C0" decides whether the joined
// path is right or garbage. W35 guessed 2, W61 guessed it was the pointer's low bytes, and both were
// inferences from a concatenated string rather than from the call. Watch the call and the return.
//
// $s2 is callee-saved but $a0-$a3 are not, so the argument is only readable at the JAL. The result
// is only readable at the return, and by then the caller's $s2 is intact, so the same site pairs.
// W65. THE MEASUREMENT W35 AND W61 BOTH GUESSED. sub_01003E10 builds the memory-card path with
//   0x1003e40  lw    $s2, -0x2338($v0)   $s2 = *(0x0102DCC8)
//   0x1003e44  jal   func_1013D68        func_1013D68 IS the guest's SIMD strlen
//   0x1003e48  daddu $a0, $s2, $zero
// so "what does the guest's strlen return for 0x010519C0" decides whether the joined path is right or
// garbage. W35 said 2. W61 said it was the pointer's low bytes. Neither measured it.
//
// WHY THIS NEEDS THE BRANCH DISPATCHER: PS2Runtime::dispatchGuestBranch calls the callee INLINE
// (`targetFn(rdram, ctx, this)`), so a guest call nests on the C++ stack and never passes through the
// harness's function-entry loop. Two consequences worth more than this one measurement:
//   * functions_entered and distinct_pcs -- the two numbers every G1 progress claim rests on -- count
//     TOP-LEVEL entries only and are blind to nested guest calls.
//   * a `jr $ra` is emitted as a plain C++ `return;` whenever it is the function's last exit, so
//     GuestBranchKind::Return is ~0 in 3,000,000 dispatches. Measuring a call means watching the
//     CALL for arguments and the CALLER'S NEXT DISPATCH for the result, not waiting for a return.
constexpr uint32_t kPathStrlenCallSite = 0x01003E44u;
constexpr uint32_t kPathStrlenCallee = 0x01013D68u;
constexpr uint32_t kPathStrlenCallerLo = 0x01003E10u;
constexpr uint32_t kPathStrlenCallerHi = 0x01003F10u;
void watchGuestCallForPath(const R5900Context *ctx,
                           uint32_t sourcePc,
                           uint32_t targetPc,
                           uint32_t /*fallthroughPc*/,
                           PS2Runtime::GuestBranchKind /*kind*/,
                           bool /*isReturning*/)
{
    // W119 R1. $v0 AS SEEN BY THE bltz AT 0x10089dc. func_10057F0 has several paths that compute
    // $v0 = $a0 - 1, which is -1 (and therefore "keep spinning") whenever $a0 is 0 there. Probe the
    // return edge so the value the branch actually tests is logged, not inferred.
    if (ctx != nullptr && targetPc == 0x010089DCu)
    {
        static uint32_t w119Ret = 0;
        if (w119Ret < 6u)
        {
            ++w119Ret;
            const uint32_t v0 = getRegU32(ctx, 2);
            std::cerr << "[w119:ret] back at 0x10089dc v0=0x" << std::hex << v0
                      << " signed=" << static_cast<int32_t>(v0)
                      << " bltz_taken=" << (static_cast<int32_t>(v0) < 0 ? "YES -> LOOPS" : "no -> exits")
                      << " a0=0x" << getRegU32(ctx, 4)
                      << " s2=0x" << getRegU32(ctx, 18) << std::dec << std::endl;
        }
    }

    // W119 R1. THE SPIN, WITH ITS ACTUAL OPERANDS. sub_010088E8 loops while $v0 < 0 at 0x10089dc, where
    // $v0 is what func_1005870 returned. func_1005870 calls func_10057F0 only when
    // *(a0+0x10) == *(a1+0x10) (0x1005880-0x1005888), and func_10057F0 compares *(a0+8) against *(a1+8)
    // and returns $a0-1. Those three addresses are 99.96% of all control transfers in the pathological
    // boot shape. Print the operands instead of reasoning about them.
    if (ctx != nullptr && sourcePc == 0x010089D4u)
    {
        static uint32_t w119Spin = 0;
        if (w119Spin < 6u)
        {
            ++w119Spin;
            const uint32_t a0 = getRegU32(ctx, 4);
            const uint32_t a1 = getRegU32(ctx, 5);
            const uint32_t v0 = getRegU32(ctx, 2);
            auto peek = [&](uint32_t base, uint32_t off) -> uint32_t
            {
                if (g_rdramForWatch == nullptr || base == 0u || base >= 0x02000000u)
                {
                    return 0xDEADBEEFu;
                }
                const uint32_t o = (base + off) & 0x01FFFFFFu;
                return static_cast<uint32_t>(g_rdramForWatch[o])
                     | (static_cast<uint32_t>(g_rdramForWatch[o + 1u]) << 8)
                     | (static_cast<uint32_t>(g_rdramForWatch[o + 2u]) << 16)
                     | (static_cast<uint32_t>(g_rdramForWatch[o + 3u]) << 24);
            };
            std::cerr << "[w119:spin] jal func_1005870: a0=0x" << std::hex << a0
                      << " a1=0x" << a1
                      << " | *(a0+0x10)=0x" << peek(a0, 0x10u)
                      << " *(a1+0x10)=0x" << peek(a1, 0x10u)
                      << " EQUAL=" << (peek(a0, 0x10u) == peek(a1, 0x10u) ? "YES -> will call func_10057F0" : "no")
                      << " | *(a0+8)=0x" << peek(a0, 8u)
                      << " *(a1+8)=0x" << peek(a1, 8u)
                      << " | v0OnEntry=0x" << v0
                      << " sp=0x" << getRegU32(ctx, 29) << std::dec << std::endl;
        }
    }

    // W65. Off unless asked for: this is one indirect call per branch dispatch, and a 2M-entry run
    // makes three million of them.
    static const bool kWatch = watchEnv("VULCAN4_BRANCH_WATCH", 0u) != 0u;
    static uint32_t lastArg = 0u;
    static uint32_t argBytes = 0u;
    static bool pending = false;
    static int callLogs = 0;
    static int retLogs = 0;
    if (!kWatch || ctx == nullptr)
    {
        return;
    }
    if (sourcePc == kPathStrlenCallSite && targetPc == kPathStrlenCallee)
    {
        lastArg = getRegU32(ctx, 4);
        argBytes = 0u;
        if (const uint8_t *raw = getConstMemPtr(g_rdramForWatch, lastArg))
        {
            while (argBytes < 64u && raw[argBytes] != 0u)
            {
                ++argBytes;
            }
        }
        pending = true;
        if (callLogs < 3)
        {
            ++callLogs;
            char text[32];
            for (uint32_t i = 0; i < 8u; ++i)
            {
                const uint8_t *one = getConstMemPtr(g_rdramForWatch, lastArg + i);
                const uint8_t b = (one != nullptr) ? *one : 0u;
                std::snprintf(text + (i * 2u), 4u, "%02x", b);
            }
            std::cout << "VULCAN4 STRLENCALL arg(a0)=0x" << std::hex << lastArg
                      << " bytes=" << text << " hostStrlen=" << std::dec << argBytes << "\n";
        }
        return;
    }
    // strlen has returned and the caller is running again: its next transfer carries the result in
    // $v0, because nothing between the two overwrites it (0x1003e48 is `daddu $a0, $s2, $zero`).
    if (pending && sourcePc >= kPathStrlenCallerLo && sourcePc < kPathStrlenCallerHi)
    {
        pending = false;
        if (retLogs < 3)
        {
            ++retLogs;
            const uint32_t got = getRegU32(ctx, 2);
            std::cout << "VULCAN4 STRLENRET  arg=0x" << std::hex << lastArg
                      << " returned(v0)=" << std::dec << got
                      << " hostStrlen=" << argBytes
                      << " MATCH=" << ((got == argBytes) ? "yes" : "NO") << "\n";
        }
    }
}

void watchGuestStoreForPath(uint32_t guestAddr,
                                       uint32_t size,
                                       uint64_t value,
                                       const R5900Context *ctx,
                                       const char *op,
                                       uint32_t srcAddr)
{
    // W65. WINDOW ON THE RESOLVED ADDRESS, NOT THE RAW ONE. The overlap test and the W30WRITE
    // early-return both compare the RAW address the instruction used. PS2 gives the same physical
    // memory several aliases -- 0x80000000 kseg1 mirrors 0x00000000, and 0x20000000/0xA0000000 are
    // the same RAM again -- so a store to 0x810519C0 lands on exactly the bytes at 0x010519C0 and is
    // thrown away by `guestAddr >= kWatchHi` as being outside the window. That is precisely how
    // "the bytes changed and nothing was logged" survives a complete write trace: the write WAS
    // logged, under an alias. Mask first, then filter, or the instrument lies about its own window.
    // getConstMemPtr resolves scratchpad too; a scratchpad store is NOT rdram and must not match.
    const bool isScratchStore = ps2IsScratchpadAddress(guestAddr);
    const uint32_t resolvedAddr = isScratchStore ? 0u : (guestAddr & 0x01FFFFFFu);

    // W45. BEFORE-SNAPSHOT. W44 found a write to 0x010519C0 that the observer never reported, so a
    // log line describing only the state AFTER the store cannot say what was overwritten. Capturing
    // the destination bytes first turns every line into a before/after pair, which is what makes the
    // next occurrence of this event legible instead of another contradiction.
    //
    // Only for writes that actually overlap the watched window: the snapshot is 16 bytes of copying
    // and the whole reason it is affordable is that the overlap test rejects almost everything.
    const bool overlapsWatch =
        !isScratchStore && (resolvedAddr < kWatchHi) && ((resolvedAddr + size) > kWatchLo);
    // W57c. COMPACT UNFILTERED TRACE. The window filter is why this took so long: at W57b the
    // observer's own dump changed between seq=409 and seq=416 -- the guest's memory went from an
    // 8-byte string to an 11-byte one, and the Open agreed with the new value -- with NO event
    // logged in between. The change was real; the write that made it was outside the window, so the
    // dump could never show it. This prints every traced write's shape with no filtering and no
    // window dump, so "the bytes changed and nothing was logged" stops being possible. Off unless
    // VULCAN4_TRACE_WRITES is set, because it is one line per guest write.
    static const bool kTraceAllWrites = watchEnv("VULCAN4_TRACE_WRITES", 0u) != 0u;

    // W83. THE ONE MEASUREMENT. $s1 (register 17) at pc=0x100a3e8 inside sub_0100A348, where the
    // generated unit says `sw $v0, 0x110($s1)`. W82 bounded the overwrite of 0x010519C0 to two trace
    // events; those three stores are twelve bytes, which is the size of the change. If $s1 is
    // 0x010518B0 they are the writer. Report the register and the three computed addresses, and
    // nothing that was not measured.
    // W86. $s4 and $s1 at pc=0x1003e98, which the generated unit says is `sw $s1, 0x4($s4)`. W85
    // measured that store landing 0x05 0x80 0x2f into 0x01051A13 -- one byte into the buffer the
    // failing open reads, and 0x8005 is a GS register word. So: is $s4 really 0x01051A0F, and what is
    // $s1? Report the registers, not a story about them.
    // W89. The guest is POLLING A FLAG BYTE, not branching on the sleep result. At 0x100afa8, which
    // is where 23 of the 24 sleeps resume:
    //     0x100afa8: lbu  $v1, 0x0($s0)
    //     0x100afac: bnel $v1, $zero, ... -> back to 0x100af50
    // The harness's own boot report guessed "the wall is the return value"; this is the measurement
    // that says otherwise. So: what address is the flag on, and is anything ever writing it?
    if (ctx != nullptr && ctx->pc == 0x0100AFA8u)
    {
        static int w89Logs = 0;
        if (w89Logs < 2)
        {
            ++w89Logs;
            const uint32_t s0 = getRegU32(ctx, 16);
            RUNTIME_LOG("W89 POLL pc=0x100afa8 s0=0x" << std::hex << s0
                        << " flagByte=" << std::dec
                        << (g_rdramForWatch != nullptr && s0 < 0x02000000u
                                ? static_cast<int>(g_rdramForWatch[s0 & 0x01FFFFFFu])
                                : -1)
                        << " v0(ret)=0x" << std::hex << getRegU32(ctx, 2) << std::dec);
        }
    }

    if (ctx != nullptr && ctx->pc == 0x01003E98u)
    {
        static int w86Logs = 0;
        if (w86Logs < 3)
        {
            ++w86Logs;
            const uint32_t s4 = getRegU32(ctx, 20);
            RUNTIME_LOG("W86 SW86 pc=0x1003e98 s4=0x" << std::hex << s4
                        << " s4+4=0x" << (s4 + 4u)
                        << " s1=0x" << getRegU32(ctx, 17)
                        << " v0=0x" << getRegU32(ctx, 2)
                        << " a0=0x" << getRegU32(ctx, 4)
                        << " a1=0x" << getRegU32(ctx, 5)
                        << " a2=0x" << getRegU32(ctx, 6)
                        << " a3=0x" << getRegU32(ctx, 7)
                        << " isBuf=" << ((s4 + 4u) >= 0x01051A10u && (s4 + 4u) < 0x01051A40u ? "IN-WATCH" : "no")
                        << std::dec);
        }
    }

    if (ctx != nullptr && ctx->pc == 0x0100A3E8u)
    {
        static int s1Logs = 0;
        if (s1Logs < 4)
        {
            ++s1Logs;
            const uint32_t s1 = getRegU32(ctx, 17);
            RUNTIME_LOG("W83 S1 pc=0x100a3e8 s1=0x" << std::hex << s1
                        << " +0x110=0x" << (s1 + 0x110u)
                        << " +0x114=0x" << (s1 + 0x114u)
                        << " +0x118=0x" << (s1 + 0x118u)
                        << " v0=0x" << getRegU32(ctx, 2)
                        << " isPathBuf=" << ((s1 + 0x110u) == 0x010519C0u ? "YES" : "no")
                        << std::dec);
        }
    }

    // W64. SHADOW COMPARE. Tracing every write path and still seeing "the bytes changed and nothing
    // was logged" means enumerating writers is the wrong strategy: there is at least one left, and
    // the list is open-ended. So stop asking who wrote it. Keep a private copy of the watched window
    // and, on EVERY traced write of ANY address, diff it. The first observer call after the change
    // names the pc at which the change became visible, which localises it without needing to know
    // the writer in advance. Off unless VULCAN4_SHADOW is set: it is a memcmp per traced write.
    static const bool kShadow = watchEnv("VULCAN4_SHADOW", 0u) != 0u;
    // W64b. The shadow diff runs at the TOP of the observer, but a traced store is announced BEFORE
    // it is applied (PS2Runtime::Store8 calls ps2TraceGuestWrite, then m_memory.write8). So a change
    // detected at call N was made by the write announced at call N-1, and printing THIS call's op
    // names the wrong write. Keep the previous one.
    static const char *prevOp = nullptr;
    static uint32_t prevAddr = 0u;
    static uint32_t prevSize = 0u;
    static uint32_t prevPc = 0u;
    static uint8_t shadow[4096];
    static bool shadowInit = false;
    // W84. NAME THE SITE. A getMemPtr announcement says "a raw pointer covering this address was
    // handed out" but not which of the ~40 call sites did it. When one covers the watch window, take a
    // native backtrace and demangle it: that turns an anonymous memcpy into a named C++ function.
    // Filter: a raw pointer that STARTS INSIDE the watch window is the guest building its own path
    // (sub_01003D20, entirely legitimate, and named already). The dangerous case is a pointer that
    // starts BELOW the window and reaches up into it -- 0x010519B0 is 0x10 below 0x010519C0, so the
    // write lands in the window at a positive offset and nothing else about the announcement looks
    // unusual. Only that case gets a backtrace.
    if (op != nullptr && std::strcmp(op, "getMemPtr") == 0 && overlapsWatch)
    {
        // W86. Dedupe by address: the path builder hands out a pointer per byte, and a backtrace per
        // byte tells us nothing. One backtrace per distinct address is the whole signal.
        static uint32_t sSeenAddrs[16];
        static uint32_t sSeenCount = 0;
        static uint32_t sTrace = 0;
        bool alreadySeen = false;
        for (uint32_t q = 0; q < sSeenCount; ++q)
        {
            if (sSeenAddrs[q] == guestAddr)
            {
                alreadySeen = true;
                break;
            }
        }
        if (!alreadySeen && sSeenCount < 16u)
        {
            sSeenAddrs[sSeenCount++] = guestAddr;
        }
        if (!alreadySeen && sTrace < 16u)
        {
            ++sTrace;
            void *frames[8];
            const int n = ::backtrace(frames, 8);
            char **syms = ::backtrace_symbols(frames, n);
            std::cout << "VULCAN4 W84SITE seq=" << ps2TraceSequenceCounter().load(std::memory_order_relaxed)
                      << " rawptr addr=0x" << std::hex << guestAddr << std::dec << "\n";
            for (int f = 2; f < n && f < 7; ++f)
            {
                std::string sym = (syms != nullptr) ? syms[f] : "?";
                const size_t open = sym.find('(');
                if (open != std::string::npos)
                {
                    const size_t plus = sym.find('+', open);
                    const std::string mangled = sym.substr(open + 1, (plus == std::string::npos ? sym.size() : plus) - open - 1);
                    int status = 0;
                    char *pretty = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
                    if (status == 0 && pretty != nullptr)
                    {
                        sym = pretty;
                        std::free(pretty);
                    }
                }
                std::cout << "    W84SITE#" << f << " " << sym << "\n";
            }
            if (syms != nullptr)
            {
                std::free(syms);
            }
        }
    }

    // W84. NAME THE TWO EVENTS. The observer is called BEFORE the store is applied, so when the shadow
    // diff below notices a change, the store that CAUSED it is the one announced on the previous call.
    // W82 could only report addresses; a ring of the last few announcements, each with its guest PC,
    // turns "two events wide" into two named instructions. A pc of 0 means the announcement carried no
    // ctx at all, i.e. it did not come from recompiled guest code.
    struct W84Event
    {
        uint32_t seq;
        uint32_t pc;
        uint32_t addr;
        uint32_t size;
        const char *op;
        bool hasCtx;
        int tid;
    };
    static W84Event g_w84Ring[8] = {};

    static uint32_t shadowChanges = 0;
    // W90. Same shadow discipline as the RDRAM window -- prime once, diff per announcement, print the
    // bracket so the writer is named rather than inferred -- pointed at the scratchpad instead.
    if (kScratchWatch)
    {
        uint8_t *scratch = ps2GetScratchpadHostPtr();
        if (scratch != nullptr)
        {
            static uint8_t scratchShadow[256];
            static bool scratchInit = false;
            const uint32_t span = std::min(kScratchHi - kScratchLo, static_cast<uint32_t>(sizeof(scratchShadow)));
            if (!scratchInit)
            {
                for (uint32_t k = 0; k < span; ++k)
                {
                    scratchShadow[k] = scratch[kScratchLo + k];
                }
                scratchInit = true;
            }
            for (uint32_t k = 0; k < span; ++k)
            {
                if (scratchShadow[k] != scratch[kScratchLo + k])
                {
                    static uint32_t sCount = 0;
                    if (sCount < 400u)
                    {
                        ++sCount;
                        std::cout << "VULCAN4 SCRATCHCHANGE #" << (sCount - 1u)
                                  << " seq=" << ps2TraceSequenceCounter().load(std::memory_order_relaxed)
                                  << " guest=0x" << std::hex << (kScratchLo + k)
                                  << " off=0x" << (kScratchLo + k)
                                  << " now=0x" << static_cast<uint32_t>(scratch[kScratchLo + k])
                                  << " was=0x" << static_cast<uint32_t>(scratchShadow[k])
                                  << " by_op=" << (op != nullptr ? op : "?")
                                  << " by_addr=0x" << guestAddr
                                  << " tid=" << g_guestThreadId << std::dec
                                  << " | W84BRACKET";
                        for (int r = 0; r < 8; ++r)
                        {
                            const W84Event &e = g_w84Ring[r];
                            if (e.op == nullptr)
                            {
                                continue;
                            }
                            std::cout << " [seq=" << e.seq << " pc=0x" << std::hex << e.pc
                                      << (e.hasCtx ? "" : "*NOCONTEXT*") << " op=" << e.op
                                      << " addr=0x" << e.addr << " size=" << std::dec << e.size
                                      << " tid=" << e.tid << "]";
                        }
                        std::cout << "\n";
                    }
                    scratchShadow[k] = scratch[kScratchLo + k];
                }
            }
        }
    }

    if (kShadow && g_rdramForWatch != nullptr)
    {
        const uint32_t span = std::min(kWatchHi - kWatchLo, static_cast<uint32_t>(sizeof(shadow)));
        if (!shadowInit)
        {
            for (uint32_t k = 0; k < span; ++k)
            {
                shadow[k] = g_rdramForWatch[kWatchLo + k];
            }
            shadowInit = true;
        }
        for (uint32_t k = 0; k < span; ++k)
        {
            if (shadow[k] != g_rdramForWatch[kWatchLo + k])
            {
                if (shadowChanges < watchEnv("VULCAN4_SHADOW_MAX", 8u))
                {
                    // W77. Is the BUFFER the same one the shadow was primed from? The shadow array is
                // initialised once; if g_rdramForWatch is ever repointed -- a memory reset, a second
                // loadELF -- then every byte that differs between the old and new buffer is reported
                // as a "change" and no write ever happened at all. Printing the pointer next to the
                // change is the only way to tell a real write from a buffer swap, and RDRAMPROBE only
                // ever printed once, at startup, where both pointers necessarily agreed.
                std::cout << "VULCAN4 SHADOWCHANGE #" << shadowChanges
                              << " seq=" << ps2NextTraceSequence()
                              << " watched=" << static_cast<const void *>(g_rdramForWatch)
                              << " live="
                              << static_cast<const void *>(
                                     g_runtimeForShadowProbe->memory().getRDRAM())
                              << " swapped="
                              << ((static_cast<const void *>(g_rdramForWatch) !=
                                   static_cast<const void *>(
                                       g_runtimeForShadowProbe->memory().getRDRAM()))
                                      ? "YES"
                                      : "no")
                              << " at addr=0x" << std::hex << (kWatchLo + k)
                              << " now=0x" << static_cast<uint32_t>(g_rdramForWatch[kWatchLo + k])
                              << " was=0x" << static_cast<uint32_t>(shadow[k])
                              << " pc=0x" << (ctx != nullptr ? ctx->pc : 0u)
                              << " ra=0x" << (ctx != nullptr ? getRegU32(ctx, 31) : 0u)
                              << " culpritop=" << (prevOp != nullptr ? prevOp : "?")
                              << " culpritaddr=0x" << prevAddr
                              << " culpritsize=" << prevSize
                              << " culpritpc=0x" << prevPc << std::dec
                              << " | W84BRACKET";
                    for (int r = 0; r < 8; ++r)
                    {
                        const W84Event &e = g_w84Ring[r];
                        if (e.op == nullptr)
                        {
                            continue;
                        }
                        std::cout << " [seq=" << e.seq << " pc=0x" << std::hex << e.pc
                                  << (e.hasCtx ? "" : "*NOCONTEXT*") << " op=" << e.op
                                  << " addr=0x" << e.addr << " size=" << std::dec << e.size << "]";
                    }
                    std::cout << "\n";
                }
                ++shadowChanges;
                shadow[k] = g_rdramForWatch[kWatchLo + k];
            }
        }
    }
    if (kShadow)
    {
        prevOp = op;
        prevAddr = guestAddr;
        prevSize = size;
        prevPc = (ctx != nullptr) ? ctx->pc : 0u;
    }

    if (kTraceAllWrites && g_rdramForWatch != nullptr)
    {
        std::cout << "VULCAN4 WTRACE seq=" << ps2NextTraceSequence() << " op="
                  << (op != nullptr ? op : "?") << " src=0x" << std::hex << srcAddr
                  << " addr=0x" << guestAddr << " size=" << std::dec << size << " inwin="
                  << (overlapsWatch ? 1 : 0) << "\n";
    }

    {
        static uint32_t w84Cursor = 0;
        g_w84Ring[w84Cursor].seq =
            static_cast<uint32_t>(ps2TraceSequenceCounter().load(std::memory_order_relaxed));
        g_w84Ring[w84Cursor].pc = (ctx != nullptr) ? ctx->pc : 0u;
        g_w84Ring[w84Cursor].addr = guestAddr;
        g_w84Ring[w84Cursor].size = size;
        g_w84Ring[w84Cursor].op = op;
        g_w84Ring[w84Cursor].hasCtx = (ctx != nullptr);
        g_w84Ring[w84Cursor].tid = g_guestThreadId;
        w84Cursor = (w84Cursor + 1u) & 7u;
    }
    // W64. This used to be a VLA sized by the watch window: `uint8_t before[kWatchHi - kWatchLo]`.
    // That is fine for the default ~0x70-byte window and a guaranteed stack overflow for any wide
    // one -- VULCAN4_WATCH_HI=0x02000000 with a 16 MB window meant a 16 MB stack array, which is
    // how a diagnostic segfaulted the whole boot and briefly looked like a real bug. Bounded, static,
    // and the dump below is clamped to what it can actually hold.
    static uint8_t before[4096];
    const uint32_t windowBytes = (kWatchHi > kWatchLo) ? (kWatchHi - kWatchLo) : 0u;
    const uint32_t dumpBytes = std::min(windowBytes, static_cast<uint32_t>(sizeof(before)));
    if (overlapsWatch && g_rdramForWatch != nullptr && g_watchStoreHits < kWatchStoreMax)
    {
        for (uint32_t k = 0; k < dumpBytes; ++k)
        {
            before[k] = g_rdramForWatch[kWatchLo + k];
        }
        std::cout << "VULCAN4 W45BEFORE seq=" << ps2NextTraceSequence() << " addr=0x" << std::hex << guestAddr << " size=" << std::dec << size
                  << " op=" << ((ctx != nullptr) ? "guest" : "fastpath") << " bytes=";
        for (uint32_t k = 0; k < dumpBytes; ++k)
        {
            char byteText[4];
            std::snprintf(byteText, sizeof(byteText), "%02x", before[k]);
            std::cout << byteText << " ";
        }
        std::cout << "\n";
    }

    // W30. A RANGE write big enough to straddle most of RDRAM is not a field update, it is a copy
    // that ran away -- sub_01010EA8 computed a 16,410,192-byte length from two guest globals and
    // called the guest's own memcpy. Report those regardless of address: the destination and the
    // size are the whole question, and filtering them out by address is how they stayed invisible
    // for this long. Two of them is plenty; the answer is in the first.
    if (size >= kWatchBigCopy)
    {
        ++g_watchBigCopyHits;
        if (g_watchBigCopyHits <= watchEnv("VULCAN4_BIGCOPY_MAX", 2u))
        {
            std::cout << "VULCAN4 W30BIGCOPY seq=" << ps2NextTraceSequence()
                      << " tid=" << g_guestThreadId << " op=" << (op != nullptr ? op : "?")
                      << " src=0x" << std::hex << srcAddr << std::dec << " n=" << g_watchBigCopyHits << " pc=0x" << std::hex
                      << (ctx != nullptr ? ctx->pc : 0u) << " dst=0x" << guestAddr << " size="
                      << std::dec << size << "\n";
        }
    }
    // W33. Filter by SITE, not by address. sub_01003E10 stores its freshly built path with
    // `sw $s1, 0x4($s4)` at pc=0x1003e98, where $s4 is the object it was called with. That one
    // event gives BOTH halves of the question: addr-4 is `obj`, and the value is the heap buffer the
    // guest built `<name>/<name>` into. Watching an address range could never find it because we do
    // not know where the guest will allocate.
    if (ctx != nullptr && ctx->pc == kWatchPathStorePc)
    {
        if (g_watchPathHits < watchEnv("VULCAN4_PATHSTORE_MAX", 4u) && g_rdramForWatch != nullptr)
        {
            ++g_watchPathHits;
            const uint32_t obj = guestAddr >= 4u ? guestAddr - 4u : 0u;
            const uint32_t f0 = obj + 4u <= PS2_RAM_SIZE ? g_watchPeek(obj + 0u) : 0u;
            const uint32_t f4 = obj + 4u <= PS2_RAM_SIZE ? g_watchPeek(obj + 4u) : 0u;
            const uint32_t buf = static_cast<uint32_t>(value);
            std::cout << "VULCAN4 W33PATHSTORE seq=" << ps2NextTraceSequence()
                      << " tid=" << g_guestThreadId << " op=" << (op != nullptr ? op : "?")
                      << " src=0x" << std::hex << srcAddr << std::dec << " n=" << g_watchPathHits << " obj=0x" << std::hex << obj
                      << " obj->0x0=0x" << f0 << " obj->0x4=0x" << f4 << std::dec;
            // Print both candidates' first bytes. Whichever one is the path the guest built is the
            // one that reads as "<name>/<name>"; that settles it without any interpretation.
            for (uint32_t k = 0; k < 2; ++k)
            {
                const uint32_t cand = k == 0 ? f0 : f4;
                std::cout << " [" << (k == 0 ? "obj->0x0" : "obj->0x4") << "=0x" << std::hex << cand
                          << std::dec << " '";
                for (uint32_t j = 0; j < 24u; ++j)
                {
                    const uint32_t a = cand + j;
                    if (a >= PS2_RAM_SIZE)
                    {
                        break;
                    }
                    const uint8_t ch = g_rdramForWatch[a];
                    if (ch == 0u)
                    {
                        break;
                    }
                    std::cout << (ch >= 32 && ch < 127 ? static_cast<char>(ch) : '.');
                }
                std::cout << "']";
            }
            std::cout << " built=0x" << std::hex << buf << std::dec << "\n";
        }
        return;
    }
    // A scratchpad store is never an RDRAM write, so it can never be "in" an RDRAM window: fail it
    // outright. Writing `!isScratchStore && ...` here would do the exact opposite and let every
    // scratchpad store through, which is what happened the first time this was written.
    if (isScratchStore || resolvedAddr >= kWatchHi || resolvedAddr + size <= kWatchLo)
    {
        return;
    }
    if (g_watchStoreHits >= kWatchStoreMax)
    {
        return;
    }
    ++g_watchStoreHits;
    // W50. op= names the write path and src= is the far end of a bulk copy. Without them a 2-byte
    // syscallCopy and a 2-byte sh are indistinguishable, because ps2TraceGuestRangeWrite forwards
    // the DESTINATION as "value" for a range write.
    std::cout << "VULCAN4 W30WRITE seq=" << ps2NextTraceSequence()
              << " op=" << (op != nullptr ? op : "?") << " src=0x" << std::hex << srcAddr << std::dec
              << " tid=" << g_guestThreadId << " n=" << g_watchStoreHits << " pc=0x" << std::hex
              << (ctx != nullptr ? ctx->pc : 0u) << " ra=0x" << (ctx != nullptr ? getRegU32(ctx, 31) : 0u)
              << " addr=0x" << guestAddr << std::dec << " size=" << size << " value=0x" << std::hex
              << static_cast<uint32_t>(value) << std::dec << " now='";
    if (g_rdramForWatch != nullptr)
    {
        for (uint32_t a = kWatchLo; a < kWatchHi && a < PS2_RAM_SIZE; ++a)
        {
            const uint8_t ch = g_rdramForWatch[a];
            std::cout << (ch >= 32 && ch < 127 ? static_cast<char>(ch) : '.');
        }
    }
    std::cout << "' raw=";
    if (g_rdramForWatch != nullptr)
    {
        for (uint32_t a = kWatchLo; a < kWatchHi && a < PS2_RAM_SIZE; ++a)
        {
            char byteText[4];
            std::snprintf(byteText, sizeof(byteText), "%02x", g_rdramForWatch[a]);
            std::cout << byteText << " ";
        }
    }
    std::cout << "\n";
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
        // R14. The R4.8 oracle measured the guest's legit delay loop at 1,048,576 iterations of a
        // single `addiu v0,v0,-1` cycle; 4096 (~256x below) misreported it as guest_cycle_no_progress.
        // Raise to clear that loop with margin while still naming a real hang.
        uint64_t maxCycleRepeats = 0x400000ull; // 4,194,304
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

// W229. WHO ZEROES sub_01008C50's saved-$ra slot at sp+0x40 between two arrivals at 0x1009004?
static uint32_t g_w229arrLo = 0u;
static uint32_t g_w229arrHi = 0u;
static bool g_w229arrArm = false;
static uint32_t g_w229arrW = 0u;
static void w229ArrStoreObserver(uint32_t addr, uint32_t size, uint64_t val,
                                 const R5900Context *c, const char *op, uint32_t)
{
    if (!g_w229arrArm) return;
    if (addr >= g_w229arrLo && addr < g_w229arrHi && g_w229arrW < 40u
        && static_cast<uint32_t>(val) == 0u)
    {
        ++g_w229arrW;
        std::cout << "VULCAN4 W229 W addr=" << toHex(addr) << " size=" << size
                  << " val=" << toHex(static_cast<uint32_t>(val))
                  << " writerPc=" << toHex(c != nullptr ? c->pc : 0u)
                  << " op=" << (op != nullptr ? op : "?") << std::endl;
    }
}

// W276. WHO WRITES THE ENGINE'S SIF0 RECORD TABLE?
//
// label_5b1180 spins on tab[0]: `while (*(uint32_t*)0x008869C0 == 0) { }` (func_5B0880 reads
// *(u32*)(0x008869C0 + 4*a0)). The channel-5 handler sub_5b0e30 IS dispatched (measured: W275 INV
// prints `dmac=1 cause=5 matching=1 [en h=0x5b0e30 arg=0x20 hasFn=1]`) and the spin still never
// clears, so either the handler does not store there, or the store goes through a path the write
// macro never sees. The `op` string is the discriminator: a generated `"WRITE32"`/`"Ps2FastWrite32"`
// is a guest CPU store, while `"SIF IOP-to-EE DMA"` / a range write is a host-side copy. Watch the
// table AND the descriptor it lives in (0x00886818 holds the pointer the handler dereferences).
// OFF unless VULCAN4_W276_TABWATCH.
// The window is settable (VULCAN4_W276_LO / VULCAN4_W276_HI, hex) so the watch can be narrowed to
// the 8-word record table alone when the wide window overflows the print cap. The cap is far above
// any plausible count for a narrow window, so a narrowed run has NO gap in the write history.
// NOTE, and it matters: this observer runs on EVERY guest store, so the bounds are resolved ONCE into
// static consts. Calling getenv() per store here was measured to change the boot path outright (the
// diverted run never reached GT4's own code at all), which is the standard probe hazard.
static uint32_t w276WinLo()
{
    static const uint32_t v = [] {
        const char *s = std::getenv("VULCAN4_W276_LO");
        return s != nullptr ? static_cast<uint32_t>(std::strtoul(s, nullptr, 16)) : 0x00886800u;
    }();
    return v;
}
static uint32_t w276WinHi()
{
    static const uint32_t v = [] {
        const char *s = std::getenv("VULCAN4_W276_HI");
        return s != nullptr ? static_cast<uint32_t>(std::strtoul(s, nullptr, 16)) : 0x00886A40u;
    }();
    return v;
}
static uint64_t &w276Hits()
{
    static uint64_t n = 0;
    return n;
}

static void w276TabStoreObserver(uint32_t addr, uint32_t size, uint64_t val,
                                 const R5900Context *c, const char *op, uint32_t srcAddr)
{
    // Always (any window) tally writes that land inside the 8-word table itself, so the halt dump can
    // state the definitive writer set for 0x008869C0..0x008869DF no matter how the window was set.
    if (addr >= 0x008869C0u && addr < 0x008869E0u)
    {
        static uint32_t s_tabN = 0;
        static uint32_t s_tabPc[16] = {0};
        if (s_tabN < 16u) s_tabPc[s_tabN] = (c != nullptr ? c->pc : 0u);
        ++s_tabN;
        std::cout << "VULCAN4 W276 TAB8 n=" << s_tabN
                  << " addr=" << toHex(addr)
                  << " val=" << toHex(static_cast<uint32_t>(val))
                  << " writerPc=" << toHex(c != nullptr ? c->pc : 0u)
                  << " op=" << (op != nullptr ? op : "?") << std::endl;
    }
    if (addr < w276WinLo() || addr >= w276WinHi()) return;
    static const uint64_t kCap = 20000u;
    uint64_t &n = w276Hits();
    ++n;
    if (n <= kCap)
    {
        std::cout << "VULCAN4 W276 TABW addr=" << toHex(addr) << " size=" << size
                  << " val=" << toHex(static_cast<uint32_t>(val))
                  << " writerPc=" << toHex(c != nullptr ? c->pc : 0u)
                  << " op=" << (op != nullptr ? op : "?")
                  << " src=" << toHex(srcAddr) << std::endl;
    }
    else if (n == kCap + 1u)
    {
        std::cout << "VULCAN4 W276 TABW ... cap " << kCap << " reached, further writes suppressed"
                  << std::endl;
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
    // W92. THE OBSERVER IS THE MOST EXPENSIVE THING IN THIS PROGRAM, AND IT WAS ON BY DEFAULT.
    //
    // Measured, 12 s wall, same guest, same binary, one boolean:
    //     observer installed    functions_entered=549    (knob_0.log)
    //     observer not installed functions_entered=4884  (knob_1.log)
    // 8.9x. Every traced guest write calls into it, and the ring record alone runs on every call.
    //
    // So the default run -- the one whose number decides whether the game is alive -- pays 8.9x for an
    // instrument nobody asked for. Install it when a diagnostic is actually requested and not before.
    // This is the same law as everything else we add being off by default, and the default had it
    // exactly backwards: the instrumentation was the default and the speed was the opt-in.
    {
        const bool diagnosticsRequested =
            (watchEnv("VULCAN4_BRANCH_WATCH", 0u) != 0u) ||
            (watchEnv("VULCAN4_SHADOW", 0u) != 0u) ||
            (watchEnv("VULCAN4_TRACE_WRITES", 0u) != 0u) ||
            (watchEnv("VULCAN4_BIGCOPY_MAX", 2u) != 2u) ||
            (watchEnv("VULCAN4_PATHSTORE_MAX", 4u) != 4u) ||
            (watchEnv("VULCAN4_WATCH_MAX", 120u) != 120u) ||
            (kScratchHi > kScratchLo) || (kWatchLo != kWatchLoDefault) || (kWatchHi != kWatchHiDefault);
        // W122 ITERATION 9. ARM THE STACK-STREAM WATCH BEFORE ANY GUEST INSTRUCTION RUNS.
        // The watch on 0x1fffba0..0x1fffbb4 was previously armed from the first func_10057F0 compare,
        // which is inside the loop it is meant to observe -- $sp is set up long before that, so it
        // could only ever report zero. Arm it here, at the harness's own guest start, and it chains
        // to whatever observer is installed below so nothing is silenced.
        ps2ArmSpStreamWatch();

        // W276. OFF unless VULCAN4_W276_TABWATCH. Arm the SIF0 record-table store watch before the
        // first guest instruction so an early writer is not missed. See w276TabStoreObserver.
        if (std::getenv("VULCAN4_W276_TABWATCH") != nullptr)
        {
            ps2AddStoreSubscriber(&w276TabStoreObserver);
            std::cout << "VULCAN4 W276 TABW armed lo=" << toHex(w276WinLo()) << " hi=" << toHex(w276WinHi())
                      << std::endl;
        }

        if (diagnosticsRequested)
        {
            ps2SetGuestStoreObserver(&watchGuestStoreForPath);
        }
        else
        {
            std::cerr << "VULCAN4 TRACE store observer NOT installed (no diagnostic knob set). "
                      << "Set VULCAN4_WATCH_LO, VULCAN4_SHADOW, VULCAN4_TRACE_WRITES or "
                      << "VULCAN4_BRANCH_WATCH to turn it on; it costs about 9x." << std::endl;
        }
    }
    // W92. Opt in to raw-pointer announcements ONLY when a diagnostic window is configured. The
    // getMemPtr hook is on the runtime's hottest path; turning it on unconditionally cost 697x of
    // guest throughput with no knob to undo it. watchEnv() is read at namespace scope above, so the
    // decision is made once, here, and the default path pays nothing.
    {
        const bool wantRaw = (kScratchHi > kScratchLo) || (kWatchLo != kWatchLoDefault) ||
                             (kWatchHi != kWatchHiDefault);
        ps2SetRawPtrObserverEnabled(wantRaw);
    }
    ps2SetGuestBranchObserver(&watchGuestCallForPath);
    g_rdramForWatch = rdram;
    g_runtimeForShadowProbe = &runtime;

    // W44. Two probes read the same guest address in the same process and DISAGREE: the store
    // observer saw '/BA' written to 0x10519C0, while the copy probe in ps2_stubs::memcpy read '05 80'
    // from that address. The only way both can be true is that they are not looking at the same
    // memory, so the harness's cached RDRAM pointer is printed against the live one. If getRDRAM()
    // can hand back a different buffer than the one the harness cached at startup, then every
    // store observation this project has made is about a stale copy and the whole line of work is
    // built on sand.
    std::cout << "VULCAN4 RDRAMPROBE cached_g_rdramForWatch=" << static_cast<const void *>(g_rdramForWatch)
              << " live_getRDRAM=" << static_cast<const void *>(runtime.memory().getRDRAM())
              << " same=" << (static_cast<const void *>(g_rdramForWatch) ==
                              static_cast<const void *>(runtime.memory().getRDRAM())
                                 ? "YES"
                                 : "NO")
              << "\n";

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

    // W241. UNIFIED FUNCTION RESOLUTION. The loader image [0x01000008,0x0102dbec) and the
    // runtime-loaded engine image [0x00100008,0x00616d94) form ONE address space: entering the
    // engine must NOT remove the loader's entries and vice versa. Every lookup consults both.
    auto resolveFunction = [](uint32_t pc) -> PS2Runtime::RecompiledFunction
    {
        if ((pc & 3u) != 0u) return nullptr;
        if (pc >= g_ps2RecompiledFunctionTableBase && pc < g_ps2RecompiledFunctionTableEnd)
        {
            const uint32_t s = (pc - g_ps2RecompiledFunctionTableBase) >> 2;
            if (s < g_ps2RecompiledFunctionTableSlotCount && g_ps2RecompiledFunctionTable[s] != nullptr)
                return g_ps2RecompiledFunctionTable[s];
        }
        if (pc >= g_ps2EngineFunctionTableBase && pc < g_ps2EngineFunctionTableEnd)
        {
            const uint32_t s = (pc - g_ps2EngineFunctionTableBase) >> 2;
            if (s < g_ps2EngineFunctionTableSlotCount && g_ps2EngineFunctionTable[s] != nullptr)
                return g_ps2EngineFunctionTable[s];
        }
        return nullptr;
    };
    auto inAnyImage = [](uint32_t pc)
    {
        return (pc >= g_ps2RecompiledFunctionTableBase && pc < g_ps2RecompiledFunctionTableEnd)
            || (pc >= g_ps2EngineFunctionTableBase && pc < g_ps2EngineFunctionTableEnd);
    };

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
    uint32_t execPS2Relaunches = 0; // W231: EE syscall 0x07 ExecPS2 relaunches performed
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
    // W108. frames_presented is declared on the display thread below and read here at report
    // time, so "the display path ran zero times" is a measured fact rather than an inference from
    // a screenshot. That distinction is the entire W107 finding.
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
    // W103. The last PC that resolved to a real generated function, and whether there ever was
    // one. Recorded on every successful table lookup so a wild jump can name its own origin.
    uint32_t lastResolvedPc = 0;
    bool lastResolvedPcValid = false;

    std::unordered_map<uint32_t, std::string> functionNames; // address -> name, first time seen
    std::map<uint32_t, GuestCall> missing;                  // address -> named call
    std::set<uint32_t> missingBoundaryAddresses;            // W241: every unmapped in-image address
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
    // W95. Turn on the guest copy-size histogram. Gated, and off by default, because it is a
    // diagnostic and law 12 is that diagnostics are opt-in.
    // W95 gate. Guarded so the harness still builds against a runtime that predates the histogram,
    // which is what makes the pre-W92 A/B possible at all.
    // W95's gate lives in the runtime (W95::g_enabled reads its own env var), deliberately: a
    // harness that calls a runtime setter will not build against a runtime that predates it, and a
    // failed harness build leaves a STALE binary that reports numbers for code that is no longer
    // there. That happened twice today and both times the number looked plausible.
    (void)0;

    // W93. ITIMER_PROF counts CPU time, not wall time, which is the right clock here: the guest is
    // compute-bound and the box is shared with a Minecraft server, so wall-clock sampling would spend
    // its samples on other people's processes.
    if (watchEnv("VULCAN4_PROF_HZ", 0u) != 0u)
    {
        W93Sampler::start(static_cast<uint32_t>(watchEnv("VULCAN4_PROF_HZ", 997u)));
    }

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

    // ---- W108 (CORRECTED). THE DISPLAY THREAD.
    //
    // One thread presents, one thread plays, and they never touch the GS lock at the same time.
    // That is not a stylistic preference: it is how the hardware works, and it is the only shape
    // in which the display cannot starve the guest. See the long note above for the measurement
    // that proved the inline version was fatal.
    //
    // Everything raylib touches from here on -- presentFrame() and SetWindowTitle() -- happens on
    // THIS thread and only this thread, because raylib's drawing calls are not thread safe and the
    // guest thread must not be inside BeginDrawing/EndDrawing while this one is. The guest thread
    // does no raylib work at all.
    std::atomic<bool> displayRunning{true};
    std::atomic<uint64_t> presentedFrames{0};
    std::atomic<bool> guestDone{false};

    // W108 SEGFAULT FIX. This was a std::thread, and that was the segfault.
    //
    // The real backtrace (gdb, DISPLAY=:0, /mnt/ssd/vulcan4-build/run/, this binary):
    //
    //   Thread 4  #0 libGLdispatch.so.0
    //             #1 rlLoadTexture ()
    //             #2 LoadTextureFromImage ()
    //             #3 PS2Runtime::presentFrame()
    //             #4 main::{lambda()#2}::operator()() const
    //   Thread 1  #0 IopTimrman::nextEventCycle
    //             ... #4 sub_0100F390_0x100f390        <- fioOpen, i.e. THE GUEST
    //
    // Two things fall out of that, and one of them contradicts the note above this function.
    //
    // (1) The crash is NOT in fioOpen. fioOpen is merely the last line the guest printed before
    //     the other thread died. The guest was healthy; Thread 4 was not.
    //
    // (2) The threads are the wrong way round. runtime.initialize() -- which calls raylib's
    //     InitWindow -- runs on the MAIN thread at line 1328, so MAIN owns the OpenGL context.
    //     The comment above this function says "raylib's drawing calls are not thread safe" and
    //     is right about the hazard, but it then put presentFrame() on a SPAWNED thread, which has
    //     no current GL context at all. rlLoadTexture -> glGenTextures/glTexImage2D on a thread
    //     that never made the context current lands in libGLdispatch and dies. It survived under
    //     xvfb because swrast takes a different path; it dies on a real desktop GL driver, which
    //     is exactly where the captain saw it.
    //
    // The correct shape is the one real hardware has: the GS renders on one thread while another
    // scans out. So the GUEST goes on a worker thread and MAIN -- the GL owner -- presents.
    auto displayLoop = [&]() {
        int64_t lastPresentNs = 0;
        uint64_t hudFramesAtMark = 0;
        uint64_t hudCyclesAtMark = 0;
        int64_t hudMarkNs = 0;

        while (displayRunning.load(std::memory_order_acquire))
        {
            const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now() - start)
                                      .count();

            // 60 Hz, the rate real hardware refreshes at. Presenting per guest iteration would
            // upload a 1.3 MB texture tens of thousands of times a second and become the
            // bottleneck, changing the very numbers the run exists to measure.
            if (nowNs - lastPresentNs >= 16666667ll)
            {
                lastPresentNs = nowNs;
                runtime.presentFrame();
                presentedFrames.store(presentedFrames.load() + 1u, std::memory_order_relaxed);

                // The window title is the HUD: the captain reads FPS and speed off the game window
                // instead of a second terminal he has to find. Same atomics the PROGRESS line
                // prints, one process, so it can never report another run's numbers.
                const uint64_t cyc = runtime.eeScheduler().snapshot().eeCycle;
                if (hudMarkNs == 0)
                {
                    hudMarkNs = nowNs;
                    hudFramesAtMark = guestFrameCounter().load();
                    hudCyclesAtMark = cyc;
                }
                else if (nowNs - hudMarkNs >= 500000000ll) // 0.5 s
                {
                    const double secs = static_cast<double>(nowNs - hudMarkNs) / 1e9;
                    const uint64_t frames = guestFrameCounter().load();
                    const uint64_t deltaFrames = frames > hudFramesAtMark ? frames - hudFramesAtMark : 0u;
                    const uint64_t deltaCycles = cyc > hudCyclesAtMark ? cyc - hudCyclesAtMark : 0u;

                    const double packetsPerSec = static_cast<double>(deltaFrames) / secs;
                    // kEeClockHz = 294,912,000 is how many EE cycles a real PS2 retires in one wall
                    // second, so (cycles retired per wall second) / kEeClockHz is exactly "PS2
                    // seconds passing per wall second". It was briefly divided by cycles per FRAME,
                    // which printed 37x for a boot that was measurably at 1.0x -- a 60x-too-high
                    // claim that survived only because nothing compared it against the ee_cycle
                    // figure printed in the same run.
                    constexpr double kEeCyclesPerPs2Second = 294912000.0;
                    const double speed =
                        static_cast<double>(deltaCycles) / secs / kEeCyclesPerPs2Second;

                    // NAMED HONESTLY: this counts GS GIF packets, not display frames. One display
                    // frame is many packets, so calling it FPS was a lie wearing a real number's
                    // clothes and the captain caught it. It says packets/sec because that is what
                    // it counts.
                    char hudTitle[128];
                    std::snprintf(hudTitle, sizeof(hudTitle),
                                  "VULCAN 4 - %.0f GS packets/s | Speed: %.2fx PS2 | %llu shown",
                                  packetsPerSec, speed,
                                  static_cast<unsigned long long>(
                                      presentedFrames.load(std::memory_order_relaxed)));
                    SetWindowTitle(hudTitle);

                    hudFramesAtMark = frames;
                    hudCyclesAtMark = cyc;
                    hudMarkNs = nowNs;
                }
            }

            // The close button and Escape. Nothing polled this before W108, because nothing cared
            // about the window except to open it. Now that it shows the game, a captain who wants
            // his desktop back presses Escape, and without this the run would continue invisibly
            // to its full budget, eating a core.
            if (WindowShouldClose())
            {
                std::cout << "VULCAN4 HARNESS window closed by the captain -- stopping the boot"
                          << std::endl;
                runtime.requestStop();
                displayRunning.store(false, std::memory_order_release);
                break;
            }

            // W108. The guest reaching its budget used to end the boot on the same thread that ran
            // it. Now the guest is the worker, so this loop would otherwise keep presenting a dead
            // frame until the watchdog fired. Exit when the worker is done.
            if (guestDone.load(std::memory_order_acquire))
            {
                displayRunning.store(false, std::memory_order_release);
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    };

    // ---- THE GUEST, ON A WORKER. MAIN IS NOW FREE TO PRESENT.
    std::thread guestThread([&]() {
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
        g_guestThreadId = runtime.eeScheduler().currentThreadId();

        R5900Context *const schedulerFrame = runtime.eeScheduler().currentContext();

        // W273. `runtime.cpu()` is PS2Runtime::m_cpuContext -- the main thread's SHADOW, refreshed
        // only at guest-return points, so its pc sits wherever the guest last returned. Falling
        // back to it whenever the scheduler has no current thread hands the driver a FROZEN frame
        // to re-enter. W272 measured the cost as exactly one wrong inflate byte at 0x1394420
        // (0x2d where the disc's own zlib says 0x7c), because a frozen copy of the LZ77 copy loop
        // was re-entered at its yield pc 0x100f800. The live frame is GuestThread::activeContext();
        // take that, and only reach for the shadow if there is no main thread at all.
        GuestThread *const mainThread = runtime.eeScheduler().thread(EeScheduler::kMainThreadId);
        R5900Context *const liveFrame = schedulerFrame != nullptr ? schedulerFrame
                                       : (mainThread != nullptr ? &mainThread->activeContext()
                                                                : nullptr);
        R5900Context &ctx = liveFrame != nullptr ? *liveFrame : runtime.cpu();

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

        // W247. OFF-by-default probe at the engine's FindAddress(0x83) loop head: args + slots.
        {
            static const bool s_w247On = (std::getenv("VULCAN4_FINDADDR") != nullptr);
            static uint32_t s_w247N = 0;
            if (s_w247On && s_w247N < 24u && (ctx.pc == 0x5B7408u || ctx.pc == 0x5B74ECu))
            {
                ++s_w247N;
                auto rw = [&](uint32_t a) -> uint32_t { uint32_t v = 0u; const uint32_t q = a & 0x1FFFFFFFu; if (q + 4u <= 0x02000000u) std::memcpy(&v, rdram + q, 4u); return v; };
                std::cout << "[w247] pc=" << toHex(ctx.pc)
                          << " a0=" << toHex(getRegU32(&ctx, 4)) << " a1=" << toHex(getRegU32(&ctx, 5))
                          << " a2=" << toHex(getRegU32(&ctx, 6)) << " a3=" << toHex(getRegU32(&ctx, 7))
                          << " v0=" << toHex(getRegU32(&ctx, 2)) << " s2=0x" << std::hex << getRegU32(&ctx, 18)
                          << " s3=0x" << getRegU32(&ctx, 19)
                          << " [1218C]=" << rw(0x1218Cu) << " [120E8]=" << rw(0x120E8u)
                          << " [8001218C]=" << rw(0x8001218Cu) << " [800120E8]=" << rw(0x800120E8u)
                          << std::dec << std::endl;
            }
        }
        // ---- W108. THE TITLE BAR IS THE HUD, AND IT LIVES ON THE DISPLAY THREAD.
        //
        // This block used to run here, in the guest's own loop, updating the window title from
        // inside the guest. That was wrong twice: raylib's drawing calls are not thread safe, and
        // the guest thread had no business in BeginDrawing/EndDrawing at all. It now runs on the
        // display thread started above, next to presentFrame(), because the title and the picture
        // are the same concern: what this window is showing you right now.
        //
        // The numbers themselves are unchanged from W101 and read from the same atomics:
        // guestFrameCounter() and EeScheduler::snapshot().eeCycle.

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
        if ((ctx.pc & 3u) != 0u || !inAnyImage(ctx.pc))
        {
            haltReason = kHaltOutOfTable;
            haltPc = ctx.pc;
            haltDetail = "pc is outside the generated function table";

            // W103. NAME THE CULPRIT. The guest dies at some address it can never have meant to
            // execute -- W100's was 0x8481e343, which is offset 0x481E343 = 72.1 MB into a 32 MB
            // machine, i.e. a value that was never a code pointer at all. Knowing the dead address
            // tells you the guest was already lost; it does NOT tell you which instruction threw
            // it, and that instruction is the entire fix.
            //
            // So keep the last PC that resolved to a real generated function and its return
            // address. On a crash the pair reads "the guest was inside <function>, and its
            // $ra already held 0x8481e343" -- which is either a bad jr/jalr target, or a return
            // onto a stack slot something else scribbled on. Those are two completely different
            // bugs and the fix for one is worthless against the other.
            // Registers are a raw __m128i r[32] with no named members, so they are read through
            // the same accessor the rest of this harness uses. On the PS2 R5900: r0 is the zero
            // register, r2 ($v0) is the return value, r29 ($sp), r31 ($ra).
            const uint32_t raNow = getRegU32(&ctx, 31);
            const uint32_t spNow = getRegU32(&ctx, 29);
            const uint32_t v0Now = getRegU32(&ctx, 2);
            const uint32_t s0Now = getRegU32(&ctx, 16);

            std::cout << "VULCAN4 WILDPC dead=" << toHex(haltPc)
                      << " last_good=" << (lastResolvedPcValid ? toHex(lastResolvedPc)
                                                               : std::string("NONE"))
                      << " ra=" << toHex(raNow) << " sp=" << toHex(spNow)
                      << " v0=" << toHex(v0Now) << " s0=" << toHex(s0Now) << std::endl;

            // W195 (Sanji). WHICH MECHANISM? For a `jr $ra` return out of sub_0100AE78 the prologue
            // saved $ra at (entry_sp - 112) + 104 == entry_sp - 8, and the epilogue's delay slot
            // `addiu sp,sp,112` has already restored sp to entry_sp by the time we get here. So the
            // slot the return read is sp-8. Print a window of stack words around it: if [sp-8] == ra
            // the slot itself was written (mechanism 3); if [sp-8] != ra the restore read elsewhere
            // (mechanism 2); if the whole window is data-looking, something bulk-copied over it.
            {
                auto rd = [&](int32_t delta) -> uint32_t
                {
                    const int64_t a = static_cast<int64_t>(spNow) + delta;
                    uint32_t w = 0u;
                    if (a >= 0 && a + 4 <= 0x02000000)
                    {
                        std::memcpy(&w, rdram + a, 4);
                    }
                    return w;
                };
                std::cout << "VULCAN4 W195 slotwin sp=" << toHex(spNow)
                          << " [sp-16]=" << toHex(rd(-16))
                          << " [sp-12]=" << toHex(rd(-12))
                          << " [sp-8]=" << toHex(rd(-8))
                          << " [sp-4]=" << toHex(rd(-4))
                          << " [sp]=" << toHex(rd(0))
                          << " [sp+4]=" << toHex(rd(4))
                          << " [sp+8]=" << toHex(rd(8))
                          << " | ra==[sp-8]? " << ((rd(-8) == raNow) ? "YES (slot written)" : "NO (wrong slot / elsewhere)")
                          << std::endl;
            }
            break;
        }

        PS2Runtime::RecompiledFunction resolvedFn = resolveFunction(ctx.pc);
        if (resolvedFn == nullptr)
        {
            missingBoundaryAddresses.insert(ctx.pc);

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
        // W103. Remember this PC: it is the last place the guest was verifiably executing real
        // recompiled code, so if the next PC is garbage, this is where the guest went wrong.
        lastResolvedPc = ctx.pc;
        lastResolvedPcValid = true;
        if (functionNames.find(ctx.pc) == functionNames.end())
        {
            std::ostringstream name;
            name << "func_" << toHex(ctx.pc);
            functionNames.emplace(ctx.pc, name.str());
            pcOrder.push_back(ctx.pc);
            ++distinctPcs;
        }

        ++functionsEntered;
        // W226c. WHERE DOES THE SPIN'S FRAME ENTER? The harness arrival loop enters functions by
        // TABLE LOOKUP at ctx.pc (vulcan4_harness.cpp:2409), which bypasses dispatchGuestBranch, so the
        // W221 probe cannot see it. Log every arrival at a pc in the spin region or func_10057F0, with
        // the predecessor, so the entry path is measured. OFF unless VULCAN4_W226_ARRIVE.
        {
            static const bool s_w226On = (std::getenv("VULCAN4_W226_ARRIVE") != nullptr);
            static int s_w226N = 0;
            const bool spinRegion = (ctx.pc >= 0x10088e8u && ctx.pc < 0x1009068u) ||
                                    (ctx.pc == 0x10057f0u);
            if (s_w226On && s_w226N < 60 && spinRegion)
            {
                ++s_w226N;
                std::cout << "VULCAN4 W226 ARRIVE pc=" << toHex(ctx.pc)
                          << " ra=" << toHex(getRegU32(&ctx, 31))
                          << " sp=" << toHex(getRegU32(&ctx, 29))
                          << " prev=" << (lastResolvedPcValid ? toHex(lastResolvedPc) : std::string("NONE"))
                          << " n=" << functionsEntered << std::endl;
            }
        }
        // W229r. IS THE DECODER FUN_0100F390 RESUMED at an interior pc (a yield)? Log arrivals in
        // [0x100f390,0x100f8c8) that are NOT the entry. OFF unless VULCAN4_W229_DRES.
        {
            static const bool s_w229dresOn = (std::getenv("VULCAN4_W229_DRES") != nullptr);
            static int s_w229dresN = 0;
            if (s_w229dresOn && s_w229dresN < 2000 && ctx.pc >= 0x100f390u && ctx.pc < 0x100f8c8u
                && ctx.pc != 0x100f390u)
            {
                ++s_w229dresN;
                std::cout << "VULCAN4 W229 DRES pc=" << toHex(ctx.pc)
                          << " ra=" << toHex(getRegU32(&ctx, 31))
                          << " sp=" << toHex(getRegU32(&ctx, 29))
                          << " s1=0x" << toHex(getRegU32(&ctx, 17))
                          << " t1=0x" << toHex(getRegU32(&ctx, 9))
                          << " rt1=0x" << toHex(getRegU32(&runtime.cpu(), 9))
                          << " rs1=0x" << toHex(getRegU32(&runtime.cpu(), 17))
                          << " rtid=" << runtime.eeScheduler().currentThreadId()
                          << " prev=" << (lastResolvedPcValid ? toHex(lastResolvedPc) : std::string("NONE"))
                          << std::endl;
            }
        }
        // W229s. DOES THE DRIVER FUN_0100F8C8 RESUME at its post-jal pc (0x1010a70) while the decoder
        // is still suspended? Log arrivals there. OFF unless VULCAN4_W229_DDRIVE.
        {
            static const bool s_w229ddOn = (std::getenv("VULCAN4_W229_DDRIVE") != nullptr);
            static int s_w229ddN = 0;
            if (s_w229ddOn && s_w229ddN < 200 && ctx.pc == 0x1010a70u)
            {
                ++s_w229ddN;
                std::cout << "VULCAN4 W229 DDRIVE pc=0x1010a70 ra=" << toHex(getRegU32(&ctx, 31))
                          << " sp=" << toHex(getRegU32(&ctx, 29))
                          << " s7(out)=0x" << toHex(getRegU32(&ctx, 23))
                          << " prev=" << (lastResolvedPcValid ? toHex(lastResolvedPc) : std::string("NONE"))
                          << std::endl;
            }
        }
        // W229. SHAPE-A DERail: the main thread resumes sub_01008C50 at label 0x1009004 (the merge
        // call's fallthrough) and its epilogue restores $ra from sp+0x40 = 0. Probe that slot.
        // OFF unless VULCAN4_W229_ARR.
        {
            static const bool s_w229arrOn = (std::getenv("VULCAN4_W229_ARR") != nullptr);
            static int s_w229arrN = 0;
            if (s_w229arrOn && s_w229arrN < 40 && ctx.pc == 0x1009004u)
            {
                ++s_w229arrN;
                static bool s_w229arrReg = false;
                if (!s_w229arrReg) { ps2AddStoreSubscriber(&w229ArrStoreObserver); s_w229arrReg = true; }
                if (s_w229arrN == 1)
                {
                    g_w229arrLo = (getRegU32(&ctx, 29) + 0x40u) & 0x01FFFFFFu;
                    g_w229arrHi = g_w229arrLo + 8u;
                    g_w229arrArm = true;
                }
                else if (s_w229arrN == 2)
                {
                    g_w229arrArm = false;
                }
                uint32_t saved = 0u;
                const uint32_t so = (getRegU32(&ctx, 29) + 0x40u) & 0x01FFFFFFu;
                if (so + 4u <= 0x02000000u) std::memcpy(&saved, rdram + so, 4);
                std::cout << "VULCAN4 W229 ARR9004 n=" << s_w229arrN
                          << " ra=" << toHex(getRegU32(&ctx, 31))
                          << " sp=" << toHex(getRegU32(&ctx, 29))
                          << " savedAtSp40=" << toHex(saved)
                          << " s3=" << toHex(getRegU32(&ctx, 19))
                          << " prev=" << (lastResolvedPcValid ? toHex(lastResolvedPc) : std::string("NONE"))
                          << std::endl;
            }
        }
        // W227. THE MERGE INPUTS, dumped on OUR side to compare against PCSX2 ground truth.
        // Real GT4 reaches sub_010088E8 with a0/a1/a2/a3 pointing at populated structs
        // (count(+8)=0x20, real data buffers). This prints the same four structs in our recomp.
        // OFF unless VULCAN4_W227_MERGE.
        {
            static const bool s_w227On = (std::getenv("VULCAN4_W227_MERGE") != nullptr);
            static int s_w227N = 0;
            if (s_w227On && s_w227N < 8 && ctx.pc >= 0x10088e8u && ctx.pc < 0x1009068u)
            {
                ++s_w227N;
                auto rd32 = [&](uint32_t a) -> uint32_t {
                    const uint32_t o = a & 0x1FFFFFFFu;
                    if (o > 0x02000000u - 4u) return 0u;
                    return static_cast<uint32_t>(rdram[o]) |
                           (static_cast<uint32_t>(rdram[o + 1u]) << 8) |
                           (static_cast<uint32_t>(rdram[o + 2u]) << 16) |
                           (static_cast<uint32_t>(rdram[o + 3u]) << 24);
                };
                std::cout << "VULCAN4 W227 MERGE n=" << s_w227N
                          << " pc=" << toHex(ctx.pc)
                          << " a0=" << toHex(getRegU32(&ctx, 4))
                          << " a1=" << toHex(getRegU32(&ctx, 5))
                          << " a2=" << toHex(getRegU32(&ctx, 6))
                          << " a3=" << toHex(getRegU32(&ctx, 7))
                          << " s2=" << toHex(getRegU32(&ctx, 18))
                          << " ra=" << toHex(getRegU32(&ctx, 31))
                          << " sp=" << toHex(getRegU32(&ctx, 29)) << std::endl;
                const uint32_t regs[4] = { getRegU32(&ctx, 4), getRegU32(&ctx, 5),
                                           getRegU32(&ctx, 6), getRegU32(&ctx, 7) };
                for (int i = 0; i < 4; ++i)
                {
                    const uint32_t p = regs[i];
                    const uint32_t base = rd32(p + 0x14u);
                    std::cout << "VULCAN4 W227 STRUCT[" << i << "] p=" << toHex(p)
                              << " count(+8)=" << toHex(rd32(p + 8u))
                              << " fC(+C)=" << toHex(rd32(p + 0xCu))
                              << " base(+14)=" << toHex(base)
                              << " w0=" << toHex(rd32(base))
                              << " w1=" << toHex(rd32(base + 4u)) << std::endl;
                }
            }
        }
        // W96. PROGRESS, ON BY DEFAULT, ONE LINE PER 50,000 ENTRIES. With the store observer now
        // opt-in the default run prints nothing at all until it ends, which makes a four-hour run
        // indistinguishable from a hung one -- and "calling a slow run dead" is this project's most
        // expensive documented habit. One line per 50,000 entries is free, and it is the difference
        // between watching the boot climb and guessing whether it is still climbing.
        if ((functionsEntered % 50000u) == 0u)
        {
            // W101. FRAMES= is the live count from the same atomic the north star increments.
            // The `VULCAN4 FRAME source=guest` line deliberately prints only the first 64 frames
            // and then every 1000th, so counting those lines gives a frame rate that updates once
            // every ~19 minutes at boot speed -- technically honest, practically useless, and it is
            // the only way the captain can see the frame rate at all. Reading the counter itself
            // costs one atomic load and makes this line the real heartbeat.
            //
            // The counter lives in an anonymous namespace inside gs_frontend.cpp, so it is declared
            // here rather than included. That is a real coupling and it is the reason this is
            // W101's smallest change and not a wider refactor: it binds the harness to a private
            // symbol that a future runtime refactor can rename. If that rename ever happens, the
            // link fails loudly at build time -- never silently at runtime.
            std::cout << "VULCAN4 PROGRESS entry=" << functionsEntered
                  << " TRUE_ENTRIES=" << ps2_log::entryCounter().load()
                      << " FRAMES=" << guestFrameCounter().load()
                      << " distinct=" << distinctPcs
                      << " wall_ms="
                      << std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count()
                      << " gs_kicks=" << g_gsKickCount << std::endl;
        }
        ++pcEntryCounts[ctx.pc];
        entryCallers[ctx.pc].insert(getRegU32(&ctx, 31));

        // W188 (Sanji). HOW IS 0x1000BA0 ENTERED? The runtime dispatch probe never sees a
        // targetPc==0x1000BA0, yet its fade-loop call site 0x1000CEC runs -- so it arrives through
        // THIS loop, not through dispatchGuestBranch. Log the first entries into the function range
        // with the predecessor PC, so the mechanism is a measurement, not a theory. OFF unless
        // VULCAN4_W188_DISP.
        {
            static const bool s_w188hOn = (std::getenv("VULCAN4_W188_DISP") != nullptr);
            static int s_w188hN = 0;
            if (s_w188hOn && s_w188hN < 40 && ctx.pc >= 0x1000ba0u && ctx.pc < 0x1000db0u)
            {
                ++s_w188hN;
                std::cout << "VULCAN4 W188 ENTER pc=" << toHex(ctx.pc)
                          << " ra=" << toHex(getRegU32(&ctx, 31))
                          << " sp=" << toHex(getRegU32(&ctx, 29))
                          << " prev=" << (lastResolvedPcValid ? toHex(lastResolvedPc) : std::string("NONE"))
                          << std::endl;
            }
        }
        // W206 (Sanji). THE PARSE'S BYTE-COPY LOOP AT 0x100F800. The decompressor's inner copy loop is
        // where tid1 parks between yields; log its bound registers so "converging" vs "stuck" is a
        // number. OFF unless VULCAN4_W206_COPY.
        {
            static const bool s_w206On = (std::getenv("VULCAN4_W206_COPY") != nullptr);
            static int s_w206N = 0;
            if (s_w206On && s_w206N < 40 && ctx.pc == 0x100f800u)
            {
                ++s_w206N;
                std::cout << "VULCAN4 W206 COPY pc=0x100f800"
                          << " s1=0x" << toHex(getRegU32(&ctx, 17))
                          << " s2=0x" << toHex(getRegU32(&ctx, 18))
                          << " s3=0x" << toHex(getRegU32(&ctx, 19))
                          << " t1=0x" << toHex(getRegU32(&ctx, 9))
                          << " t4=0x" << toHex(getRegU32(&ctx, 12)) << std::endl;
            }
        }
        // W201 (Sanji). IS sub_0100AE78 ENTERED AT A RESUME LABEL (prologue skipped)? If the harness
        // re-enters it at a pc other than 0x100AE78, `sd ra,104(sp)` never ran for that entry and the
        // epilogue reads whatever the stack held. OFF unless VULCAN4_W201_RESUME.
        {
            static const bool s_w201On = (std::getenv("VULCAN4_W201_RESUME") != nullptr);
            static int s_w201N = 0;
            if (s_w201On && s_w201N < 40 && ctx.pc >= 0x100ae78u && ctx.pc < 0x100b048u)
            {
                ++s_w201N;
                std::cout << "VULCAN4 W201 ENTER pc=" << toHex(ctx.pc)
                          << " ra=" << toHex(getRegU32(&ctx, 31))
                          << " sp=" << toHex(getRegU32(&ctx, 29))
                          << " slot[sp-8]=" << toHex([&]{ uint32_t w=0; const uint32_t a=(getRegU32(&ctx,29)-8u)&0x01FFFFFFu; if(a+4<=0x02000000u) std::memcpy(&w, rdram+a, 4); return w; }())
                          << " prev=" << (lastResolvedPcValid ? toHex(lastResolvedPc) : std::string("NONE"))
                          << std::endl;
            }
        }

        // W43. Interrupt counters at the halt. "The delivery path works" and "the guest takes
        // interrupts" are different claims; only the second matters to the boot, so both are printed.
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
            resolvedFn(rdram, &ctx, &runtime);
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

        // ---- W108 (CORRECTED). THE DISPLAY IS A SEPARATE THREAD, NOT A CALL IN THIS LOOP.
        //
        // FIRST ATTEMPT, AND WHY IT WAS WRONG. I called presentFrame() inline here, from inside
        // the guest's own drive loop. It broke the game. Measured, same binary:
        //
        //     W106 (no display call at all)   gs_packets=1244
        //     W108 first attempt              gs_packets=1669
        //     W108 first attempt              gs_packets=1094
        //     W108 second attempt             gs_packets=  29
        //
        // and the boot report said runningThreadId=0: no thread was running at all, both threads
        // Ready with waitReason=0. UploadFrame() -> latchHostPresentationFrame() takes the GS
        // lock, which is the SAME lock the guest needs in order to submit a GIF packet. Presenting
        // 3407 times a minute from the guest's own thread starved it: the guest could not draw
        // while I was busy painting. Four days of a black screen became a black screen AND a dead
        // game, which is worse, and it was caused by the fix for the black screen.
        //
        // THE CORRECT SHAPE IS THE ONE run() ALWAYS USED: the game runs on its own thread and the
        // window presents from the main thread, exactly as real hardware scans out while the GS
        // renders. Guest and display never contend for the same lock, because they never run on the
        // same thread. Presenting on the main thread is also the only place raylib's window events
        // get polled, so this is where the close button finally starts working too.

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

        // ---- ExecPS2 (EE syscall 0x07): the guest loader re-execs the game.
        //
        // On hardware the kernel clears all state and starts a fresh priority-0 main thread at
        // `entry`. The syscall handler cannot do that in place (its stub would resume the old
        // frame and this loop holds a reference to the current thread's context), so it recorded
        // the request and stopped the guest. Here -- after the invocation has returned, so no
        // context reference is live -- rebuild a fresh launch context, clear kernel state, and
        // restart the loop. RDRAM is deliberately NOT touched: the loaded game data must survive
        // the relaunch, which is the whole point of ExecPS2 on this game.
        {
            uint32_t xEntry = 0u, xGp = 0u, xArgc = 0u, xArgv = 0u;
            if (runtime.consumePendingExecPS2(xEntry, xGp, xArgc, xArgv))
            {
                // Fail LOUDLY and honestly if the relaunch target is not part of the recompiled
                // guest image. GT4's loader passes entry=0x100008 (low RDRAM, the 0x100000 page),
                // which is NOT in the ELF's PT_LOAD .text (0x1000000). The recompiler emits guest
                // code from the ELF only, so it cannot execute runtime-loaded/relocated code.
                const bool xMapped = (resolveFunction(xEntry) != nullptr);
                if (xMapped)
                {
                    std::cout << "VULCAN4 EXECPS2 -> unified resolve entry=" << toHex(xEntry)
                              << " (engine " << toHex(g_ps2EngineFunctionTableBase) << ","
                              << toHex(g_ps2EngineFunctionTableEnd) << ")" << std::endl;
                }
                if (!xMapped)
                {
                    std::cerr << "VULCAN 4 LIMITATION: ExecPS2 (EE syscall 0x07) entry "
                              << toHex(xEntry) << " is outside the recompiled guest image ["
                              << toHex(tableBase) << "," << toHex(tableEnd) << ") — GT4's loader "
                                 "re-executes into low RDRAM (the 0x100000 page), which is not part "
                                 "of the ELF's PT_LOAD .text and not in the engine table either; "
                                 "the engine image was not recompiled for this entry." << std::endl;
                    runtime.clearStop();
                    haltReason = "execps2_unmapped_entry";
                    haltPc = xEntry;
                    haltDetail = "ExecPS2 entry " + toHex(xEntry) + " outside the generated table ["
                                 + toHex(tableBase) + "," + toHex(tableEnd) + ")";
                    break;
                }

                R5900Context fresh{};
                fresh.pc = xEntry;
                fresh.r[4] = _mm_set_epi64x(0, static_cast<int64_t>(xArgc));
                fresh.r[5] = _mm_set_epi64x(0, static_cast<int64_t>(xArgv));
                fresh.r[28] = _mm_set_epi64x(0, static_cast<int64_t>(xGp));
                fresh.r[29] = _mm_set_epi64x(0, static_cast<int64_t>(PS2_RAM_SIZE - 0x10u));
                runtime.clearStop();
                runtime.eeScheduler().reset(rdram, fresh);
                previousPc = 0xffffffffu;
                ++execPS2Relaunches;
                std::cout << "VULCAN4 EXECPS2 relaunch#" << execPS2Relaunches
                          << " entry=" << toHex(xEntry) << " gp=" << toHex(xGp)
                          << " argc=" << xArgc << " argv=" << toHex(xArgv) << std::endl;
                continue;
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
    guestDone.store(true, std::memory_order_release);
    });

    // ---- THE DISPLAY, ON MAIN, WHICH IS THE THREAD THAT OWNS THE GL CONTEXT.
    displayLoop();

    if (guestThread.joinable())
    {
        guestThread.join();
    }

    // Measured BEFORE the join, because the gap between this and elapsed is the harness
    // waiting on something of its own making, and a report that only prints the total invites
    // exactly the misreading that cost this project two dishes (W7's detector, W8's watchdog).
    const auto guestElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - start)
                                  .count();

    driverFinished.store(true, std::memory_order_release);

    // W108. Stop and join the display thread BEFORE the report is printed, so frames_presented is
    // final by the time anyone reads it. The flag is cleared first and then joined: clearing it is
    // what tells the thread to leave its loop, and joining is what guarantees it has left, so the
    // counter cannot still be climbing while the BOOT REPORT claims a number for it. Joining before
    // the report would deadlock nothing -- the display thread never waits on the guest -- but
    // joining after would make the number a lie in the only direction that matters.
    displayRunning.store(false, std::memory_order_release);
    // W108: there is no display thread to join any more. displayLoop() above ran on THIS thread and
    // has already returned, so joining it would be joining the current thread.
    runtime.requestStop(); // tell the watchdog thread to exit its wait loop
    watchdog.join();

    // W273. THE ENGINE IMAGE, OFF THE MACHINE.
    //
    // `VULCAN4_RDRAM_DUMP=<addr>:<len>[:<path>]` (hex addr, hex len) writes that slice of RDRAM to a
    // file once the guest has stopped. Three probes already cover this ground and none of them can
    // answer the actual question:
    //   - W30BIGCOPY says WHERE a copy went and HOW BIG it was, but not what landed there;
    //   - W45BEFORE snapshots 4096 bytes of RDRAM BEFORE a store, which is the wrong side of it;
    //   - the store observer sees individual writes, not the finished image.
    // "Is GT4's engine image correct in RDRAM?" is a whole-image question, so it needs a whole-image
    // dump. Default path is under the run dir; the length is clamped to RDRAM so a typo cannot read
    // off the end of the array. Env-gated: unset means this block does not execute and the run is
    // byte-for-byte what it was before.
    if (const char *dumpSpec = std::getenv("VULCAN4_RDRAM_DUMP"))
    {
        unsigned dumpAddr = 0u;
        unsigned dumpLen = 0u;
        char dumpPath[512] = {0};
        const int dumpFields = std::sscanf(dumpSpec, "%x:%x:%511s", &dumpAddr, &dumpLen, dumpPath);
        if (dumpFields >= 2)
        {
            const char *const resolvedDumpPath =
                dumpFields >= 3 ? dumpPath : "/mnt/ssd/vulcan4-build/run/rdram_dump.bin";
            constexpr unsigned kRdramBytes = 0x02000000u; // 32 MB, the EE's flat RDRAM window
            if (dumpAddr < kRdramBytes)
            {
                const unsigned clampedLen =
                    dumpLen > (kRdramBytes - dumpAddr) ? (kRdramBytes - dumpAddr) : dumpLen;
                std::ofstream dumpOut(resolvedDumpPath, std::ios::binary | std::ios::trunc);
                if (dumpOut)
                {
                    dumpOut.write(reinterpret_cast<const char *>(rdram + dumpAddr),
                                  static_cast<std::streamsize>(clampedLen));
                    dumpOut.flush();
                    std::cout << "VULCAN4 RDRAM_DUMP addr=0x" << std::hex << dumpAddr << " len=0x"
                              << clampedLen << std::dec << " path=" << resolvedDumpPath
                              << " ok=" << (dumpOut.good() ? 1 : 0) << "\n";
                }
                else
                {
                    std::cout << "VULCAN4 RDRAM_DUMP addr=0x" << std::hex << dumpAddr << " len=0x"
                              << clampedLen << std::dec << " path=" << resolvedDumpPath
                              << " ok=0 (open failed)\n";
                }
            }
            else
            {
                std::cout << "VULCAN4 RDRAM_DUMP addr=0x" << std::hex << dumpAddr << std::dec
                          << " REFUSED: outside RDRAM\n";
            }
        }
        else
        {
            std::cout << "VULCAN4 RDRAM_DUMP REFUSED: expected <hexaddr>:<hexlen>[:<path>], got '"
                      << dumpSpec << "'\n";
        }
    }

    // W93. Stop sampling and print the histogram. After the watchdog join, so the samples cover the
    // boot and nothing else, and before the thread dump so a profile failure cannot cost us the
    // thread state.
    if (watchEnv("VULCAN4_PROF_HZ", 0u) != 0u)
    {
        W93Sampler::stop();
    }

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
    // W97. true_guest_entries is the number that matters and functions_entered never was: the harness
    // counter is incremented once per OUTER DISPATCHER ITERATION, and the dispatcher inlines callees
    // (W71), so it under-reports real guest function entries by roughly a thousand. ps2_log's counter
    // is incremented inside every recompiled function, so it counts what actually ran. Both are
    // printed, and the honest one is labelled.
    std::cout << "VULCAN4 MISSING-BOUNDARIES n=" << missingBoundaryAddresses.size() << " :";
    for (uint32_t a : missingBoundaryAddresses) std::cout << " 0x" << toHex(a);
    std::cout << std::endl;
    // W276. Dump the SIF0 record table the engine is parked on. label_5b1180 spins on tab[0] and the
    // whole point of the dish is whether anything ever made it non-zero. OFF unless the watch knob is
    // set, so the default run is byte-for-byte unchanged.
    if (std::getenv("VULCAN4_W276_TABWATCH") != nullptr)
    {
        std::cout << "VULCAN4 W276 TABDUMP";
        for (uint32_t i = 0u; i < 8u; ++i)
        {
            const uint32_t off = 0x008869C0u + 4u * i;
            uint32_t v = 0u;
            if (off + 4u <= 0x02000000u)
            {
                std::memcpy(&v, rdram + off, 4);
            }
            std::cout << " [" << i << "]=" << toHex(v);
        }
        uint32_t descriptorPtr = 0u;
        std::memcpy(&descriptorPtr, rdram + 0x00886818u, 4);
        std::cout << " descPtr@0x00886818=" << toHex(descriptorPtr) << std::endl;
    }

    std::cout << "VULCAN4 BOOT REPORT functions_entered=" << functionsEntered
              << " true_guest_entries=" << ps2_log::entryCounter().load()
              << " true_guest_exits=" << ps2_log::exitCounter().load()
              << " halt=" << haltReason
              << " bios_files=" << biosFilesOpened
              // W106. THE INTERRUPT NUMBERS THIS PROJECT ACTUALLY USES, AND THE ONES IT DOES NOT.
              //
              // intr_queued / intr_run are interrupt invocations created and RUN, dispatched straight to
              // the guest's registered handler. That is how interrupts are delivered here, and those are
              // the numbers to read.
              //
              // cop0_raised / cop0_delivered are printed as INAPPLICABLE, not as zeros, because this
              // design never calls raiseInterrupt() or servicePendingInterrupt(): there is no COP0
              // interrupt to raise or take. W105 read those zeros as "the interrupt path is dead", went
              // and made them non-zero by raising Cause.IP, and the guest then vectored to
              // 0x80000080 and entered 71 functions instead of 22,518. A zero printed beside a live number
              // is a trap, and this one cost a session.
              << " intr_queued=" << kernelSnapshot.invocationsQueued
              << " intr_run=" << kernelSnapshot.invocationsRun
              << " intr_run_by_kind=" << kernelSnapshot.invocationsRunByKind[0]
              // W277/R4. Prove the IOP LLE actually executes: the R3000A interpreter's instruction
              // counter, so "the IOP runs the disc's own IRX" is a number, not a claim.
              << " iop_instructions=" << runtime.iopDebugSnapshot().emulatorInstructions
              << " iop_modules=" << runtime.iopDebugSnapshot().emulatorLoadedModules
              // W107. LINK 1 OF THE DISPLAY CHAIN, as a number. The window stays magenta until this is
              // non-zero: the guest has to deliver a FRAME register (0x04/0x05) to tell the GS which
              // RDRAM address holds the picture, and nothing downstream can substitute for that.
              << " gs_packets=" << runtime.gs().gsPacketsSeen()
              // W108. frames_presented belongs ON THE REPORT LINE, not only in the HARNESS detail
              // line further up. It is the number that answers "does the captain see anything", and
              // it is the only number that distinguishes a display path that ran from one that is
              // dead code again -- which is exactly what four days of black window was. Leaving it
              // on the detail line means a run that ends early prints a report with no display
              // number at all, and the reader has to know to go looking somewhere else.
              << " frames_presented=" << presentedFrames.load(std::memory_order_relaxed)
              << " gs_frame_reg_writes=" << runtime.gs().gsFrameRegWrites()
              << " (ctx0=" << runtime.gs().gsFrameRegWritesCtx0()
              << " ctx1=" << runtime.gs().gsFrameRegWritesCtx1() << ")"
              << " cop0_raised=INAPPLICABLE(" << runtime.interruptsRaised() << ")"
              << " cop0_delivered=INAPPLICABLE(" << runtime.interruptsDelivered() << ")"
              << " pending_ip=0x" << std::hex << runtime.pendingInterrupts() << std::dec << "\n";

    // W91. WHO IS STILL ALIVE AT THE END. Every "the guest is waiting for something" question in this
    // campaign has been answered by inference from counters, and a counter cannot say whether the
    // thread that is supposed to answer is still running. EeKernelSnapshot is public and carries
    // every thread's id, status, pc, priority and wait reason, so the question is one dump away and
    // there is no excuse for another round of guessing.
    {
        const EeKernelSnapshot snap = runtime.eeScheduler().snapshot();
        std::cout << "VULCAN4 THREADS eeCycle=" << snap.eeCycle
                  << " nextEventCycle=" << snap.nextEventCycle
                  << " runningThreadId=" << snap.runningThreadId
                  << " count=" << snap.threads.size() << "\n";
        for (const EeThreadSnapshot &t : snap.threads)
        {
            static const char *kStatus[] = {"Running", "Ready", "Waiting", "WaitingSuspended",
                                            "Suspended", "Dormant"};
            const char *status = kStatus[static_cast<size_t>(t.status)];
            std::cout << "    THREAD id=" << t.id
                      << " status=" << status
                      << " prio=" << t.currentPriority
                      << " pc=0x" << std::hex << t.pc
                      << " ra=0x" << t.ra
                      << " sp=0x" << t.sp
                      << " entry=0x" << t.entry
                      << " waitReason=" << static_cast<int>(t.waitReason)
                      << " wakeupCount=" << std::dec << t.wakeupCount << std::endl;
        }
        for (const EeSemaphoreSnapshot &sem : snap.semaphores)
        {
            std::cout << "    SEM id=" << sem.id << " count=" << sem.count
                      << " waiters=" << sem.waiters << std::endl;
        }
        for (const EeEventFlagSnapshot &flag : snap.eventFlags)
        {
            std::cout << "    EVENTFLAG id=" << flag.id << " waiters=" << flag.waiters << std::endl;
        }
    }

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
            // W30. sub_01010EA8 computed a 16,410,192-byte memcpy length from two guest globals:
            //   s0 = (*(0x1033214) + 0xF) & ~0xF ; size = *(0x1033218) - s0
            // and copied to 0x10519b0, which is 0x60 bytes BELOW the memory-card path buffer -- so
            // the "path" sceMcOpen reads is residue from this copy, not something the guest wrote as
            // a path. If these two globals hold garbage, the length is garbage and this is the root
            // cause of everything in W29 and W30. Read them before theorising about the path.
            std::cout << "VULCAN4 W30LEN *(0x1033214)=0x" << std::hex << qword(0x01033214u) << std::dec
                      << " *(0x1033218)=0x" << std::hex << qword(0x01033218u) << std::dec
                      << " copy_dst=0x10519b0 copy_size=16410192"
                      << " big_copy_count=" << g_watchBigCopyHits << std::endl;

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
              << " vblanks_processed=" << kernelSnapshot.vblankStartProcessed
              << " frames_presented=" << presentedFrames.load(std::memory_order_relaxed)
              << " intr_queued=" << kernelSnapshot.invocationsQueued
              << " intr_run=" << kernelSnapshot.invocationsRun
              << " intr_run_by_kind=" << kernelSnapshot.invocationsRunByKind[0]
              << " step_intr_run=" << kernelSnapshot.stepIntrRun
              << " irq_q=" << kernelSnapshot.irqQueuedOnly
              << " irq_attach=" << kernelSnapshot.irqAttached
              << " irq_runsite=" << kernelSnapshot.irqRunSite
              << " irq_done=" << kernelSnapshot.irqCompleted
              << " pending_now=" << kernelSnapshot.pendingInvocationsNow
              << " pending_hi=" << kernelSnapshot.pendingInvocationsHighWater
              << " thread_attached=" << kernelSnapshot.threadInvocationsAttached
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
              << " sleepCurrentCalls=" << runtime.eeScheduler().sleepCurrentCalls()
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
