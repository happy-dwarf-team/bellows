// opinion_prefetch_core.hpp: the tag index, the positions ahead, the row of a tag and the check's arms.
#include "../src/modules/opinion_prefetch_core.hpp"

#include <cassert>
#include <cstdio>
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

    // The check alternates the arms row by row.
    int on = 0;
    for (std::uint64_t row = 1; row <= 10; ++row) on += op::CheckedRowPrefetches(row) ? 1 : 0;
    assert(on == 5 && op::CheckedRowPrefetches(1) && !op::CheckedRowPrefetches(2));

    std::puts("opinion_prefetch ok");
    return 0;
}
