// Fixture for startup_trigger_intern.inc: builds trigger nodes and a key map in
// the game's memory layout and compares EU4LabTriggerIntern with a naive
// reference of 0xB9B4E0 (structural key equality, children as sets); no code
// patching.
// Ported from the lab (scripts/native/test_startup_trigger_intern.cpp), plus non-empty map scenarios
// (shared subtrees, foreign nodes, keys inside keys); module source
// included directly so its internals are visible to the test.
#include "../src/modules/trigger_intern.cpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <deque>
#include <map>
#include <random>
#include <set>
#include <unordered_map>

namespace {

using sb::Read;

// Trigger node: children array at +0x08, count at +0x14, id at +0x50.
struct alignas(8) Node {
    unsigned char bytes[0x60] = {};
    std::vector<Node*> children;
    void Sync(int id) {
        Node** data = children.data();
        const std::int32_t count = static_cast<std::int32_t>(children.size());
        std::memcpy(bytes + kNodeChildren, &data, sizeof(data));
        std::memcpy(bytes + kNodeCount, &count, sizeof(count));
        std::memcpy(bytes + kNodeId, &id, sizeof(id));
    }
    int Id() const { return Read<std::int32_t>(bytes, kNodeId); }
};

// Doubly linked list node with the payload pointer at +0x10.
struct alignas(8) ListNode {
    ListNode* next = nullptr;
    ListNode* prev = nullptr;
    const void* payload = nullptr;
};

// Key: id at +0x00, set list sentinel pointer at +0x10.
struct alignas(8) Key {
    unsigned char bytes[0x48] = {};
};

// Map entry: key at +0x10, value at +0x58.
struct alignas(8) MapEntry {
    unsigned char bytes[0x60] = {};
};

std::deque<Node> g_nodes;
std::deque<ListNode> g_lists;
std::deque<Key> g_keys;
std::deque<MapEntry> g_entries;
alignas(8) unsigned char g_singleton[0x90] = {};

// Canonical text of a structure: "id(sorted unique children)"; cached, as nodes never change.
std::string Canon(const Node* node) {
    static std::unordered_map<const Node*, std::string> cache;
    const auto known = cache.find(node);
    if (known != cache.end()) return known->second;
    std::set<std::string> children;
    for (const Node* child : node->children) children.insert(Canon(child));
    std::string text = std::to_string(node->Id()) + "(";
    for (const auto& child : children) text += child + ",";
    return cache[node] = text + ")";
}

std::string CanonKey(const void* key) {
    std::set<std::string> children;
    const auto* head = Read<const ListNode*>(key, kKeySetHead);
    for (const ListNode* e = head->next; e != head; e = e->next) children.insert(CanonKey(e->payload));
    std::string text = std::to_string(Read<std::int32_t>(key, kKeyId)) + "(";
    for (const auto& child : children) text += child + ",";
    return text + ")";
}

ListNode* NewList() {
    g_lists.emplace_back();
    ListNode* head = &g_lists.back();
    head->next = head->prev = head;
    return head;
}

void Append(ListNode* head, const void* payload) {
    g_lists.emplace_back();
    ListNode* n = &g_lists.back();
    n->payload = payload;
    n->prev = head->prev;
    n->next = head;
    head->prev->next = n;
    head->prev = n;
}

// Builds a game-layout key from a node (children deduplicated by structure, set order shuffled).
const void* BuildKey(const Node* node, std::mt19937& rng) {
    g_keys.emplace_back();
    Key* key = &g_keys.back();
    const std::int32_t id = node->Id();
    std::memcpy(key->bytes + kKeyId, &id, sizeof(id));
    ListNode* head = NewList();
    std::memcpy(key->bytes + kKeySetHead, &head, sizeof(head));
    std::vector<const Node*> unique;
    std::set<std::string> seen;
    for (const Node* child : node->children) {
        if (seen.insert(Canon(child)).second) unique.push_back(child);
    }
    std::shuffle(unique.begin(), unique.end(), rng);
    for (const Node* child : unique) Append(head, BuildKey(child, rng));
    return key;
}

std::map<std::string, std::uint64_t> g_reference;
int g_originalCalls = 0;

std::uint64_t MockOriginal(void*, void* node) {
    ++g_originalCalls;
    const auto found = g_reference.find(Canon(static_cast<Node*>(node)));
    return found == g_reference.end() ? 0 : found->second;
}

Node* Random(std::mt19937& rng, int depth) {
    g_nodes.emplace_back();
    Node* node = &g_nodes.back();
    const int count = depth > 5 ? 0 : static_cast<int>(rng() % 4);
    for (int i = 0; i < count; ++i) node->children.push_back(Random(rng, depth + 1));
    if (count >= 2 && rng() % 3 == 0) node->children.push_back(node->children[0]);  // duplicate child
    node->Sync(static_cast<int>(rng() % 5));
    return node;
}

LONG64 S(volatile LONG64* v) { return InterlockedCompareExchange64(v, 0, 0); }
LONG64 S(volatile LONG* v) { return InterlockedCompareExchange(v, 0, 0); }

void ResetStats() {
    EU4SB_TriggerInternStats.requests = EU4SB_TriggerInternStats.hits = EU4SB_TriggerInternStats.misses = 0;
    EU4SB_TriggerInternStats.skips = EU4SB_TriggerInternStats.verifications = 0;
    EU4SB_TriggerInternStats.mismatches = EU4SB_TriggerInternStats.duplicateKeys = 0;
    EU4SB_TriggerInternStats.cacheDisabled = EU4SB_TriggerInternStats.readyDisabled = 0;
}

Node* Make(int id, std::vector<Node*> children) {
    g_nodes.emplace_back();
    Node* node = &g_nodes.back();
    node->children = std::move(children);
    node->Sync(id);
    return node;
}

// Registry stand-in: two unordered_set headers; only the sizes at +0x10 and +0x50 are read.
std::uintptr_t Registry(std::uint64_t first, std::uint64_t second) {
    static std::deque<std::array<std::uint64_t, 16>> registries;
    registries.emplace_back();
    auto& r = registries.back();
    r.fill(0);
    r[kRegistrySizes[0] / 8] = first;
    r[kRegistrySizes[1] / 8] = second;
    return reinterpret_cast<std::uintptr_t>(r.data());
}

// Fills the singleton's map with the structures of `sources` (structural
// duplicates skipped) and makes it the reference.
std::uint64_t BuildMap(unsigned char* singleton, const std::vector<const Node*>& sources, std::mt19937& rng) {
    static std::uint64_t nextValue = 0x1000;
    g_reference.clear();
    ListNode* mapHead = NewList();
    std::uint64_t size = 0;
    for (const Node* source : sources) {
        const std::string canon = Canon(source);
        if (g_reference.count(canon)) continue;
        g_entries.emplace_back();
        MapEntry* entry = &g_entries.back();
        const void* key = BuildKey(source, rng);
        std::memcpy(entry->bytes + kMapNodeKey, key, sizeof(Key));
        g_reference[canon] = nextValue;
        std::memcpy(entry->bytes + kMapNodeValue, &nextValue, sizeof(nextValue));
        nextValue += 0x10;
        // Map entries are list nodes themselves (next/prev at +0/+8).
        auto* asList = reinterpret_cast<ListNode*>(entry->bytes);
        asList->prev = mapHead->prev;
        asList->next = mapHead;
        mapHead->prev->next = asList;
        mapHead->prev = asList;
        ++size;
    }
    std::memcpy(singleton + kMapListHead, &mapHead, sizeof(mapHead));
    std::memcpy(singleton + kMapSize, &size, sizeof(size));
    return size;
}

unsigned char* NewSingleton() {
    struct alignas(8) Singleton {
        unsigned char bytes[0x90] = {};
    };
    static std::deque<Singleton> singletons;
    singletons.emplace_back();
    return singletons.back().bytes;
}

// Every registered node in verify mode, then in on mode, against the naive
// reference; returns the number of non-null answers.
std::size_t CheckAll(unsigned char* singleton, const std::vector<Node*>& registry) {
    g_verifyOnly = true;
    ResetStats();
    g_answerSequence = 0;
    std::size_t nonNull = 0;
    for (Node* n : registry) {
        const std::uint64_t expected = MockOriginal(nullptr, n);
        assert(SB_TriggerIntern(singleton, n) == expected);
        nonNull += expected != 0;
    }
    assert(S(&EU4SB_TriggerInternStats.mismatches) == 0 && S(&EU4SB_TriggerInternStats.cacheDisabled) == 0);
    assert(S(&EU4SB_TriggerInternStats.verifications) == static_cast<LONG64>(registry.size()));
    assert(S(&EU4SB_TriggerInternStats.duplicateKeys) == 1);

    g_verifyOnly = false;
    ResetStats();
    for (Node* n : registry) assert(SB_TriggerIntern(singleton, n) == MockOriginal(nullptr, n));
    assert(S(&EU4SB_TriggerInternStats.mismatches) == 0 && S(&EU4SB_TriggerInternStats.cacheDisabled) == 0);
    assert(S(&EU4SB_TriggerInternStats.duplicateKeys) == 0);
    assert(static_cast<std::size_t>(S(&EU4SB_TriggerInternStats.hits)) == nonNull);
    // Every inner node is memoized once; leaves are looked up directly.
    std::size_t inner = 0;
    for (Node* n : registry) inner += !n->children.empty();
    assert(g_state->memo.used == inner);
    return nonNull;
}

// Structure id of a node from the current index (memo for inner nodes, direct for leaves).
std::uint32_t Memoized(const Node* node) {
    std::uint32_t id = 0;
    if (!node->children.empty()) {
        const bool found = g_state->memo.Get(node, id);
        assert(found);
        return id;
    }
    return NodeId(*g_state, node, 0);
}

std::size_t ForeignCount() {
    std::size_t count = 0;
    for (const auto& slot : g_state->memo.slots) count += slot.node != nullptr && slot.id == kForeign;
    return count;
}

// Every node reachable from the roots, each pointer once, parents before children.
std::vector<Node*> Reachable(const std::vector<Node*>& roots) {
    std::vector<Node*> out;
    std::set<const Node*> seen;
    std::vector<Node*> stack(roots.rbegin(), roots.rend());
    while (!stack.empty()) {
        Node* n = stack.back();
        stack.pop_back();
        if (!seen.insert(n).second) continue;
        out.push_back(n);
        for (Node* c : n->children) stack.push_back(c);
    }
    return out;
}

void HandBuiltScenario(volatile std::uintptr_t& registrySlot) {
    std::mt19937 rng(7);
    Node* a = Make(1, {});
    Node* b = Make(2, {});
    Node* c = Make(3, {});
    Node* x = Make(99, {});
    Node* neg = Make(-7, {});
    Node* s = Make(10, {a, b});  // shared by several parents
    Node* p1 = Make(20, {s, c});
    Node* p2 = Make(21, {s, s, a});                                   // duplicate child pointer
    Node* p3 = Make(20, {c, Make(10, {b, a}), Make(10, {a, b, b})});  // equals p1 after deduplication
    Node* f1 = Make(20, {Make(10, {a, x}), c});                       // foreign at depth 1
    Node* f2 = Make(30, {p1});                                        // foreign root above a key
    Node* f3 = Make(20, {s, Make(5, {Make(6, {neg})})});              // foreign at depths 1 and 2, key leaf below
    Node* f4 = Make(21, {s, a, Make(77, {})});                        // unknown leaf
    Node* k5 = Make(40, {x});  // its child 99() is a key substructure but no key
    std::vector<const Node*> keys{p1, s, b, p2, k5, neg};
    unsigned char* singleton = NewSingleton();
    assert(BuildMap(singleton, keys, rng) == keys.size());
    const std::vector<Node*> registry = Reachable({f2, p2, p3, f1, f3, f4, k5});
    registrySlot = Registry(registry.size(), 0);

    assert(MockOriginal(nullptr, p3) == MockOriginal(nullptr, p1) && MockOriginal(nullptr, p1) != 0);
    for (Node* n : {f1, f2, f3, f4, x, a}) assert(MockOriginal(nullptr, n) == 0);
    for (Node* n : {s, b, p2, k5, neg}) assert(MockOriginal(nullptr, n) != 0);
    CheckAll(singleton, registry);
    for (Node* n : {f1, f2, f3, f4, f1->children[0], f3->children[1], f3->children[1]->children[0], f4->children[2]}) {
        assert(Memoized(n) == kForeign);
    }
    for (Node* n : {x, a, c, neg, s, p1, p3, k5}) assert(Memoized(n) != kForeign);
    assert(Memoized(p1) == Memoized(p3) && Memoized(s) == Memoized(p3->children[2]));
}

// Layered DAG: children drawn from any lower layer (shared subtrees), repeated
// pointers, few ids (structurally equal distinct nodes), keys from registered
// nodes and from unregistered variants of them.
void RandomDagScenario(std::uint32_t seed, volatile std::uintptr_t& registrySlot) {
    std::mt19937 rng(seed);
    std::vector<std::vector<Node*>> layers(7);
    std::vector<Node*> registry;
    for (int depth = 0; depth < 7; ++depth) {
        for (int i = 0; i < (depth == 0 ? 60 : 300); ++i) {
            std::vector<Node*> children;
            if (depth > 0) {
                const int count = static_cast<int>(rng() % 5);
                for (int j = 0; j < count; ++j) {
                    const auto& from = layers[rng() % depth];
                    children.push_back(from[rng() % from.size()]);
                }
                if (count >= 1 && rng() % 4 == 0) children.push_back(children[0]);
            }
            Node* n = Make(static_cast<int>(rng() % (depth == 0 ? 6 : 4)) - 1, std::move(children));
            layers[depth].push_back(n);
            registry.push_back(n);
        }
    }
    std::shuffle(registry.begin(), registry.end(), rng);
    std::vector<const Node*> keys;
    for (std::size_t i = 0; i < registry.size(); i += 7) keys.push_back(registry[i]);
    for (int i = 0; i < 20; ++i) {
        const Node* base = registry[rng() % registry.size()];
        keys.push_back(Make(50 + static_cast<int>(rng() % 3), base->children));
    }
    unsigned char* singleton = NewSingleton();
    BuildMap(singleton, keys, rng);
    registrySlot = seed % 2 ? Registry(registry.size() / 3, registry.size() / 3) : Registry(16, 16);
    const std::size_t nonNull = CheckAll(singleton, registry);
    assert(nonNull > 0 && nonNull < registry.size() && ForeignCount() > 0);
    std::printf("dag seed %u: %zu nodes, %zu non-null, %zu foreign\n", seed, registry.size(), nonNull, ForeignCount());
}

}  // namespace

int main() {
    std::mt19937 rng(1234);
    std::vector<Node*> registry;
    for (int i = 0; i < 400; ++i) {
        Node* root = Random(rng, 0);
        // Register every node of the tree, like the game's registry.
        std::vector<Node*> stack{root};
        while (!stack.empty()) {
            Node* n = stack.back();
            stack.pop_back();
            registry.push_back(n);
            for (Node* c : n->children) stack.push_back(c);
        }
    }
    // Map keys: structures of every third registered node.
    std::vector<const Node*> sources;
    for (std::size_t i = 0; i < registry.size(); i += 3) sources.push_back(registry[i]);
    const std::uint64_t size = BuildMap(g_singleton, sources, rng);
    // The embedded key's set sentinel was copied with the key, so its canonical form must match.
    for (auto& entry : g_entries) {
        const std::string text = CanonKey(entry.bytes + kMapNodeKey);
        assert(g_reference.count(text));
    }

    static volatile unsigned char ready = 0;
    static volatile std::uintptr_t registrySlot = Registry(registry.size(), 0);
    sb::context().ready_marker = &ready;
    g_original = &MockOriginal;
    g_registrySlot = &registrySlot;
    g_state = new State();

    // Verify mode: every answer compared, original returned.
    g_verifyOnly = true;
    ResetStats();
    for (Node* n : registry) {
        const std::uint64_t expected = MockOriginal(nullptr, n);
        assert(SB_TriggerIntern(g_singleton, n) == expected);
    }
    assert(S(&EU4SB_TriggerInternStats.mismatches) == 0);
    assert(S(&EU4SB_TriggerInternStats.verifications) == static_cast<LONG64>(registry.size()));
    assert(S(&EU4SB_TriggerInternStats.duplicateKeys) == 1);
    assert(S(&EU4SB_TriggerInternStats.cacheDisabled) == 0);
    assert(S(&EU4SB_TriggerInternMap.requests) == static_cast<LONG64>(size));
    assert(S(&EU4SB_TriggerInternMap.hits) == static_cast<LONG64>(size));

    // On mode: answers from the index, sampled checks only.
    g_verifyOnly = false;
    ResetStats();
    std::size_t nonNull = 0;
    for (Node* n : registry) {
        const std::uint64_t expected = MockOriginal(nullptr, n);
        assert(SB_TriggerIntern(g_singleton, n) == expected);
        nonNull += expected != 0;
    }
    assert(S(&EU4SB_TriggerInternStats.mismatches) == 0);
    assert(static_cast<std::size_t>(S(&EU4SB_TriggerInternStats.hits)) <= nonNull);
    assert(S(&EU4SB_TriggerInternStats.verifications) >= 1);
    assert(S(&EU4SB_TriggerInternStats.duplicateKeys) == 0);  // index reused

    // A changed registry forces a rebuild; a corrupted value is caught by the sampled check.
    registrySlot = Registry(0, 0);  // presize too small: the memo grows
    ResetStats();
    g_answerSequence = 0;
    SB_TriggerIntern(g_singleton, registry[0]);
    assert(S(&EU4SB_TriggerInternStats.duplicateKeys) == 1);
    std::uint64_t bogus = 0xDEAD;
    std::memcpy(g_entries.front().bytes + kMapNodeValue, &bogus, sizeof(bogus));
    registrySlot = Registry(registry.size() / 2, registry.size() / 2);  // rebuild index with the corrupted value
    g_answerSequence = 0;   // next call is sampled
    Node* target = nullptr;
    for (Node* n : registry) {
        if (Canon(n) == CanonKey(g_entries.front().bytes + kMapNodeKey)) { target = n; break; }
    }
    assert(target != nullptr);
    assert(SB_TriggerIntern(g_singleton, target) == MockOriginal(nullptr, target));
    assert(S(&EU4SB_TriggerInternStats.mismatches) == 1);
    assert(S(&EU4SB_TriggerInternStats.cacheDisabled) == 1);
    // Disabled: everything goes to the original.
    const LONG64 skipsBefore = S(&EU4SB_TriggerInternStats.skips);
    SB_TriggerIntern(g_singleton, registry[1]);
    assert(S(&EU4SB_TriggerInternStats.skips) == skipsBefore + 1);

    // Ready marker: original.
    ResetStats();
    ready = 1;
    SB_TriggerIntern(g_singleton, registry[2]);
    assert(S(&EU4SB_TriggerInternStats.readyDisabled) == 1 && S(&EU4SB_TriggerInternStats.skips) == 1);

    // Empty map (the observed game configuration): every answer is 0 without building node ids.
    ready = 0;
    ResetStats();
    g_answerSequence = 0;
    alignas(8) static unsigned char emptySingleton[0x90] = {};
    ListNode* emptyHead = NewList();
    const std::uint64_t zero = 0;
    std::memcpy(emptySingleton + kMapListHead, &emptyHead, sizeof(emptyHead));
    std::memcpy(emptySingleton + kMapSize, &zero, sizeof(zero));
    g_reference.clear();
    for (Node* n : registry) assert(SB_TriggerIntern(emptySingleton, n) == 0);
    assert(S(&EU4SB_TriggerInternStats.mismatches) == 0 && S(&EU4SB_TriggerInternStats.cacheDisabled) == 0);
    assert(S(&EU4SB_TriggerInternStats.misses) == static_cast<LONG64>(registry.size()));
    assert(S(&EU4SB_TriggerInternStats.verifications) >= 1);
    assert(S(&EU4SB_TriggerInternMap.requests) == 0 && g_state->memo.used == 0);

    // Non-empty maps: shared subtrees, duplicate children, foreign nodes at
    // several depths, keys that are substructures of other keys.
    HandBuiltScenario(registrySlot);
    for (std::uint32_t seed = 1; seed <= 4; ++seed) RandomDagScenario(seed, registrySlot);

    std::printf("trigger intern fixture ok: %zu registered nodes, %llu map keys, %zu non-null\n", registry.size(),
                static_cast<unsigned long long>(size), nonNull);
    return 0;
}
