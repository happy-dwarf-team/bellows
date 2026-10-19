// token_dict_cache against a ternary search tree in the game's layout and a lookup
// with the game's semantics (0x6E4880/0x6E4800: tolower(key char) vs node char).
// Module source included directly so its internals are visible to the test.
#include "../src/modules/token_dict_cache.cpp"
#include <cassert>
#include <cctype>
#include <cstdio>
#include <deque>
#include <random>
#include <string>

namespace {

struct alignas(8) TNode {
    void* value;
    char ch;
    TNode* lo;
    TNode* hi;
    TNode* eq;
};
static_assert(offsetof(TNode, lo) == 0x10 && offsetof(TNode, eq) == 0x20, "game node layout");

std::deque<TNode> g_nodes;
std::deque<int> g_values;
alignas(8) unsigned char g_dictObj[0x18] = {};
uintptr_t g_vector[3] = {};  // begin, end, capacity of the run-time token vector
std::vector<void*> g_records;
int g_originalCalls = 0;

TNode*& Root() { return *reinterpret_cast<TNode**>(g_dictObj + kDictRoot); }

void Insert(const std::string& word, void* value) {
    TNode** link = &Root();
    std::size_t i = 0;
    for (;;) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(word[i])));
        if (*link == nullptr) {
            g_nodes.push_back(TNode{nullptr, c, nullptr, nullptr, nullptr});
            *link = &g_nodes.back();
        }
        TNode* n = *link;
        if (c < n->ch) {
            link = &n->lo;
        } else if (c > n->ch) {
            link = &n->hi;
        } else if (i + 1 < word.size()) {
            ++i;
            link = &n->eq;
        } else {
            n->value = value;
            return;
        }
    }
}

// The game's lookup semantics (tolower on the key, node chars as stored).
void* TreeLookup(const char* key) {
    const TNode* n = Root();
    if (n == nullptr) return nullptr;
    for (;;) {
        const int d = std::tolower(static_cast<signed char>(*key)) - n->ch;
        const TNode* next;
        if (d == 0) {
            ++key;
            if (*key == 0) return n->value;
            next = n->eq;
        } else {
            next = d > 0 ? n->hi : n->lo;
        }
        if (next == nullptr) return nullptr;
        n = next;
    }
}

void* MockOriginal(void* dict, const char* key) {
    assert(dict == g_dictObj);
    ++g_originalCalls;
    return TreeLookup(key);
}

void* AddWord(const std::string& word, bool appendToVector = true) {
    g_values.push_back(static_cast<int>(g_values.size()) + 1);
    Insert(word, &g_values.back());
    if (appendToVector) {
        g_records.push_back(&g_values.back());
        g_vector[0] = reinterpret_cast<uintptr_t>(g_records.data());
        g_vector[1] = g_vector[0] + g_records.size() * sizeof(void*);
    }
    return &g_values.back();
}

std::uint64_t Sum(std::uint64_t Cache::*field) {
    std::uint64_t total = 0;
    for (const Cache* c : *g_caches) total += c->*field;
    return total;
}

void* Cached(const std::string& key) { return SB_TokenDictCache(g_dictObj, key.c_str()); }

}  // namespace

int main() {
    g_lookup = &MockOriginal;
    g_dict = g_dictObj;
    g_dynamicBegin = &g_vector[0];
    g_dynamicEnd = &g_vector[1];
    g_caches = new std::vector<Cache*>();
    volatile LONG& ready = sb::g_gameReady;  // set by the anvil-game ready event in the plugin
    ready = 0;

    // Static words (empty word = a node with char 0, as in the game's tree).
    const char* words[] = {"not", "yes", "no", "has_country_flag", "clr_country_flag", "key", "type", "estate", "size",
                           "if", "limit", "and", "or", "a", "ab", "abc", "zz", "y\xE9s"};
    for (const char* w : words) AddWord(w, false);
    AddWord(std::string(1, '\0'), false);

    // Random keys: words, case variants, prefixes, extensions, non-ASCII, empty, long.
    std::mt19937 rng(7);
    std::vector<std::string> keys;
    for (const char* w : words) {
        std::string s = w;
        keys.push_back(s);
        std::string upper = s;
        for (auto& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        keys.push_back(upper);
        if (s.size() > 1) keys.push_back(s.substr(0, s.size() - 1));
        keys.push_back(s + "x");
    }
    keys.push_back("");
    keys.push_back("Y\xC9S");  // non-ASCII: the cache keeps 0xC9 and 0xE9 apart
    keys.push_back(std::string(300, 'a'));
    keys.push_back(std::string(255, 'b'));
    keys.push_back("batch");

    // Verify mode over many random calls: every cache answer compared with the tree.
    g_verifyOnly = true;
    for (int i = 0; i < 20000; ++i) {
        const std::string& k = keys[rng() % keys.size()];
        assert(Cached(k) == TreeLookup(k.c_str()));
    }
    assert(Sum(&Cache::mismatches) == 0);
    assert(Sum(&Cache::hits) > 15000);
    assert(Sum(&Cache::skips) > 0);  // the 300-byte key

    // A cached miss is refreshed after a word is added (count grows).
    g_verifyOnly = false;
    assert(Cached("batch") == nullptr);
    void* batch = AddWord("batch");
    assert(Cached("batch") == batch && Cached("BATCH") == batch);
    // A cached hit stays valid while words are added.
    void* yes = TreeLookup("yes");
    AddWord("spawn_rebels");
    assert(Cached("Yes") == yes);

    // Race window (word in the tree, vector not yet appended): only verify mode can see it.
    assert(Cached("as_if_faction") == nullptr);
    void* late = AddWord("as_if_faction", false);
    g_verifyOnly = true;
    const std::uint64_t before = Sum(&Cache::mismatches);
    assert(Cached("as_if_faction") == late);  // verify returns the original's answer
    assert(Sum(&Cache::mismatches) == before + 1);
    g_verifyOnly = false;

    // Tree rebuilt (new root, run-time tokens reset): the cache is flushed.
    const std::uint64_t flushesBefore = Sum(&Cache::flushes);
    Root() = nullptr;
    g_records.clear();
    g_vector[0] = g_vector[1] = 0;
    void* fresh = AddWord("yes", false);
    assert(Cached("yes") == fresh);
    assert(Sum(&Cache::flushes) == flushesBefore + 1);

    // On mode: a corrupted cache entry is caught by the sampled check and disables the module.
    for (Cache* c : *g_caches) {
        for (auto& s : c->slots) {
            if (s.hash != 0 && s.value == fresh) s.value = &g_values.front();
        }
        c->requests = 0;  // next call is sampled
    }
    assert(Cached("yes") == fresh);
    assert(g_disabled == 1);
    const int callsBefore = g_originalCalls;
    assert(Cached("yes") == fresh && g_originalCalls == callsBefore + 1);  // disabled: original only

    // Ready marker: original.
    g_disabled = 0;
    ready = 1;
    assert(Cached("no") == TreeLookup("no"));
    assert(g_disabled == 2);

    // Threads: each gets its own cache.
    ready = 0;
    g_disabled = 0;
    std::size_t cachesBefore = g_caches->size();
    HANDLE thread = CreateThread(nullptr, 0, [](void*) -> DWORD {
        for (int i = 0; i < 1000; ++i) assert(SB_TokenDictCache(g_dictObj, "estate") == TreeLookup("estate"));
        return 0;
    }, nullptr, 0, nullptr);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    assert(g_caches->size() == cachesBefore + 1);

    std::printf("token dict cache ok: %zu caches, %llu hits, %llu misses\n", g_caches->size(),
                static_cast<unsigned long long>(Sum(&Cache::hits)), static_cast<unsigned long long>(Sum(&Cache::misses)));
    return 0;
}
