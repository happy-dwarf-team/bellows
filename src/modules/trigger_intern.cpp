// Trigger interning at the end of loading (ported from the lab's
// startup_trigger_intern.inc; docs in the lab: docs/startup-trigger-intern.md).
//
// Every trigger node created while loading registers itself in a registry.
// Near the end of loading the game walks both registry lists and sets
// node+0x58 = intern(singleton, node). The original deep-copies the node's whole
// subtree into a key (id = node+0x50, children as a deduplicated set), hashes
// it, looks it up in the singleton's map and returns the entry's value or 0.
// In 1.37.5 with the tested mods the map is empty, so 2.35 M calls each build a
// key only to find nothing.
//
// Both call sites are redirected (Anvil redirect_call + byte checks around the
// call) to SB_TriggerIntern: an empty map answers 0 immediately; otherwise
// every structure gets a canonical id by hash-consing (id + sorted unique child
// ids), memoized per node pointer, and the map keys are indexed the same way.
// "verify" = compute both, return the original's value, count mismatches;
// "on" = return the index answer, compare every 256th call and disable on a
// mismatch. A changed map size or registry, an unexpected shape, the ready
// marker or any exception send the call to the original.
#include "modules.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace {

constexpr std::size_t kMaxDepth = 4096;

// Game layouts (eu4.exe 1.37.5).
constexpr std::size_t kNodeChildren = 0x08;  // node**: child array
constexpr std::size_t kNodeCount = 0x14;     // int: number of children
constexpr std::size_t kNodeId = 0x50;        // int
constexpr std::size_t kMapListHead = 0x50;   // singleton + 0x48 map, +0x50 list sentinel
constexpr std::size_t kMapSize = 0x58;       // size_t
constexpr std::size_t kMapNodeKey = 0x10;    // key embedded in a map node
constexpr std::size_t kMapNodeValue = 0x58;  // value of a map entry
constexpr std::size_t kKeyId = 0x00;         // int
constexpr std::size_t kKeySetHead = 0x10;    // key + 8 set, +0x10 list sentinel
constexpr std::size_t kSetNodeValue = 0x10;  // unique_ptr<key> in a set node

using InternFn = std::uint64_t (*)(void*, void*);

struct VectorHash {
    std::size_t operator()(const std::vector<std::uint32_t>& v) const noexcept {
        std::uint64_t h = 0xcbf29ce484222325ULL;
        for (std::uint32_t x : v) {
            h ^= x;
            h *= 0x100000001b3ULL;
        }
        return static_cast<std::size_t>(h);
    }
};

struct State {
    std::unordered_map<std::vector<std::uint32_t>, std::uint32_t, VectorHash> structures;
    std::unordered_map<const void*, std::uint32_t> nodeIds;   // trigger node -> structure id
    std::unordered_map<std::uint32_t, std::uint64_t> values;  // structure id -> map value
    const void* singleton = nullptr;
    std::uint64_t mapSize = 0;
    std::uintptr_t registry = 0;
    bool usable = false;
};

InternFn g_original = nullptr;
volatile std::uintptr_t* g_registrySlot = nullptr;
bool g_verifyOnly = false;
SRWLOCK g_lock = SRWLOCK_INIT;
State* g_state = nullptr;
volatile LONG64 g_answerSequence = 0;

struct ShapeError {};

}  // namespace

extern "C" {
// requests = calls, hits = non-null answers, misses = null answers, skips =
// passed to the original, duplicateKeys = index builds, verifications /
// mismatches = comparisons with the original, mode = 1 on / 2 verify,
// cacheDisabled = disabled after a mismatch or a shape error.
__declspec(dllexport) sb::Stats EU4SB_TriggerInternStats = SB_STATS_INIT;
// Last index build: requests = size of the singleton's key map, hits = structures indexed.
__declspec(dllexport) sb::Stats EU4SB_TriggerInternMap = SB_STATS_INIT;
}

namespace {

using sb::Read;

std::uint32_t Intern(State& state, std::vector<std::uint32_t>&& shape) {
    const auto next = static_cast<std::uint32_t>(state.structures.size());
    return state.structures.emplace(std::move(shape), next).first->second;
}

std::uint32_t Finish(State& state, std::int32_t id, std::vector<std::uint32_t>& children) {
    std::sort(children.begin(), children.end());
    children.erase(std::unique(children.begin(), children.end()), children.end());
    std::vector<std::uint32_t> shape;
    shape.reserve(children.size() + 1);
    shape.push_back(static_cast<std::uint32_t>(id));
    shape.insert(shape.end(), children.begin(), children.end());
    return Intern(state, std::move(shape));
}

// Canonical id of a key stored in the map (key -> set of unique_ptr<key>).
std::uint32_t KeyId(State& state, const void* key, std::size_t depth) {
    if (key == nullptr || depth > kMaxDepth) throw ShapeError{};
    std::vector<std::uint32_t> children;
    const void* head = Read<const void*>(key, kKeySetHead);
    if (head == nullptr) throw ShapeError{};
    for (const void* entry = Read<const void*>(head, 0); entry != head; entry = Read<const void*>(entry, 0)) {
        if (entry == nullptr) throw ShapeError{};
        children.push_back(KeyId(state, Read<const void*>(entry, kSetNodeValue), depth + 1));
    }
    return Finish(state, Read<std::int32_t>(key, kKeyId), children);
}

// Canonical id of a trigger node, memoized by node pointer.
std::uint32_t NodeId(State& state, const void* node, std::size_t depth) {
    if (node == nullptr || depth > kMaxDepth) throw ShapeError{};
    const auto known = state.nodeIds.find(node);
    if (known != state.nodeIds.end()) return known->second;
    const auto* const* array = Read<const void* const*>(node, kNodeChildren);
    const std::int32_t count = Read<std::int32_t>(node, kNodeCount);
    if (count < 0 || (count > 0 && array == nullptr)) throw ShapeError{};
    std::vector<std::uint32_t> children;
    children.reserve(static_cast<std::size_t>(count));
    for (std::int32_t index = 0; index < count; ++index) {
        children.push_back(NodeId(state, array[index], depth + 1));
    }
    const std::uint32_t id = Finish(state, Read<std::int32_t>(node, kNodeId), children);
    state.nodeIds.emplace(node, id);
    return id;
}

// (Re)build the index when the singleton, the map size or the registry changed.
State& Prepare(void* singleton) {
    const std::uint64_t mapSize = Read<std::uint64_t>(singleton, kMapSize);
    const std::uintptr_t registry = g_registrySlot ? *g_registrySlot : 0;
    State& state = *g_state;
    if (state.usable && state.singleton == singleton && state.mapSize == mapSize && state.registry == registry) {
        return state;
    }
    state = State{};
    state.singleton = singleton;
    state.mapSize = mapSize;
    state.registry = registry;
    const void* head = Read<const void*>(singleton, kMapListHead);
    if (head == nullptr) throw ShapeError{};
    std::uint64_t seen = 0;
    for (const void* entry = Read<const void*>(head, 0); entry != head; entry = Read<const void*>(entry, 0)) {
        if (entry == nullptr || ++seen > mapSize) throw ShapeError{};
        const std::uint32_t id = KeyId(state, static_cast<const unsigned char*>(entry) + kMapNodeKey, 0);
        // Keys in the game's map are unique; a duplicate structure would mean a model error.
        if (!state.values.emplace(id, Read<std::uint64_t>(entry, kMapNodeValue)).second) throw ShapeError{};
    }
    if (seen != mapSize) throw ShapeError{};
    sb::Increment(&EU4SB_TriggerInternStats.duplicateKeys);  // number of index builds
    InterlockedExchange64(&EU4SB_TriggerInternMap.requests, static_cast<LONG64>(mapSize));
    InterlockedExchange64(&EU4SB_TriggerInternMap.hits, static_cast<LONG64>(state.values.size()));
    state.usable = true;
    return state;
}

bool Disabled() noexcept {
    if (InterlockedCompareExchange(&EU4SB_TriggerInternStats.cacheDisabled, 0, 0) != 0) return true;
    if (InterlockedCompareExchange(&EU4SB_TriggerInternStats.readyDisabled, 0, 0) != 0) return true;
    if (sb::ReadyReached()) {
        InterlockedExchange(&EU4SB_TriggerInternStats.readyDisabled, 1);
        return true;
    }
    return false;
}

}  // namespace

extern "C" std::uint64_t SB_TriggerIntern(void* singleton, void* node) {
    sb::Increment(&EU4SB_TriggerInternStats.requests);
    if (Disabled() || g_state == nullptr || singleton == nullptr) {
        sb::Increment(&EU4SB_TriggerInternStats.skips);
        return g_original(singleton, node);
    }
    std::uint64_t answer = 0;
    try {
        AcquireSRWLockExclusive(&g_lock);
        try {
            State& state = Prepare(singleton);
            // An empty map cannot contain the key: the original returns 0 after building it.
            if (!state.values.empty()) {
                const auto found = state.values.find(NodeId(state, node, 0));
                answer = found == state.values.end() ? 0 : found->second;
            }
        } catch (...) {
            g_state->usable = false;
            ReleaseSRWLockExclusive(&g_lock);
            throw;
        }
        ReleaseSRWLockExclusive(&g_lock);
    } catch (...) {
        InterlockedExchange(&EU4SB_TriggerInternStats.cacheDisabled, 1);
        sb::Increment(&EU4SB_TriggerInternStats.skips);
        return g_original(singleton, node);
    }
    const LONG64 sequence = InterlockedIncrement64(&g_answerSequence);
    if (g_verifyOnly || (sequence & 255) == 1) {
        sb::Increment(&EU4SB_TriggerInternStats.verifications);
        const std::uint64_t actual = g_original(singleton, node);
        if (actual != answer) {
            sb::Increment(&EU4SB_TriggerInternStats.mismatches);
            if (!g_verifyOnly) InterlockedExchange(&EU4SB_TriggerInternStats.cacheDisabled, 1);
            return actual;
        }
        if (g_verifyOnly) return actual;
    }
    sb::Increment(answer ? &EU4SB_TriggerInternStats.hits : &EU4SB_TriggerInternStats.misses);
    return answer;
}

namespace sb {

bool InstallTriggerIntern(const Build& build, Mode mode) {
    if (mode == Mode::Off) return false;
    // Each site: "mov rdx,rdi; mov rcx,rax; call <intern>" followed by "mov [rdi+58h],rax".
    for (uint32_t site : build.trigger_sites) {
        const unsigned char* bytes = AtRva<const unsigned char>(site - 6);
        const unsigned char before[6] = {0x48, 0x8B, 0xD7, 0x48, 0x8B, 0xC8};
        const unsigned char after[4] = {0x48, 0x89, 0x47, 0x58};
        if (std::memcmp(bytes, before, sizeof(before)) != 0 || std::memcmp(bytes + 11, after, sizeof(after)) != 0) {
            Log(ANVIL_LOG_WARNING, "trigger_intern: unexpected code around call site; module off");
            return false;
        }
    }
    try {
        g_state = new State();
    } catch (...) {
        return false;
    }
    g_original = reinterpret_cast<InternFn>(AtRva<unsigned char>(build.trigger_intern_fn));
    g_registrySlot = AtRva<std::uintptr_t>(build.trigger_registry);
    g_verifyOnly = mode == Mode::Verify;
    const AnvilHost* host = context().host;
    for (uint32_t site : build.trigger_sites) {
        const AnvilResult r = host->redirect_call(context().self, site, build.trigger_intern_fn,
                                                  reinterpret_cast<void*>(&SB_TriggerIntern));
        if (r != ANVIL_OK) {
            // A redirected first site with a refused second one is still correct:
            // both targets compute the same values.
            Log(ANVIL_LOG_WARNING, "trigger_intern: call site refused (%s)", ResultName(r));
            return false;
        }
    }
    InterlockedExchange(&EU4SB_TriggerInternStats.mode, g_verifyOnly ? 2 : 1);
    InterlockedExchange(&EU4SB_TriggerInternStats.initialized, 1);
    return true;
}

void ReportTriggerIntern() {
    LogStats("trigger_intern", EU4SB_TriggerInternStats);
    Log(ANVIL_LOG_INFO, "trigger_intern: map size %lld, indexed %lld", EU4SB_TriggerInternMap.requests,
        EU4SB_TriggerInternMap.hits);
}

}  // namespace sb
