#pragma once

// opinion_prefetch (prototype, off by default): the parts that do not touch the game, so that
// tests/test_opinion_prefetch.cpp can run them.
//
// The game's pool hands the items of a ParallelFor3 out one index at a time, in increasing order, to whichever
// thread asks next, so item i + d is taken a little after item i by some thread. The module's item prefetches
// what the item d positions ahead will read; the line lands in the shared cache or in a core's own cache, where
// the thread that takes that item finds it sooner than in memory. It reads two pointers of that country and
// executes prefetch instructions; it writes nothing.

#include <cstddef>
#include <cstdint>

namespace sb::opinion_prefetch {

// A CCountryTag is 8 bytes; its index is the signed 16-bit field in bits 32..47 (as the game's code shifts it).
inline int TagIndex(std::uint64_t tag) noexcept { return static_cast<std::int16_t>(tag >> 32); }

// The two prefetch distances of an item: the country `kFar` positions ahead gets its own lines prefetched, and
// the one `kNear` positions ahead (whose lines the item kFar - kNear positions earlier asked for) gets the lines
// its pointers lead to.
constexpr std::size_t kFar = 16;
constexpr std::size_t kNear = 8;

// The position `distance` ahead of `index` in an array of `count`, or -1 when there is none.
inline std::ptrdiff_t Ahead(std::int32_t index, std::size_t count, std::size_t distance) noexcept {
    if (index < 0) return -1;
    const std::size_t at = static_cast<std::size_t>(index) + distance;
    return at < count ? static_cast<std::ptrdiff_t>(at) : -1;
}

// CDiplomacyStatus rows are 72 bytes, one per tag index, in a vector [begin, end). The row of `tag`, or nullptr
// when the tag is not in it.
constexpr std::size_t kRowSize = 72;
inline const unsigned char* Row(const unsigned char* begin, const unsigned char* end, int tag) noexcept {
    if (begin == nullptr || end <= begin || tag < 0) return nullptr;
    const std::size_t rows = static_cast<std::size_t>(end - begin) / kRowSize;
    return static_cast<std::size_t>(tag) < rows ? begin + static_cast<std::size_t>(tag) * kRowSize : nullptr;
}

// Which arm a row takes in the check: rows alternate, so both arms see rows of the same game days.
inline bool CheckedRowPrefetches(std::uint64_t row) noexcept { return (row & 1) != 0; }

}  // namespace sb::opinion_prefetch
