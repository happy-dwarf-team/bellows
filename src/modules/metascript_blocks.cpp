// Single-pass resolution of the conditional blocks [[param] ...] of scripted
// effects/triggers (lab docs: docs/startup-metascript-blocks.md).
//
// Before substituting $param$ (metascript_subst), the game resolves the
// blocks of the expanded script (0xDB1410, called once from 0xDB0860). It
// walks the blocks from last to first; for each it checks whether an argument
// with the block's parameter name exists (linear search), keeps the block's
// body when that differs from the block's negation flag, and rebuilds the
// whole text as prefix + body-or-nothing + suffix, the parts and the result
// assigned with strlen (cut at the first NUL). The cost is ~ blocks x length:
// Anbennar's anb_define_long_lived_heir (411 KB, ~6800 blocks) takes ~0.6 s
// per expansion.
//
// For blocks that are sorted, non-overlapping and inside the text, and for
// NUL-free text and kept bodies, that equals one forward pass: text[0:s0] +
// body0-or-nothing + text[e0:s1] + ... + text[en:]. The call site is
// redirected to SB_MetascriptBlocks, which checks those preconditions, builds
// the result once and stores it with the game's own string assign. Anything
// else goes to the original. "verify" = compute ours, run the original,
// compare (the game keeps the original's result); "on" = ours, every 256th
// call with blocks compared (on a mismatch the original's result stays and the
// module disables itself). A sample that falls on a call whose original would
// be slow (blocks x length above kSampleWorkLimit; on Anbennar one such call
// costs ~0.6 s) moves to the next call below the limit. Not used once the game
// is ready.
#include "modules.hpp"

#include <climits>
#include <string>

namespace {

// Game containers: data pointer at +0x08, int count at +0x14.
constexpr std::size_t kVecData = 0x08;
constexpr std::size_t kVecCount = 0x14;
constexpr std::size_t kArgStride = 0x40;  // key string +0x00, value string +0x20
// The expanded script's block container at +0x70 of the first argument.
constexpr std::size_t kBlockData = 0x70 + kVecData;
constexpr std::size_t kBlockCount = 0x70 + kVecCount;
constexpr std::size_t kBlockStride = 0x50;  // name string +0x00, body string +0x20, int start +0x40, int end +0x44
constexpr std::size_t kBlockBody = 0x20;
constexpr std::size_t kBlockStart = 0x40;
constexpr std::size_t kBlockEnd = 0x44;
constexpr std::size_t kBlockNegate = 0x48;  // byte: the body is kept when it differs from "argument present"
// "on": the largest blocks x text length a sampled comparison runs the original
// for (the original rebuilds the text once per block; ~4 ms at this size).
constexpr std::uint64_t kSampleWorkLimit = std::uint64_t{1} << 24;

using BlocksFn = void (*)(void*, void*, void*);
using AssignFn = void* (*)(void*, const char*, std::size_t);

// The rest of the call site's chain (the game's block resolution unless
// another plugin redirected the site too).
BlocksFn g_original = nullptr;
anvil::FunctionHook<void(void*, void*, void*)> g_site;
AssignFn g_assign = nullptr;
bool g_verifyOnly = false;
volatile LONG64 g_sequence = 0;
volatile LONG g_samplePending = 0;  // a sample fell on a slow call and waits for a cheaper one
volatile LONG64 g_ticksTotal = 0;
volatile LONG64 g_ticksOriginal = 0;

void CallOriginal(void* script, void* text, void* arguments) {
    sb::ScopedTicks t(&g_ticksOriginal);
    g_original(script, text, arguments);
}

// MSVC std::string: buffer/pointer +0x00, size +0x10, capacity +0x18 (SSO when capacity < 16).
struct View {
    const char* data;
    std::size_t size;
};

View StringView(const void* s) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(s);
    std::size_t size = 0;
    std::size_t capacity = 0;
    std::memcpy(&size, bytes + 0x10, sizeof(size));
    std::memcpy(&capacity, bytes + 0x18, sizeof(capacity));
    const char* data = nullptr;
    if (capacity > 15) {
        std::memcpy(&data, bytes, sizeof(data));
    } else {
        data = reinterpret_cast<const char*>(bytes);
    }
    return {data, size};
}

bool HasNul(View v) noexcept { return v.size != 0 && std::memchr(v.data, 0, v.size) != nullptr; }

bool Equal(View a, View b) noexcept { return a.size == b.size && (a.size == 0 || std::memcmp(a.data, b.data, a.size) == 0); }

}  // namespace

extern "C" {
// requests = calls, misses = calls without blocks (left to the original, which
// changes nothing then), hits = answered in one pass, skips = sent to the
// original, duplicateKeys = blocks resolved, verifications / mismatches =
// comparisons with the original, mode = 1 on / 2 verify, cacheDisabled =
// disabled after a mismatch.
__declspec(dllexport) sb::Stats EU4SB_MetascriptBlocksStats = SB_STATS_INIT;
}

namespace {

using sb::Read;

std::int32_t BlockCount(const void* script) noexcept { return Read<std::int32_t>(script, kBlockCount); }

// Returns false when the call must go to the original.
bool BuildResult(const void* script, const void* text, const void* arguments, std::string& out) {
    const View source = StringView(text);
    if (source.size > static_cast<std::size_t>(INT_MAX) || HasNul(source)) return false;
    const auto* blockData = Read<const unsigned char*>(script, kBlockData);
    const std::int32_t blockCount = BlockCount(script);
    const auto* argData = Read<const unsigned char*>(arguments, kVecData);
    const std::int32_t argCount = Read<std::int32_t>(arguments, kVecCount);
    if (blockCount < 0 || argCount < 0 || (blockCount > 0 && blockData == nullptr) || (argCount > 0 && argData == nullptr)) {
        return false;
    }
    out.clear();
    out.reserve(source.size);
    std::int64_t cursor = 0;
    for (std::int32_t index = 0; index < blockCount; ++index) {
        const unsigned char* block = blockData + static_cast<std::size_t>(index) * kBlockStride;
        const std::int32_t start = Read<std::int32_t>(block, kBlockStart);
        const std::int32_t end = Read<std::int32_t>(block, kBlockEnd);
        if (start < cursor || end < start || static_cast<std::size_t>(end) > source.size) return false;
        const View name = StringView(block);
        unsigned char present = 0;
        for (std::int32_t a = 0; a < argCount; ++a) {
            if (Equal(StringView(argData + static_cast<std::size_t>(a) * kArgStride), name)) {
                present = 1;
                break;
            }
        }
        out.append(source.data + cursor, static_cast<std::size_t>(start - cursor));
        if (Read<unsigned char>(block, kBlockNegate) != present) {
            const View body = StringView(block + kBlockBody);
            if (HasNul(body)) return false;
            out.append(body.data, body.size);
        }
        cursor = end;
    }
    out.append(source.data + cursor, source.size - static_cast<std::size_t>(cursor));
    return true;
}

bool Disabled() noexcept {
    if (InterlockedCompareExchange(&EU4SB_MetascriptBlocksStats.cacheDisabled, 0, 0) != 0) return true;
    if (InterlockedCompareExchange(&EU4SB_MetascriptBlocksStats.readyDisabled, 0, 0) != 0) return true;
    if (sb::ReadyReached()) {
        InterlockedExchange(&EU4SB_MetascriptBlocksStats.readyDisabled, 1);
        return true;
    }
    return false;
}

}  // namespace

extern "C" void SB_MetascriptBlocks(void* script, void* text, void* arguments) {
    sb::ScopedTicks total(&g_ticksTotal);
    sb::Increment(&EU4SB_MetascriptBlocksStats.requests);
    if (script == nullptr || BlockCount(script) <= 0) {
        sb::Increment(&EU4SB_MetascriptBlocksStats.misses);
        CallOriginal(script, text, arguments);
        return;
    }
    std::string result;
    bool ours = false;
    if (!Disabled() && text != nullptr && arguments != nullptr) {
        try {
            ours = BuildResult(script, text, arguments, result);
        } catch (...) {
            ours = false;
        }
    }
    if (!ours) {
        sb::Increment(&EU4SB_MetascriptBlocksStats.skips);
        CallOriginal(script, text, arguments);
        return;
    }
    const LONG64 blocks = BlockCount(script);
    const LONG64 sequence = InterlockedIncrement64(&g_sequence);
    if ((sequence & 255) == 1) InterlockedExchange(&g_samplePending, 1);
    const bool cheap = static_cast<std::uint64_t>(blocks) * StringView(text).size <= kSampleWorkLimit;
    if (g_verifyOnly || (cheap && InterlockedCompareExchange(&g_samplePending, 0, 1) == 1)) {
        sb::Increment(&EU4SB_MetascriptBlocksStats.verifications);
        CallOriginal(script, text, arguments);
        if (!Equal(StringView(text), View{result.data(), result.size()})) {
            sb::Increment(&EU4SB_MetascriptBlocksStats.mismatches);
            if (!g_verifyOnly) InterlockedExchange(&EU4SB_MetascriptBlocksStats.cacheDisabled, 1);
            return;  // the original's result stays
        }
        if (g_verifyOnly) return;
        sb::Increment(&EU4SB_MetascriptBlocksStats.hits);
        InterlockedExchangeAdd64(&EU4SB_MetascriptBlocksStats.duplicateKeys, blocks);
        return;
    }
    g_assign(text, result.data(), result.size());
    sb::Increment(&EU4SB_MetascriptBlocksStats.hits);
    InterlockedExchangeAdd64(&EU4SB_MetascriptBlocksStats.duplicateKeys, blocks);
}

namespace sb {

bool InstallMetascriptBlocks(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    // The site (declared with its set-up): "mov r8,rsi; mov rdx,rdi; mov rcx,rbx; call <blocks>".
    g_assign = host.function(sym::string_assign);
    g_verifyOnly = mode == Mode::Verify;
    Undo undo;
    host.redirect(g_site, sym::metascript_blocks_call, &SB_MetascriptBlocks);
    undo.add(g_site);
    g_original = g_site.next_pointer();
    undo.keep();
    InterlockedExchange(&EU4SB_MetascriptBlocksStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_MetascriptBlocksStats.initialized, 1);
    return true;
}

void ReportMetascriptBlocks() {
    LogStats("metascript_blocks", EU4SB_MetascriptBlocksStats);
    bench::SetCounters("metascript_blocks", EU4SB_MetascriptBlocksStats);
    if (EU4SB_MetascriptBlocksStats.requests > 0) {
        Log(ANVIL_LOG_INFO, "metascript_blocks: time in hook %.1f ms, of which original %.1f ms", TicksToMs(g_ticksTotal),
            TicksToMs(g_ticksOriginal));
    }
}

}  // namespace sb
