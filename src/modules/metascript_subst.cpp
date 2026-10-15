// Single-pass $param$ substitution for scripted effects/triggers (ported from
// the lab's startup_metascript_subst.inc; lab docs: docs/startup-metascript-subst.md).
//
// The game's substitution walks the tokens from last to first; for each it
// searches the argument list linearly (first matching key; value or "" when
// absent) and rebuilds the whole text as prefix + value + suffix (cost ~
// tokens x length). It finally assigns the result with strlen, so the text is
// cut at the first NUL.
//
// For tokens that are sorted, non-overlapping and inside the text, and for
// NUL-free text and values, that equals one forward pass: text[0:s0] + value0 +
// text[e0:s1] + ... + text[en:]. Both call sites are redirected (Anvil
// redirect_call + byte checks) to SB_MetascriptSubst, which checks those
// preconditions, builds the result once and stores it with the game's own
// string assign. Anything else goes to the original. "verify" = compute ours,
// run the original, compare (the game keeps the original's result); "on" =
// ours, every 256th call compared (on a mismatch the original's result stays
// and the module disables itself). Not used after the ready marker.
#include "modules.hpp"

#include <string>

namespace {

// Game containers: data pointer at +0x08, int count at +0x14.
constexpr std::size_t kVecData = 0x08;
constexpr std::size_t kVecCount = 0x14;
constexpr std::size_t kArgStride = 0x40;  // key string +0x00, value string +0x20
constexpr std::size_t kArgValue = 0x20;
constexpr std::size_t kTokenStride = 0x28;  // name string +0x00, int start +0x20, int end +0x24
constexpr std::size_t kTokenStart = 0x20;
constexpr std::size_t kTokenEnd = 0x24;

using SubstFn = void (*)(void*, void*, void*, void*);
using AssignFn = void* (*)(void*, const char*, std::size_t);

SubstFn g_original = nullptr;
AssignFn g_assign = nullptr;
bool g_verifyOnly = false;
volatile LONG64 g_sequence = 0;

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
// requests = calls, hits = answered in one pass, skips = sent to the original,
// duplicateKeys = tokens substituted, verifications / mismatches = comparisons
// with the original, mode = 1 on / 2 verify, cacheDisabled = disabled after a mismatch.
__declspec(dllexport) sb::Stats EU4SB_MetascriptStats = SB_STATS_INIT;
}

namespace {

using sb::Read;

// Returns false when the call must go to the original.
bool BuildResult(const void* text, const void* arguments, const void* tokens, std::string& out, LONG64& used) {
    const View source = StringView(text);
    if (HasNul(source)) return false;
    const auto* tokenData = Read<const unsigned char*>(tokens, kVecData);
    const std::int32_t tokenCount = Read<std::int32_t>(tokens, kVecCount);
    const auto* argData = Read<const unsigned char*>(arguments, kVecData);
    const std::int32_t argCount = Read<std::int32_t>(arguments, kVecCount);
    if (tokenCount < 0 || argCount < 0 || (tokenCount > 0 && tokenData == nullptr) || (argCount > 0 && argData == nullptr)) {
        return false;
    }
    out.clear();
    out.reserve(source.size + 64);
    std::int64_t cursor = 0;
    for (std::int32_t index = 0; index < tokenCount; ++index) {
        const unsigned char* token = tokenData + static_cast<std::size_t>(index) * kTokenStride;
        const std::int32_t start = Read<std::int32_t>(token, kTokenStart);
        const std::int32_t end = Read<std::int32_t>(token, kTokenEnd);
        if (start < cursor || end < start || static_cast<std::size_t>(end) > source.size) return false;
        const View name = StringView(token);
        View value{"", 0};
        for (std::int32_t a = 0; a < argCount; ++a) {
            const unsigned char* arg = argData + static_cast<std::size_t>(a) * kArgStride;
            if (Equal(StringView(arg), name)) {
                value = StringView(arg + kArgValue);
                break;
            }
        }
        if (HasNul(value)) return false;
        out.append(source.data + cursor, static_cast<std::size_t>(start - cursor));
        out.append(value.data, value.size);
        cursor = end;
    }
    out.append(source.data + cursor, source.size - static_cast<std::size_t>(cursor));
    used = tokenCount;
    return true;
}

bool Disabled() noexcept {
    if (InterlockedCompareExchange(&EU4SB_MetascriptStats.cacheDisabled, 0, 0) != 0) return true;
    if (InterlockedCompareExchange(&EU4SB_MetascriptStats.readyDisabled, 0, 0) != 0) return true;
    if (sb::ReadyReached()) {
        InterlockedExchange(&EU4SB_MetascriptStats.readyDisabled, 1);
        return true;
    }
    return false;
}

}  // namespace

extern "C" void SB_MetascriptSubst(void* unused, void* text, void* arguments, void* tokens) {
    sb::Increment(&EU4SB_MetascriptStats.requests);
    std::string result;
    LONG64 used = 0;
    bool ours = false;
    if (!Disabled() && text != nullptr && arguments != nullptr && tokens != nullptr) {
        try {
            ours = BuildResult(text, arguments, tokens, result, used);
        } catch (...) {
            ours = false;
        }
    }
    if (!ours) {
        sb::Increment(&EU4SB_MetascriptStats.skips);
        g_original(unused, text, arguments, tokens);
        return;
    }
    const LONG64 sequence = InterlockedIncrement64(&g_sequence);
    if (g_verifyOnly || (sequence & 255) == 1) {
        sb::Increment(&EU4SB_MetascriptStats.verifications);
        g_original(unused, text, arguments, tokens);
        if (!Equal(StringView(text), View{result.data(), result.size()})) {
            sb::Increment(&EU4SB_MetascriptStats.mismatches);
            if (!g_verifyOnly) InterlockedExchange(&EU4SB_MetascriptStats.cacheDisabled, 1);
            return;  // the original's result stays
        }
        if (g_verifyOnly) return;
        sb::Increment(&EU4SB_MetascriptStats.hits);
        InterlockedExchangeAdd64(&EU4SB_MetascriptStats.duplicateKeys, used);
        return;
    }
    g_assign(text, result.data(), result.size());
    sb::Increment(&EU4SB_MetascriptStats.hits);
    InterlockedExchangeAdd64(&EU4SB_MetascriptStats.duplicateKeys, used);
}

namespace sb {

bool InstallMetascriptSubst(const Build& build, Mode mode) {
    if (mode == Mode::Off) return false;
    // Each site: "mov r8,rsi; mov rdx,rdi; call <subst>".
    for (uint32_t site : build.metascript_sites) {
        const unsigned char* bytes = AtRva<const unsigned char>(site - 6);
        const unsigned char before[6] = {0x4C, 0x8B, 0xC6, 0x48, 0x8B, 0xD7};
        if (std::memcmp(bytes, before, sizeof(before)) != 0) {
            Log(ANVIL_LOG_WARNING, "metascript_subst: unexpected code around call site; module off");
            return false;
        }
    }
    g_original = reinterpret_cast<SubstFn>(AtRva<unsigned char>(build.metascript_subst_fn));
    g_assign = reinterpret_cast<AssignFn>(AtRva<unsigned char>(build.string_assign_fn));
    g_verifyOnly = mode == Mode::Verify;
    const AnvilHost* host = context().host;
    for (uint32_t site : build.metascript_sites) {
        const AnvilResult r = host->redirect_call(context().self, site, build.metascript_subst_fn,
                                                  reinterpret_cast<void*>(&SB_MetascriptSubst));
        if (r != ANVIL_OK) {
            Log(ANVIL_LOG_WARNING, "metascript_subst: call site refused (%s)", ResultName(r));
            return false;
        }
    }
    InterlockedExchange(&EU4SB_MetascriptStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_MetascriptStats.initialized, 1);
    return true;
}

void ReportMetascriptSubst() { LogStats("metascript_subst", EU4SB_MetascriptStats); }

}  // namespace sb
