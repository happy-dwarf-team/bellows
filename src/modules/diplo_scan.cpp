// diplo_scan (prototype, off by default): skips the daily diplomacy-change scan of a country none of whose
// diplomacy rows changed.
//
// The daily diplomacy-change check of a country (0x2EE070) runs for every living country every day, in the
// serial country update. It reads the change bit (bit 2 of +0x46) of every 72-byte relation row of the
// country, one row per tag (about 1,900 on Anbennar, so about 134 KB a country), and clears the set ones while
// no save is being loaded; with a save loader present it refreshes the trade routes instead. Nearly every day
// it finds nothing: the time goes to reading memory.
//
// Every code path of eu4.exe that sets the bit also sets the dirty byte +0x2884 of the row's owner: the inlined
// "set changed" of the setters of a relation row and of a country's diplomacy, of the change of a ruler and the
// end of old royal marriages, and the move of relations from another country, which copies a row's flags and
// then sets both. A new country starts with the byte set. The only code that reads the byte, and the only
// code that clears it, is the diplomacy view's update (0xF01DC0), for the country it shows.
//
// So while no save is loading: a country whose byte is 0 has had no bit set since the module last cleared its
// byte, which it does only right after the game's scan of that country cleared every bit; the module skips its
// scan. A country whose byte is set gets the game's scan; then the module clears the byte and holds it, and
// gives it back right before the diplomacy view's update reads it for that country. The view reads the value
// the game would have, and the rows are the game's.
//
// "verify" checks every skipped country: it counts the set bits (a difference when any is set, after which the
// game's scan runs), then times the game's scan of it as well. "on" checks every 256th skipped country and, after
// a difference, gives back every held byte and leaves every scan to the game. A background thread writes the
// counters every 5 s to diplo_scan.tsv in the data directory.
#include "modules.hpp"
#include "diplo_scan_core.hpp"
#include "../file_write.hpp"

#include <sstream>
#include <string>

extern "C" {
// requests = scans, hits = scans skipped, misses = scans run because the country's byte was set, skips = scans
// left to the game (a save loading, no rows, or after a difference), verifications = skipped scans checked,
// mismatches = checked ones with a set bit, cacheDisabled = 1 after a difference in "on", mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_DiploScanStats = SB_STATS_INIT;
}

namespace {

namespace ds = sb::diplo_scan;

anvil::FunctionHook<void(void*)> g_scanHook;
anvil::FunctionHook<void(void*)> g_viewHook;
void (*g_scan)(void*) = nullptr;
void (*g_view)(void*) = nullptr;
const unsigned char* const* g_state = nullptr;        // the current game state's instance pointer
const unsigned char* const* g_countryRoot = nullptr;  // the object whose +0x118 is the country array
std::size_t g_rowsBegin = 0, g_rowsEnd = 0, g_rowFlags = 0, g_loader = 0;
std::size_t g_viewCountry = 0, g_countryList = 0, g_dirty = 0;
unsigned char g_changedMask = 0;
bool g_verifyOnly = false;
volatile LONG g_disabled = 0;
ds::Shadow* g_shadow = nullptr;  // main thread
// The countries the diplomacy view updated since their last scan: the view clears their byte without clearing
// their rows' bits, so their next scan is the game's.
ds::Shadow* g_viewed = nullptr;  // main thread
// A session event or a scan during a load starts a new epoch; the first scan of a country in an epoch is the
// game's, whatever its byte, in case a load read set bits into a country it did not build anew.
volatile LONG g_epoch = 1;
ds::EpochMarks* g_scanned = nullptr;  // main thread
std::wstring* g_dir = nullptr;
constexpr DWORD kWriteEveryMs = 5000;
constexpr LONG64 kCheckEvery = 256;

volatile LONG64 g_skipCount = 0, g_rows = 0, g_scanTicks = 0, g_checkTicks = 0, g_skipGameTicks = 0,
                g_given = 0, g_viewCalls = 0;

unsigned char* CountryAt(int index) noexcept {
    const unsigned char* root = g_countryRoot != nullptr ? *g_countryRoot : nullptr;
    if (root == nullptr || index < 0) return nullptr;
    const auto* list = sb::Read<unsigned char* const*>(root, g_countryList);
    return list != nullptr ? list[index] : nullptr;
}

void GiveBack(int index) noexcept {
    if (unsigned char* country = CountryAt(index)) {
        country[g_dirty] = 1;
        InterlockedIncrement64(&g_given);
    }
}

void Disable() {
    InterlockedExchange(&g_disabled, 1);
    InterlockedExchange(&EU4SB_DiploScanStats.cacheDisabled, 1);
    g_shadow->TakeAll(GiveBack);
}

void ScanDetour(void* country) {
    sb::Stats& s = EU4SB_DiploScanStats;
    InterlockedIncrement64(&s.requests);
    auto* c = static_cast<unsigned char*>(country);
    const unsigned char* state = *g_state;
    const auto* begin = sb::Read<const unsigned char*>(c, g_rowsBegin);
    const auto* end = sb::Read<const unsigned char*>(c, g_rowsEnd);
    const std::size_t rows = begin != nullptr && end > begin ? static_cast<std::size_t>(end - begin) / ds::kRowSize : 0;
    const bool loading = state == nullptr || sb::Read<const void*>(state, g_loader) != nullptr;
    if (loading) InterlockedIncrement(&g_epoch);
    if (g_disabled != 0 || loading || rows == 0) {
        InterlockedIncrement64(&s.skips);
        g_scan(country);
        return;
    }
    InterlockedExchangeAdd64(&g_rows, static_cast<LONG64>(rows));
    const int index = ds::TagIndex(sb::Read<std::uint64_t>(begin, ds::kRowOwner));
    const bool firstOfEpoch = g_scanned->Mark(index, static_cast<std::uint32_t>(g_epoch));
    const bool viewed = g_viewed->Take(index);
    if (c[g_dirty] != 0 || firstOfEpoch || viewed) {
        InterlockedIncrement64(&s.misses);
        {
            const sb::ScopedTicks t(&g_scanTicks);
            g_scan(country);  // clears every set bit: no loader
        }
        if (c[g_dirty] != 0) {
            c[g_dirty] = 0;
            g_shadow->Hold(index);
        }
        return;
    }
    // No bit was set since the game's scan of this country cleared them all.
    InterlockedIncrement64(&s.hits);
    if (!g_verifyOnly && InterlockedIncrement64(&g_skipCount) % kCheckEvery != 0) return;
    InterlockedIncrement64(&s.verifications);
    std::size_t changed = 0;
    {
        const sb::ScopedTicks t(&g_checkTicks);
        changed = ds::ChangedRows(begin, rows, g_rowFlags, g_changedMask);
    }
    if (changed != 0) {
        InterlockedIncrement64(&s.mismatches);
        sb::Log(ANVIL_LOG_WARNING, "diplo_scan: %zu changed rows in a country whose diplomacy byte was clear%s",
                changed, g_verifyOnly ? "" : "; every scan is the game's from now on");
        if (!g_verifyOnly) Disable();
        g_scan(country);
        return;
    }
    if (g_verifyOnly) {
        const sb::ScopedTicks t(&g_skipGameTicks);
        g_scan(country);  // finds nothing: the time the module saves
    }
}

void ViewDetour(void* view) {
    InterlockedIncrement64(&g_viewCalls);
    const int index = ds::TagIndex(sb::Read<std::uint64_t>(view, g_viewCountry));
    if (g_shadow->Take(index)) GiveBack(index);
    g_view(view);
    // The update may have cleared the byte of a country whose rows still have bits set.
    g_viewed->Hold(index);
}

void WriteFile() {
    const sb::Stats& s = EU4SB_DiploScanStats;
    try {
        const double ms = sb::TicksToMs(1);
        std::ostringstream out;
        out << "mode\tscans\tskipped\tscanned\tto_game\tchecked\tmismatches\tdisabled\trows\tscanned_ms\tcheck_ms"
               "\tskipped_game_ms\tgiven_back\tview_calls\n"
            << (g_verifyOnly ? "verify" : "on") << "\t" << s.requests << "\t" << s.hits << "\t" << s.misses << "\t"
            << s.skips << "\t" << s.verifications << "\t" << s.mismatches << "\t" << s.cacheDisabled << "\t" << g_rows
            << "\t" << g_scanTicks * ms << "\t" << g_checkTicks * ms << "\t" << g_skipGameTicks * ms << "\t" << g_given
            << "\t" << g_viewCalls << "\n";
        sb::ReplaceFileWith(*g_dir + L"\\diplo_scan.tsv", out.str());
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    LONG64 written = -1;
    for (;;) {
        Sleep(kWriteEveryMs);
        if (EU4SB_DiploScanStats.requests != written) {
            written = EU4SB_DiploScanStats.requests;
            WriteFile();
        }
    }
}

}  // namespace

namespace sb {

void NoteDiploScanSession() noexcept { InterlockedIncrement(&g_epoch); }

bool InstallDiploScan(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyOnly = mode == Mode::Verify;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
        g_shadow = new ds::Shadow();
        g_viewed = new ds::Shadow();
        g_scanned = new ds::EpochMarks();
    } catch (...) {
        return false;
    }
    g_state = host.data(sym::diplo_game_state);
    g_countryRoot = host.data(sym::diplo_country_root);
    g_rowsBegin = static_cast<std::size_t>(host.value(sym::diplo_scan, "rows_begin_offset"));
    g_rowsEnd = static_cast<std::size_t>(host.value(sym::diplo_scan, "rows_end_offset"));
    g_rowFlags = static_cast<std::size_t>(host.value(sym::diplo_scan, "row_flags_offset"));
    g_changedMask = static_cast<unsigned char>(host.value(sym::diplo_scan, "row_changed_mask"));
    g_loader = static_cast<std::size_t>(host.value(sym::diplo_scan, "loader_offset"));
    g_viewCountry = static_cast<std::size_t>(host.value(sym::diplo_view_update, "view_country_offset"));
    g_countryList = static_cast<std::size_t>(host.value(sym::diplo_view_update, "country_list_offset"));
    g_dirty = static_cast<std::size_t>(host.value(sym::diplo_view_update, "diplo_dirty_offset"));
    Undo undo;
    host.hook(g_viewHook, sym::diplo_view_update, &ViewDetour);
    undo.add(g_viewHook);
    g_view = g_viewHook.next_pointer();
    host.hook(g_scanHook, sym::diplo_scan, &ScanDetour);
    undo.add(g_scanHook);
    g_scan = g_scanHook.next_pointer();
    undo.keep();
    InterlockedExchange(&EU4SB_DiploScanStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_DiploScanStats.initialized, 1);
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows diplo_scan writer");
        CloseHandle(thread);
    }
    Log(ANVIL_LOG_INFO, "diplo_scan: %s the daily diplomacy-change scan of countries with no changed row",
        g_verifyOnly ? "checking" : "skipping");
    return true;
}

}  // namespace sb
