// MergeFast against MergeReference (the game's loop written out): same entries, order, count,
// capacity and sequence of capacity changes, on random lists of every shape the game uses.
#include "../src/modules/modifier_merge_core.hpp"

#include <cassert>
#include <cstdio>
#include <random>

namespace {

std::mt19937 g_rng(12345);

int Rand(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(g_rng); }

// A random id: mostly static, some dynamic, some 0x286, a few repeats come from the small range.
std::int32_t RandomId() {
    switch (Rand(0, 9)) {
    case 0: return sb::kModStaticEnd;
    case 1: case 2: case 3: return Rand(sb::kModStaticEnd + 1, sb::kModStaticEnd + 60);
    default: return Rand(0, sb::kModStaticEnd - 1);
    }
}

sb::VectorModList RandomDest(bool dense) {
    sb::VectorModList d;
    d.is_dense = dense;
    const int cap = dense ? sb::kModStaticEnd + Rand(0, 64) : Rand(0, 80);
    d.data.assign(2 * static_cast<std::size_t>(cap), 0);
    if (dense) {
        for (int j = 0; j < sb::kModStaticEnd; ++j) {
            d.data[2 * j] = Rand(0, 3) == 0 ? sb::kModStaticEnd : j;  // unset static slots hold 0x286
            d.data[2 * j + 1] = Rand(-50, 50);
        }
        d.n = sb::kModStaticEnd + Rand(0, cap - sb::kModStaticEnd);
        for (int j = sb::kModStaticEnd; j < d.n; ++j) {
            d.data[2 * j] = Rand(sb::kModStaticEnd + 1, sb::kModStaticEnd + 60);
            d.data[2 * j + 1] = Rand(-50, 50);
        }
    } else {
        d.n = Rand(0, cap);
        for (int j = 0; j < d.n; ++j) {
            d.data[2 * j] = RandomId();
            d.data[2 * j + 1] = Rand(-50, 50);
        }
    }
    return d;
}

std::vector<std::int32_t> RandomSource(bool dense, int count) {
    std::vector<std::int32_t> s(2 * static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        s[2 * i] = dense && i < sb::kModStaticEnd ? i : RandomId();
        s[2 * i + 1] = Rand(-1000, 1000);
    }
    return s;
}

bool Same(const sb::VectorModList& a, const sb::VectorModList& b) {
    return a.n == b.n && a.data == b.data && a.growths == b.growths;
}

}  // namespace

int main() {
    sb::ModIndex index;
    int compared = 0;
    for (int round = 0; round < 4000; ++round) {
        const bool destDense = Rand(0, 1) == 1;
        const bool srcDense = Rand(0, 1) == 1;
        const int count = srcDense ? sb::kModStaticEnd + Rand(1, 60) : Rand(1, 120);
        const std::vector<std::int32_t> entries = RandomSource(srcDense, count);
        const sb::ModSource src{srcDense, entries.data(), count};
        sb::VectorModList ref = RandomDest(destDense);
        sb::VectorModList fast = ref;
        if (!sb::MergeFastApplies(fast, src)) continue;
        sb::MergeReference(ref, src);
        sb::MergeFast(fast, src, index);
        assert(Same(ref, fast));
        ++compared;
    }
    assert(compared > 3000);

    // The game's capacity rule: 0 -> 1 -> 2 -> 4 ... and count + 1 when doubling is not enough.
    {
        sb::VectorModList d;
        const std::int32_t e[] = {700, 1, 701, 2, 702, 3};
        sb::MergeFast(d, {false, e, 3}, index);
        assert(d.n == 3 && (d.growths == std::vector<std::int32_t>{1, 2, 4}));
    }
    // A repeated source id is added to the entry appended by its first occurrence.
    {
        sb::VectorModList d;
        const std::int32_t e[] = {700, 5, 700, 7};
        sb::MergeFast(d, {false, e, 2}, index);
        assert(d.n == 1 && d.data[0] == 700 && d.data[1] == 12);
    }
    // Ids the index does not cover, and a dense destination with a dynamic id below 0x286, go to the game.
    {
        sb::VectorModList d;
        const std::int32_t negative[] = {-3, 1};
        assert(!sb::MergeFastApplies(d, {false, negative, 1}));
        const std::int32_t large[] = {sb::kModMaxId, 1};
        assert(!sb::MergeFastApplies(d, {false, large, 1}));
        sb::VectorModList dense = RandomDest(true);
        dense.data[2 * 7] = 900;
        const std::int32_t one[] = {1, 1};
        assert(!sb::MergeFastApplies(dense, {false, one, 1}));
    }
    // ScanWork counts the scans of the entries appended during the call: 700 new ids into an empty list.
    {
        sb::VectorModList d;
        std::vector<std::int32_t> e;
        for (int i = 0; i < 700; ++i) e.insert(e.end(), {1000 + i, 1});
        assert(sb::ScanWork(d, {false, e.data(), 700}) == 700LL * 699 / 2);
        std::int32_t id = 0;
        const std::int32_t bad[] = {5, 1, -9, 1};
        assert(sb::FirstUncoveredId({false, bad, 2}, &id) && id == -9);
    }
    // The index survives generation wrap-around.
    {
        index.generation = 0xFFFFFFFFu;
        sb::VectorModList d;
        const std::int32_t e[] = {800, 1};
        sb::MergeFast(d, {false, e, 1}, index);
        sb::MergeFast(d, {false, e, 1}, index);
        assert(d.n == 1 && d.data[1] == 2 && index.generation == 2);
    }
    std::printf("modifier_merge ok (%d random merges compared)\n", compared);
    return 0;
}
