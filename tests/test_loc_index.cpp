// loc_index: the index answers what the game's walk answers, on stand-ins with the game's entry layout, while the
// array grows, moves, shrinks and is rebuilt. That the game builds the same table is a lab test ("verify").
#include "modules/loc_index_core.hpp"

#include <cassert>
#include <cstdio>
#include <random>
#include <vector>

namespace {

struct Entry {
    std::uint64_t hash;
    std::int32_t offsets[2];
    std::uint32_t tag;
    std::uint32_t other;
};
static_assert(sizeof(Entry) == sb::loc::kEntrySize);
static_assert(offsetof(Entry, hash) == sb::loc::kEntryHash);
static_assert(offsetof(Entry, tag) == sb::loc::kEntryTag);

const unsigned char* Bytes(const std::vector<Entry>& v) { return reinterpret_cast<const unsigned char*>(v.data()); }

// As the game: find, and append when not found.
std::size_t Store(sb::loc::Index& index, std::vector<Entry>& v, sb::loc::Key key, bool check = true) {
    const std::size_t found = index.Find(Bytes(v), v.size(), key);
    if (check) assert(found == sb::loc::Scan(Bytes(v), v.size(), key));
    if (found == v.size()) v.push_back(Entry{key.hash, {0, 0}, key.tag, 0});
    return found;
}

}  // namespace

int main() {
    using sb::loc::Key;
    sb::loc::Index index;
    std::vector<Entry> v;

    // Empty array: not found, then the first entry.
    assert(index.Find(Bytes(v), 0, Key{1, 0}) == 0);
    assert(Store(index, v, Key{1, 0}) == 0 && v.size() == 1);
    assert(Store(index, v, Key{1, 0}) == 0);
    // Same hash, another tag: a new entry; the first index of each key stays.
    assert(Store(index, v, Key{1, 7}) == 1 && v.size() == 2);
    v.push_back(Entry{1, {0, 0}, 0, 0});  // a later duplicate of key {1, 0}: the walk still finds index 0
    assert(Store(index, v, Key{1, 0}) == 0);
    // Only the hash word and the tag count.
    v[1].offsets[0] = 42;
    v[1].other = 9;
    assert(Store(index, v, Key{1, 7}) == 1);

    // Random keys from a small set, many redefinitions; the vector reallocates as it grows.
    std::mt19937_64 rng(270);
    for (int i = 0; i < 200000; ++i) {
        const Key k{rng() % 5000, static_cast<std::uint32_t>(rng() % 3)};
        Store(index, v, k, i % 7 == 0);
    }
    for (int i = 0; i < 20000; ++i) Store(index, v, Key{rng() % 6000, static_cast<std::uint32_t>(rng() % 3)});
    assert(index.resets() == 0);

    // Cleared and filled again in another order: the index starts over.
    const std::uint64_t resets = index.resets();
    v.clear();
    assert(index.Find(Bytes(v), 0, Key{3, 1}) == 0 && index.resets() == resets + 1);
    for (int i = 0; i < 5000; ++i) Store(index, v, Key{static_cast<std::uint64_t>(4999 - i), 0});
    // Rebuilt to a longer array whose first entry differs: noticed without a shorter array in between.
    std::vector<Entry> w;
    for (int i = 0; i < 9000; ++i) w.push_back(Entry{static_cast<std::uint64_t>(i), {0, 0}, 0, 0});
    assert(index.Find(Bytes(w), w.size(), Key{10, 0}) == 10);
    // The same length, the last seen entry changed.
    w[8999].hash = 123456;
    assert(index.Find(Bytes(w), w.size(), Key{8999, 0}) == w.size());
    assert(index.Find(Bytes(w), w.size(), Key{123456, 0}) == 8999);

    index.Clear();
    assert(index.seen() == 0 && index.bytes() == 0);
    assert(index.Find(Bytes(w), w.size(), Key{77, 0}) == 77);

    // The collision report: every hash word's entries in load order, any tag, up to the limit (0: all).
    {
        std::vector<Entry> c;
        for (int i = 0; i < 50000; ++i)
            c.push_back(Entry{rng() % 3000, {0, 0}, static_cast<std::uint32_t>(rng() % 4), 0});
        sb::loc::HashOrder order;
        order.Sync(Bytes(c), c.size());
        std::vector<std::uint32_t> a, b;
        for (std::uint64_t h = 0; h < 3100; h += 7) {
            for (std::size_t limit : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{5}, std::size_t{1000}}) {
                sb::loc::CollectScan(Bytes(c), c.size(), h, limit, a);
                order.Collect(h, limit, b);
                assert(a == b);
                if (h < 3000 && limit != 0) assert(!b.empty() && b.size() <= limit);
            }
        }
        // Same array: no new build; a changed last entry: a new one.
        const std::uint64_t builds = order.builds();
        order.Sync(Bytes(c), c.size());
        assert(order.builds() == builds);
        c.back().hash = 999999;
        order.Sync(Bytes(c), c.size());
        assert(order.builds() == builds + 1);
        order.Collect(999999, 0, b);
        assert(b.size() == 1 && b[0] == c.size() - 1);
        order.Clear();
        assert(order.bytes() == 0);
    }
    std::puts("loc_index: ok");
    return 0;
}
