// Fixture for startup_metascript_subst.inc: MSVC-layout strings, argument and
// token containers in the game's layout, a reference of 0xDB1800 (tokens from
// last to first, linear first-match lookup, prefix + value + suffix, strlen on
// assign) and a mock of the game's assign; no code patching.
// Ported from the lab (scripts/native/test_startup_metascript_subst.cpp): same scenarios, module source
// included directly so its internals are visible to the test.
#include "../src/modules/metascript_subst.cpp"
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

struct alignas(8) Token {
    MsString name;
    std::int32_t start = 0, end = 0;
};
static_assert(sizeof(Token) == kTokenStride, "token stride");

template <typename T>
struct alignas(8) Vec {
    unsigned char bytes[0x18] = {};
    std::vector<T> items;
    void Sync() {
        T* data = items.data();
        const std::int32_t count = static_cast<std::int32_t>(items.size());
        std::memcpy(bytes + kVecData, &data, sizeof(data));
        std::memcpy(bytes + kVecCount, &count, sizeof(count));
    }
};

void* MockAssign(void* s, const char* data, std::size_t size) {
    Set(*static_cast<MsString*>(s), std::string(data, size));
    return s;
}

int g_originalCalls = 0;

// Reference of 0xDB1800.
void MockOriginal(void*, void* textPtr, void* argsPtr, void* tokensPtr) {
    ++g_originalCalls;
    auto& text = *static_cast<MsString*>(textPtr);
    auto& args = *static_cast<Vec<Arg>*>(argsPtr);
    auto& tokens = *static_cast<Vec<Token>*>(tokensPtr);
    std::string current = Get(text);
    for (int i = static_cast<int>(tokens.items.size()) - 1; i >= 0; --i) {
        const Token& t = tokens.items[static_cast<std::size_t>(i)];
        std::string value;
        for (const Arg& a : args.items) {
            if (Get(a.key) == Get(t.name)) {
                value = Get(a.value);
                value = value.substr(0, std::strlen(value.c_str()));
                break;
            }
        }
        const std::size_t start = t.start < 0 ? current.size() : std::min<std::size_t>(t.start, current.size());
        const std::size_t end = t.end < 0 ? 0 : static_cast<std::size_t>(t.end);
        if (end > current.size()) throw std::out_of_range("suffix");
        std::string prefix = current.substr(0, start);
        prefix = prefix.substr(0, std::strlen(prefix.c_str()));
        std::string suffix = current.substr(end);
        suffix = suffix.substr(0, std::strlen(suffix.c_str()));
        current = prefix + value + suffix;
    }
    current = current.substr(0, std::strlen(current.c_str()));
    Set(text, current);
}

LONG64 S(volatile LONG64* v) { return InterlockedCompareExchange64(v, 0, 0); }
LONG64 S(volatile LONG* v) { return InterlockedCompareExchange(v, 0, 0); }

void ResetStats() {
    EU4SB_MetascriptStats.requests = EU4SB_MetascriptStats.hits = EU4SB_MetascriptStats.skips = 0;
    EU4SB_MetascriptStats.verifications = EU4SB_MetascriptStats.mismatches = EU4SB_MetascriptStats.duplicateKeys = 0;
    EU4SB_MetascriptStats.cacheDisabled = EU4SB_MetascriptStats.readyDisabled = 0;
}

struct Case {
    std::string text;
    Vec<Arg> args;
    Vec<Token> tokens;
};

// Random scripted body with $name$ tokens (positions cover the whole $...$).
Case Random(std::mt19937& rng) {
    static const char* names[] = {"who", "amount", "tag", "x", "long_parameter_name", "missing"};
    Case c;
    const int argCount = static_cast<int>(rng() % 5);
    for (int i = 0; i < argCount; ++i) {
        Arg a;
        Set(a.key, names[rng() % 5]);
        std::string v = (rng() % 3 == 0) ? std::string(20 + rng() % 30, 'v') : std::to_string(rng() % 1000);
        Set(a.value, v);
        c.args.items.push_back(a);  // duplicate keys possible: first match wins
    }
    const int pieces = static_cast<int>(rng() % 12);
    for (int i = 0; i < pieces; ++i) {
        c.text += std::string(rng() % 40, 'a' + static_cast<char>(rng() % 26));
        const std::string name = names[rng() % 6];
        Token t;
        Set(t.name, name);
        t.start = static_cast<std::int32_t>(c.text.size());
        c.text += "$" + name + "$";
        t.end = static_cast<std::int32_t>(c.text.size());
        c.tokens.items.push_back(t);
    }
    c.text += std::string(rng() % 20, 'z');
    c.args.Sync();
    c.tokens.Sync();
    return c;
}

std::string RunOriginal(Case c) {
    MsString text;
    Set(text, c.text);
    c.args.Sync();
    c.tokens.Sync();
    MockOriginal(nullptr, &text, c.args.bytes, c.tokens.bytes);
    return Get(text);
}

std::string RunModule(Case& c) {
    MsString text;
    Set(text, c.text);
    SB_MetascriptSubst(nullptr, &text, c.args.bytes, c.tokens.bytes);
    return Get(text);
}

}  // namespace

int main() {
    static volatile unsigned char ready = 0;
    sb::context().ready_marker = &ready;
    g_original = &MockOriginal;
    g_assign = &MockAssign;
    std::mt19937 rng(77);

    // Verify mode: every call compared, result equals the reference.
    g_verifyOnly = true;
    ResetStats();
    for (int i = 0; i < 3000; ++i) {
        Case c = Random(rng);
        const std::string expected = RunOriginal(c);
        assert(RunModule(c) == expected);
    }
    assert(S(&EU4SB_MetascriptStats.mismatches) == 0 && S(&EU4SB_MetascriptStats.verifications) == 3000);

    // On mode: results from the single pass, sampled checks.
    g_verifyOnly = false;
    ResetStats();
    for (int i = 0; i < 3000; ++i) {
        Case c = Random(rng);
        const std::string expected = RunOriginal(c);
        assert(RunModule(c) == expected);
    }
    assert(S(&EU4SB_MetascriptStats.mismatches) == 0 && S(&EU4SB_MetascriptStats.hits) == 3000);
    assert(S(&EU4SB_MetascriptStats.verifications) >= 11);

    // Preconditions: overlapping/unsorted tokens, out-of-range end, NUL in text or value -> original.
    {
        // Overlapping tokens [3,6) and [4,9).
        Case c;
        c.text = "abc$x$def";
        Token a, b;
        Set(a.name, "x");
        a.start = 3;
        a.end = 6;
        Set(b.name, "y");
        b.start = 4;
        b.end = 9;
        c.tokens.items = {a, b};
        Arg y;
        Set(y.key, "y");
        Set(y.value, "LONGVALUE");
        c.args.items.push_back(y);
        c.args.Sync();
        c.tokens.Sync();
        const LONG64 skips = S(&EU4SB_MetascriptStats.skips);
        const int calls = g_originalCalls;
        const std::string expected = RunOriginal(c);
        assert(RunModule(c) == expected);
        assert(S(&EU4SB_MetascriptStats.skips) == skips + 1 && g_originalCalls == calls + 2);
    }
    {
        Case c;
        c.text = std::string("ab\0cd$x$", 8);
        Token t;
        Set(t.name, "x");
        t.start = 5;
        t.end = 8;
        c.tokens.items.push_back(t);
        c.args.Sync();
        c.tokens.Sync();
        const LONG64 skips = S(&EU4SB_MetascriptStats.skips);
        assert(RunModule(c) == RunOriginal(c));
        assert(S(&EU4SB_MetascriptStats.skips) == skips + 1);
    }
    {
        Case c;
        c.text = "abc$x$";
        Token t;
        Set(t.name, "x");
        t.start = 3;
        t.end = 6;
        c.tokens.items.push_back(t);
        Arg a;
        Set(a.key, "x");
        Set(a.value, std::string("1\0" "2", 3));
        c.args.items.push_back(a);
        c.args.Sync();
        c.tokens.Sync();
        const LONG64 skips = S(&EU4SB_MetascriptStats.skips);
        assert(RunModule(c) == RunOriginal(c));
        assert(S(&EU4SB_MetascriptStats.skips) == skips + 1);
    }

    // A sampled disagreement keeps the original's result and disables the module.
    ResetStats();
    g_original = [](void*, void* text, void*, void*) { Set(*static_cast<MsString*>(text), "other"); };
    g_sequence = 0;
    {
        Case c = Random(rng);
        assert(RunModule(c) == "other");
        assert(S(&EU4SB_MetascriptStats.mismatches) == 1 && S(&EU4SB_MetascriptStats.cacheDisabled) == 1);
        const LONG64 skips = S(&EU4SB_MetascriptStats.skips);
        RunModule(c);
        assert(S(&EU4SB_MetascriptStats.skips) == skips + 1);
    }

    // Ready marker: original only.
    ResetStats();
    g_original = &MockOriginal;
    ready = 1;
    {
        Case c = Random(rng);
        assert(RunModule(c) == RunOriginal(c));
        assert(S(&EU4SB_MetascriptStats.readyDisabled) == 1 && S(&EU4SB_MetascriptStats.skips) == 1);
    }
    std::printf("metascript substitution fixture ok\n");
    return 0;
}
