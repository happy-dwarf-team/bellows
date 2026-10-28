// Fixture for metascript_blocks.cpp: MSVC-layout strings, the expanded
// script's block container and the argument container in the game's layout, a
// reference of 0xDB1410 (blocks from last to first, "argument present" by a
// linear search, body kept when that differs from the negation flag, prefix +
// body-or-nothing + suffix with strlen on each part and on the result) and a
// mock of the game's assign; no code patching. The module source is included
// directly so its internals are visible to the test.
#include "../src/modules/metascript_blocks.cpp"
#include <cassert>
#include <cstdio>
#include <deque>
#include <random>
#include <stdexcept>

namespace {

// MSVC std::string: 16-byte buffer/pointer, size +0x10, capacity +0x18.
struct alignas(8) MsString {
    unsigned char bytes[0x20] = {};
};

std::deque<std::string> g_heap;  // owns heap buffers of MsString

void Set(MsString& s, const std::string& value) {
    const std::size_t size = value.size();
    std::size_t capacity = 15;
    if (size <= 15) {
        std::memset(s.bytes, 0, 16);
        std::memcpy(s.bytes, value.data(), size);
    } else {
        g_heap.push_back(value);
        g_heap.back().push_back('\0');
        const char* p = g_heap.back().data();
        std::memcpy(s.bytes, &p, sizeof(p));
        capacity = size;
    }
    std::memcpy(s.bytes + 0x10, &size, sizeof(size));
    std::memcpy(s.bytes + 0x18, &capacity, sizeof(capacity));
}

std::string Get(const MsString& s) {
    const View v = StringView(s.bytes);
    return std::string(v.data, v.size);
}

struct alignas(8) Arg {
    MsString key, value;
};
static_assert(sizeof(Arg) == kArgStride, "arg stride");

struct alignas(8) Block {
    MsString name, body;
    std::int32_t start = 0, end = 0;
    unsigned char negate = 0;
};
static_assert(sizeof(Block) == kBlockStride, "block stride");
static_assert(offsetof(Block, body) == kBlockBody && offsetof(Block, start) == kBlockStart, "block layout");
static_assert(offsetof(Block, end) == kBlockEnd && offsetof(Block, negate) == kBlockNegate, "block layout");

struct alignas(8) Args {
    unsigned char bytes[0x18] = {};
    std::vector<Arg> items;
    void Sync() {
        Arg* data = items.data();
        const std::int32_t count = static_cast<std::int32_t>(items.size());
        std::memcpy(bytes + kVecData, &data, sizeof(data));
        std::memcpy(bytes + kVecCount, &count, sizeof(count));
    }
};

// The expanded script: only its block container (+0x70) is read.
struct alignas(8) Script {
    unsigned char bytes[0x88] = {};
    std::vector<Block> items;
    void Sync() {
        Block* data = items.data();
        const std::int32_t count = static_cast<std::int32_t>(items.size());
        std::memcpy(bytes + kBlockData, &data, sizeof(data));
        std::memcpy(bytes + kBlockCount, &count, sizeof(count));
    }
};

void* MockAssign(void* s, const char* data, std::size_t size) {
    Set(*static_cast<MsString*>(s), std::string(data, size));
    return s;
}

std::string Cut(const std::string& s) { return s.substr(0, std::strlen(s.c_str())); }

int g_originalCalls = 0;

// Reference of 0xDB1410.
void MockOriginal(void* scriptPtr, void* textPtr, void* argsPtr) {
    ++g_originalCalls;
    auto& script = *static_cast<Script*>(scriptPtr);
    auto& text = *static_cast<MsString*>(textPtr);
    auto& args = *static_cast<Args*>(argsPtr);
    std::string current = Get(text);
    for (int i = static_cast<int>(script.items.size()) - 1; i >= 0; --i) {
        const Block& b = script.items[static_cast<std::size_t>(i)];
        unsigned char present = 0;
        for (const Arg& a : args.items) {
            if (Get(a.key) == Get(b.name)) {
                present = 1;
                break;
            }
        }
        const std::size_t start = b.start < 0 ? current.size() : std::min<std::size_t>(b.start, current.size());
        std::string result = Cut(current.substr(0, start));
        if (b.negate != present) result += Get(b.body);
        const std::size_t end = b.end < 0 ? 0 : static_cast<std::size_t>(b.end);
        if (end > current.size()) throw std::out_of_range("suffix");
        result += Cut(current.substr(end));
        current = Cut(result);
        Set(text, current);
    }
}

LONG64 S(volatile LONG64* v) { return InterlockedCompareExchange64(v, 0, 0); }
LONG64 S(volatile LONG* v) { return InterlockedCompareExchange(v, 0, 0); }

sb::Stats& Stats() { return EU4SB_MetascriptBlocksStats; }

void ResetStats() {
    Stats().requests = Stats().hits = Stats().misses = Stats().skips = 0;
    Stats().verifications = Stats().mismatches = Stats().duplicateKeys = 0;
    Stats().cacheDisabled = Stats().readyDisabled = 0;
}

struct Case {
    std::string text;
    Args args;
    Script script;
    void Sync() {
        args.Sync();
        script.Sync();
    }
};

void AddArg(Case& c, const std::string& key, const std::string& value) {
    Arg a;
    Set(a.key, key);
    Set(a.value, value);
    c.args.items.push_back(a);
}

// Appends "[[name] body ]" (or "[[!name] ...]") to the text and its block.
void AddBlock(Case& c, const std::string& name, const std::string& body, bool negate) {
    Block b;
    Set(b.name, name);
    Set(b.body, body);
    b.negate = negate ? 1 : 0;
    b.start = static_cast<std::int32_t>(c.text.size());
    c.text += "[[" + std::string(negate ? "!" : "") + name + "]" + body + "]";
    b.end = static_cast<std::int32_t>(c.text.size());
    c.script.items.push_back(b);
}

// Random scripted body with conditional blocks; arguments may repeat a key
// (presence only matters) or miss a block's parameter.
Case Random(std::mt19937& rng) {
    static const char* names[] = {"who", "amount", "tag", "x", "long_parameter_name", "missing"};
    Case c;
    const int argCount = static_cast<int>(rng() % 5);
    for (int i = 0; i < argCount; ++i) AddArg(c, names[rng() % 5], std::to_string(rng() % 1000));
    const int pieces = static_cast<int>(rng() % 12);
    for (int i = 0; i < pieces; ++i) {
        c.text += std::string(rng() % 40, 'a' + static_cast<char>(rng() % 26));
        const std::string body = (rng() % 3 == 0) ? std::string(20 + rng() % 60, 'b') : " add_prestige = $amount$ ";
        AddBlock(c, names[rng() % 6], body, rng() % 4 == 0);
    }
    c.text += std::string(rng() % 20, 'z');
    c.Sync();
    return c;
}

std::string RunOriginal(Case c) {
    MsString text;
    Set(text, c.text);
    c.Sync();
    MockOriginal(c.script.bytes, &text, c.args.bytes);
    return Get(text);
}

std::string RunModule(Case& c) {
    MsString text;
    Set(text, c.text);
    SB_MetascriptBlocks(c.script.bytes, &text, c.args.bytes);
    return Get(text);
}

// The module's answer equals the reference and the call went the expected way.
void Expect(Case& c, volatile LONG64* counter, const std::string& expected) {
    c.Sync();
    const LONG64 before = S(counter);
    assert(RunOriginal(c) == expected);
    assert(RunModule(c) == expected);
    assert(S(counter) == before + 1);
}

}  // namespace

int main() {
    volatile LONG& ready = sb::g_gameReady;  // set by the anvil-game ready event in the plugin
    ready = 0;
    g_original = &MockOriginal;
    g_assign = &MockAssign;
    std::mt19937 rng(1410);

    // Verify mode: every call with blocks compared, result equals the reference.
    g_verifyOnly = true;
    ResetStats();
    LONG64 withBlocks = 0;
    for (int i = 0; i < 3000; ++i) {
        Case c = Random(rng);
        withBlocks += c.script.items.empty() ? 0 : 1;
        const std::string expected = RunOriginal(c);
        assert(RunModule(c) == expected);
    }
    assert(S(&Stats().mismatches) == 0 && S(&Stats().skips) == 0 && S(&Stats().verifications) == withBlocks);
    assert(S(&Stats().misses) == 3000 - withBlocks);

    // On mode: results from the single pass, sampled checks.
    g_verifyOnly = false;
    ResetStats();
    withBlocks = 0;
    for (int i = 0; i < 3000; ++i) {
        Case c = Random(rng);
        withBlocks += c.script.items.empty() ? 0 : 1;
        const std::string expected = RunOriginal(c);
        assert(RunModule(c) == expected);
    }
    assert(S(&Stats().mismatches) == 0 && S(&Stats().hits) == withBlocks && S(&Stats().skips) == 0);
    assert(S(&Stats().verifications) >= withBlocks / 256);

    // Presence, negation, a missing argument and a repeated key, by hand.
    {
        Case c;
        c.text = "a ";
        AddBlock(c, "who", "W", false);   // present: kept
        c.text += " b ";
        AddBlock(c, "who", "N", true);    // present, negated: dropped
        c.text += " c ";
        AddBlock(c, "tag", "T", false);   // missing: dropped
        c.text += " d ";
        AddBlock(c, "tag", "M", true);    // missing, negated: kept
        c.text += " e";
        AddArg(c, "who", "1");
        AddArg(c, "who", "");             // repeated key: still present
        Expect(c, &Stats().hits, "a W b  c  d M e");
        assert(S(&Stats().duplicateKeys) >= 4);
    }
    // An argument whose key only shares a prefix with the parameter is not a match.
    {
        Case c;
        AddBlock(c, "who", "W", false);
        AddArg(c, "whom", "1");
        AddArg(c, "wh", "1");
        Expect(c, &Stats().hits, "");
    }
    // No blocks: the original, which leaves the text as it is.
    {
        Case c;
        c.text = "no blocks [[x] here";
        AddArg(c, "x", "1");
        Expect(c, &Stats().misses, "no blocks [[x] here");
    }
    // A dropped body may hold a NUL: it never reaches the text.
    {
        Case c;
        c.text = "a";
        AddBlock(c, "x", "1_2", false);
        Set(c.script.items.back().body, std::string("1\0" "2", 3));  // the text keeps a NUL-free spelling
        c.text += "b";
        Expect(c, &Stats().hits, "ab");
    }

    // Preconditions: overlapping or unsorted blocks, NUL in the text or a kept body -> original.
    {
        Case c;
        c.text = "abc[[x]1]def[[y]2]";
        AddArg(c, "y", "1");
        Block a, b;
        Set(a.name, "x");
        Set(a.body, "ONE");
        a.start = 3;
        a.end = 9;
        Set(b.name, "y");
        Set(b.body, "TWO");
        b.start = 8;  // overlaps [3, 9)
        b.end = 18;
        c.script.items = {a, b};
        const int calls = g_originalCalls;
        Expect(c, &Stats().skips, "abcWO");
        assert(g_originalCalls == calls + 2);
        // Unsorted: [12, 18) before [3, 9); the kept body has the block's length, so the original stays in range.
        AddArg(c, "x", "1");
        Set(a.body, "XXXXXX");
        b.start = 12;
        c.script.items = {b, a};
        Expect(c, &Stats().skips, "abcXXXXXXdefTWO");
    }
    {
        Case c;
        c.text = std::string("ab\0c", 4);
        AddBlock(c, "x", "1", false);
        AddArg(c, "x", "1");
        Expect(c, &Stats().skips, "ab1");
    }
    {
        Case c;
        c.text = "a";
        AddBlock(c, "x", "1_2", false);
        Set(c.script.items.back().body, std::string("1\0" "2", 3));  // the text keeps a NUL-free spelling
        c.text += "b";
        AddArg(c, "x", "1");
        Expect(c, &Stats().skips, "a1");
    }

    // A sample that falls on a slow call (blocks x length above the limit) moves to the next cheap call.
    ResetStats();
    g_sequence = 0;
    g_samplePending = 0;
    {
        Case big;
        for (int i = 0; i < 20; ++i) {
            big.text += std::string(50000, 'a');
            AddBlock(big, i % 2 ? "x" : "y", "kept", false);
        }
        AddArg(big, "x", "1");
        big.Sync();
        assert(static_cast<std::uint64_t>(big.script.items.size()) * big.text.size() > kSampleWorkLimit);
        const int calls = g_originalCalls;
        const std::string expected = RunOriginal(big);
        assert(RunModule(big) == expected);  // sequence 1: sampled, but not compared
        assert(g_originalCalls == calls + 1 && S(&Stats().verifications) == 0 && S(&Stats().hits) == 1);
        Case small = Random(rng);
        while (small.script.items.empty()) small = Random(rng);
        assert(RunModule(small) == RunOriginal(small));
        assert(S(&Stats().verifications) == 1 && S(&Stats().hits) == 2 && g_samplePending == 0);
    }

    // A sampled disagreement keeps the original's result and disables the module.
    ResetStats();
    g_original = [](void*, void* text, void*) { Set(*static_cast<MsString*>(text), "other"); };
    g_sequence = 0;
    {
        Case c;
        c.text = "a";
        AddBlock(c, "x", "1", false);
        c.Sync();
        assert(RunModule(c) == "other");
        assert(S(&Stats().mismatches) == 1 && S(&Stats().cacheDisabled) == 1);
        const LONG64 skips = S(&Stats().skips);
        RunModule(c);
        assert(S(&Stats().skips) == skips + 1);
    }

    // Ready marker: original only.
    ResetStats();
    g_original = &MockOriginal;
    ready = 1;
    {
        Case c;
        c.text = "a";
        AddBlock(c, "x", "1", false);
        c.Sync();
        assert(RunModule(c) == RunOriginal(c));
        assert(S(&Stats().readyDisabled) == 1 && S(&Stats().skips) == 1);
    }
    std::printf("metascript blocks fixture ok\n");
    return 0;
}
