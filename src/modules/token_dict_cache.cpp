// Cache for the script tokenizer's keyword lookups (lab docs:
// docs/startup-phase-profile.md, "Tokenizer").
//
// The tokenizer (0x170E700) looks every word token up in a global
// case-insensitive ternary search tree (object 0x2326720, root at +0x08) via
// 0x6E4880: one CRT tolower and one tree node per character, ~17 nodes per
// successful lookup over ~8.7 M lookups per start. The tokenizer's call site
// is redirected (byte checks around the call) to SB_TokenDictCache, which
// answers from a per-thread open-addressing cache keyed by the token with
// ASCII letters lowercased (other bytes unchanged, so only spellings the tree
// itself treats as equal share an entry). A miss calls the original and
// stores its answer.
//
// The tree only grows during loading: "get or add" (0x170D410) inserts a
// word and then appends it to the run-time token vector (0x242B8A0). Hence:
// - a cached hit (pointer to the word's value) stays valid while the tree is
//   not rebuilt: same root and a run-time token count that did not drop;
// - a cached miss is valid only for the run-time token count read before the
//   lookup that produced it.
// "verify" = compute both, return the original's value, count mismatches;
// "on" = return the cache answer, compare every 256th call per thread and
// disable on a mismatch. The ready marker sends calls to the original.
// Caches are per thread (no locked instructions on the hot path) and are
// kept until the process exits; their counters are summed at the report.
#include "modules.hpp"

#include <vector>

namespace {

using LookupFn = void* (*)(void*, const char*);

constexpr std::size_t kDictRoot = 0x08;
constexpr std::size_t kMaxKey = 255;     // longer keys go to the original
constexpr std::size_t kInitialSlots = 1024;

LookupFn g_lookup = nullptr;
const void* g_dict = nullptr;
const uintptr_t volatile* g_dynamicBegin = nullptr;
const uintptr_t volatile* g_dynamicEnd = nullptr;
bool g_verifyOnly = false;
volatile LONG g_disabled = 0;

struct Cache {
    struct Slot {
        std::uint64_t hash;    // 0 = empty
        void* value;           // the tree's answer (may be null)
        std::uint32_t count;   // run-time token count the answer was read at (misses only)
        std::uint32_t offset;  // key bytes in `keys`
        std::uint32_t length;
        std::uint32_t pad;
    };
    std::vector<Slot> slots;
    std::vector<char> keys;
    std::size_t mask = 0, used = 0;
    const void* root = nullptr;
    std::uint32_t maxCount = 0;
    // Plain counters, summed at the report.
    std::uint64_t requests = 0, hits = 0, misses = 0, skips = 0, verifications = 0, mismatches = 0, flushes = 0;

    void Reset(std::size_t capacity) {
        slots.assign(capacity, Slot{});
        mask = capacity - 1;
        used = 0;
        keys.clear();
    }
    void Grow() {
        std::vector<Slot> old;
        old.swap(slots);
        slots.assign(old.size() * 2, Slot{});
        mask = slots.size() - 1;
        for (const Slot& s : old) {
            if (s.hash == 0) continue;
            std::size_t i = s.hash & mask;
            while (slots[i].hash != 0) i = (i + 1) & mask;
            slots[i] = s;
        }
    }
};

SRWLOCK g_cachesLock = SRWLOCK_INIT;
std::vector<Cache*>* g_caches = nullptr;
thread_local Cache* t_cache = nullptr;

Cache* ThreadCache() noexcept {
    if (t_cache != nullptr) return t_cache;
    try {
        auto* cache = new Cache();
        cache->Reset(kInitialSlots);
        AcquireSRWLockExclusive(&g_cachesLock);
        try {
            g_caches->push_back(cache);
        } catch (...) {
            ReleaseSRWLockExclusive(&g_cachesLock);
            delete cache;
            return nullptr;
        }
        ReleaseSRWLockExclusive(&g_cachesLock);
        t_cache = cache;
    } catch (...) {
        return nullptr;
    }
    return t_cache;
}

std::uint32_t DynamicCount() noexcept {
    const uintptr_t begin = *g_dynamicBegin;
    const uintptr_t end = *g_dynamicEnd;
    return (begin != 0 && end >= begin) ? static_cast<std::uint32_t>((end - begin) / sizeof(void*)) : 0;
}

bool Disabled() noexcept {
    if (g_disabled != 0) return true;
    if (sb::ReadyReached()) {
        InterlockedExchange(&g_disabled, 2);
        return true;
    }
    return false;
}

// Cache answer for key; falls back to the original for long keys or when no cache exists.
void* CachedLookup(Cache& c, void* dict, const char* key, bool& fromCache) {
    fromCache = false;
    char lower[kMaxKey];
    std::uint64_t h = 0x9E3779B97F4A7C15ULL;
    std::size_t n = 0;
    for (; key[n] != 0; ++n) {
        if (n >= kMaxKey) {
            ++c.skips;
            return g_lookup(dict, key);
        }
        unsigned char ch = static_cast<unsigned char>(key[n]);
        if (ch - 'A' < 26u) ch |= 0x20;
        lower[n] = static_cast<char>(ch);
        h = (h ^ ch) * 0xff51afd7ed558ccdULL;
    }
    h ^= h >> 29;
    h |= 1;

    // Tree rebuilt (cleared and refilled): forget everything.
    const void* root = sb::Read<const void*>(g_dict, kDictRoot);
    const std::uint32_t count = DynamicCount();  // read before the lookup that may fill a slot
    if (root != c.root || count < c.maxCount) {
        if (c.used != 0) ++c.flushes;
        c.Reset(c.slots.size());
        c.root = root;
        c.maxCount = count;
    } else if (count > c.maxCount) {
        c.maxCount = count;
    }

    for (std::size_t i = h & c.mask;; i = (i + 1) & c.mask) {
        Cache::Slot& s = c.slots[i];
        if (s.hash == 0) {
            void* value = g_lookup(dict, key);
            ++c.misses;
            if ((c.used + 1) * 2 > c.slots.size()) {
                try {
                    c.Grow();
                } catch (...) {
                    return value;  // keep working without storing
                }
                // Re-probe in the grown table.
                std::size_t j = h & c.mask;
                while (c.slots[j].hash != 0) j = (j + 1) & c.mask;
                i = j;
            }
            Cache::Slot& t = c.slots[i];
            try {
                t.offset = static_cast<std::uint32_t>(c.keys.size());
                c.keys.insert(c.keys.end(), lower, lower + n);
            } catch (...) {
                return value;
            }
            t.hash = h;
            t.value = value;
            t.count = count;
            t.length = static_cast<std::uint32_t>(n);
            ++c.used;
            return value;
        }
        if (s.hash != h || s.length != n || std::memcmp(c.keys.data() + s.offset, lower, n) != 0) continue;
        if (s.value == nullptr && s.count != count) {
            // A miss from before a word was added: ask the tree again.
            s.value = g_lookup(dict, key);
            s.count = count;
            ++c.misses;
            return s.value;
        }
        ++c.hits;
        fromCache = true;
        return s.value;
    }
}

}  // namespace

extern "C" {
// requests = calls, hits = cache answers, misses = tree lookups, skips = to the original,
// verifications / mismatches = comparisons, duplicateKeys = cache flushes, mode = 1 on / 2 verify,
// cacheDisabled = disabled after a mismatch, readyDisabled = stopped at the ready marker.
__declspec(dllexport) sb::Stats EU4SB_TokenDictCacheStats = SB_STATS_INIT;

void* SB_TokenDictCache(void* dict, const char* key) {
    if (Disabled() || key == nullptr) return g_lookup(dict, key);
    Cache* c = ThreadCache();
    if (c == nullptr) return g_lookup(dict, key);
    ++c->requests;
    bool fromCache = false;
    void* answer = CachedLookup(*c, dict, key, fromCache);
    if (fromCache && (g_verifyOnly || (c->requests & 255) == 1)) {
        ++c->verifications;
        void* actual = g_lookup(dict, key);
        if (actual != answer) {
            ++c->mismatches;
            if (!g_verifyOnly) InterlockedExchange(&g_disabled, 1);
            return actual;
        }
    }
    return answer;
}
}

namespace sb {

bool InstallTokenDictCache(const Build& build, Mode mode) {
    if (mode == Mode::Off) return false;
    // "mov rdx,rsi; lea rcx,[dict]; call lookup; test rax,rax"
    const uint32_t site = build.token_lookup_tokenizer_site;
    const auto* lea = AtRva<const unsigned char>(site - 7);
    std::int32_t disp;
    std::memcpy(&disp, lea + 3, 4);
    const uintptr_t dict = context().image_base + build.token_dict;
    const unsigned char movRdx[3] = {0x48, 0x8B, 0xD6};
    const unsigned char leaRcx[3] = {0x48, 0x8D, 0x0D};
    const unsigned char testRax[3] = {0x48, 0x85, 0xC0};
    if (std::memcmp(lea - 3, movRdx, 3) != 0 || std::memcmp(lea, leaRcx, 3) != 0 ||
        reinterpret_cast<uintptr_t>(lea) + 7 + disp != dict || std::memcmp(AtRva<const unsigned char>(site + 5), testRax, 3) != 0) {
        Log(ANVIL_LOG_WARNING, "token_dict_cache: unexpected code around the tokenizer call; module off");
        return false;
    }
    try {
        g_caches = new std::vector<Cache*>();
        g_caches->reserve(64);
    } catch (...) {
        return false;
    }
    g_lookup = reinterpret_cast<LookupFn>(AtRva<unsigned char>(build.token_lookup_fn));
    g_dict = reinterpret_cast<const void*>(dict);
    g_dynamicBegin = AtRva<const uintptr_t>(build.token_dynamic_vector);
    g_dynamicEnd = AtRva<const uintptr_t>(build.token_dynamic_vector + 8);
    g_verifyOnly = mode == Mode::Verify;
    const AnvilResult r = context().host->redirect_call(context().self, site, build.token_lookup_fn,
                                                        reinterpret_cast<void*>(&SB_TokenDictCache));
    if (r != ANVIL_OK) {
        Log(ANVIL_LOG_WARNING, "token_dict_cache: call site refused (%s); module off", ResultName(r));
        return false;
    }
    InterlockedExchange(&EU4SB_TokenDictCacheStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_TokenDictCacheStats.initialized, 1);
    return true;
}

void ReportTokenDictCache() {
    if (g_caches == nullptr) return;
    // Counters of other threads are read without synchronization: the loading threads are done.
    std::uint64_t requests = 0, hits = 0, misses = 0, skips = 0, verifications = 0, mismatches = 0, flushes = 0, entries = 0;
    std::size_t caches = 0;
    AcquireSRWLockShared(&g_cachesLock);
    for (const Cache* c : *g_caches) {
        requests += c->requests, hits += c->hits, misses += c->misses, skips += c->skips;
        verifications += c->verifications, mismatches += c->mismatches, flushes += c->flushes, entries += c->used;
        ++caches;
    }
    ReleaseSRWLockShared(&g_cachesLock);
    auto& s = EU4SB_TokenDictCacheStats;
    InterlockedExchange64(&s.requests, static_cast<LONG64>(requests));
    InterlockedExchange64(&s.hits, static_cast<LONG64>(hits));
    InterlockedExchange64(&s.misses, static_cast<LONG64>(misses));
    InterlockedExchange64(&s.skips, static_cast<LONG64>(skips));
    InterlockedExchange64(&s.verifications, static_cast<LONG64>(verifications));
    InterlockedExchange64(&s.mismatches, static_cast<LONG64>(mismatches));
    InterlockedExchange64(&s.duplicateKeys, static_cast<LONG64>(flushes));
    InterlockedExchange(&s.cacheDisabled, g_disabled == 1 ? 1 : 0);
    InterlockedExchange(&s.readyDisabled, g_disabled == 2 ? 1 : 0);
    LogStats("token_dict_cache", s);
    Log(ANVIL_LOG_INFO, "token_dict_cache: %zu thread caches, %llu entries", caches, static_cast<unsigned long long>(entries));
}

}  // namespace sb
