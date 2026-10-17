// Diagnostic (off by default): what the country range check costs during the
// simulation (lab docs: docs/simulation-profile.md, "Sprawdzenie zasięgu kraju").
//
// The province day (0x9F52E0) reaches 0x3520D0 ("is the province within the
// country's range"), which calls 0x3526B0: the minimum distance from the
// country's flagged provinces to the target, through 0x2D2520 for each of them,
// stopping early once below the threshold. Both functions are hooked (inline,
// through Anvil) only to observe: every 0x3526B0 call is recorded with its
// country, province, value, in-range answer, the 0x2D2520 calls made inside it,
// its time and its caller. Nothing the game computes is changed.
//
// The game is usually closed without a clean exit, so a background thread writes
// the counters every 5 s to range_profile*.tsv in the data directory.
#include "modules.hpp"
#include "range_stats.hpp"

#include <intrin.h>

#include <fstream>
#include <string>

namespace {

using MinDistanceFn = int* (*)(void* country, int* out, void* province, void* arg4, int* threshold);
using DistanceFn = void* (*)(void* out, void* a2, void* a3, void* a4, std::uintptr_t a5, std::uintptr_t a6,
                             std::uintptr_t a7);

// First instructions of 0x3526B0: mov [rsp+20h],r9; mov [rsp+10h],rdx; mov [rsp+8],rcx
constexpr unsigned char kMinDistancePrologue[] = {0x4C, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x54,
                                                  0x24, 0x10, 0x48, 0x89, 0x4C, 0x24, 0x08};
// First instructions of 0x2D2520: mov [rsp+20h],r9; mov [rsp+18h],r8; mov [rsp+10h],rdx; mov [rsp+8],rcx
constexpr unsigned char kDistancePrologue[] = {0x4C, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x89, 0x44, 0x24, 0x18,
                                               0x48, 0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4C, 0x24, 0x08};
constexpr std::size_t kProvinceId = 0x20;  // int16 province id in the province object
constexpr std::size_t kCountryId = 0x24;   // int16 tag index in the country object
constexpr std::size_t kRootDate = 0x1DD0;  // int32 hours since year -5000 (game root object)
constexpr DWORD kWriteEveryMs = 5000;

MinDistanceFn g_minDistance = nullptr;
DistanceFn g_distance = nullptr;
const void* const* g_rootGlobal = nullptr;
std::uintptr_t g_imageBase = 0;

SRWLOCK g_lock = SRWLOCK_INIT;
sb::RangeStats* g_stats = nullptr;
volatile LONG64 g_distanceTotal = 0;  // all 0x2D2520 calls, inside the range check or not
volatile LONG g_dirty = 0;
std::wstring* g_dir = nullptr;

thread_local int t_depth = 0;          // nesting of 0x3526B0 on this thread
thread_local int t_distanceCalls = 0;  // 0x2D2520 calls inside the outermost 0x3526B0

int GameDay() noexcept {
    const void* root = *g_rootGlobal;
    if (root == nullptr) return 0;
    const std::int32_t raw = sb::Read<std::int32_t>(root, kRootDate);
    return static_cast<int>((static_cast<std::int64_t>(raw) - 43'800'000) / 24);
}

int* MinDistanceDetour(void* country, int* out, void* province, void* arg4, int* threshold) {
    const bool outer = t_depth++ == 0;
    if (outer) t_distanceCalls = 0;
    const LONG64 start = sb::Ticks();
    int* result = g_minDistance(country, out, province, arg4, threshold);
    const LONG64 ticks = sb::Ticks() - start;
    --t_depth;
    if (!outer || out == nullptr || country == nullptr || province == nullptr) return result;
    sb::RangeObservation o;
    o.country = reinterpret_cast<std::uintptr_t>(country);
    o.country_id = sb::Read<std::int16_t>(country, kCountryId);
    o.province = sb::Read<std::int16_t>(province, kProvinceId);
    o.value = *out;
    o.has_threshold = threshold != nullptr;
    o.in_range = threshold != nullptr && *out <= *threshold;
    o.day = GameDay();
    o.distance_calls = t_distanceCalls;
    o.ticks = ticks;
    o.caller = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - g_imageBase);
    AcquireSRWLockExclusive(&g_lock);
    try {
        g_stats->Add(o);
    } catch (...) {
    }
    ReleaseSRWLockExclusive(&g_lock);
    InterlockedExchange(&g_dirty, 1);
    return result;
}

void* DistanceDetour(void* out, void* a2, void* a3, void* a4, std::uintptr_t a5, std::uintptr_t a6, std::uintptr_t a7) {
    if (t_depth > 0) ++t_distanceCalls;
    InterlockedIncrement64(&g_distanceTotal);
    return g_distance(out, a2, a3, a4, a5, a6, a7);
}

void WriteFiles() {
    const double msPerTick = sb::TicksToMs(1);
    AcquireSRWLockShared(&g_lock);
    try {
        const std::wstring& dir = *g_dir;
        auto write = [&](const wchar_t* name, auto&& body) {
            const std::wstring path = dir + L"\\" + name;
            const std::wstring temp = path + L".tmp";
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                body(out);
            }
            MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
        };
        write(L"range_profile.tsv", [&](std::ostream& out) {
            g_stats->WriteSummary(out, msPerTick);
            out << "distance_calls_all\t" << g_distanceTotal << "\n";
        });
        write(L"range_profile_days.tsv", [&](std::ostream& out) { g_stats->WriteDays(out, msPerTick); });
        write(L"range_profile_pairs.tsv", [&](std::ostream& out) { g_stats->WritePairs(out, 5000); });
        write(L"range_profile_callers.tsv", [&](std::ostream& out) { g_stats->WriteCallers(out); });
    } catch (...) {
    }
    ReleaseSRWLockShared(&g_lock);
}

DWORD WINAPI WriterThread(void*) {
    for (;;) {
        Sleep(kWriteEveryMs);
        if (InterlockedExchange(&g_dirty, 0) != 0) WriteFiles();
    }
}

}  // namespace

extern "C" {
// requests = range checks, hits = 0x2D2520 calls inside them, mode = 1 on.
__declspec(dllexport) sb::Stats EU4SB_RangeProfileStats = SB_STATS_INIT;
}

namespace sb {

bool InstallRangeProfile(const Build& build, Mode mode) {
    if (mode == Mode::Off) return false;
    try {
        g_stats = new RangeStats();
        g_dir = new std::wstring(context().host->data_dir(context().self));
    } catch (...) {
        return false;
    }
    g_imageBase = context().image_base;
    g_rootGlobal = AtRva<const void* const>(build.game_root);
    void* original = nullptr;
    AnvilResult r = context().host->hook_function(context().self, build.range_min_distance_fn, kMinDistancePrologue,
                                                   sizeof kMinDistancePrologue,
                                                   reinterpret_cast<void*>(&MinDistanceDetour), &original);
    if (r != ANVIL_OK) {
        Log(ANVIL_LOG_WARNING, "range_profile: cannot hook the range check (%s); module off", ResultName(r));
        return false;
    }
    g_minDistance = reinterpret_cast<MinDistanceFn>(original);
    r = context().host->hook_function(context().self, build.province_distance_fn, kDistancePrologue, sizeof kDistancePrologue,
                                      reinterpret_cast<void*>(&DistanceDetour), &original);
    if (r != ANVIL_OK) {
        // The first hook only observes; leaving it in place is harmless, but say what is missing.
        Log(ANVIL_LOG_WARNING, "range_profile: cannot hook the province distance (%s); distance counts stay 0",
            ResultName(r));
    } else {
        g_distance = reinterpret_cast<DistanceFn>(original);
    }
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows range_profile writer");
        CloseHandle(thread);
    }
    InterlockedExchange(&EU4SB_RangeProfileStats.mode, 1);
    InterlockedExchange(&EU4SB_RangeProfileStats.initialized, 1);
    Log(ANVIL_LOG_INFO, "range_profile: observing 0x%X and 0x%X; counters in range_profile*.tsv every %u s",
        build.range_min_distance_fn, build.province_distance_fn, kWriteEveryMs / 1000);
    return true;
}

void ReportRangeProfile() {
    if (g_stats == nullptr) return;
    AcquireSRWLockShared(&g_lock);
    const LONG64 calls = g_stats->calls();
    const LONG64 distance = g_stats->distance_calls();
    ReleaseSRWLockShared(&g_lock);
    InterlockedExchange64(&EU4SB_RangeProfileStats.requests, calls);
    InterlockedExchange64(&EU4SB_RangeProfileStats.hits, distance);
    Log(ANVIL_LOG_INFO, "range_profile: at game ready %lld range checks, %lld distance calls inside them", calls, distance);
}

}  // namespace sb
