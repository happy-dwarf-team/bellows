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

constexpr DWORD kWriteEveryMs = 5000;

anvil::HookOf<sym::daily_tick> g_dayHook;
anvil::HookOf<sym::nested_frame_check> g_checkHook;
DailyTickFn g_day = nullptr;  // the rest of each hook's chain
CheckpointFn g_check = nullptr;
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
        write(L"tick_profile_frames.tsv", [&](std::ostream& out) { g_profile->WriteFrames(out, msPerTick); });
        write(L"tick_profile_days.tsv", [&](std::ostream& out) { g_profile->WriteDays(out, msPerTick); });
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
    g_day = g_dayHook.next_pointer();
    g_check = g_checkHook.next_pointer();
    undo.keep();
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows tick_profile writer");
        CloseHandle(thread);
    }
    InterlockedExchange(&EU4SB_TickProfileStats.mode, 1);
    InterlockedExchange(&EU4SB_TickProfileStats.initialized, 1);
    Log(ANVIL_LOG_INFO, "tick_profile: observing the daily tick and its frame checkpoints; counters in "
        "tick_profile*.tsv every %u s", kWriteEveryMs / 1000);
    return true;
}

}  // namespace sb
