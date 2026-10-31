// Diagnostic (off by default): where the monthly tick spends its time, and whether its parts could
// run on several threads (lab docs: docs/simulation-profile.md, "Tick miesięczny").
//
// The monthly tick 0x75D690 runs, among other stages, the monthly update of every country (0x2F36C0,
// one call per country in a serial loop), of every province (0x9F35C0) and of the units (0x763470).
// Inside the country update, 0x350B80 rebuilds the country's table of the nearest own port for every
// province of the map. Across the whole game, 0x8EA420 adds one modifier list to another (shapes in
// monthly_probe_core.hpp). The game's Mersenne Twister 0x14C71B0 is hooked too: random draws inside a
// country, province or unit update tie the result to the order of the updates, so they decide
// whether such a loop could run in parallel and still give the same game.
//
// All hooks only observe (inline, through Anvil): they call the original and count. Nothing the game
// computes is changed. The game is usually closed without a clean exit, so a background thread
// writes the counters every 5 s to monthly_probe*.tsv in the data directory.
#include "modules.hpp"
#include "monthly_probe_core.hpp"

#include <intrin.h>

#include <fstream>
#include <string>

extern "C" {
// requests = monthly ticks, hits = modifier merges, mode = 1 on.
__declspec(dllexport) sb::Stats EU4SB_MonthlyProbeStats = SB_STATS_INIT;
}

namespace {

using MonthlyFn = void (*)(void* state, std::uint8_t a2);
using CountryMonthFn = void (*)(void* country, std::uint8_t a2, std::uint8_t a3);
using ProvinceMonthFn = void (*)(void* province);
using UnitsMonthFn = void (*)(void* state, std::uint8_t a2);
using PortTableFn = void (*)(void* country);
using MergeFn = void* (*)(void* dest, void* src);
using RandomFn = std::uint32_t (*)(void* twister);

constexpr DWORD kWriteEveryMs = 5000;
constexpr std::size_t kCountryId = 0x20;  // tag index in bits 32-47
constexpr std::size_t kCountryProvinces = 0x1344;
constexpr int kCountries = 4096;

anvil::HookOf<sym::monthly_tick> g_monthlyHook;
anvil::HookOf<sym::country_month> g_countryHook;
anvil::HookOf<sym::province_month> g_provinceHook;
anvil::HookOf<sym::units_month> g_unitsHook;
anvil::HookOf<sym::port_table> g_portHook;
anvil::HookOf<sym::modifier_merge> g_mergeHook;
anvil::HookOf<sym::game_random> g_randomHook;
MonthlyFn g_monthly = nullptr;  // the rest of each hook's chain
CountryMonthFn g_country = nullptr;
ProvinceMonthFn g_province = nullptr;
UnitsMonthFn g_units = nullptr;
PortTableFn g_port = nullptr;
MergeFn g_merge = nullptr;
RandomFn g_random = nullptr;
std::uintptr_t g_imageBase = 0;
volatile LONG g_dirty = 0;
std::wstring* g_dir = nullptr;

// Where a thread is: random draws and merges are counted per context.
enum Context : int { kElsewhere, kMonthly, kCountry, kProvince, kUnits, kContexts };
const char* const kContextNames[kContexts] = {"elsewhere", "monthly_other", "country_month", "province_month",
                                              "units_month"};
thread_local int t_context = kElsewhere;
thread_local LONG64 t_draws = 0;  // random draws on this thread, for the update in progress

struct Span {
    volatile LONG64 calls = 0, ticks = 0, max = 0;

    void Add(LONG64 t) noexcept {
        InterlockedIncrement64(&calls);
        InterlockedExchangeAdd64(&ticks, t);
        LONG64 seen = max;
        while (t > seen) {
            const LONG64 prev = InterlockedCompareExchange64(&max, t, seen);
            if (prev == seen) break;
            seen = prev;
        }
    }
};

Span g_monthSpan, g_countrySpan, g_provinceSpan, g_unitsSpan, g_portSpan;
volatile LONG64 g_provincesWithDraws = 0, g_countriesWithDraws = 0;

// Per country (tag index): monthly update, port table, random draws inside the update.
volatile LONG64 g_cCalls[kCountries], g_cTicks[kCountries], g_cDraws[kCountries], g_cPortTicks[kCountries],
    g_cProvinces[kCountries];

// Random draws per context and per twister object (RVA of the object, 8 slots).
constexpr int kTwisters = 8;
volatile LONG64 g_draws[kContexts];
volatile LONG g_twisterRva[kTwisters];
volatile LONG64 g_twisterDraws[kTwisters][kContexts];

// Modifier merges: per context and destination/source density, and per call site (return address).
struct MergeCell {
    volatile LONG64 calls = 0, ticks = 0, src = 0, dest = 0, scanned = 0, steps = 0;

    void Add(const sb::MergeShape& s, LONG64 t) noexcept {
        InterlockedIncrement64(&calls);
        InterlockedExchangeAdd64(&ticks, t);
        InterlockedExchangeAdd64(&src, s.src_count);
        InterlockedExchangeAdd64(&dest, s.dest_count);
        InterlockedExchangeAdd64(&scanned, s.scanned);
        InterlockedExchangeAdd64(&steps, sb::ScanSteps(s));
    }
};
MergeCell g_mergeByKind[kContexts][4];
MergeCell g_mergeBySteps[16];  // log2 bucket of the scan steps
constexpr int kSites = 512;
volatile LONG g_siteRva[kSites];
MergeCell g_mergeBySite[kSites];
volatile LONG64 g_siteOverflow = 0;

int CountryIndex(const void* country) noexcept {
    const auto raw = sb::Read<std::uint64_t>(country, kCountryId);
    const auto index = static_cast<std::int16_t>(raw >> 32);
    return index >= 0 && index < kCountries ? index : 0;
}

void Mark() noexcept { InterlockedExchange(&g_dirty, 1); }

template <class F> void InContext(int context, F&& call) {
    const int saved = t_context;
    t_context = context;
    call();
    t_context = saved;
}

void MonthlyDetour(void* state, std::uint8_t a2) {
    const LONG64 start = sb::Ticks();
    InContext(kMonthly, [&] { g_monthly(state, a2); });
    g_monthSpan.Add(sb::Ticks() - start);
    InterlockedIncrement64(&EU4SB_MonthlyProbeStats.requests);
    Mark();
}

void CountryDetour(void* country, std::uint8_t a2, std::uint8_t a3) {
    const LONG64 drawsBefore = t_draws;
    const LONG64 start = sb::Ticks();
    InContext(kCountry, [&] { g_country(country, a2, a3); });
    const LONG64 ticks = sb::Ticks() - start;
    const LONG64 draws = t_draws - drawsBefore;
    g_countrySpan.Add(ticks);
    const int c = CountryIndex(country);
    InterlockedIncrement64(&g_cCalls[c]);
    InterlockedExchangeAdd64(&g_cTicks[c], ticks);
    InterlockedExchangeAdd64(&g_cDraws[c], draws);
    if (draws != 0) InterlockedIncrement64(&g_countriesWithDraws);
}

void ProvinceDetour(void* province) {
    const LONG64 drawsBefore = t_draws;
    const LONG64 start = sb::Ticks();
    InContext(kProvince, [&] { g_province(province); });
    g_provinceSpan.Add(sb::Ticks() - start);
    if (t_draws != drawsBefore) InterlockedIncrement64(&g_provincesWithDraws);
}

void UnitsDetour(void* state, std::uint8_t a2) {
    const LONG64 start = sb::Ticks();
    InContext(kUnits, [&] { g_units(state, a2); });
    g_unitsSpan.Add(sb::Ticks() - start);
}

void PortDetour(void* country) {
    const LONG64 start = sb::Ticks();
    g_port(country);
    const LONG64 ticks = sb::Ticks() - start;
    g_portSpan.Add(ticks);
    const int c = CountryIndex(country);
    InterlockedExchangeAdd64(&g_cPortTicks[c], ticks);
    InterlockedExchange64(&g_cProvinces[c], sb::Read<std::int32_t>(country, kCountryProvinces));
}

MergeCell* SiteCell(std::uint32_t rva) noexcept {
    std::uint32_t slot = (rva * 2654435761u) % kSites;
    for (int probe = 0; probe < kSites; ++probe, slot = (slot + 1) % kSites) {
        const LONG key = g_siteRva[slot];
        if (key == static_cast<LONG>(rva)) return &g_mergeBySite[slot];
        if (key == 0) {
            const LONG prev = InterlockedCompareExchange(&g_siteRva[slot], static_cast<LONG>(rva), 0);
            if (prev == 0 || prev == static_cast<LONG>(rva)) return &g_mergeBySite[slot];
        }
    }
    return nullptr;
}

void* MergeDetour(void* dest, void* src) {
    const auto site = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - g_imageBase);
    const auto* entries = sb::Read<const std::int32_t*>(src, sb::kModEntries);
    const auto count = sb::Read<std::int32_t>(src, sb::kModCount);
    const sb::MergeShape shape = sb::ShapeOfMerge(sb::Read<std::uint8_t>(dest, sb::kModDense) != 0,
                                                  sb::Read<std::int32_t>(dest, sb::kModCount),
                                                  sb::Read<std::uint8_t>(src, sb::kModDense) != 0,
                                                  entries, entries != nullptr ? count : 0);
    const LONG64 start = sb::Ticks();
    void* result = g_merge(dest, src);
    const LONG64 ticks = sb::Ticks() - start;
    g_mergeByKind[t_context][(shape.dest_dense ? 2 : 0) + (shape.src_dense ? 1 : 0)].Add(shape, ticks);
    g_mergeBySteps[sb::Bucket(sb::ScanSteps(shape))].Add(shape, ticks);
    if (MergeCell* cell = SiteCell(site)) {
        cell->Add(shape, ticks);
    } else {
        InterlockedIncrement64(&g_siteOverflow);
    }
    InterlockedIncrement64(&EU4SB_MonthlyProbeStats.hits);
    return result;
}

std::uint32_t RandomDetour(void* twister) {
    ++t_draws;
    InterlockedIncrement64(&g_draws[t_context]);
    const auto rva = static_cast<LONG>(reinterpret_cast<std::uintptr_t>(twister) - g_imageBase);
    for (int i = 0; i < kTwisters; ++i) {
        LONG key = g_twisterRva[i];
        if (key == 0) {
            const LONG prev = InterlockedCompareExchange(&g_twisterRva[i], rva, 0);
            key = prev == 0 ? rva : prev;
        }
        if (key == rva) {
            InterlockedIncrement64(&g_twisterDraws[i][t_context]);
            break;
        }
    }
    return g_random(twister);
}

void WriteSpan(std::ostream& out, const char* name, const Span& s, double msPerTick) {
    out << name << "\t" << s.calls << "\t" << s.ticks * msPerTick << "\t" << s.max * msPerTick << "\n";
}

void WriteMerge(std::ostream& out, const std::string& key, const MergeCell& m, double msPerTick) {
    out << key << "\t" << m.calls << "\t" << m.ticks * msPerTick << "\t" << m.src << "\t" << m.dest << "\t"
        << m.scanned << "\t" << m.steps << "\n";
}

void Replace(const std::wstring& name, void (*write)(std::ostream&, double)) {
    const std::wstring path = *g_dir + L"\\" + name;
    const std::wstring temp = path + L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        write(out, sb::TicksToMs(1));
    }
    MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

void WriteFiles() {
    try {
        Replace(L"monthly_probe.tsv", [](std::ostream& out, double ms) {
            out << "part\tcalls\ttotal_ms\tmax_ms\n";
            WriteSpan(out, "monthly_tick", g_monthSpan, ms);
            WriteSpan(out, "country_month", g_countrySpan, ms);
            WriteSpan(out, "province_month", g_provinceSpan, ms);
            WriteSpan(out, "units_month", g_unitsSpan, ms);
            WriteSpan(out, "port_table", g_portSpan, ms);
            out << "countries_with_draws\t" << g_countriesWithDraws << "\t\t\n";
            out << "provinces_with_draws\t" << g_provincesWithDraws << "\t\t\n";
            for (int c = 0; c < kContexts; ++c) out << "draws_" << kContextNames[c] << "\t" << g_draws[c] << "\t\t\n";
            for (int i = 0; i < kTwisters; ++i) {
                if (g_twisterRva[i] == 0) continue;
                for (int c = 0; c < kContexts; ++c) {
                    char key[64];
                    std::snprintf(key, sizeof key, "twister_0x%X_%s", static_cast<unsigned>(g_twisterRva[i]),
                                  kContextNames[c]);
                    out << key << "\t" << g_twisterDraws[i][c] << "\t\t\n";
                }
            }
        });
        Replace(L"monthly_probe_countries.tsv", [](std::ostream& out, double ms) {
            out << "country\tcalls\ttotal_ms\tdraws\tport_ms\tprovinces\n";
            for (int c = 0; c < kCountries; ++c) {
                if (g_cCalls[c] == 0 && g_cPortTicks[c] == 0) continue;
                out << c << "\t" << g_cCalls[c] << "\t" << g_cTicks[c] * ms << "\t" << g_cDraws[c] << "\t"
                    << g_cPortTicks[c] * ms << "\t" << g_cProvinces[c] << "\n";
            }
        });
        Replace(L"monthly_probe_merges.tsv", [](std::ostream& out, double ms) {
            out << "key\tcalls\ttotal_ms\tsrc_entries\tdest_entries\tscanned\tscan_steps\n";
            const char* const kinds[4] = {"sparse<sparse", "sparse<dense", "dense<sparse", "dense<dense"};
            for (int c = 0; c < kContexts; ++c) {
                for (int k = 0; k < 4; ++k) {
                    if (g_mergeByKind[c][k].calls != 0)
                        WriteMerge(out, std::string("kind ") + kContextNames[c] + " " + kinds[k], g_mergeByKind[c][k], ms);
                }
            }
            for (int b = 0; b < 16; ++b) {
                if (g_mergeBySteps[b].calls != 0) WriteMerge(out, "steps_log2 " + std::to_string(b), g_mergeBySteps[b], ms);
            }
            for (int s = 0; s < kSites; ++s) {
                if (g_siteRva[s] == 0) continue;
                char key[32];
                std::snprintf(key, sizeof key, "site 0x%X", static_cast<unsigned>(g_siteRva[s]));
                WriteMerge(out, key, g_mergeBySite[s], ms);
            }
            out << "site_overflow\t" << g_siteOverflow << "\t\t\t\t\t\n";
        });
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    for (;;) {
        Sleep(kWriteEveryMs);
        if (InterlockedExchange(&g_dirty, 0) != 0) WriteFiles();
    }
}

}  // namespace

namespace sb {

bool InstallMonthlyProbe(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    g_imageBase = host.game().image_base;
    Undo undo;
    host.hook(g_monthlyHook, sym::monthly_tick, &MonthlyDetour);
    undo.add(g_monthlyHook);
    host.hook(g_countryHook, sym::country_month, &CountryDetour);
    undo.add(g_countryHook);
    host.hook(g_provinceHook, sym::province_month, &ProvinceDetour);
    undo.add(g_provinceHook);
    host.hook(g_unitsHook, sym::units_month, &UnitsDetour);
    undo.add(g_unitsHook);
    host.hook(g_portHook, sym::port_table, &PortDetour);
    undo.add(g_portHook);
    host.hook(g_mergeHook, sym::modifier_merge, &MergeDetour);
    undo.add(g_mergeHook);
    host.hook(g_randomHook, sym::game_random, &RandomDetour);
    undo.add(g_randomHook);
    g_monthly = g_monthlyHook.next_pointer();
    g_country = g_countryHook.next_pointer();
    g_province = g_provinceHook.next_pointer();
    g_units = g_unitsHook.next_pointer();
    g_port = g_portHook.next_pointer();
    g_merge = g_mergeHook.next_pointer();
    g_random = g_randomHook.next_pointer();
    undo.keep();
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows monthly_probe writer");
        CloseHandle(thread);
    }
    InterlockedExchange(&EU4SB_MonthlyProbeStats.mode, 1);
    InterlockedExchange(&EU4SB_MonthlyProbeStats.initialized, 1);
    Log(ANVIL_LOG_INFO, "monthly_probe: observing the monthly tick, modifier merges and random draws; counters "
        "in monthly_probe*.tsv every %u s", kWriteEveryMs / 1000);
    return true;
}

}  // namespace sb
