// opinion_serial (prototype, off by default): a country's opinion row without the worker pool.
//
// CCountry::UpdateOpinionCache (0x38B1F0) runs in the serial daily country update when a country's opinion row
// is out of date (on a big mod set, for almost every country on the day after the monthly tick). It fills the
// row in a ParallelFor3 (0x424380) over all countries of the root: each item writes row[i] =
// 0x3839B0(country, tag of country i, nullptr, true) for a country i with provinces (or of kind 7), and only
// row[i]. On a big mod set the calling thread waits for the pool for a good part of the call.
//
// The module redirects the call of 0x424380 (0x38B322) to a loop on the calling thread, in index order, with
// the game's item. "verify" runs the game's ParallelFor3 for every call as well, into the country's row (the
// game's answer is the one kept), and the module's loop into a row of its own, and compares the two; "on" does
// that for every 256th call and goes back to the game's loop for good after a difference. A background thread
// writes the counters every 5 s to opinion_serial.tsv in the data directory, with the time of each loop by
// whether it ran first or second in a checked call.
#include "modules.hpp"
#include "serial_loop.hpp"
#include "serial_loop_core.hpp"
#include "../file_write.hpp"

#include <sstream>
#include <string>
#include <vector>

extern "C" {
// requests = calls, hits = calls the module's loop answered, skips = calls left to the game (empty, or after a
// difference), verifications = checked calls, mismatches = checked calls whose rows differed, cacheDisabled = 1
// after a difference in "on", mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_OpinionSerialStats = SB_STATS_INIT;
}

namespace {

namespace sl = sb::serial_loop;
using Ti = sym::Types::opinion_item;

anvil::FunctionHook<void(const void*, const void*)> g_fillSite;
void (*g_gameFill)(const void*, const void*) = nullptr;
sym::Types::opinion_value::Pointer g_value = nullptr;
bool g_verifyOnly = false;
std::wstring* g_dir = nullptr;
constexpr DWORD kWriteEveryMs = 5000;

volatile LONG64 g_countries = 0, g_written = 0, g_loopTicks = 0, g_checked = 0;
sl::OrderedTimes g_times;
SRWLOCK g_timesLock = SRWLOCK_INIT;

// The item's test: the game writes row[i] only for these countries.
bool Written(const void* country) noexcept {
    return sb::Read<std::int32_t>(country, Ti::provinces_offset) != 0 ||
           sb::Read<std::uint8_t>(country, Ti::kind_offset) == Ti::kind_value;
}

// The module's loop: the game's item for every country, in index order, into `row`.
std::size_t Fill(const sl::PointerArray& countries, void* self, std::int16_t* row) {
    const sl::GameLoopScope scope;
    std::size_t written = 0;
    for (std::size_t i = 0; i < countries.size(); ++i) {
        const void* other = countries.begin[i];
        if (!Written(other)) continue;
        row[i] = g_value(self, sb::Read<std::uint64_t>(other, Ti::tag_offset), nullptr, 1);
        ++written;
    }
    return written;
}

void CheckedCall(const sl::PointerArray& countries, const void* lambda, void* self) {
    thread_local std::vector<std::int16_t> mine;
    mine.assign(countries.size(), 0);
    const bool gameFirst = sl::GameFirst(static_cast<std::uint64_t>(InterlockedIncrement64(&g_checked)));
    LONG64 gameTicks = 0, mineTicks = 0;
    for (int run = 0; run < 2; ++run) {
        const LONG64 start = sb::Ticks();
        if ((run == 0) == gameFirst) {
            g_gameFill(&countries, lambda);
            gameTicks = sb::Ticks() - start;
        } else {
            Fill(countries, self, mine.data());
            mineTicks = sb::Ticks() - start;
        }
    }
    const auto* row = sb::Read<const std::int16_t*>(self, Ti::row_offset);
    const std::size_t differences = sl::RowDifferences(row, mine.data(), countries.size(),
                                                        [&](std::size_t i) { return Written(countries.begin[i]); });
    InterlockedIncrement64(&EU4SB_OpinionSerialStats.verifications);
    AcquireSRWLockExclusive(&g_timesLock);
    g_times.Add(true, gameFirst, gameTicks);
    g_times.Add(false, !gameFirst, mineTicks);
    ReleaseSRWLockExclusive(&g_timesLock);
    if (differences != 0) {
        InterlockedIncrement64(&EU4SB_OpinionSerialStats.mismatches);
        if (!g_verifyOnly && InterlockedExchange(&EU4SB_OpinionSerialStats.cacheDisabled, 1) == 0)
            sb::Log(ANVIL_LOG_WARNING, "opinion_serial: an opinion row differed from the game's; back to the game's loop");
    }
}

void FillDetour(const void* array, const void* lambda) {
    const auto& countries = *static_cast<const sl::PointerArray*>(array);
    void* self = *static_cast<void* const*>(lambda);  // the game's lambda holds the country
    const LONG64 call = InterlockedIncrement64(&EU4SB_OpinionSerialStats.requests);
    if (countries.size() == 0 || EU4SB_OpinionSerialStats.cacheDisabled != 0) {
        InterlockedIncrement64(&EU4SB_OpinionSerialStats.skips);
        g_gameFill(array, lambda);
        return;
    }
    InterlockedExchangeAdd64(&g_countries, static_cast<LONG64>(countries.size()));
    if (sl::Checked(g_verifyOnly, static_cast<std::uint64_t>(call))) {
        CheckedCall(countries, lambda, self);
        return;
    }
    const LONG64 start = sb::Ticks();
    const std::size_t written = Fill(countries, self, sb::Read<std::int16_t*>(self, Ti::row_offset));
    InterlockedExchangeAdd64(&g_loopTicks, sb::Ticks() - start);
    InterlockedExchangeAdd64(&g_written, static_cast<LONG64>(written));
    InterlockedIncrement64(&EU4SB_OpinionSerialStats.hits);
}

void WriteFile() {
    const sb::Stats& s = EU4SB_OpinionSerialStats;
    try {
        sl::OrderedTimes t;
        AcquireSRWLockShared(&g_timesLock);
        t = g_times;
        ReleaseSRWLockShared(&g_timesLock);
        const double ms = sb::TicksToMs(1);
        std::ostringstream out;
        out << "mode\tcalls\tloop_calls\tgame_calls\tchecked\tmismatches\tdisabled\tcountries\twritten\tloop_ms"
               "\tmine_first_calls\tmine_first_ms\tmine_second_calls\tmine_second_ms\tgame_first_calls\tgame_first_ms"
               "\tgame_second_calls\tgame_second_ms\n"
            << (g_verifyOnly ? "verify" : "on") << "\t" << s.requests << "\t" << s.hits << "\t" << s.skips << "\t"
            << s.verifications << "\t" << s.mismatches << "\t" << s.cacheDisabled << "\t" << g_countries << "\t"
            << g_written << "\t" << g_loopTicks * ms;
        for (int who = 0; who < 2; ++who) {
            for (int order = 0; order < 2; ++order) out << "\t" << t.calls[who][order] << "\t" << t.ticks[who][order] * ms;
        }
        out << "\n";
        sb::ReplaceFileWith(*g_dir + L"\\opinion_serial.tsv", out.str());
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    LONG64 written = -1;
    for (;;) {
        Sleep(kWriteEveryMs);
        if (EU4SB_OpinionSerialStats.requests != written) {
            written = EU4SB_OpinionSerialStats.requests;
            WriteFile();
        }
    }
}

}  // namespace

namespace sb {

bool InstallOpinionSerial(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyOnly = mode == Mode::Verify;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    serial_loop::Resolve(host);
    g_value = host.function(sym::opinion_value);
    Undo undo;
    host.redirect(g_fillSite, sym::opinion_fill_call, &FillDetour);
    undo.add(g_fillSite);
    g_gameFill = g_fillSite.next_pointer();
    undo.keep();
    InterlockedExchange(&EU4SB_OpinionSerialStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_OpinionSerialStats.initialized, 1);
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows opinion_serial writer");
        CloseHandle(thread);
    }
    Log(ANVIL_LOG_INFO, "opinion_serial: %s each country's opinion row on the calling thread",
        g_verifyOnly ? "verifying" : "filling");
    return true;
}

}  // namespace sb
