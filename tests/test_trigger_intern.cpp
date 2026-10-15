// Fixture for startup_trigger_intern.inc: builds trigger nodes and a key map in
// the game's memory layout and compares EU4LabTriggerIntern with a naive
// reference of 0xB9B4E0 (structural key equality, children as sets); no code
// patching.
// Ported from the lab (scripts/native/test_startup_trigger_intern.cpp): same scenarios, module source
// included directly so its internals are visible to the test.
#include "../src/modules/trigger_intern.cpp"
#include <cassert>
#include <cstdio>
#include <deque>
#include <map>
#include <random>
#include <set>

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

// Canonical text of a structure: "id(sorted unique children)".
std::string Canon(const Node* node) {
    std::set<std::string> children;
    for (const Node* child : node->children) children.insert(Canon(child));
    std::string text = std::to_string(node->Id()) + "(";
    for (const auto& child : children) text += child + ",";
    return text + ")";
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
    // Map keys: structures of every third registered node plus one unrelated key.
    ListNode* mapHead = NewList();
    std::uint64_t size = 0;
    std::uint64_t nextValue = 0x1000;
    for (std::size_t i = 0; i < registry.size(); i += 3) {
        const std::string canon = Canon(registry[i]);
        if (g_reference.count(canon)) continue;
        g_entries.emplace_back();
        MapEntry* entry = &g_entries.back();
        const void* key = BuildKey(registry[i], rng);
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
    // The embedded key's set sentinel was copied with the key, so its canonical form must match.
    for (auto& entry : g_entries) {
        const std::string text = CanonKey(entry.bytes + kMapNodeKey);
        assert(g_reference.count(text));
    }
    std::memcpy(g_singleton + kMapListHead, &mapHead, sizeof(mapHead));
    std::memcpy(g_singleton + kMapSize, &size, sizeof(size));

    static volatile unsigned char ready = 0;
    static volatile std::uintptr_t registrySlot = 0x5000;
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
    registrySlot = 0x6000;
    ResetStats();
    g_answerSequence = 0;
    SB_TriggerIntern(g_singleton, registry[0]);
    assert(S(&EU4SB_TriggerInternStats.duplicateKeys) == 1);
    std::uint64_t bogus = 0xDEAD;
    std::memcpy(g_entries.front().bytes + kMapNodeValue, &bogus, sizeof(bogus));
    registrySlot = 0x7000;  // rebuild index with the corrupted value
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
    assert(S(&EU4SB_TriggerInternMap.requests) == 0 && g_state->nodeIds.empty());

    std::printf("trigger intern fixture ok: %zu registered nodes, %llu map keys, %zu non-null\n", registry.size(),
                static_cast<unsigned long long>(size), nonNull);
    return 0;
}
