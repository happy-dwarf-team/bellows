// Diagnostic (off by default): what the AI army search costs (lab docs: docs/simulation-profile.md,
// "Co kosztuje w AI kraju").
//
// About half of the per-country daily AI (0xB0FA0) is 0x1022B0, called from ai_army.cpp (0xEAB50): a
// priority-queue search over the province graph with a scoring functor, which also clears two arrays
// sized by the province count on every call. The function is hooked (inline, through Anvil) only to
// observe: every call is recorded with its army, arguments, result, score, time and game day, and the
// counters tell how often the same army asked the same thing got the same answer. Nothing the game
// computes is changed.
//
// The game is usually closed without a clean exit, so a background thread writes the counters every
// 5 s to army_search*.tsv in the data directory.
#include "army_search_core.hpp"
#include "modules.hpp"

#include <fstream>
#include <string>

extern "C" {
// requests = searches, mode = 1 on.
__declspec(dllexport) sb::Stats EU4SB_ArmySearchProfileStats = SB_STATS_INIT;
}

namespace {

using SearchFn = std::uint64_t (*)(void* scorer, int mode, void* army, std::uint64_t arg, float* score,
                                   std::uint8_t a6, std::uint8_t* a7, std::uint8_t* a8, std::uint8_t* a9,
                                   std::uint8_t a10, std::uint8_t a11, void* path);

constexpr DWORD kWriteEveryMs = 5000;
constexpr std::size_t kRootProvinceVector = 0x1C90;  // begin/end of 8-byte entries, sized by 0x1022B0

anvil::HookOf<sym::army_search> g_hook;
SearchFn g_search = nullptr;  // the rest of the hook's chain
const unsigned char* const* g_rootGlobal = nullptr;
std::size_t g_rootDate = 0;  // int32 hours since year -5000 in the game root object
SRWLOCK g_lock = SRWLOCK_INIT;
sb::ArmySearchStats* g_stats = nullptr;
volatile LONG g_dirty = 0;
std::wstring* g_dir = nullptr;

std::int32_t GameDay() noexcept {
    const unsigned char* root = *g_rootGlobal;
    if (root == nullptr) return 0;
    const std::int32_t raw = sb::Read<std::int32_t>(root, g_rootDate);
    return static_cast<std::int32_t>((static_cast<std::int64_t>(raw) - 43'800'000) / 24);
}

std::int64_t Provinces() noexcept {
    const unsigned char* root = *g_rootGlobal;
    if (root == nullptr) return 0;
    const auto begin = sb::Read<std::uintptr_t>(root, kRootProvinceVector);
    const auto end = sb::Read<std::uintptr_t>(root, kRootProvinceVector + 8);
    return end > begin ? static_cast<std::int64_t>((end - begin) / 8) : 0;
}

std::uint64_t SearchDetour(void* scorer, int mode, void* army, std::uint64_t arg, float* score, std::uint8_t a6,
                           std::uint8_t* a7, std::uint8_t* a8, std::uint8_t* a9, std::uint8_t a10, std::uint8_t a11,
                           void* path) {
    const LONG64 start = sb::Ticks();
    const std::uint64_t result = g_search(scorer, mode, army, arg, score, a6, a7, a8, a9, a10, a11, path);
    sb::ArmySearchObservation o;
    o.ticks = sb::Ticks() - start;
    o.army = reinterpret_cast<std::uintptr_t>(army);
    o.mode = mode;
    o.arg = arg;
    o.result = static_cast<std::int32_t>(result);
    o.score = score != nullptr ? *score : 0.0f;
    o.path = path != nullptr;
    o.day = GameDay();
    AcquireSRWLockExclusive(&g_lock);
    try {
        g_stats->Add(o);
    } catch (...) {
    }
    ReleaseSRWLockExclusive(&g_lock);
    InterlockedIncrement64(&EU4SB_ArmySearchProfileStats.requests);
    InterlockedExchange(&g_dirty, 1);
    return result;
}

void WriteFiles() {
    const double msPerTick = sb::TicksToMs(1);
    const std::int64_t provinces = Provinces();
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
        write(L"army_search.tsv", [&](std::ostream& out) { g_stats->WriteSummary(out, msPerTick, provinces); });
        write(L"army_search_days.tsv", [&](std::ostream& out) { g_stats->WriteDays(out, msPerTick); });
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

bool InstallArmySearchProfile(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    try {
        g_stats = new ArmySearchStats();
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    g_rootGlobal = host.data(sym::game_root);
    g_rootDate = static_cast<std::size_t>(host.value(sym::root_and_date, "date_offset"));
    host.hook(g_hook, sym::army_search, &SearchDetour);
    g_search = g_hook.next_pointer();
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows army_search_profile writer");
        CloseHandle(thread);
    }
    InterlockedExchange(&EU4SB_ArmySearchProfileStats.mode, 1);
    InterlockedExchange(&EU4SB_ArmySearchProfileStats.initialized, 1);
    Log(ANVIL_LOG_INFO, "army_search_profile: observing the AI army search; counters in army_search*.tsv every %u s",
        kWriteEveryMs / 1000);
    return true;
}

}  // namespace sb
