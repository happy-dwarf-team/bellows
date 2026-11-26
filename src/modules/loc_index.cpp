// loc_index: storing a localisation key that is defined again without walking the table's whole load-order array
// (the reasoning in loc_index_core.hpp, bellows#270).
//
// A mid hook at the start of the walk in 0x16FAAE0 answers it from sb::loc::Index and jumps to where the game
// goes with its answer: the found entry (rdx = its index) or the append (rdx = the count). What the game does there
// does not change. "verify" lets the game walk and walks the array as well, comparing the answers; "on" does that
// for every 1024th key and goes back to the game's walk for good after a mismatch. The indexes are dropped after
// the game is ready (a later load builds them again).
//
// A second mid hook, inside the hash-collision report 0x16FB1C0, answers its walk for one run of equal hashes
// from sb::loc::HashOrder: it writes the three pointers of each entry the walk would take into the report's
// arrays, as the walk does, sets the count (r11d) and jumps past the walk. "verify" and "on" compare as above,
// "on" every 64th run.
#include "loc_index_core.hpp"
#include "modules.hpp"

#include <unordered_map>

extern "C" {
// requests = walks reached, hits = walks answered by the index, misses = of those, keys not in the array (the
// game appends), verifications = walks compared, mismatches = different answers, skips = index restarts (a table
// cleared or rebuilt), cacheDisabled = 1 after a mismatch or a failure, mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_LocIndexStats = SB_STATS_INIT;
// The collision report's walks: requests = runs reached, hits = answered, verifications, mismatches as above,
// skips = sorted builds, duplicateKeys = entries copied.
__declspec(dllexport) sb::Stats EU4SB_LocCollisionStats = SB_STATS_INIT;
}

namespace {

anvil::Change g_walk;
std::uintptr_t g_found = 0, g_notFound = 0;
bool g_verifyOnly = false;
volatile LONG64 g_sequence = 0;
SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_map<std::uintptr_t, sb::loc::Index>* g_indexes = nullptr;  // by the table's load-order array
anvil::Change g_collisionWalk;
std::uintptr_t g_afterCollisionWalk = 0;
volatile LONG64 g_collisionSequence = 0;
sb::loc::HashOrder* g_order = nullptr;  // one table's report at a time
std::vector<std::uint32_t>* g_taken = nullptr;
std::vector<std::uint32_t>* g_walked = nullptr;

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

// One call of 0x16FB1C0 has increasing run ends in [rbp + 0x88]; a lower or equal one, or another frame, is the
// next call, which sorts the array again (it may have changed in between, at the same address and length).
std::uintptr_t g_lastFrame = 0;
std::int64_t g_lastRunEnd = -1;

// rsi = the table, r14 = the run's hash, r12 = the run's length; r13, r15 and rcx = the report's three pointer
// arrays; rbx = the object whose +0x20 is the base of the entries' offsets; [rbp] = the entry count. The walk
// past this point (rdi, r9, rdx, r8, r13 rewritten, rax and rcx scratch) is what the jump leaves out; after it
// only r11d (the entries taken) is read, the rest is reloaded from the frame.
void AtCollisionWalk(anvil::Cpu& cpu) noexcept {
    sb::Stats& s = EU4SB_LocCollisionStats;
    InterlockedIncrement64(&s.requests);
    if (s.cacheDisabled != 0) return;
    const auto* table = reinterpret_cast<const unsigned char*>(cpu.rsi);
    const unsigned char* begin = sb::Read<const unsigned char*>(table, 0x38);
    const std::size_t count = static_cast<std::size_t>(sb::Read<const unsigned char*>(table, 0x40) - begin) / sb::loc::kEntrySize;
    const std::uint64_t hash = cpu.r14;
    const std::size_t limit = cpu.r12;
    const std::int64_t runEnd = sb::Read<std::int32_t>(reinterpret_cast<const void*>(cpu.rbp), 0x88);
    try {
        AcquireSRWLockExclusive(&g_lock);
        if (cpu.rbp != g_lastFrame || runEnd <= g_lastRunEnd) g_order->Clear();
        g_lastFrame = cpu.rbp;
        g_lastRunEnd = runEnd;
        const std::uint64_t builds = g_order->builds();
        g_order->Sync(begin, count);
        if (g_order->builds() != builds) InterlockedIncrement64(&s.skips);
        g_order->Collect(hash, limit, *g_taken);
        ReleaseSRWLockExclusive(&g_lock);
    } catch (...) {
        ReleaseSRWLockExclusive(&g_lock);
        if (InterlockedExchange(&s.cacheDisabled, 1) == 0)
            sb::Log(ANVIL_LOG_WARNING, "loc_index: out of memory for the collision report; back to the game's walk");
        return;
    }
    const std::vector<std::uint32_t>& taken = *g_taken;  // only this thread reports: the game's main thread
    if (g_verifyOnly || (InterlockedIncrement64(&g_collisionSequence) & 63) == 0) {
        try {
            sb::loc::CollectScan(begin, count, hash, limit, *g_walked);
        } catch (...) {
            return;
        }
        InterlockedIncrement64(&s.verifications);
        if (*g_walked != taken) {
            InterlockedIncrement64(&s.mismatches);
            if (!g_verifyOnly && InterlockedExchange(&s.cacheDisabled, 1) == 0)
                sb::Log(ANVIL_LOG_WARNING, "loc_index: the collision report's walk took %zu entries, the index %zu; back to the game's walk",
                        g_walked->size(), taken.size());
            return;
        }
        if (g_verifyOnly) return;
    }
    const std::uintptr_t base = sb::Read<std::uintptr_t>(reinterpret_cast<const void*>(cpu.rbx), 0x20);
    const auto* files = sb::Read<const unsigned char*>(table, 0x50);
    auto* text = reinterpret_cast<std::uintptr_t*>(cpu.r13);
    auto* other = reinterpret_cast<std::uintptr_t*>(cpu.r15);
    auto* file = reinterpret_cast<std::uintptr_t*>(cpu.rcx);
    for (std::size_t k = 0; k < taken.size(); ++k) {
        const unsigned char* e = begin + static_cast<std::size_t>(taken[k]) * sb::loc::kEntrySize;
        text[k] = sb::Read<std::uint32_t>(e, 0x08) + base;
        other[k] = sb::Read<std::uint32_t>(e, 0x0C) + base;
        const std::uint32_t f = sb::Read<std::uint32_t>(e, 0x10);
        if (f == 0xFFFFFFFFu) {
            file[k] = 0;
        } else {
            const unsigned char* name = files + static_cast<std::size_t>(f) * 0x20;  // a string: buffer, size, capacity at +0x18
            file[k] = sb::Read<std::uint64_t>(name, 0x18) >= 0x10 ? sb::Read<std::uintptr_t>(name, 0) : reinterpret_cast<std::uintptr_t>(name);
        }
    }
    InterlockedIncrement64(&s.hits);
    InterlockedExchangeAdd64(&s.duplicateKeys, static_cast<LONG64>(taken.size()));
    cpu.r11 = taken.size();
    cpu.rip = g_afterCollisionWalk;
}

}  // namespace

namespace sb {

bool InstallLocIndex(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_verifyOnly = mode == Mode::Verify;
    try {
        g_indexes = new std::unordered_map<std::uintptr_t, loc::Index>();
        g_order = new loc::HashOrder();
        g_taken = new std::vector<std::uint32_t>();
        g_walked = new std::vector<std::uint32_t>();
    } catch (...) {
        return false;
    }
    g_found = reinterpret_cast<std::uintptr_t>(host.address(sym::loc_index_found));
    g_notFound = reinterpret_cast<std::uintptr_t>(host.address(sym::loc_index_not_found));
    g_afterCollisionWalk = reinterpret_cast<std::uintptr_t>(host.address(sym::loc_collision_after_walk));
    Undo undo;
    g_walk = host.mid_hook(sym::loc_index_walk, &AtWalk);
    undo.add(g_walk);
    g_collisionWalk = host.mid_hook(sym::loc_collision_walk, &AtCollisionWalk);
    undo.add(g_collisionWalk);
    undo.keep();
    for (Stats* s : {&EU4SB_LocIndexStats, &EU4SB_LocCollisionStats}) {
        InterlockedExchange(&s->mode, g_verifyOnly ? 2 : 1);
        InterlockedExchange(&s->initialized, 1);
    }
    Log(ANVIL_LOG_INFO, "loc_index: %s the walks over a localisation table for a key defined again and for hash collisions",
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
    bytes += g_order->bytes();
    g_order->Clear();
    g_lastFrame = 0;
    g_lastRunEnd = -1;
    ReleaseSRWLockExclusive(&g_lock);
    Log(ANVIL_LOG_INFO, "loc_index: %zu table(s), %zu entries indexed, %zu KiB freed", tables, entries, bytes / 1024);
    LogStats("loc_index", EU4SB_LocIndexStats);
    LogStats("loc_index collisions", EU4SB_LocCollisionStats);
}

}  // namespace sb
