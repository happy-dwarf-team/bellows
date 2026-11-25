#pragma once

// conquer_scan (prototype, off by default): the parts that do not touch the game, so that
// tests/test_serial_loop.cpp can run them.
//
// The module fills the AI's conquest list from the land provinces only, one of two ways: a loop on the calling
// thread in index order, or the game's own parallel loop on an array of the land provinces. The game's
// parallel loop hands the items to its worker pool and the calling thread takes items itself while it waits, so
// the loop in index order on the calling thread is one of the schedules the game's own pool can produce: each
// item of the loop writes only its own result.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sb::serial_loop {

// One entry of the conquest list's buffer, as the game's item writes it: province index, value (> 0).
struct Pair {
    std::int32_t index;
    std::int32_t value;
};
static_assert(sizeof(Pair) == 8, "the game writes two int32 per entry");

inline bool operator==(const Pair& a, const Pair& b) noexcept { return a.index == b.index && a.value == b.value; }

// The indices of the items whose answer can be other than 0: the game's value function returns 0 at once for
// a province whose template is not land, before it reads anything that changes during a game.
template <class IsLand>
std::vector<std::int32_t> CandidateList(std::int32_t count, IsLand&& isLand) {
    std::vector<std::int32_t> list;
    list.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        if (isLand(i)) list.push_back(i);
    }
    return list;
}

// The conquest list's fill: value(i) for every candidate in order, (i, value) appended where value > 0, as the
// game's item does it. prefetch(k) is called with the position in `list` that is `distance` ahead (and
// prefetchNear(k) half as far ahead) before the value of position k is computed; both only warm the cache.
// Returns the number of pairs written to out, which holds at least list.size() entries.
template <class Value, class Prefetch, class PrefetchNear>
std::size_t FillPairs(const std::vector<std::int32_t>& list, Value&& value, Prefetch&& prefetch,
                      PrefetchNear&& prefetchNear, std::size_t distance, Pair* out) {
    std::size_t written = 0;
    const std::size_t n = list.size();
    const std::size_t half = distance / 2;
    for (std::size_t k = 0; k < n; ++k) {
        if (distance != 0 && k + distance < n) prefetch(list[k + distance]);
        if (half != 0 && k + half < n) prefetchNear(list[k + half]);
        const std::int32_t i = list[k];
        const std::int32_t v = value(i);
        if (v > 0) out[written++] = Pair{i, v};
    }
    return written;
}

// The game sorts its buffer by province index after the loop (the conquest list 0x187AD0, the comparator
// reads only the index) and adds the entries in that order, so two buffers give the same list when they hold
// the same pairs. Sorts both and returns the number of positions where they differ (0: the same list).
inline std::size_t Differences(std::vector<Pair>& a, std::vector<Pair>& b) {
    const auto byIndex = [](const Pair& x, const Pair& y) { return x.index < y.index; };
    std::sort(a.begin(), a.end(), byIndex);
    std::sort(b.begin(), b.end(), byIndex);
    const std::size_t common = std::min(a.size(), b.size());
    std::size_t differences = std::max(a.size(), b.size()) - common;
    for (std::size_t k = 0; k < common; ++k) {
        if (!(a[k] == b[k])) ++differences;
    }
    return differences;
}

// The pool's way: the game's item writes the index in the array it was given, here the position in the list of
// land provinces. Maps the entries [from, count) back to the province indices; the mapping keeps their order.
inline void MapToProvinces(Pair* pairs, std::int32_t from, std::int32_t count, const std::vector<std::int32_t>& list) {
    for (std::int32_t j = from; j < count; ++j) pairs[j].index = list[static_cast<std::size_t>(pairs[j].index)];
}

// Which way a call takes. Both give the same list; the loop on the calling thread wins where the items are
// cheap (the pool's hand-out costs more than they do: Anbennar), the pool where they are dear (the base game).
// Keeps a moving average of each way's time per call (1/8 of each new call), takes the cheaper, and every
// 16th call the other one, so that its average follows the game. Not thread-safe: the caller holds a lock.
class WayChooser {
public:
    static constexpr int kSerial = 0, kPool = 1;
    int Pick(std::uint64_t call) const noexcept {
        if (!seen_[kSerial]) return kSerial;
        if (!seen_[kPool]) return kPool;
        const int best = average_[kPool] < average_[kSerial] ? kPool : kSerial;
        return call % 16 == 0 ? 1 - best : best;
    }
    void Add(int way, std::int64_t ticks) noexcept {
        if (!seen_[way]) {
            average_[way] = ticks;
            seen_[way] = true;
        } else {
            average_[way] += (ticks - average_[way]) / 8;
        }
    }
    std::int64_t Average(int way) const noexcept { return average_[way]; }

private:
    std::int64_t average_[2] = {};
    bool seen_[2] = {};
};

// Which call is checked against the game's parallel loop: every call in "verify", one in 256 in "on".
inline bool Checked(bool verifyOnly, std::uint64_t sequence) noexcept { return verifyOnly || (sequence & 255) == 0; }

// In a checked call the three loops run one after the other: the game's on all provinces, and the module's two
// ways. A later one finds the memory an earlier one loaded in the cache, so each time is kept by whether that
// loop ran first: ticks[who][0] first, [who][1] later; who: 0 the game, 1 the serial loop, 2 the pool.
struct OrderedTimes {
    std::int64_t ticks[3][2] = {};
    std::int64_t calls[3][2] = {};
    void Add(int who, bool first, std::int64_t t) noexcept {
        ticks[who][first ? 0 : 1] += t;
        ++calls[who][first ? 0 : 1];
    }
};

// Which loop runs first in the n-th checked call (counted from 1): in turn, so each runs first in a third of them.
inline int FirstOf3(std::uint64_t checkedCall) noexcept { return static_cast<int>(checkedCall % 3); }

}  // namespace sb::serial_loop
