#pragma once

// opinion_prefetch (prototype, off by default): the parts that do not touch the game, so that
// tests/test_opinion_prefetch.cpp can run them.
//
// The game's pool hands the items of a ParallelFor3 out one index at a time, in increasing order, to whichever
// thread asks next, so item i + d is taken a little after item i by some thread. The module's item prefetches
// what the item d positions ahead will read; the line lands in the shared cache or in a core's own cache, where
// the thread that takes that item finds it sooner than in memory. It reads fields of the item's capture and of
// live country objects and executes prefetch instructions; it writes nothing.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sb::opinion_prefetch {

template <class T>
inline T Load(const void* base, std::size_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const unsigned char*>(base) + offset, sizeof value);
    return value;
}

// The item's capture (std::_Func_impl, the hook's `self`): at +arrayAt a pointer to the root's country array
// (CArray: begin, end), at +countryAt a pointer to the lambda's capture, whose first field is the row's country.
struct Capture {
    const unsigned char* const* begin = nullptr;
    std::size_t count = 0;
    const unsigned char* a = nullptr;  // the row's country, or nullptr
};

// The country array's entries (begin and count) behind a CArray*, or none.
inline Capture Countries(const void* array) noexcept {
    Capture c;
    if (array == nullptr) return c;
    const auto* begin = Load<const unsigned char* const*>(array, 0);
    const auto* end = Load<const unsigned char* const*>(array, sizeof(void*));
    if (begin == nullptr || end <= begin) return c;
    c.begin = begin;
    c.count = static_cast<std::size_t>(end - begin);
    return c;
}

inline Capture ReadCapture(const void* self, std::size_t arrayAt, std::size_t countryAt) noexcept {
    Capture c = Countries(Load<const void*>(self, arrayAt));
    if (c.begin == nullptr) return c;
    if (const void* lambda = Load<const void*>(self, countryAt)) c.a = Load<const unsigned char*>(lambda, 0);
    return c;
}

// The game's item computes B's cell when B owns provinces or is of kind `kindValue` (the item's
// `cmp [B + owned], 0` / `cmp byte [B + kind], 7`); for every other B it reads only these two fields.
inline bool Computed(const unsigned char* b, std::size_t owned, std::size_t kind, std::uint8_t kindValue) noexcept {
    return Load<std::int32_t>(b, owned) != 0 || Load<std::uint8_t>(b, kind) == kindValue;
}

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

// Which arm a row takes in the check: the top bit of the row's number times the golden ratio (Fibonacci
// hashing), so both arms see rows of the same game days and a country whose rows come every n-th row (n even, as
// in a fixed order of the day's rows) is not always in the same arm, as it would be with odd/even rows.
inline bool CheckedRowPrefetches(std::uint64_t row) noexcept { return ((row * 0x9E3779B97F4A7C15ull) >> 63) != 0; }

}  // namespace sb::opinion_prefetch
