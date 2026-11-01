#pragma once
// province_modifier_view: what 0x2FFC60 copies and what 0x300720 reads from the copy (lab docs:
// docs/simulation-profile.md, "RefreshKnowledge").
//
// 0x2FFC60 copies a province's modifier list into a sparse list (dense flag 0): every entry in order except
// those with id 0x286, the placeholder of an unset static slot, growing the buffer from 8 entries by doubling.
// 0x3003B0 then hands the copy to 0x300720, which reads one value from it: the value of the first entry with
// id 0x266 or 0x267, or 0; it passes the copy on only to the value object of that modifier, if one is set.
// Filtering out id 0x286 keeps the order, so for any other id the first entry in the copy is the first entry
// in the province's own list, and a sparse view of the province's entries gives the same value.
#include <cstdint>
#include <vector>

#include "modifier_merge_core.hpp"  // kModDense, kModEntries, kModCount, kModStaticEnd

namespace sb {

// The entries 0x2FFC60 copies: (id, value) pairs, without id 0x286, in order.
inline std::vector<std::int32_t> CopyWithoutPlaceholders(const std::int32_t* entries, std::int32_t count) {
    std::vector<std::int32_t> copy;
    for (std::int32_t i = 0; i < count; ++i) {
        if (entries[2 * i] == kModStaticEnd) continue;
        copy.push_back(entries[2 * i]);
        copy.push_back(entries[2 * i + 1]);
    }
    return copy;
}

// What 0x300720 reads from a sparse list: the value of the first entry with this id, or 0.
inline std::int32_t SparseValue(const std::int32_t* entries, std::int32_t count, std::int32_t id) {
    for (std::int32_t i = 0; i < count; ++i) {
        if (entries[2 * i] == id) return entries[2 * i + 1];
    }
    return 0;
}

// The fields of a modifier list 0x300720 reads: the dense flag, the entries and their count. The view
// borrows the province's entries; the rest stays zero.
struct alignas(8) ModListView {
    unsigned char bytes[0xC8] = {};
};

inline void PointView(ModListView& view, const std::int32_t* entries, std::int32_t count) {
    view.bytes[kModDense] = 0;
    *reinterpret_cast<const std::int32_t**>(view.bytes + kModEntries) = entries;
    *reinterpret_cast<std::int32_t*>(view.bytes + kModCount) = count;
}

}  // namespace sb
