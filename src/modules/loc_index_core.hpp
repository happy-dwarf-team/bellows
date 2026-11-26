#pragma once

// loc_index: the search of a localisation table's load-order array, without the game (bellows#270).
//
// 0x16FAAE0 stores one localisation key of a language table. When the key's hash is in the table's sorted key
// array already (a mod defining a key of the game again, or a key repeated), it walks the table's load-order array
// (+0x38: begin, end; 24-byte entries) from index 0 for the first entry with the same 64-bit word at +0 (the hash)
// and the same int at +0x10 (the call's third int argument). Found, it rewrites two offsets of that entry;
// not found, it appends a new entry. With ~960k entries and ~83k keys defined again (Europa Expanded: Renewed)
// that walk is quadratic: ~8 s per load, and the game loads the localisation twice.
//
// Index answers the same question from a hash table: (word at +0, int at +0x10) -> the first index with them.
// The entries' +0 and +0x10 are written only when an entry is appended, so the index follows the array by adding
// the entries past the last one it saw. A shorter array, or a first or last seen entry that changed, means the
// table was cleared or rebuilt: the index starts again from index 0.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>

namespace sb::loc {

inline constexpr std::size_t kEntrySize = 0x18;
inline constexpr std::size_t kEntryHash = 0x00;
inline constexpr std::size_t kEntryTag = 0x10;

struct Key {
    std::uint64_t hash = 0;
    std::uint32_t tag = 0;
    bool operator==(const Key&) const = default;
};

inline Key EntryKey(const unsigned char* entry) noexcept {
    Key k;
    std::memcpy(&k.hash, entry + kEntryHash, sizeof k.hash);
    std::memcpy(&k.tag, entry + kEntryTag, sizeof k.tag);
    return k;
}

// The game's walk: the first index in [0, count) with `key`, or `count`.
inline std::size_t Scan(const unsigned char* begin, std::size_t count, Key key) noexcept {
    for (std::size_t i = 0; i < count; ++i)
        if (EntryKey(begin + i * kEntrySize) == key) return i;
    return count;
}

class Index {
public:
    // The first index of `key` in the array [begin, begin + count * kEntrySize), or `count` when none has it.
    std::size_t Find(const unsigned char* begin, std::size_t count, Key key) {
        Sync(begin, count);
        if (slots_.empty()) return count;
        for (std::size_t s = Slot(key);; s = (s + 1) & mask_) {
            const Item& it = slots_[s];
            if (it.index == kEmpty) return count;
            if (it.hash == key.hash && it.tag == key.tag) return it.index;
        }
    }

    std::size_t seen() const noexcept { return seen_; }
    std::uint64_t resets() const noexcept { return resets_; }
    std::size_t bytes() const noexcept { return slots_.capacity() * sizeof(Item); }
    void Clear() noexcept {
        std::vector<Item>().swap(slots_);
        mask_ = 0;
        used_ = 0;
        seen_ = 0;
    }

private:
    static constexpr std::uint32_t kEmpty = 0xFFFFFFFFu;
    struct Item {  // 16 bytes: ~900k entries of a table take 32 MiB at most
        std::uint64_t hash = 0;
        std::uint32_t tag = 0;
        std::uint32_t index = kEmpty;
        Key key() const noexcept { return Key{hash, tag}; }
    };
    static_assert(sizeof(Item) == 16);

    std::vector<Item> slots_;
    std::size_t mask_ = 0;
    std::size_t used_ = 0;
    std::size_t seen_ = 0;  // entries [0, seen_) are in the index
    Key first_{}, last_{};  // entries 0 and seen_ - 1 as they were when indexed
    std::uint64_t resets_ = 0;

    std::size_t Slot(Key k) const noexcept {
        std::uint64_t x = k.hash * 0x9E3779B97F4A7C15ull ^ (static_cast<std::uint64_t>(k.tag) * 0xC2B2AE3D27D4EB4Full);
        x ^= x >> 29;
        return static_cast<std::size_t>(x) & mask_;
    }

    void Grow() {
        std::vector<Item> old;
        old.swap(slots_);
        const std::size_t size = old.empty() ? 1024 : old.size() * 2;
        slots_.assign(size, Item{});
        mask_ = size - 1;
        for (const Item& it : old)
            if (it.index != kEmpty) Put(it.key(), it.index);
    }

    void Put(Key k, std::uint32_t index) {
        for (std::size_t s = Slot(k);; s = (s + 1) & mask_) {
            Item& it = slots_[s];
            if (it.index == kEmpty) {
                it.hash = k.hash;
                it.tag = k.tag;
                it.index = index;
                ++used_;
                return;
            }
            if (it.hash == k.hash && it.tag == k.tag) return;  // the first index stays
        }
    }

    void Sync(const unsigned char* begin, std::size_t count) {
        if (seen_ > 0 && (count < seen_ || !(EntryKey(begin) == first_) ||
                          !(EntryKey(begin + (seen_ - 1) * kEntrySize) == last_))) {
            ++resets_;
            Clear();
        }
        if (count == seen_) return;
        for (std::size_t i = seen_; i < count; ++i) {
            if ((used_ + 1) * 4 > slots_.size() * 3) Grow();
            Put(EntryKey(begin + i * kEntrySize), static_cast<std::uint32_t>(i));
        }
        seen_ = count;
        first_ = EntryKey(begin);
        last_ = EntryKey(begin + (count - 1) * kEntrySize);
    }
};

// The hash-collision report, 0x16FB1C0: for each run of equal hashes in the sorted key array it walks the whole
// load-order array for the entries with that hash word (any int), in order, until it has `limit` of them (the
// run's length; 0 never stops), and copies three pointers of each into the report. One call per load, with a
// few thousand runs on Europa Expanded: Renewed: each run a walk over ~900k entries.
//
// The game's walk: the indices it takes for `hash`.
inline void CollectScan(const unsigned char* begin, std::size_t count, std::uint64_t hash, std::size_t limit,
                        std::vector<std::uint32_t>& out) {
    out.clear();
    for (std::size_t i = 0; i < count; ++i) {
        if (EntryKey(begin + i * kEntrySize).hash != hash) continue;
        out.push_back(static_cast<std::uint32_t>(i));
        if (out.size() == limit) return;
    }
}

// The same from (hash word, index) pairs sorted once per array.
class HashOrder {
public:
    // Rebuilds when the array is another one or its length or first or last entry changed since the last build.
    void Sync(const unsigned char* begin, std::size_t count) {
        if (count == count_ && begin == begin_ && count != 0 && EntryKey(begin) == first_ &&
            EntryKey(begin + (count - 1) * kEntrySize) == last_)
            return;
        pairs_.clear();
        pairs_.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
            pairs_.push_back(Pair{EntryKey(begin + i * kEntrySize).hash, static_cast<std::uint32_t>(i)});
        std::sort(pairs_.begin(), pairs_.end(),
                  [](const Pair& a, const Pair& b) { return a.hash != b.hash ? a.hash < b.hash : a.index < b.index; });
        begin_ = begin;
        count_ = count;
        if (count != 0) {
            first_ = EntryKey(begin);
            last_ = EntryKey(begin + (count - 1) * kEntrySize);
        }
        ++builds_;
    }

    // What CollectScan takes, after Sync with the same array.
    void Collect(std::uint64_t hash, std::size_t limit, std::vector<std::uint32_t>& out) const {
        out.clear();
        auto it = std::lower_bound(pairs_.begin(), pairs_.end(), hash, [](const Pair& p, std::uint64_t h) { return p.hash < h; });
        for (; it != pairs_.end() && it->hash == hash; ++it) {
            out.push_back(it->index);
            if (out.size() == limit) return;
        }
    }

    std::uint64_t builds() const noexcept { return builds_; }
    std::size_t bytes() const noexcept { return pairs_.capacity() * sizeof(Pair); }
    void Clear() noexcept {
        std::vector<Pair>().swap(pairs_);
        begin_ = nullptr;
        count_ = 0;
    }

private:
    struct Pair {
        std::uint64_t hash;
        std::uint32_t index;
    };
    std::vector<Pair> pairs_;
    const unsigned char* begin_ = nullptr;
    std::size_t count_ = 0;
    Key first_{}, last_{};
    std::uint64_t builds_ = 0;
};

}  // namespace sb::loc
