// Diagnostic (off by default): what the script tokenizer's keyword dictionary
// costs (lab docs: docs/startup-phase-profile.md, "Tokenizer").
//
// The tokenizer (0x170E700) looks every word token up in a global
// case-insensitive ternary search tree (object 0x2326720, root at +0x08) via
// 0x6E4880; "get or add" (0x170D410) does the same before adding a dynamic
// token. Both call sites are redirected (Anvil redirect_call; the code around
// each call is declared too) to a wrapper that calls the original (the rest
// of that site's chain) and counts calls per thread, key lengths,
// hits and per-key frequencies. Nothing is changed. At the ready report the
// tree is walked once (node count, word count, lookup depth per word) and
// two files are written to the data directory for an offline benchmark:
// token_dict_keys.tsv (observed keys) and token_dict_tree.tsv (tree words).
#include "modules.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using LookupFn = void* (*)(void*, const char*);

constexpr int kThreads = 48;
constexpr int kLengths = 64;  // keys this long or longer share the last bucket
constexpr int kSites = 2;     // 0 = tokenizer, 1 = get-or-add
constexpr LONG64 kGrowthEvery = 1 << 16;
constexpr std::size_t kMaxGrowthPoints = 512;

// Tree node (eu4.exe 1.37.5): value, character, lower, higher, equal.
constexpr std::size_t kNodeValue = 0x00;
constexpr std::size_t kNodeChar = 0x08;
constexpr std::size_t kNodeLo = 0x10;
constexpr std::size_t kNodeHi = 0x18;
constexpr std::size_t kNodeEq = 0x20;
constexpr std::size_t kDictRoot = 0x08;

LookupFn g_lookup[kSites] = {};  // the rest of each redirected call's chain
anvil::FunctionHook<void*(void*, const char*)> g_sites[kSites];
void* g_dict = nullptr;
const void* const volatile* g_dynamicBegin = nullptr;  // vector<record*> of tokens added at run time
const void* const volatile* g_dynamicEnd = nullptr;

struct ThreadRow {
    volatile LONG tid;
    volatile LONG64 calls[kSites];
};
ThreadRow g_rows[kThreads];
volatile LONG64 g_calls[kSites] = {};
volatile LONG64 g_hits[kSites] = {};
volatile LONG64 g_lengths[kLengths] = {};

struct KeyStat {
    LONG64 count = 0;
    bool hit = false;
};
SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_map<std::string, KeyStat>* g_keys = nullptr;
struct GrowthPoint {
    LONG64 calls;
    LONG64 ticks;
    LONG64 dynamicTokens;
};
std::vector<GrowthPoint>* g_growth = nullptr;
LONG64 g_startTicks = 0;
bool g_done = false;  // set at the ready report; later calls are only forwarded
std::wstring* g_dir = nullptr;  // the data directory, for the files of the report

ThreadRow* Row() noexcept {
    const LONG tid = static_cast<LONG>(GetCurrentThreadId());
    for (auto& row : g_rows) {
        LONG seen = InterlockedCompareExchange(&row.tid, 0, 0);
        if (seen == 0) seen = InterlockedCompareExchange(&row.tid, tid, 0);
        if (seen == 0 || seen == tid) return &row;
    }
    return nullptr;
}

LONG64 DynamicTokens() noexcept {
    const auto begin = reinterpret_cast<uintptr_t>(*g_dynamicBegin);
    const auto end = reinterpret_cast<uintptr_t>(*g_dynamicEnd);
    return (begin != 0 && end >= begin) ? static_cast<LONG64>((end - begin) / sizeof(void*)) : 0;
}

void* Record(int site, void* dict, const char* key) {
    void* value = g_lookup[site](dict, key);
    if (g_done || key == nullptr) return value;
    const LONG64 n = InterlockedIncrement64(&g_calls[site]);
    if (value != nullptr) InterlockedIncrement64(&g_hits[site]);
    if (ThreadRow* row = Row()) InterlockedIncrement64(&row->calls[site]);
    std::size_t length = 0;
    while (key[length] != 0 && length < 4096) ++length;
    InterlockedIncrement64(&g_lengths[std::min<std::size_t>(length, kLengths - 1)]);
    AcquireSRWLockExclusive(&g_lock);
    try {
        if (g_done) {
            // The ready report took over the map; nothing more is recorded.
        } else if (site == 0) {
            KeyStat& stat = (*g_keys)[std::string(key, length)];
            ++stat.count;
            stat.hit = value != nullptr;
        }
        if (!g_done && n % kGrowthEvery == 1 && g_growth->size() < kMaxGrowthPoints) {
            g_growth->push_back({InterlockedCompareExchange64(&g_calls[0], 0, 0), sb::Ticks() - g_startTicks, DynamicTokens()});
        }
    } catch (...) {
    }
    ReleaseSRWLockExclusive(&g_lock);
    return value;
}

struct TreeStats {
    LONG64 nodes = 0, words = 0, maxDepth = 0, depthSum = 0;
};

// Iterative walk; depth = nodes visited by a lookup that ends at this node.
TreeStats WalkTree(std::vector<std::pair<std::string, LONG64>>* words) {
    TreeStats stats;
    struct Frame {
        const unsigned char* node;
        std::string prefix;
        LONG64 depth;
    };
    std::vector<Frame> stack;
    const auto* root = sb::Read<const unsigned char*>(g_dict, kDictRoot);
    if (root != nullptr) stack.push_back({root, std::string(), 1});
    while (!stack.empty()) {
        Frame f = std::move(stack.back());
        stack.pop_back();
        if (++stats.nodes > 50'000'000) break;  // corrupt tree guard
        const char ch = sb::Read<char>(f.node, kNodeChar);
        const void* value = sb::Read<const void*>(f.node, kNodeValue);
        std::string here = f.prefix + ch;
        if (value != nullptr) {
            ++stats.words;
            stats.depthSum += f.depth;
            stats.maxDepth = std::max(stats.maxDepth, f.depth);
            if (words) words->push_back({here, f.depth});
        }
        if (const auto* lo = sb::Read<const unsigned char*>(f.node, kNodeLo)) stack.push_back({lo, f.prefix, f.depth + 1});
        if (const auto* hi = sb::Read<const unsigned char*>(f.node, kNodeHi)) stack.push_back({hi, f.prefix, f.depth + 1});
        if (const auto* eq = sb::Read<const unsigned char*>(f.node, kNodeEq)) stack.push_back({eq, std::move(here), f.depth + 1});
    }
    return stats;
}

}  // namespace

extern "C" {
void* SB_TokenLookupTokenizer(void* dict, const char* key) { return Record(0, dict, key); }
void* SB_TokenLookupGetOrAdd(void* dict, const char* key) { return Record(1, dict, key); }
// requests = tokenizer lookups, hits = tokenizer hits, skips = get-or-add lookups, mode = 1 on.
__declspec(dllexport) sb::Stats EU4SB_TokenDictStats = SB_STATS_INIT;
}

namespace sb {

bool InstallTokenDictProfile(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    // Tokenizer: "mov rdx,rsi; lea rcx,[dict]; call lookup; test rax,rax"
    // get-or-add: "lea rcx,[dict]; call lookup; test rax,rax"; both declared.
    try {
        g_keys = new std::unordered_map<std::string, KeyStat>();
        g_keys->reserve(1 << 16);
        g_growth = new std::vector<GrowthPoint>();
        g_growth->reserve(kMaxGrowthPoints);
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    g_dict = const_cast<unsigned char*>(host.data(sym::token_dict));
    g_dynamicBegin = reinterpret_cast<const void* const*>(host.data(sym::token_dynamic_begin));
    g_dynamicEnd = reinterpret_cast<const void* const*>(host.data(sym::token_dynamic_end));
    g_startTicks = Ticks();
    Undo undo;
    host.redirect(g_sites[0], sym::token_lookup_tokenizer_call, &SB_TokenLookupTokenizer);
    undo.add(g_sites[0]);
    host.redirect(g_sites[1], sym::token_lookup_get_or_add_call, &SB_TokenLookupGetOrAdd);
    undo.add(g_sites[1]);
    for (int i = 0; i < kSites; ++i) g_lookup[i] = g_sites[i].next_pointer();
    undo.keep();
    InterlockedExchange(&EU4SB_TokenDictStats.mode, 1);
    InterlockedExchange(&EU4SB_TokenDictStats.initialized, 1);
    return true;
}

void ReportTokenDictProfile() {
    if (g_keys == nullptr) return;
    AcquireSRWLockExclusive(&g_lock);
    g_done = true;
    ReleaseSRWLockExclusive(&g_lock);
    const LONG64 calls0 = g_calls[0], calls1 = g_calls[1];
    InterlockedExchange64(&EU4SB_TokenDictStats.requests, calls0);
    InterlockedExchange64(&EU4SB_TokenDictStats.hits, g_hits[0]);
    InterlockedExchange64(&EU4SB_TokenDictStats.skips, calls1);
    bench::Get().set("token_dict_profile.tokenizer_lookups", calls0);
    bench::Get().set("token_dict_profile.tokenizer_hits", g_hits[0]);
    bench::Get().set("token_dict_profile.get_or_add_lookups", calls1);
    Log(ANVIL_LOG_INFO, "token_dict_profile: tokenizer lookups %lld (hits %lld, distinct keys %zu), get-or-add lookups %lld (hits %lld), dynamic tokens %lld",
        calls0, g_hits[0], g_keys->size(), calls1, g_hits[1], DynamicTokens());
    std::string threads;
    for (const auto& row : g_rows) {
        if (row.tid == 0) break;
        threads += " " + std::to_string(row.tid) + ":" + std::to_string(row.calls[0]) + "/" + std::to_string(row.calls[1]);
    }
    Log(ANVIL_LOG_INFO, "token_dict_profile: per thread tid:tokenizer/get-or-add%s", threads.c_str());
    std::string lengths;
    for (int i = 0; i < kLengths; ++i) {
        if (g_lengths[i] != 0) lengths += " " + std::to_string(i) + ":" + std::to_string(g_lengths[i]);
    }
    Log(ANVIL_LOG_INFO, "token_dict_profile: key lengths%s", lengths.c_str());
    std::string growth;
    for (const auto& g : *g_growth) {
        growth += " " + std::to_string(g.calls) + "@" + std::to_string(static_cast<long long>(TicksToMs(g.ticks))) + "ms=" + std::to_string(g.dynamicTokens);
    }
    Log(ANVIL_LOG_INFO, "token_dict_profile: dynamic tokens by tokenizer lookups (calls@ms=count)%s", growth.c_str());

    try {
        std::vector<std::pair<std::string, LONG64>> words;
        const TreeStats tree = WalkTree(&words);
        // Lookup cost weighted by the observed hits: nodes visited per successful tokenizer lookup.
        std::unordered_map<std::string, LONG64> depthOf;
        depthOf.reserve(words.size() * 2);
        for (const auto& w : words) {
            std::string lower = w.first;
            for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            depthOf.emplace(std::move(lower), w.second);
        }
        LONG64 weighted = 0, weightedCount = 0;
        for (const auto& [key, stat] : *g_keys) {
            if (!stat.hit) continue;
            std::string lower = key;
            for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            const auto found = depthOf.find(lower);
            if (found == depthOf.end()) continue;
            weighted += found->second * stat.count;
            weightedCount += stat.count;
        }
        Log(ANVIL_LOG_INFO, "token_dict_profile: tree nodes %lld, words %lld, word depth avg %.1f max %lld, nodes per hit lookup %.1f",
            tree.nodes, tree.words, tree.words ? static_cast<double>(tree.depthSum) / tree.words : 0.0, tree.maxDepth,
            weightedCount ? static_cast<double>(weighted) / weightedCount : 0.0);

        const std::wstring& dir = *g_dir;
        std::ofstream keys(dir + L"\\token_dict_keys.tsv", std::ios::binary);
        keys << "count\thit\tkey\n";
        for (const auto& [key, stat] : *g_keys) keys << stat.count << '\t' << (stat.hit ? 1 : 0) << '\t' << key << '\n';
        std::ofstream treeFile(dir + L"\\token_dict_tree.tsv", std::ios::binary);
        treeFile << "depth\tword\n";
        for (const auto& w : words) treeFile << w.second << '\t' << w.first << '\n';
    } catch (...) {
        Log(ANVIL_LOG_WARNING, "token_dict_profile: tree walk or file output failed");
    }
}

}  // namespace sb
