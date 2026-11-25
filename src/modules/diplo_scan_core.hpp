#pragma once
// diplo_scan: what does not touch the game (tests/test_diplo_scan.cpp).
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sb::diplo_scan {

// A relation row: 72 bytes, the owner's tag at +8 (the game's scan divides the array's size by 72, which
// symbols.toml checks).
constexpr std::size_t kRowSize = 72;
constexpr std::size_t kRowOwner = 8;

// The country index of a tag as the game reads it from the 8 bytes of a country tag: the low 16 bits of the
// high half, sign-extended.
inline int TagIndex(std::uint64_t tag) noexcept { return static_cast<std::int16_t>(tag >> 32); }

// The rows whose flags byte has `mask` set: what the game's scan finds.
inline std::size_t ChangedRows(const unsigned char* rows, std::size_t count, std::size_t flagsOffset,
                               unsigned char mask) noexcept {
    std::size_t changed = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if ((rows[i * kRowSize + flagsOffset] & mask) != 0) ++changed;
    }
    return changed;
}

// The countries whose "diplomacy changed" byte the module cleared after the game's scan and still owes to the
// diplomacy view, by country index. One thread (the game's main thread) uses it.
class Shadow {
public:
    void Hold(int index) {
        if (index < 0) return;
        const auto i = static_cast<std::size_t>(index);
        if (i >= bits_.size()) bits_.resize(i + 1, 0);
        if (bits_[i] == 0) {
            bits_[i] = 1;
            ++held_;
        }
    }
    // True when the module held it; it no longer does.
    bool Take(int index) noexcept {
        if (index < 0) return false;
        const auto i = static_cast<std::size_t>(index);
        if (i >= bits_.size() || bits_[i] == 0) return false;
        bits_[i] = 0;
        --held_;
        return true;
    }
    template <class F> void TakeAll(F give) {
        for (std::size_t i = 0; i < bits_.size(); ++i) {
            if (bits_[i] != 0) {
                bits_[i] = 0;
                give(static_cast<int>(i));
            }
        }
        held_ = 0;
    }
    std::size_t held() const noexcept { return held_; }

private:
    std::vector<unsigned char> bits_;
    std::size_t held_ = 0;
};

// The epoch in which each country's scan last ran, by country index. Mark is true for the first scan of a
// country in an epoch. One thread uses it.
class EpochMarks {
public:
    bool Mark(int index, std::uint32_t epoch) {
        if (index < 0) return true;
        const auto i = static_cast<std::size_t>(index);
        if (i >= epochs_.size()) epochs_.resize(i + 1, 0);
        if (epochs_[i] == epoch) return false;
        epochs_[i] = epoch;
        return true;
    }

private:
    std::vector<std::uint32_t> epochs_;
};

}  // namespace sb::diplo_scan
