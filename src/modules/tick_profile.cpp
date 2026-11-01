// Diagnostic (off by default): where the daily tick spends its time (lab docs:
// docs/simulation-profile.md, "Etapy dziennego ticku").
//
// 0x758430 runs one game day: the daily part (0x75BBE0), the monthly tick (0x75D690) on the
// first of a month and the yearly tick (0x75E260) on 1 January. Between their stages it calls
// 0x769EA0, which draws a frame when enough time passed since the last one, from about 40
// fixed places. Both functions are hooked (inline, through Anvil) only to observe: the day is
// cut into segments at those calls, named by the return addresses at both ends, and the time
// inside 0x769EA0 is counted apart. Nothing the game computes is changed.
//
// The largest segment of the daily part holds the AI pool job: 0x75B4B0 sorts the per-country AI
// objects by tag and runs 0x77D8A0, a parallel_for over them whose item is 0xB0FA0 (one country's daily
// AI). Both are hooked too, to time each job, each item, the threads the items ran on and each country.
//
// The game is usually closed without a clean exit, so a background thread writes the counters
// every 5 s to tick_profile*.tsv in the data directory.
#include "modules.hpp"
#include "tick_profile_core.hpp"

#include <intrin.h>

#include <fstream>
#include <string>

extern "C" {
// requests = game days, hits = checkpoints (0x769EA0 calls), mode = 1 on.
__declspec(dllexport) sb::Stats EU4SB_TickProfileStats = SB_STATS_INIT;
}

namespace {

using DailyTickFn = void (*)(void* state);
using CheckpointFn = void (*)(void* state, std::uint8_t force, void* a3, int line);
using AiJobFn = void (*)(void* list, void* arg);
using AiCountryFn = void (*)(void* ai);

constexpr DWORD kWriteEveryMs = 5000;

anvil::HookOf<sym::daily_tick> g_dayHook;
anvil::HookOf<sym::nested_frame_check> g_checkHook;
DailyTickFn g_day = nullptr;  // the rest of each hook's chain
CheckpointFn g_check = nullptr;
anvil::HookOf<sym::ai_job> g_jobHook;
anvil::HookOf<sym::ai_country> g_countryHook;
AiJobFn g_job = nullptr;
AiCountryFn g_country = nullptr;

// The job in progress (one at a time, started on the main thread); items run on any thread.
constexpr int kSlots = 64;
struct Slot {
    volatile LONG tid;
    volatile LONG64 busy;
};
Slot g_slots[kSlots];
volatile LONG g_inJob = 0;
volatile LONG g_items = 0;
volatile LONG64 g_itemSum = 0;
volatile LONG64 g_itemMax = 0;
DWORD g_mainTid = 0;

// Per country (tag index, bits 32-47 of the AI object's +0x20), for the whole session.
constexpr std::size_t kAiId = 0x20;
constexpr int kCountries = 4096;
volatile LONG64 g_countryCalls[kCountries];
volatile LONG64 g_countryTotal[kCountries];
volatile LONG64 g_countryMax[kCountries];
const unsigned char* const* g_rootGlobal = nullptr;
std::size_t g_rootDate = 0;  // int32 hours since year -5000 in the game root object
std::uintptr_t g_imageBase = 0;
SRWLOCK g_lock = SRWLOCK_INIT;
sb::TickProfile* g_profile = nullptr;
volatile LONG g_dirty = 0;
std::wstring* g_dir = nullptr;

std::int32_t GameDay() noexcept {
    const unsigned char* root = *g_rootGlobal;
    if (root == nullptr) return 0;
    const std::int32_t raw = sb::Read<std::int32_t>(root, g_rootDate);
    return static_cast<std::int32_t>((static_cast<std::int64_t>(raw) - 43'800'000) / 24);
}

void DayDetour(void* state) {
    AcquireSRWLockExclusive(&g_lock);
    g_profile->DayStart(GameDay(), sb::Ticks());
    ReleaseSRWLockExclusive(&g_lock);
    g_day(state);
    AcquireSRWLockExclusive(&g_lock);
    try {
        g_profile->DayEnd(sb::Ticks());
    } catch (...) {
    }
    ReleaseSRWLockExclusive(&g_lock);
    InterlockedIncrement64(&EU4SB_TickProfileStats.requests);
    InterlockedExchange(&g_dirty, 1);
}

void CheckDetour(void* state, std::uint8_t force, void* a3, int line) {
    const auto site = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - g_imageBase);
    const LONG64 entered = sb::Ticks();
    AcquireSRWLockExclusive(&g_lock);
    try {
        g_profile->CheckpointEnter(site, entered);
    } catch (...) {
    }
    ReleaseSRWLockExclusive(&g_lock);
    g_check(state, force, a3, line);
    const LONG64 left = sb::Ticks();
    AcquireSRWLockExclusive(&g_lock);
    try {
        g_profile->CheckpointLeave(site, entered, left);
    } catch (...) {
    }
    ReleaseSRWLockExclusive(&g_lock);
    InterlockedIncrement64(&EU4SB_TickProfileStats.hits);
}

void AtomicMax(volatile LONG64* target, LONG64 value) noexcept {
    LONG64 seen = *target;
    while (value > seen) {
        const LONG64 previous = InterlockedCompareExchange64(target, value, seen);
        if (previous == seen) break;
        seen = previous;
    }
}

void JobDetour(void* list, void* arg) {
    for (auto& slot : g_slots) {
        slot.tid = 0;
        slot.busy = 0;
    }
    g_items = 0;
    g_itemSum = 0;
    g_itemMax = 0;
    g_mainTid = GetCurrentThreadId();
    InterlockedExchange(&g_inJob, 1);
    const LONG64 start = sb::Ticks();
    g_job(list, arg);
    const LONG64 wall = sb::Ticks() - start;
    InterlockedExchange(&g_inJob, 0);
    sb::TickJob job;
    job.wall = wall;
    job.items = g_items;
    job.sum = g_itemSum;
    job.max_item = g_itemMax;
    for (const auto& slot : g_slots) {
        if (slot.tid == 0) continue;
        const std::int64_t busy = slot.busy;
        ++job.threads;
        job.max_busy = std::max(job.max_busy, busy);
        if (static_cast<DWORD>(slot.tid) == g_mainTid) job.main_busy = busy;
    }
    AcquireSRWLockExclusive(&g_lock);
    try {
        g_profile->AddJob(job);
    } catch (...) {
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void CountryDetour(void* ai) {
    const LONG64 start = sb::Ticks();
    g_country(ai);
    const LONG64 ticks = sb::Ticks() - start;
    const int id = static_cast<std::int16_t>(sb::Read<std::uint64_t>(ai, kAiId) >> 32);
    if (id >= 0 && id < kCountries) {
        InterlockedIncrement64(&g_countryCalls[id]);
        InterlockedExchangeAdd64(&g_countryTotal[id], ticks);
        AtomicMax(&g_countryMax[id], ticks);
    }
    if (g_inJob == 0) return;
    InterlockedIncrement(&g_items);
    InterlockedExchangeAdd64(&g_itemSum, ticks);
    AtomicMax(&g_itemMax, ticks);
    const auto tid = static_cast<LONG>(GetCurrentThreadId());
    for (auto& slot : g_slots) {
        // The thread's own slot, or the first free one it claims.
        if (slot.tid != tid) {
            const LONG previous = slot.tid == 0 ? InterlockedCompareExchange(&slot.tid, tid, 0) : slot.tid;
            if (previous != 0 && previous != tid) continue;
        }
        InterlockedExchangeAdd64(&slot.busy, ticks);
        break;
    }
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
        write(L"tick_profile_segments.tsv", [&](std::ostream& out) { g_profile->WriteSegments(out, msPerTick); });
        write(L"tick_profile_segments_first.tsv", [&](std::ostream& out) { g_profile->WriteSegments(out, msPerTick, true); });
        write(L"tick_profile_frames.tsv", [&](std::ostream& out) { g_profile->WriteFrames(out, msPerTick); });
        write(L"tick_profile_days.tsv", [&](std::ostream& out) { g_profile->WriteDays(out, msPerTick); });
        write(L"tick_profile_jobs.tsv", [&](std::ostream& out) { g_profile->WriteJobs(out, msPerTick); });
        write(L"tick_profile_countries.tsv", [&](std::ostream& out) {
            out << "country\tcalls\ttotal_ms\tmax_ms\n";
            for (int id = 0; id < kCountries; ++id) {
                if (g_countryCalls[id] == 0) continue;
                out << id << "\t" << g_countryCalls[id] << "\t" << g_countryTotal[id] * msPerTick << "\t"
                    << g_countryMax[id] * msPerTick << "\n";
            }
        });
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

namespace sb {

bool InstallTickProfile(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    try {
        g_profile = new TickProfile();
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    g_imageBase = host.game().image_base;
    g_rootGlobal = host.data(sym::game_root);
    g_rootDate = static_cast<std::size_t>(host.value(sym::root_and_date, "date_offset"));
    Undo undo;
    host.hook(g_dayHook, sym::daily_tick, &DayDetour);
    undo.add(g_dayHook);
    host.hook(g_checkHook, sym::nested_frame_check, &CheckDetour);
    undo.add(g_checkHook);
    host.hook(g_jobHook, sym::ai_job, &JobDetour);
    undo.add(g_jobHook);
    host.hook(g_countryHook, sym::ai_country, &CountryDetour);
    undo.add(g_countryHook);
    g_day = g_dayHook.next_pointer();
    g_check = g_checkHook.next_pointer();
    g_job = g_jobHook.next_pointer();
    g_country = g_countryHook.next_pointer();
    undo.keep();
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows tick_profile writer");
        CloseHandle(thread);
    }
    InterlockedExchange(&EU4SB_TickProfileStats.mode, 1);
    InterlockedExchange(&EU4SB_TickProfileStats.initialized, 1);
    Log(ANVIL_LOG_INFO, "tick_profile: observing the daily tick, its frame checkpoints and the AI pool job; counters in "
        "tick_profile*.tsv every %u s", kWriteEveryMs / 1000);
    return true;
}

}  // namespace sb
