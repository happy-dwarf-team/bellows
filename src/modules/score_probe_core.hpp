#pragma once

// score_probe counters (lab docs/simulation-profile.md, "Wyszukiwanie armii AI"): for a function the AI
// army search spends its time in, how often its inputs repeat within a game day with the same output
// (would a per-day memo give the game's own answers?), and how often the output stays the same when the
// army or unit is left out of the key (could armies share one memo?). One memo per thread, cleared when
// the game day changes: no lock on the game's hot path. Plain data; the module feeds it keys and
// outputs, a test feeds it numbers.

#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace sb {

// FNV-1a over raw bytes, for building keys.
inline std::uint64_t Fnv(std::uint64_t h, const void* data, std::size_t size) noexcept {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 0x100000001B3ull;
    }
    return h;
}
constexpr std::uint64_t kFnvBasis = 0xCBF29CE484222325ull;

struct ProbeCounts {
    std::int64_t calls = 0;
    std::int64_t full_repeats = 0;     // the full key was seen earlier the same day
    std::int64_t full_same = 0;        // ... with the same output
    std::int64_t partial_repeats = 0;  // a new full key whose key without army / unit was seen that day
    std::int64_t partial_same = 0;     // ... with the same output
};

class ProbeMemo {
public:
    // Records one call; returns what it counted.
    ProbeCounts Add(std::int32_t day, std::uint64_t full, std::uint64_t partial, std::uint64_t output) {
        if (day != day_) {
            day_ = day;
            full_.clear();
            partial_.clear();
        }
        ProbeCounts c;
        c.calls = 1;
        if (auto it = full_.find(full); it != full_.end()) {
            c.full_repeats = 1;
            c.full_same = it->second == output ? 1 : 0;
            return c;
        }
        full_.emplace(full, output);
        if (auto it = partial_.find(partial); it != partial_.end()) {
            c.partial_repeats = 1;
            c.partial_same = it->second == output ? 1 : 0;
        } else {
            partial_.emplace(partial, output);
        }
        return c;
    }

private:
    std::int32_t day_ = -1;
    std::unordered_map<std::uint64_t, std::uint64_t> full_;
    std::unordered_map<std::uint64_t, std::uint64_t> partial_;
};

}  // namespace sb
