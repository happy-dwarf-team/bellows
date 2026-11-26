// loc_index: storing a localisation key that is defined again without walking the table's whole load-order array
// (the reasoning in loc_index_core.hpp, bellows#270).
//
// A mid hook at the start of the walk in 0x16FAAE0 answers it from sb::loc::Index and jumps to where the game
// goes with its answer: the found entry (rdx = its index) or the append (rdx = the count). What the game does there
// does not change. "verify" lets the game walk and walks the array as well, comparing the answers; "on" does that
// for every 1024th key and goes back to the game's walk for good after a mismatch. The indexes are dropped after
// the game is ready (a later load builds them again).
#include "loc_index_core.hpp"
#include "modules.hpp"

#include <unordered_map>

extern "C" {
// requests = walks reached, hits = walks answered by the index, misses = of those, keys not in the array (the
// game appends), verifications = walks compared, mismatches = different answers, skips = index restarts (a table
// cleared or rebuilt), cacheDisabled = 1 after a mismatch or a failure, mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_LocIndexStats = SB_STATS_INIT;
}

namespace {

anvil::Change g_walk;
std::uintptr_t g_found = 0, g_notFound = 0;
bool g_verifyOnly = false;
volatile LONG64 g_sequence = 0;
SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_map<std::uintptr_t, sb::loc::Index>* g_indexes = nullptr;  // by the table's load-order array

// r9 = the load-order array (begin, end), rsi = the key's hash, r11d = the third int argument; the hooked block
// loads r10 = begin, zeroes edx and computes r8 = the count, which a jump past it sets as the block would.
void AtWalk(anvil::Cpu& cpu) noexcept {
    sb::Stats& s = EU4SB_LocIndexStats;
    InterlockedIncrement64(&s.requests);
    if (s.cacheDisabled != 0) return;
    const auto* array = reinterpret_cast<const unsigned char* const*>(cpu.r9);
    const unsigned char* begin = array[0];
    const std::size_t count = static_cast<std::size_t>(array[1] - begin) / sb::loc::kEntrySize;
    const sb::loc::Key key{cpu.rsi, static_cast<std::uint32_t>(cpu.r11)};
    std::size_t index = count;
    try {
        AcquireSRWLockExclusive(&g_lock);
        sb::loc::Index& idx = (*g_indexes)[cpu.r9];
        const std::uint64_t resets = idx.resets();
        index = idx.Find(begin, count, key);
        if (idx.resets() != resets) InterlockedIncrement64(&s.skips);
        ReleaseSRWLockExclusive(&g_lock);
    } catch (...) {
        ReleaseSRWLockExclusive(&g_lock);
        if (InterlockedExchange(&s.cacheDisabled, 1) == 0)
            sb::Log(ANVIL_LOG_WARNING, "loc_index: out of memory for the index; back to the game's walk");
        return;
    }
    if (g_verifyOnly || (InterlockedIncrement64(&g_sequence) & 1023) == 0) {
        const std::size_t walked = sb::loc::Scan(begin, count, key);
        InterlockedIncrement64(&s.verifications);
        if (walked != index) {
            InterlockedIncrement64(&s.mismatches);
            if (!g_verifyOnly && InterlockedExchange(&s.cacheDisabled, 1) == 0)
                sb::Log(ANVIL_LOG_WARNING, "loc_index: the index answered %zu, the walk %zu of %zu; back to the game's walk",
                        index, walked, count);
            return;
        }
        if (g_verifyOnly) return;
    }
    InterlockedIncrement64(&s.hits);
    if (index == count) InterlockedIncrement64(&s.misses);
    cpu.r10 = reinterpret_cast<std::uintptr_t>(begin);
    cpu.r8 = count;
    cpu.rdx = index;
    cpu.rip = index < count ? g_found : g_notFound;
}

}  // namespace

namespace sb {

bool InstallLocIndex(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyOnly = mode == Mode::Verify;
    try {
        g_indexes = new std::unordered_map<std::uintptr_t, loc::Index>();
    } catch (...) {
        return false;
    }
    g_found = reinterpret_cast<std::uintptr_t>(host.address(sym::loc_index_found));
    g_notFound = reinterpret_cast<std::uintptr_t>(host.address(sym::loc_index_not_found));
    Undo undo;
    g_walk = host.mid_hook(sym::loc_index_walk, &AtWalk);
    undo.add(g_walk);
    undo.keep();
    InterlockedExchange(&EU4SB_LocIndexStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_LocIndexStats.initialized, 1);
    Log(ANVIL_LOG_INFO, "loc_index: %s the walk over a localisation table for a key defined again",
        g_verifyOnly ? "verifying" : "indexing");
    return true;
}

// After the game is ready: the counters, and the indexes' memory back (a later load builds them again).
void ReportLocIndex() {
    if (g_indexes == nullptr) return;
    std::size_t tables = 0, entries = 0, bytes = 0;
    AcquireSRWLockExclusive(&g_lock);
    for (auto& [array, idx] : *g_indexes) {
        ++tables;
        entries += idx.seen();
        bytes += idx.bytes();
    }
    g_indexes->clear();
    ReleaseSRWLockExclusive(&g_lock);
    Log(ANVIL_LOG_INFO, "loc_index: %zu table(s), %zu entries indexed, %zu KiB freed", tables, entries, bytes / 1024);
    LogStats("loc_index", EU4SB_LocIndexStats);
}

}  // namespace sb
