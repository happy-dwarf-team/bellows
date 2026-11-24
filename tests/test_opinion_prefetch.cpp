// opinion_prefetch_core.hpp: the tag index, the positions ahead, the row of a tag, the check's arms, the item's
// capture and which cells the item computes.
#include "../src/modules/opinion_prefetch_core.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace op = sb::opinion_prefetch;

int main() {
    // The game's shr 0x20; movsx of a CCountryTag: the low 16 bits of the high half, with their sign.
    assert(op::TagIndex(0x0000001234567890ull) == 0x12);
    assert(op::TagIndex(0xABCD07490000FFFFull) == 0x0749);
    assert(op::TagIndex(0x0000FFFF00000000ull) == -1);

    // Positions ahead stay inside the array.
    static_assert(op::kNear < op::kFar);
    assert(op::Ahead(0, 100, op::kFar) == 16 && op::Ahead(83, 100, op::kFar) == 99);
    assert(op::Ahead(84, 100, op::kFar) == -1 && op::Ahead(99, 100, op::kNear) == -1);
    assert(op::Ahead(-1, 100, op::kNear) == -1 && op::Ahead(0, 0, 0) == -1);

    // Rows of 72 bytes: the tag's row when the vector has it, nothing otherwise.
    std::vector<unsigned char> rows(5 * op::kRowSize + 10, 0);  // a partial row at the end does not count
    const unsigned char* b = rows.data();
    const unsigned char* e = rows.data() + rows.size();
    assert(op::Row(b, e, 0) == b && op::Row(b, e, 4) == b + 4 * op::kRowSize);
    assert(op::Row(b, e, 5) == nullptr && op::Row(b, e, -1) == nullptr);
    assert(op::Row(nullptr, e, 0) == nullptr && op::Row(b, b, 0) == nullptr && op::Row(e, b, 0) == nullptr);

    // The check's arms: about half of the rows each, also for every second and every fourth row (a country whose
    // rows come in a fixed order of an even count), which odd/even rows would put all in one arm.
    for (std::uint64_t step : {1ull, 2ull, 4ull, 80ull}) {
        int on = 0;
        for (std::uint64_t k = 1; k <= 1000; ++k) on += op::CheckedRowPrefetches(k * step) ? 1 : 0;
        assert(on > 400 && on < 600);
    }

    // The item's capture: self + 0x10 -> CArray (begin, end); self + 8 -> the lambda, whose first field is A.
    unsigned char countryA[0x40] = {}, countryB[0x40] = {}, countryC[0x40] = {};
    const unsigned char* entries[3] = {countryA, countryB, countryC};
    const unsigned char* const* array[2] = {entries, entries + 3};
    const void* lambda[1] = {countryA};
    const void* self[3] = {nullptr, lambda, array};
    const op::Capture c = op::ReadCapture(self, 0x10, 0x8);
    assert(c.begin == entries && c.count == 3 && c.a == countryA);
    assert(op::Countries(array).count == 3 && op::Countries(nullptr).count == 0);
    const unsigned char* const* empty[2] = {entries, entries};
    const void* selfEmpty[3] = {nullptr, lambda, empty};
    assert(op::ReadCapture(selfEmpty, 0x10, 0x8).begin == nullptr);
    const void* selfNoLambda[3] = {nullptr, nullptr, array};
    const op::Capture noA = op::ReadCapture(selfNoLambda, 0x10, 0x8);
    assert(noA.count == 3 && noA.a == nullptr);

    // Computed cells: B owns provinces, or B's kind byte is the item's value.
    const std::int32_t owned = 3;
    std::memcpy(countryB + 0x30, &owned, sizeof owned);
    countryC[0x26] = 7;
    assert(op::Computed(countryB, 0x30, 0x26, 7) && op::Computed(countryC, 0x30, 0x26, 7));
    assert(!op::Computed(countryA, 0x30, 0x26, 7));

    std::puts("opinion_prefetch ok");
    return 0;
}
