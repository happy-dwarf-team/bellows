// opinion_prefetch (prototype, off by default): prefetches the other country's memory in the AI's opinion row.
//
// CCountry::UpdateOpinionCache (0x38B1F0) fills a country's opinion of every other country, one cell per entry
// of the root's country array, in a ParallelFor3 (0x424380) whose item (0x432E70) computes
// CCountry::CalculateOpinionValue(A, B) when B owns provinces (or is of kind 7) and stores it in row[i]. On a big
// mod set (Anbennar: about 1,900 entries, 940 of them computed) a row costs about 200 µs and is rebuilt about
// 80 times a game day. Each cell reads B's object at several places that are far apart (its tag, its provinces'
// count, its religion, its allies and rivals, its diplomacy rows) and B's diplomacy row for A, a 72-byte entry
// in a vector of about 134 KB: going down the column of a row means a cache miss for nearly every one of them.
//
// The module hooks the item. Before it runs the game's item for index i, it prefetches B's lines of the entry
// kFar positions ahead and, for the entry kNear positions ahead (whose lines the earlier item asked for), when
// the game's item will compute that cell, reads the two countries' tags and diplomacy rows' bounds and B's
// rivals' head and prefetches A's row in B's diplomacy, B's row in A's and B's first rival. Prefetch
// instructions do not change memory or the game's results. Every load is a field of the item's capture or of a
// live country object; no thread writes them while the loop runs (the items write only A's row), and a stale
// value would only make a prefetch address useless.
//
// "on" prefetches in every row. "verify" (opinion_prefetch_check) alternates rows with and without the prefetch
// and times every row, so the module's clocks say what it saves in the same game. There is no other answer to
// compare: the cells are always the game's item's. A background thread writes the counters every 5 s to
// opinion_prefetch.tsv in the data directory.
#include "modules.hpp"
#include "opinion_prefetch_core.hpp"
#include "../file_write.hpp"

#include <intrin.h>

#include <sstream>
#include <string>

extern "C" {
// requests = rows, hits = rows with the prefetch, skips = rows without it (the check's other arm),
// verifications = rows timed by the check, mismatches = 0 (the cells are the game's item's), mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_OpinionPrefetchStats = SB_STATS_INIT;
}

namespace {

namespace op = sb::opinion_prefetch;

anvil::FunctionHook<void(const void*, const void*)> g_fillSite;
void (*g_gameFill)(const void*, const void*) = nullptr;
anvil::FunctionHook<void(void*, std::int32_t*, void*)> g_itemHook;
void (*g_item)(void*, std::int32_t*, void*) = nullptr;

// The item's capture (std::_Func_impl): +array_offset the country array (begin, end), +country_offset the
// row's country.
std::size_t g_arrayAt = 0, g_countryAt = 0;
// Offsets in a country: the tag, the count of owned provinces, the diplomacy rows (begin; end 8 bytes after),
// the religion, the allies and the rivals (list heads), and the kind (a byte) with the value the item computes.
std::size_t g_tag = 0, g_owned = 0, g_kind = 0, g_rows = 0, g_religion = 0, g_allies = 0, g_rivals = 0;
std::uint8_t g_kindValue = 0;
constexpr std::size_t kRivalNodeTag = 0x10, kRivalNodeNext = 0x28;  // CList<CRival> node: the tag, the next node

volatile LONG g_active = 0;  // 1 while a row with the prefetch runs
bool g_verifyOnly = false;
std::wstring* g_dir = nullptr;
constexpr DWORD kWriteEveryMs = 5000;

volatile LONG64 g_cells = 0, g_ticks[2] = {}, g_calls[2] = {};  // [1] rows with the prefetch, [0] without

inline void Fetch(const void* p) noexcept { _mm_prefetch(static_cast<const char*>(p), _MM_HINT_T0); }

void Prefetch(const void* self, std::int32_t index) noexcept {
    const op::Capture c = op::ReadCapture(self, g_arrayAt, g_countryAt);
    if (c.begin == nullptr) return;
    const std::ptrdiff_t ahead = op::Ahead(index, c.count, op::kFar);
    if (ahead >= 0) {
        if (const unsigned char* b = c.begin[ahead]) {
            Fetch(b + g_tag);
            Fetch(b + g_owned);
            Fetch(b + g_rows);
            Fetch(b + g_religion);
            Fetch(b + g_allies);
            Fetch(b + g_rivals);
        }
    }
    const std::ptrdiff_t close = op::Ahead(index, c.count, op::kNear);
    if (close < 0) return;
    const unsigned char* b = c.begin[close];
    const unsigned char* a = c.a;
    if (b == nullptr || a == nullptr || !op::Computed(b, g_owned, g_kind, g_kindValue)) return;
    const int aTag = op::TagIndex(sb::Read<std::uint64_t>(a, g_tag));
    const int bTag = op::TagIndex(sb::Read<std::uint64_t>(b, g_tag));
    if (const unsigned char* row = op::Row(sb::Read<const unsigned char*>(b, g_rows),
                                           sb::Read<const unsigned char*>(b, g_rows + 8), aTag)) {
        Fetch(row);
        Fetch(row + op::kRowSize - 1);
    }
    if (const unsigned char* row = op::Row(sb::Read<const unsigned char*>(a, g_rows),
                                           sb::Read<const unsigned char*>(a, g_rows + 8), bTag)) {
        Fetch(row);
        Fetch(row + op::kRowSize - 1);
    }
    if (const unsigned char* node = sb::Read<const unsigned char*>(b, g_rivals)) {
        Fetch(node + kRivalNodeTag);
        Fetch(node + kRivalNodeNext);
    }
}

// Runs on the pool's threads and the calling thread; must not throw. The third register is passed on unchanged
// (the game's item does not read it).
void ItemDetour(void* self, std::int32_t* index, void* unused) {
    if (g_active != 0) Prefetch(self, *index);
    g_item(self, index, unused);
}

// The call of the row's ParallelFor3 in UpdateOpinionCache (on the thread that updates the row; g_active and the
// arm assume one row at a time, and a wrong arm would only count a row's time on the other side).
void FillDetour(const void* array, const void* lambda) {
    sb::Stats& s = EU4SB_OpinionPrefetchStats;
    const LONG64 row = InterlockedIncrement64(&s.requests);
    const bool prefetch = !g_verifyOnly || op::CheckedRowPrefetches(static_cast<std::uint64_t>(row));
    InterlockedIncrement64(prefetch ? &s.hits : &s.skips);
    InterlockedExchangeAdd64(&g_cells, static_cast<LONG64>(op::Countries(array).count));
    const LONG before = InterlockedExchange(&g_active, prefetch ? 1 : 0);
    const LONG64 start = sb::Ticks();
    g_gameFill(array, lambda);
    const LONG64 ticks = sb::Ticks() - start;
    InterlockedExchange(&g_active, before);
    InterlockedExchangeAdd64(&g_ticks[prefetch ? 1 : 0], ticks);
    InterlockedIncrement64(&g_calls[prefetch ? 1 : 0]);
    if (g_verifyOnly) InterlockedIncrement64(&s.verifications);
}

void WriteFile() {
    const sb::Stats& s = EU4SB_OpinionPrefetchStats;
    try {
        const double ms = sb::TicksToMs(1);
        std::ostringstream out;
        out << "mode\trows\tprefetch_rows\tprefetch_ms\tplain_rows\tplain_ms\tcells\tchecked\tmismatches\n"
            << (g_verifyOnly ? "verify" : "on") << "\t" << s.requests << "\t" << g_calls[1] << "\t" << g_ticks[1] * ms
            << "\t" << g_calls[0] << "\t" << g_ticks[0] * ms << "\t" << g_cells << "\t" << s.verifications << "\t"
            << s.mismatches << "\n";
        sb::ReplaceFileWith(*g_dir + L"\\opinion_prefetch.tsv", out.str());
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    LONG64 written = -1;
    for (;;) {
        Sleep(kWriteEveryMs);
        if (EU4SB_OpinionPrefetchStats.requests != written) {
            written = EU4SB_OpinionPrefetchStats.requests;
            WriteFile();
        }
    }
}

}  // namespace

namespace sb {

bool InstallOpinionPrefetch(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyOnly = mode == Mode::Verify;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    g_arrayAt = static_cast<std::size_t>(host.value(sym::opinion_item, "array_offset"));
    g_countryAt = static_cast<std::size_t>(host.value(sym::opinion_item, "country_offset"));
    g_owned = static_cast<std::size_t>(host.value(sym::opinion_item, "owned_offset"));
    g_tag = static_cast<std::size_t>(host.value(sym::opinion_item, "tag_offset"));
    g_kind = static_cast<std::size_t>(host.value(sym::opinion_item, "kind_offset"));
    g_kindValue = static_cast<std::uint8_t>(host.value(sym::opinion_item, "kind_value"));
    g_rows = static_cast<std::size_t>(host.value(sym::opinion_status_read, "rows_offset"));
    g_religion = static_cast<std::size_t>(host.value(sym::opinion_religion_read, "religion_offset"));
    g_allies = static_cast<std::size_t>(host.value(sym::opinion_allies_read, "allies_offset"));
    g_rivals = static_cast<std::size_t>(host.value(sym::opinion_rivals_read, "rivals_offset"));
    Undo undo;
    host.hook(g_itemHook, sym::opinion_item, &ItemDetour);
    undo.add(g_itemHook);
    g_item = g_itemHook.next_pointer();
    host.redirect(g_fillSite, sym::opinion_fill_call, &FillDetour);
    undo.add(g_fillSite);
    g_gameFill = g_fillSite.next_pointer();
    undo.keep();
    InterlockedExchange(&EU4SB_OpinionPrefetchStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_OpinionPrefetchStats.initialized, 1);
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows opinion_prefetch writer");
        CloseHandle(thread);
    }
    Log(ANVIL_LOG_INFO, "opinion_prefetch: %s the other country's memory in the AI's opinion row",
        g_verifyOnly ? "timing rows with and without prefetching" : "prefetching");
    return true;
}

}  // namespace sb
