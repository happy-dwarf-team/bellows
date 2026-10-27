// Diagnostic (off by default): could the scoring work of the AI army search be remembered within a game
// day (lab docs: docs/simulation-profile.md, "Wyszukiwanie armii AI")?
//
// The search 0x1022B0 spends its time in 0xF8EA0 (province evaluation), 0x96F860 (a method of the
// scoring functor: can the unit move into a province) and 0x102140 (strength against a province, mostly
// 0x198C70 > 0x1998F0 on the per-province table 0x242BA00). The last three are hooked (inline, through
// Anvil) only to observe: per call the inputs become a key, with and without the army or unit, and a
// memo per thread, cleared at each new game day, counts the keys that come back and whether the output
// is the same. Nothing the game computes is changed.
//
// The game is usually closed without a clean exit, so a background thread writes the counters every
// 5 s to score_probe.tsv in the data directory.
#include "modules.hpp"
#include "score_probe_core.hpp"

#include <fstream>
#include <string>

extern "C" {
// requests = observed calls of all three functions, mode = 1 on.
__declspec(dllexport) sb::Stats EU4SB_ScoreProbeStats = SB_STATS_INIT;
}

namespace {

using MoveFn = std::uint64_t (*)(void* self, void* unit, void* a3, void* a4, void* a5, void* a6, char* a7);
using StrengthFn = float (*)(std::uint64_t flag, std::uint64_t army, void* province, std::uint8_t a4, int a5);
using TableFn = int* (*)(int* out, std::int64_t* entry, int delta, std::int64_t table, std::int64_t* extra);

constexpr DWORD kWriteEveryMs = 5000;

struct Probe {
    const char* name;
    volatile LONG64 calls = 0, ticks = 0, full_repeats = 0, full_same = 0, partial_repeats = 0, partial_same = 0;

    void Count(const sb::ProbeCounts& c, LONG64 t) noexcept {
        InterlockedIncrement64(&calls);
        InterlockedExchangeAdd64(&ticks, t);
        if (c.full_repeats) InterlockedIncrement64(&full_repeats);
        if (c.full_same) InterlockedIncrement64(&full_same);
        if (c.partial_repeats) InterlockedIncrement64(&partial_repeats);
        if (c.partial_same) InterlockedIncrement64(&partial_same);
    }
};

Probe g_move{"move_0x96F860"}, g_strength{"strength_0x102140"}, g_table{"table_0x1998F0"};
anvil::HookOf<sym::score_move> g_moveHook;
anvil::HookOf<sym::score_strength> g_strengthHook;
anvil::HookOf<sym::score_table> g_tableHook;
MoveFn g_moveNext = nullptr;  // the rest of each hook's chain
StrengthFn g_strengthNext = nullptr;
TableFn g_tableNext = nullptr;
const unsigned char* const* g_rootGlobal = nullptr;
std::size_t g_rootDate = 0;  // int32 hours since year -5000 in the game root object
volatile LONG g_dirty = 0;
std::wstring* g_dir = nullptr;

thread_local sb::ProbeMemo* t_memos = nullptr;  // move, strength, table

sb::ProbeMemo* Memos() {
    if (t_memos == nullptr) t_memos = new sb::ProbeMemo[3];  // one per thread, for the process
    return t_memos;
}

std::int32_t GameDay() noexcept {
    const unsigned char* root = *g_rootGlobal;
    if (root == nullptr) return 0;
    const std::int32_t raw = sb::Read<std::int32_t>(root, g_rootDate);
    return static_cast<std::int32_t>((static_cast<std::int64_t>(raw) - 43'800'000) / 24);
}

template <class T> std::uint64_t Mix(std::uint64_t h, const T& value) noexcept { return sb::Fnv(h, &value, sizeof value); }

void Record(Probe& probe, int index, std::uint64_t full, std::uint64_t partial, std::uint64_t output, LONG64 ticks) {
    try {
        probe.Count(Memos()[index].Add(GameDay(), full, partial, output), ticks);
    } catch (...) {
        InterlockedIncrement64(&probe.calls);
    }
    InterlockedIncrement64(&EU4SB_ScoreProbeStats.requests);
    InterlockedExchange(&g_dirty, 1);
}

std::uint64_t MoveDetour(void* self, void* unit, void* a3, void* a4, void* a5, void* a6, char* a7) {
    const LONG64 start = sb::Ticks();
    const std::uint64_t result = g_moveNext(self, unit, a3, a4, a5, a6, a7);
    const LONG64 ticks = sb::Ticks() - start;
    std::uint64_t partial = sb::kFnvBasis;
    partial = Mix(Mix(Mix(Mix(partial, a3), a4), a5), a6);
    if (a7 != nullptr) partial = sb::Fnv(partial, a7, 16);
    Record(g_move, 0, Mix(partial, unit), partial, result & 0xFF, ticks);
    return result;
}

float StrengthDetour(std::uint64_t flag, std::uint64_t army, void* province, std::uint8_t a4, int a5) {
    const LONG64 start = sb::Ticks();
    const float result = g_strengthNext(flag, army, province, a4, a5);
    const LONG64 ticks = sb::Ticks() - start;
    const std::uint8_t flagByte = static_cast<std::uint8_t>(flag);  // a byte in a 64-bit slot
    std::uint64_t partial = Mix(Mix(Mix(Mix(sb::kFnvBasis, flagByte), province), a4), a5);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &result, sizeof bits);
    Record(g_strength, 1, Mix(partial, army), partial, bits, ticks);
    return result;
}

int* TableDetour(int* out, std::int64_t* entry, int delta, std::int64_t table, std::int64_t* extra) {
    const LONG64 start = sb::Ticks();
    int* result = g_tableNext(out, entry, delta, table, extra);
    const LONG64 ticks = sb::Ticks() - start;
    const std::uint64_t partial = Mix(Mix(Mix(sb::kFnvBasis, entry), delta), table);
    const std::uint64_t full = extra != nullptr ? sb::Fnv(partial, extra, 32) : partial;
    Record(g_table, 2, full, partial, result != nullptr ? static_cast<std::uint32_t>(*result) : 0, ticks);
    return result;
}

void WriteFile() {
    const double msPerTick = sb::TicksToMs(1);
    try {
        const std::wstring path = *g_dir + L"\\score_probe.tsv";
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << "function\tcalls\ttotal_ms\tfull_repeats\tfull_same\tpartial_repeats\tpartial_same\n";
            for (const Probe* p : {&g_move, &g_strength, &g_table}) {
                out << p->name << "\t" << p->calls << "\t" << p->ticks * msPerTick << "\t" << p->full_repeats << "\t"
                    << p->full_same << "\t" << p->partial_repeats << "\t" << p->partial_same << "\n";
            }
        }
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    for (;;) {
        Sleep(kWriteEveryMs);
        if (InterlockedExchange(&g_dirty, 0) != 0) WriteFile();
    }
}

}  // namespace

namespace sb {

bool InstallScoreProbe(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    g_rootGlobal = host.data(sym::game_root);
    g_rootDate = static_cast<std::size_t>(host.value(sym::root_and_date, "date_offset"));
    Undo undo;
    host.hook(g_moveHook, sym::score_move, &MoveDetour);
    undo.add(g_moveHook);
    host.hook(g_strengthHook, sym::score_strength, &StrengthDetour);
    undo.add(g_strengthHook);
    host.hook(g_tableHook, sym::score_table, &TableDetour);
    undo.add(g_tableHook);
    g_moveNext = g_moveHook.next_pointer();
    g_strengthNext = g_strengthHook.next_pointer();
    g_tableNext = g_tableHook.next_pointer();
    undo.keep();
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows score_probe writer");
        CloseHandle(thread);
    }
    InterlockedExchange(&EU4SB_ScoreProbeStats.mode, 1);
    InterlockedExchange(&EU4SB_ScoreProbeStats.initialized, 1);
    Log(ANVIL_LOG_INFO, "score_probe: observing the AI army search's scoring functions; counters in score_probe.tsv "
        "every %u s", kWriteEveryMs / 1000);
    return true;
}

}  // namespace sb
