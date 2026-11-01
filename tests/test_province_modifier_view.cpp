// The value 0x300720 reads from 0x2FFC60's copy (CopyWithoutPlaceholders, then SparseValue) against the
// value it reads from a sparse view of the province's own entries, on random lists of both shapes.
#include "../src/modules/province_modifier_view_core.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>

namespace {

std::mt19937 g_rng(4242);

int Rand(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(g_rng); }

// A province list: dense (every static slot, unset ones holding 0x286, then dynamic ids) or sparse (ids in
// any order, with repeats and stray 0x286 entries).
std::vector<std::int32_t> RandomList() {
    std::vector<std::int32_t> list;
    if (Rand(0, 1) == 0) {
        for (int j = 0; j < sb::kModStaticEnd; ++j) {
            list.push_back(Rand(0, 2) == 0 ? sb::kModStaticEnd : j);
            list.push_back(Rand(-1000, 1000));
        }
        for (int k = Rand(0, 40); k > 0; --k) {
            list.push_back(Rand(sb::kModStaticEnd + 1, sb::kModStaticEnd + 60));
            list.push_back(Rand(-1000, 1000));
        }
    } else {
        for (int k = Rand(0, 120); k > 0; --k) {
            const int pick = Rand(0, 9);
            list.push_back(pick == 0 ? sb::kModStaticEnd : pick < 4 ? Rand(0x264, 0x268) : Rand(0, 0x300));
            list.push_back(Rand(-1000, 1000));
        }
    }
    return list;
}

}  // namespace

int main() {
    int compared = 0;
    for (int round = 0; round < 4000; ++round) {
        const std::vector<std::int32_t> list = RandomList();
        const auto count = static_cast<std::int32_t>(list.size() / 2);
        const std::vector<std::int32_t> copy = sb::CopyWithoutPlaceholders(list.data(), count);
        const auto copyCount = static_cast<std::int32_t>(copy.size() / 2);

        sb::ModListView view;
        sb::PointView(view, list.data(), count);
        assert(view.bytes[sb::kModDense] == 0);
        const std::int32_t* viewEntries = nullptr;
        std::int32_t viewCount = 0;
        std::memcpy(&viewEntries, view.bytes + sb::kModEntries, sizeof viewEntries);
        std::memcpy(&viewCount, view.bytes + sb::kModCount, sizeof viewCount);
        assert(viewEntries == list.data() && viewCount == count);

        for (const std::int32_t id : {0x266, 0x267, Rand(0, 0x285), Rand(sb::kModStaticEnd + 1, 0x300)}) {
            assert(sb::SparseValue(copy.data(), copyCount, id) == sb::SparseValue(viewEntries, viewCount, id));
            ++compared;
        }
    }
    std::printf("province_modifier_view: %d values the same through the view and the copy\n", compared);
    return 0;
}
