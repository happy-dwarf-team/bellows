// Negative cache for the game's file-attribute queries during start-up
// (ported from the lab's startup_attribute_cache.cpp, policy "negative"; lab
// docs: docs/startup-investigation.md, results/startup-report.md).
//
// A GetFileAttributesExW / GetFileAttributesA call (the game's imports, through
// Anvil replace_import) that fails with ERROR_FILE_NOT_FOUND or
// ERROR_PATH_NOT_FOUND for a path inside the game folder or an enabled mod's
// folder is remembered, and the same query is answered "not found" from memory
// until the ready marker. Existing paths always go to the real API: a positive
// cache was rejected in the lab (a DLC folder's LastAccessTime changed during
// start-up). Paths outside those roots (user folder, PhysFS write directory)
// or with a saves/logs/cache/tmp component are never remembered. Assumes that
// game and mod files do not appear during start-up (no file watcher).
//
// "verify" = never answer from memory: every would-be hit calls the real API
// and is compared; "on" = every 1024th hit is compared, and a mismatch turns
// the module off (that call gets the real answer).
#include "modules.hpp"

#include "access_profile.hpp"
#include "path_rules.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace {

namespace profile = sb::profile;
namespace rules = sb::path_rules;

using AttributesExWFn = BOOL(WINAPI*)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
using AttributesAFn = DWORD(WINAPI*)(LPCSTR);
using MissingMap = std::unordered_map<std::wstring, DWORD>;

constexpr DWORD kPathChars = 1024;

AttributesExWFn g_realExW = nullptr;
AttributesAFn g_realA = nullptr;
bool g_verifyOnly = false;
std::vector<std::wstring>* g_roots = nullptr;
MissingMap* g_missingExW = nullptr;  // separate maps: the A and W spellings convert differently
MissingMap* g_missingA = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;
volatile LONG64 g_hitSequence = 0;
volatile LONG64 g_ticksTotal = 0;     // time in the hooks while active
volatile LONG64 g_ticksHitReal = 0;   // real API time spent on checked hits (verify: the saving)

// verify mode: every 2048th skipped path, for the log (diagnostic only).
constexpr LONG kSkipSamples = 24;
wchar_t g_skipSample[kSkipSamples][160];
volatile LONG g_skipSampleNext = 0;
volatile LONG64 g_skipSequence = 0;

}  // namespace

extern "C" {
// requests = queries while active, hits = answered "not found" from memory,
// misses = eligible queries sent to the real API, skips = not eligible
// (outside the roots, other info level, unusual spelling), duplicateKeys =
// paths remembered, verifications / mismatches = comparisons with the real
// API, mode = 1 on / 2 verify, cacheDisabled = off after a mismatch,
// readyDisabled = off at the ready marker.
__declspec(dllexport) sb::Stats EU4SB_AttrCacheStats = SB_STATS_INIT;
}

namespace {

bool Active() noexcept {
    if (InterlockedCompareExchange(&EU4SB_AttrCacheStats.cacheDisabled, 0, 0) != 0) return false;
    if (InterlockedCompareExchange(&EU4SB_AttrCacheStats.readyDisabled, 0, 0) != 0) return false;
    if (sb::ReadyReached()) {
        InterlockedExchange(&EU4SB_AttrCacheStats.readyDisabled, 1);
        return false;
    }
    return true;
}

bool FullPathW(LPCWSTR input, std::wstring& out) {
    if (input == nullptr || *input == L'\0') return false;
    wchar_t buffer[kPathChars];
    const DWORD length = GetFullPathNameW(input, kPathChars, buffer, nullptr);
    if (length == 0 || length >= kPathChars) return false;
    out.assign(buffer, length);
    return true;
}

void SampleSkip(const std::wstring& path) noexcept {
    if (!g_verifyOnly || (InterlockedIncrement64(&g_skipSequence) & 2047) != 1) return;
    const LONG index = InterlockedIncrement(&g_skipSampleNext) - 1;
    if (index >= kSkipSamples) return;
    wcsncpy_s(g_skipSample[index], path.c_str(), _TRUNCATE);
}

bool KeyW(LPCWSTR path, std::wstring& key) {
    if (!FullPathW(path, key)) return false;
    if (rules::IsEligible(key, *g_roots)) return true;
    SampleSkip(key);
    return false;
}

bool KeyA(LPCSTR path, std::wstring& key) {
    if (path == nullptr || *path == '\0') return false;
    char buffer[kPathChars];
    const DWORD length = GetFullPathNameA(path, kPathChars, buffer, nullptr);
    if (length == 0 || length >= kPathChars) return false;
    wchar_t wide[kPathChars];
    const int count = MultiByteToWideChar(CP_ACP, 0, buffer, static_cast<int>(length), wide, kPathChars);
    if (count <= 0) return false;
    key.assign(wide, static_cast<std::size_t>(count));
    if (rules::IsEligible(key, *g_roots)) return true;
    SampleSkip(key);
    return false;
}

bool Remembered(const MissingMap& map, const std::wstring& key, DWORD& error) {
    AcquireSRWLockShared(&g_lock);
    const auto found = map.find(key);
    const bool hit = found != map.end();
    if (hit) error = found->second;
    ReleaseSRWLockShared(&g_lock);
    return hit;
}

void Remember(MissingMap& map, const std::wstring& key, DWORD error) {
    if (!rules::IsCacheableError(error)) return;
    AcquireSRWLockExclusive(&g_lock);
    bool inserted = false;
    try {
        inserted = map.emplace(key, error).second;
    } catch (...) {
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (inserted) sb::Increment(&EU4SB_AttrCacheStats.duplicateKeys);
}

// For a remembered path: true when this hit must be checked against the real API.
bool CheckThisHit() noexcept {
    return g_verifyOnly || (InterlockedIncrement64(&g_hitSequence) & 1023) == 0;
}

// Real answer differs from the remembered "not found" with this error.
void RecordCheck(bool realSucceeded, DWORD realError, DWORD rememberedError) noexcept {
    sb::Increment(&EU4SB_AttrCacheStats.verifications);
    if (realSucceeded || realError != rememberedError) {
        sb::Increment(&EU4SB_AttrCacheStats.mismatches);
        if (!g_verifyOnly) InterlockedExchange(&EU4SB_AttrCacheStats.cacheDisabled, 1);
    }
}

}  // namespace

extern "C" BOOL WINAPI SB_GetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID output) {
    const DWORD prior = GetLastError();
    if (!Active()) {
        SetLastError(prior);
        return g_realExW(path, level, output);
    }
    sb::Increment(&EU4SB_AttrCacheStats.requests);
    sb::ScopedTicks total(&g_ticksTotal);
    try {
        std::wstring key;
        if (level != GetFileExInfoStandard || output == nullptr || !KeyW(path, key)) {
            sb::Increment(&EU4SB_AttrCacheStats.skips);
            SetLastError(prior);
            return g_realExW(path, level, output);
        }
        DWORD remembered = 0;
        if (Remembered(*g_missingExW, key, remembered)) {
            if (!CheckThisHit()) {
                sb::Increment(&EU4SB_AttrCacheStats.hits);
                SetLastError(remembered);
                return FALSE;
            }
            SetLastError(prior);
            const LONG64 start = sb::Ticks();
            const BOOL real = g_realExW(path, level, output);
            const DWORD error = real ? prior : GetLastError();
            InterlockedExchangeAdd64(&g_ticksHitReal, sb::Ticks() - start);
            RecordCheck(real != FALSE, error, remembered);
            SetLastError(error);
            return real;
        }
        sb::Increment(&EU4SB_AttrCacheStats.misses);
        SetLastError(prior);
        const BOOL real = g_realExW(path, level, output);
        if (real) return real;
        const DWORD error = GetLastError();
        Remember(*g_missingExW, key, error);
        SetLastError(error);
        return FALSE;
    } catch (...) {
        SetLastError(prior);
        return g_realExW(path, level, output);
    }
}

extern "C" DWORD WINAPI SB_GetFileAttributesA(LPCSTR path) {
    const DWORD prior = GetLastError();
    if (!Active()) {
        SetLastError(prior);
        return g_realA(path);
    }
    sb::Increment(&EU4SB_AttrCacheStats.requests);
    sb::ScopedTicks total(&g_ticksTotal);
    try {
        std::wstring key;
        if (!KeyA(path, key)) {
            sb::Increment(&EU4SB_AttrCacheStats.skips);
            SetLastError(prior);
            return g_realA(path);
        }
        DWORD remembered = 0;
        if (Remembered(*g_missingA, key, remembered)) {
            if (!CheckThisHit()) {
                sb::Increment(&EU4SB_AttrCacheStats.hits);
                SetLastError(remembered);
                return INVALID_FILE_ATTRIBUTES;
            }
            SetLastError(prior);
            const LONG64 start = sb::Ticks();
            const DWORD real = g_realA(path);
            const DWORD error = real != INVALID_FILE_ATTRIBUTES ? prior : GetLastError();
            InterlockedExchangeAdd64(&g_ticksHitReal, sb::Ticks() - start);
            RecordCheck(real != INVALID_FILE_ATTRIBUTES, error, remembered);
            SetLastError(error);
            return real;
        }
        sb::Increment(&EU4SB_AttrCacheStats.misses);
        SetLastError(prior);
        const DWORD real = g_realA(path);
        if (real != INVALID_FILE_ATTRIBUTES) return real;
        const DWORD error = GetLastError();
        Remember(*g_missingA, key, error);
        SetLastError(error);
        return real;
    } catch (...) {
        SetLastError(prior);
        return g_realA(path);
    }
}

namespace {

void AddRoot(std::vector<std::wstring>& roots, const std::wstring& folder) {
    std::wstring full;
    if (!FullPathW(folder.c_str(), full)) return;
    while (full.size() > 3 && rules::IsSeparator(full.back())) full.pop_back();
    if (full.size() <= 3 || !rules::IsDrivePath(full) || rules::HasExcludedComponent(full)) return;
    for (const auto& existing : roots) {
        if (CompareStringOrdinal(existing.data(), static_cast<int>(existing.size()), full.data(),
                                 static_cast<int>(full.size()), TRUE) == CSTR_EQUAL) {
            return;
        }
    }
    roots.push_back(std::move(full));
}

}  // namespace

namespace sb {

bool InstallAttrCache(const Build&, Mode mode) {
    if (mode == Mode::Off) return false;
    wchar_t exe[MAX_PATH * 2] = {};
    const DWORD length = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    if (length == 0 || length >= std::size(exe)) return false;
    try {
        g_roots = new std::vector<std::wstring>();
        std::wstring gameDir(exe, length);
        gameDir.resize(gameDir.find_last_of(L'\\'));
        AddRoot(*g_roots, gameDir);
        const std::wstring user = profile::UserDirectory();
        std::string json;
        if (!user.empty() && profile::ReadWhole(user + L"\\dlc_load.json", json, 1 << 20)) {
            for (const auto& folder : profile::ModFolders(user, profile::ParseEnabledMods(json))) AddRoot(*g_roots, folder);
        }
        g_missingExW = new MissingMap();
        g_missingA = new MissingMap();
        g_missingExW->reserve(1 << 19);
    } catch (...) {
        return false;
    }
    g_verifyOnly = mode == Mode::Verify;
    const AnvilHost* host = context().host;
    void* original = nullptr;
    AnvilResult r = host->replace_import(context().self, "KERNEL32.dll", "GetFileAttributesExW",
                                         reinterpret_cast<void*>(&SB_GetFileAttributesExW), &original);
    if (r != ANVIL_OK) {
        Log(ANVIL_LOG_WARNING, "attr_cache: GetFileAttributesExW import refused (%s); module off", ResultName(r));
        return false;
    }
    g_realExW = reinterpret_cast<AttributesExWFn>(original);
    r = host->replace_import(context().self, "KERNEL32.dll", "GetFileAttributesA",
                             reinterpret_cast<void*>(&SB_GetFileAttributesA), &original);
    if (r == ANVIL_OK) {
        g_realA = reinterpret_cast<AttributesAFn>(original);
    } else {
        Log(ANVIL_LOG_WARNING, "attr_cache: GetFileAttributesA import refused (%s); only the W import cached",
            ResultName(r));
    }
    InterlockedExchange(&EU4SB_AttrCacheStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_AttrCacheStats.initialized, 1);
    Log(ANVIL_LOG_INFO, "attr_cache: %zu roots (game folder + enabled mods)", g_roots->size());
    return true;
}

void ReportAttrCache() {
    LogStats("attr_cache", EU4SB_AttrCacheStats);
    if (EU4SB_AttrCacheStats.requests > 0) {
        Log(ANVIL_LOG_INFO, "attr_cache: time in hooks %.1f ms; real API on checked hits %.1f ms (%lld checks)",
            TicksToMs(g_ticksTotal), TicksToMs(g_ticksHitReal), EU4SB_AttrCacheStats.verifications);
    }
    const LONG samples = g_skipSampleNext < kSkipSamples ? g_skipSampleNext : kSkipSamples;
    for (LONG index = 0; index < samples; ++index) {
        Log(ANVIL_LOG_INFO, "attr_cache: skipped %s", profile::Narrow(g_skipSample[index]).c_str());
    }
}

}  // namespace sb
