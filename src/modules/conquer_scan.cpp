// conquer_scan (prototype, off by default): the AI's conquest list from the land provinces only.
//
// CAIStrategy::RebuildConquerList (0x187AD0) runs in a country's strategy recalculation, inside the serial daily
// country update. It values every province of the map in a ParallelFor3 (0x191F30): each item calls
// 0x187C00(strategy, province) and appends (index, value) to a buffer when the value is above 0; the function
// then sorts the buffer by index and adds the entries in that order. On a big map most items return 0 at the
// first checks, and the time goes to the cache misses of those checks and to the pool.
//
// The module redirects the call of 0x191F30 (0x187B84) and leaves out the provinces whose template is not land,
// for which 0x187C00 returns 0 before it reads anything else. It runs the game's item for the land provinces one
// of two ways, with the same function, the same buffer and the same count:
// - serial: a loop on the calling thread, in index order, which prefetches the fields of the first checks a few
//   provinces ahead. It wins where the items are cheap and the pool's hand-out costs more than they do
//   (Anbennar's many small countries).
// - pool: the game's own ParallelFor3 on an array of the land provinces; the item writes the position in that
//   array, which the module maps back to the province index (the mapping keeps the order). It wins where the
//   items are dear (the base game).
// It takes the way whose recent time per call is lower and tries the other every 16th call (WayChooser). The
// game's sort and the adding run as before.
//
// "verify" runs the game's ParallelFor3 for every call as well, into the game's buffer (the game's answer is
// the one kept), and both ways into buffers of their own; it compares the three lists and checks that every
// left-out province is still not land. "on" does that for every 256th call and goes back to the game's loop for
// good after a difference. A background thread writes the counters every 5 s to conquer_scan.tsv in the data
// directory, with the time of each loop in a checked call by whether it ran first.
#include "modules.hpp"
#include "serial_loop.hpp"
#include "serial_loop_core.hpp"
#include "../file_write.hpp"

#include <intrin.h>

#include <sstream>
#include <string>
#include <vector>

extern "C" {
// requests = calls, hits = calls the module's loop answered, skips = calls left to the game (empty, or after a
// difference), verifications = checked calls, mismatches = checked calls whose lists differed (or a left-out
// province that became land), cacheDisabled = 1 after a difference in "on", mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_ConquerScanStats = SB_STATS_INIT;
}

namespace {

namespace sl = sb::serial_loop;
using Tv = sym::Types::conquer_value;

// The game's lambda, which the call passes by address (0x187AD0 builds it on its stack).
struct Capture {
    void* strategy;
    std::int32_t** count;
    sl::Pair** pairs;
};

anvil::FunctionHook<void(const void*, const void*)> g_fillSite;
void (*g_gameFill)(const void*, const void*) = nullptr;
Tv::Pointer g_value = nullptr;
bool g_verifyOnly = false;
std::wstring* g_dir = nullptr;
constexpr DWORD kWriteEveryMs = 5000;
constexpr std::size_t kPrefetchDistance = 8;

volatile LONG64 g_provinces = 0, g_evaluated = 0, g_written = 0, g_loopTicks = 0, g_checked = 0, g_landChecks = 0;
volatile LONG64 g_wayCalls[2] = {}, g_wayTicks[2] = {};  // unchecked calls by way (WayChooser)
sl::OrderedTimes g_times;  // checked calls; updated on the calling thread under g_timesLock
sl::WayChooser g_way;      // under g_timesLock
SRWLOCK g_timesLock = SRWLOCK_INIT;

// The land provinces of the map, by the province array they were taken from.
SRWLOCK g_listLock = SRWLOCK_INIT;
void* const* g_listBegin = nullptr;
std::size_t g_listSize = 0;
std::vector<std::int32_t>* g_land = nullptr;

bool IsLand(const void* province) noexcept {
    const auto* tmpl = sb::Read<const unsigned char*>(province, Tv::template_offset);
    return ((sb::Read<std::uint8_t>(tmpl, Tv::land_flags_offset) >> Tv::land_bit) & 1) != 0;
}

// The list for this array, built at its first call (the map does not change while the game runs; a new
// province array, after a load, builds it again). Returns with g_listLock held shared.
const std::vector<std::int32_t>& LandList(const sl::PointerArray& provinces) {
    AcquireSRWLockShared(&g_listLock);
    if (g_listBegin == provinces.begin && g_listSize == provinces.size()) return *g_land;
    ReleaseSRWLockShared(&g_listLock);
    AcquireSRWLockExclusive(&g_listLock);
    if (g_listBegin != provinces.begin || g_listSize != provinces.size()) {
        *g_land = sl::CandidateList(static_cast<std::int32_t>(provinces.size()),
                                    [&](std::int32_t i) { return IsLand(provinces.begin[i]); });
        g_listBegin = provinces.begin;
        g_listSize = provinces.size();
        sb::Log(ANVIL_LOG_INFO, "conquer_scan: %zu of %zu provinces are land", g_land->size(), g_listSize);
    }
    ReleaseSRWLockExclusive(&g_listLock);
    AcquireSRWLockShared(&g_listLock);
    return *g_land;
}

// The module's loop: the game's item for every land province, in index order, with the prefetch.
std::size_t Fill(const sl::PointerArray& provinces, const std::vector<std::int32_t>& land, void* strategy, sl::Pair* out) {
    const sl::GameLoopScope scope;
    const auto prefetch = [&](std::int32_t i) {
        const char* p = static_cast<const char*>(provinces.begin[i]);
        _mm_prefetch(p + Tv::template_offset, _MM_HINT_T0);
        _mm_prefetch(p + Tv::controller_area_offset, _MM_HINT_T0);
        _mm_prefetch(p + Tv::owner_offset, _MM_HINT_T0);
    };
    const auto prefetchNear = [&](std::int32_t i) {
        const auto* tmpl = sb::Read<const char*>(provinces.begin[i], Tv::template_offset);
        _mm_prefetch(tmpl + Tv::land_flags_offset, _MM_HINT_T0);
    };
    const auto value = [&](std::int32_t i) { return g_value(strategy, provinces.begin[i]); };
    return sl::FillPairs(land, value, prefetch, prefetchNear, kPrefetchDistance, out);
}

// The pool's way: the game's ParallelFor3 on an array of the land provinces, then the entries it added mapped
// back to province indices. `lambda` is the game's capture or one of the same layout; its count goes up by the
// entries written.
void PoolFill(const sl::PointerArray& provinces, const std::vector<std::int32_t>& land, const Capture& lambda) {
    thread_local std::vector<void*> compact;
    compact.resize(land.size());
    for (std::size_t k = 0; k < land.size(); ++k) compact[k] = provinces.begin[land[k]];
    const sl::PointerArray only{compact.data(), compact.data() + compact.size()};
    const std::int32_t from = **lambda.count;
    g_gameFill(&only, &lambda);
    sl::MapToProvinces(*lambda.pairs, from, **lambda.count, land);
}

// Every province the list leaves out is still not land; the number that are.
std::size_t LandChanged(const sl::PointerArray& provinces, const std::vector<std::int32_t>& land) {
    std::size_t changed = 0, k = 0;
    for (std::size_t i = 0; i < provinces.size(); ++i) {
        if (k < land.size() && static_cast<std::size_t>(land[k]) == i) {
            ++k;
            continue;
        }
        if (IsLand(provinces.begin[i])) ++changed;
    }
    return changed;
}

void CheckedCall(const sl::PointerArray& provinces, const Capture& game, const std::vector<std::int32_t>& land) {
    thread_local std::vector<sl::Pair> serial, pool, theirs;
    serial.resize(land.size());
    pool.resize(land.size());
    const int first = sl::FirstOf3(static_cast<std::uint64_t>(InterlockedIncrement64(&g_checked)));
    std::size_t written = 0;
    std::int32_t poolCount = 0;
    std::int32_t* poolCountAt = &poolCount;
    sl::Pair* poolPairs = pool.data();
    const Capture poolLambda{game.strategy, &poolCountAt, &poolPairs};
    LONG64 ticks[3] = {};
    for (int run = 0; run < 3; ++run) {
        const int who = (first + run) % 3;  // 0 the game, 1 serial, 2 pool
        const LONG64 start = sb::Ticks();
        if (who == 0) {
            g_gameFill(&provinces, &game);
        } else if (who == 1) {
            written = Fill(provinces, land, game.strategy, serial.data());
        } else {
            PoolFill(provinces, land, poolLambda);
        }
        ticks[who] = sb::Ticks() - start;
    }
    serial.resize(written);
    pool.resize(static_cast<std::size_t>(poolCount));
    const std::int32_t count = **game.count;
    theirs.assign(*game.pairs, *game.pairs + (count > 0 ? count : 0));
    const std::size_t changed = LandChanged(provinces, land);
    InterlockedExchangeAdd64(&g_landChecks, static_cast<LONG64>(provinces.size() - land.size()));
    InterlockedIncrement64(&EU4SB_ConquerScanStats.verifications);
    AcquireSRWLockExclusive(&g_timesLock);
    for (int who = 0; who < 3; ++who) g_times.Add(who, who == first, ticks[who]);
    ReleaseSRWLockExclusive(&g_timesLock);
    const bool serialDiffers = sl::Differences(serial, theirs) != 0;
    const bool poolDiffers = sl::Differences(pool, theirs) != 0;
    if (serialDiffers || poolDiffers || changed != 0) {
        InterlockedIncrement64(&EU4SB_ConquerScanStats.mismatches);
        if (!g_verifyOnly && InterlockedExchange(&EU4SB_ConquerScanStats.cacheDisabled, 1) == 0)
            sb::Log(ANVIL_LOG_WARNING, "conquer_scan: a conquest list differed from the game's (%s); back to the game's loop",
                    serialDiffers ? (poolDiffers ? "both ways" : "serial") : (poolDiffers ? "pool" : "land"));
    }
}

void FillDetour(const void* array, const void* lambda) {
    const auto& provinces = *static_cast<const sl::PointerArray*>(array);
    const auto& game = *static_cast<const Capture*>(lambda);
    const LONG64 call = InterlockedIncrement64(&EU4SB_ConquerScanStats.requests);
    if (provinces.size() == 0 || EU4SB_ConquerScanStats.cacheDisabled != 0) {
        InterlockedIncrement64(&EU4SB_ConquerScanStats.skips);
        g_gameFill(array, lambda);
        return;
    }
    const std::vector<std::int32_t>& land = LandList(provinces);
    InterlockedExchangeAdd64(&g_provinces, static_cast<LONG64>(provinces.size()));
    InterlockedExchangeAdd64(&g_evaluated, static_cast<LONG64>(land.size()));
    if (sl::Checked(g_verifyOnly, static_cast<std::uint64_t>(call))) {
        CheckedCall(provinces, game, land);
    } else {
        // The game set the count to 0 before the call; the module's entries go after it, as the items' do.
        AcquireSRWLockShared(&g_timesLock);
        const int way = g_way.Pick(static_cast<std::uint64_t>(call));
        ReleaseSRWLockShared(&g_timesLock);
        std::int32_t* count = *game.count;
        const std::int32_t before = *count;
        const LONG64 start = sb::Ticks();
        if (way == sl::WayChooser::kSerial) {
            *count += static_cast<std::int32_t>(Fill(provinces, land, game.strategy, *game.pairs + before));
        } else {
            PoolFill(provinces, land, game);
        }
        const LONG64 ticks = sb::Ticks() - start;
        AcquireSRWLockExclusive(&g_timesLock);
        g_way.Add(way, ticks);
        ReleaseSRWLockExclusive(&g_timesLock);
        InterlockedExchangeAdd64(&g_loopTicks, ticks);
        InterlockedIncrement64(&g_wayCalls[way]);
        InterlockedExchangeAdd64(&g_wayTicks[way], ticks);
        InterlockedExchangeAdd64(&g_written, static_cast<LONG64>(*count - before));
        InterlockedIncrement64(&EU4SB_ConquerScanStats.hits);
    }
    ReleaseSRWLockShared(&g_listLock);
}

void WriteFile() {
    const sb::Stats& s = EU4SB_ConquerScanStats;
    try {
        sl::OrderedTimes t;
        AcquireSRWLockShared(&g_timesLock);
        t = g_times;
        ReleaseSRWLockShared(&g_timesLock);
        const double ms = sb::TicksToMs(1);
        std::ostringstream out;
        out << "mode\tcalls\tloop_calls\tgame_calls\tchecked\tmismatches\tdisabled\tprovinces\tevaluated\twritten\tloop_ms"
               "\tland_checks\tserial_calls\tserial_ms\tpool_calls\tpool_ms\tgame_first_calls\tgame_first_ms"
               "\tgame_later_calls\tgame_later_ms\tserial_first_calls\tserial_first_ms\tserial_later_calls"
               "\tserial_later_ms\tpool_first_calls\tpool_first_ms\tpool_later_calls\tpool_later_ms\n"
            << (g_verifyOnly ? "verify" : "on") << "\t" << s.requests << "\t" << s.hits << "\t" << s.skips << "\t"
            << s.verifications << "\t" << s.mismatches << "\t" << s.cacheDisabled << "\t" << g_provinces << "\t"
            << g_evaluated << "\t" << g_written << "\t" << g_loopTicks * ms << "\t" << g_landChecks;
        for (int way = 0; way < 2; ++way) out << "\t" << g_wayCalls[way] << "\t" << g_wayTicks[way] * ms;
        for (int who = 0; who < 3; ++who) {
            for (int order = 0; order < 2; ++order) out << "\t" << t.calls[who][order] << "\t" << t.ticks[who][order] * ms;
        }
        out << "\n";
        sb::ReplaceFileWith(*g_dir + L"\\conquer_scan.tsv", out.str());
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    LONG64 written = -1;
    for (;;) {
        Sleep(kWriteEveryMs);
        if (EU4SB_ConquerScanStats.requests != written) {
            written = EU4SB_ConquerScanStats.requests;
            WriteFile();
        }
    }
}

}  // namespace

namespace sb {

bool InstallConquerScan(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyOnly = mode == Mode::Verify;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
        g_land = new std::vector<std::int32_t>();
    } catch (...) {
        return false;
    }
    serial_loop::Resolve(host);
    g_value = host.function(sym::conquer_value);
    Undo undo;
    host.redirect(g_fillSite, sym::conquer_fill_call, &FillDetour);
    undo.add(g_fillSite);
    g_gameFill = g_fillSite.next_pointer();
    undo.keep();
    InterlockedExchange(&EU4SB_ConquerScanStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_ConquerScanStats.initialized, 1);
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows conquer_scan writer");
        CloseHandle(thread);
    }
    Log(ANVIL_LOG_INFO, "conquer_scan: %s the AI's conquest list from the land provinces only",
        g_verifyOnly ? "verifying" : "building");
    return true;
}

}  // namespace sb
