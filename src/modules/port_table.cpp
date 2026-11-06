// port_table (off by default): the game's table of the nearest port without its repeated walks
// (lab docs: docs/simulation-profile.md, "Tabela portów"; the loop in port_table_core.hpp).
//
// A rebuild whose WorldPrint and table are those of the country's last one is left out. "verify" never
// changes the table: the game's function runs, Fill runs on a copy that must end with the same cells, and
// a rebuild that would have been left out must leave the table as it was. "on" checks every 16th rebuild
// the same way and goes back to the game's function for good after a mismatch. A background thread writes the counters every 5 s to port_table.tsv in the data
// directory, because the game is usually closed without a clean exit.
#include "modules.hpp"
#include "port_table_core.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
// requests = rebuilds, hits = rebuilds by Fill, misses = rebuilds left out, skips = rebuilds left to the
// game's function, verifications = rebuilds compared with the game's function, mismatches = comparisons
// that differed,
// cacheDisabled = 1 after a mismatch in "on", mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_PortTableStats = SB_STATS_INIT;
}

namespace {

namespace port = sb::port;

using TableFn = void (*)(void* country);
using DistanceFn = void* (*)(std::int32_t* out, std::uint64_t country, const void* from, const void* to, void* blocked,
                             std::uint32_t land);
using RangeFn = std::int32_t* (*)(void* country, std::int32_t* out);

anvil::HookOf<sym::port_table> g_hook;
TableFn g_next = nullptr;
DistanceFn g_distance = nullptr;
RangeFn g_range = nullptr;
const unsigned char* const* g_root = nullptr;
bool g_verifyOnly = false;
volatile LONG64 g_checkSequence = 0;
volatile LONG64 g_gameTicks = 0, g_fastTicks = 0, g_collectTicks = 0, g_ports = 0, g_gameCalls = 0;
// Compared rebuilds that left the table as it was, and their time in the game's function.
volatile LONG64 g_unchanged = 0, g_unchangedTicks = 0;
std::wstring* g_dir = nullptr;
constexpr DWORD kWriteEveryMs = 5000;
constexpr LONG64 kCheckEvery = 16;
volatile LONG64 g_printTicks = 0, g_wouldSkip = 0, g_wrongSkip = 0;

struct Last {
    std::uint64_t world = 0, table = 0;
};
std::mutex* g_lastLock = nullptr;
std::unordered_map<const void*, Last>* g_last = nullptr;
thread_local std::vector<port::Bytes>* t_ports = nullptr;
thread_local std::vector<port::Bytes>* t_groups = nullptr;
thread_local std::vector<std::uint8_t>* t_own = nullptr;

struct Prepared {
    std::int32_t reach = 0;
    std::uint64_t world = 0;
    bool same = false;  // the world and the table of the country's last rebuild
};

// The list and the print; nothing is written yet. Throws only here.
Prepared Prepare(const unsigned char* root, unsigned char* country, const std::int16_t* table, std::size_t cells) {
    if (t_ports == nullptr) t_ports = new std::vector<port::Bytes>();
    if (t_groups == nullptr) t_groups = new std::vector<port::Bytes>();
    if (t_own == nullptr) t_own = new std::vector<std::uint8_t>();
    t_ports->clear();
    Prepared p;
    LONG64 start = sb::Ticks();
    port::CollectPorts(root, country, *t_ports);
    InterlockedExchangeAdd64(&g_collectTicks, sb::Ticks() - start);
    InterlockedExchangeAdd64(&g_ports, static_cast<LONG64>(t_ports->size()));
    g_range(country, &p.reach);
    start = sb::Ticks();
    p.world = port::WorldPrint(root, country, *t_ports, p.reach, *t_groups, *t_own);
    const std::uint64_t now = port::TablePrint(table, cells);
    InterlockedExchangeAdd64(&g_printTicks, sb::Ticks() - start);
    std::lock_guard lock(*g_lastLock);
    const auto found = g_last->find(country);
    p.same = found != g_last->end() && found->second.world == p.world && found->second.table == now;
    return p;
}

void Remember(const void* country, std::uint64_t world, const std::int16_t* table, std::size_t cells) {
    const std::uint64_t now = port::TablePrint(table, cells);
    std::lock_guard lock(*g_lastLock);
    (*g_last)[country] = Last{world, now};
}

void Rebuild(const unsigned char* root, unsigned char* country, std::int16_t* table, std::int32_t reach) {
    const auto id = port::At<std::uint64_t>(country, port::kCountryId);
    port::Fill(
        root, country, *t_ports, table,
        [id](port::Bytes from, port::Bytes to) {
            std::int32_t distance = 0;
            g_distance(&distance, id, from, to, nullptr, 1);
            return distance;
        },
        reach);
}

void Detour(void* countryPtr) {
    InterlockedIncrement64(&EU4SB_PortTableStats.requests);
    const unsigned char* root = *g_root;
    auto* country = static_cast<unsigned char*>(countryPtr);
    if (EU4SB_PortTableStats.cacheDisabled != 0 || root == nullptr || !port::Applies(root, country)) {
        InterlockedIncrement64(&EU4SB_PortTableStats.skips);
        return g_next(countryPtr);
    }
    auto* table = port::At<std::int16_t*>(country, port::kCountryTable);
    const std::size_t cells = (port::At<std::int16_t*>(country, port::kCountryTableEnd) - table);
    const bool check = g_verifyOnly || (InterlockedIncrement64(&g_checkSequence) & (kCheckEvery - 1)) == 0;
    if (!check) {
        const LONG64 start = sb::Ticks();
        try {
            const Prepared prepared = Prepare(root, country, table, cells);
            if (prepared.same) {
                InterlockedIncrement64(&EU4SB_PortTableStats.misses);
            } else {
                Rebuild(root, country, table, prepared.reach);
                InterlockedIncrement64(&EU4SB_PortTableStats.hits);
                Remember(country, prepared.world, table, cells);
            }
        } catch (...) {
            // Only the list, the print and the record allocate, and a table Fill has written is the game's answer.
            InterlockedIncrement64(&EU4SB_PortTableStats.skips);
            return g_next(countryPtr);
        }
        country[port::kCountryDirty] = 0;
        InterlockedExchangeAdd64(&g_fastTicks, sb::Ticks() - start);
        return;
    }
    std::vector<std::int16_t> copy, before;
    Prepared prepared;
    try {
        before.assign(table, table + cells);
        copy = before;
        const LONG64 start = sb::Ticks();
        prepared = Prepare(root, country, table, cells);
        Rebuild(root, country, copy.data(), prepared.reach);
        InterlockedExchangeAdd64(&g_fastTicks, sb::Ticks() - start);
    } catch (...) {
        InterlockedIncrement64(&EU4SB_PortTableStats.skips);
        return g_next(countryPtr);
    }
    const LONG64 start = sb::Ticks();
    g_next(countryPtr);
    const LONG64 gameTicks = sb::Ticks() - start;
    InterlockedExchangeAdd64(&g_gameTicks, gameTicks);
    InterlockedIncrement64(&g_gameCalls);
    if (std::equal(before.begin(), before.end(), table)) {
        InterlockedIncrement64(&g_unchanged);
        InterlockedExchangeAdd64(&g_unchangedTicks, gameTicks);
    }
    InterlockedIncrement64(&EU4SB_PortTableStats.verifications);
    const bool wrongSkip = prepared.same && !std::equal(before.begin(), before.end(), table);
    if (prepared.same) InterlockedIncrement64(&g_wouldSkip);
    if (wrongSkip) InterlockedIncrement64(&g_wrongSkip);
    try {
        Remember(country, prepared.world, table, cells);
    } catch (...) {
    }
    if (wrongSkip || !std::equal(copy.begin(), copy.end(), table)) {
        InterlockedIncrement64(&EU4SB_PortTableStats.mismatches);
        if (!g_verifyOnly && InterlockedExchange(&EU4SB_PortTableStats.cacheDisabled, 1) == 0) {
            sb::Log(ANVIL_LOG_WARNING, "port_table: a table differed from the game's; back to the game's function");
        }
    }
}

void WriteFile() {
    const sb::Stats& s = EU4SB_PortTableStats;
    try {
        const std::wstring path = *g_dir + L"\\port_table.tsv";
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            const double ms = sb::TicksToMs(1);
            out << "mode\trequests\thits\tskips\tverifications\tmismatches\tdisabled\tgame_calls\tgame_ms\tfast_ms\t"
                   "collect_ms\tports\tunchanged\tunchanged_game_ms\tleft_out\tprint_ms\twould_leave_out\twrong_leave_out\n"
                << (g_verifyOnly ? "verify" : "on") << "\t" << s.requests << "\t" << s.hits << "\t" << s.skips << "\t"
                << s.verifications << "\t" << s.mismatches << "\t" << s.cacheDisabled << "\t" << g_gameCalls << "\t"
                << g_gameTicks * ms << "\t" << g_fastTicks * ms << "\t" << g_collectTicks * ms << "\t" << g_ports << "\t"
                << g_unchanged << "\t" << g_unchangedTicks * ms << "\t" << s.misses << "\t" << g_printTicks * ms << "\t"
                << g_wouldSkip << "\t" << g_wrongSkip << "\n";
        }
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    LONG64 written = -1;
    for (;;) {
        Sleep(kWriteEveryMs);
        if (EU4SB_PortTableStats.requests != written) {
            written = EU4SB_PortTableStats.requests;
            WriteFile();
        }
    }
}

}  // namespace

namespace sb {

bool InstallPortTable(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyOnly = mode == Mode::Verify;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
        g_lastLock = new std::mutex;
        g_last = new std::unordered_map<const void*, Last>();
    } catch (...) {
        return false;
    }
    // The table calls 0x2D2520 with six arguments; the symbol declares the seven range_profile forwards.
    g_distance = reinterpret_cast<DistanceFn>(host.address(sym::province_distance));
    g_range = reinterpret_cast<RangeFn>(host.address(sym::port_range));
    g_root = host.data(sym::game_root);
    host.hook(g_hook, sym::port_table, &Detour);
    g_next = g_hook.next_pointer();
    InterlockedExchange(&EU4SB_PortTableStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_PortTableStats.initialized, 1);
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows port_table writer");
        CloseHandle(thread);
    }
    Log(ANVIL_LOG_INFO, "port_table: %s the nearest-port table from one list of ports",
        g_verifyOnly ? "verifying" : "rebuilding");
    return true;
}

void ReportPortTable() {
    LogStats("port_table", EU4SB_PortTableStats);  // no Benchmark counters: Anvil takes 128 fields, and they are full
}

}  // namespace sb
