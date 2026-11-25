// diplo_scan_core.hpp: the tag index, the count of changed rows and the held "diplomacy changed" bytes.
#include "../src/modules/diplo_scan_core.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ds = sb::diplo_scan;

int main() {
    // The game's reading of a country tag: the low 16 bits of the high half, with their sign.
    assert(ds::TagIndex(0x0000001234567890ull) == 0x12);
    assert(ds::TagIndex(0xABCD07490000FFFFull) == 0x0749);
    assert(ds::TagIndex(0x0000FFFF00000000ull) == -1);

    // Rows of 72 bytes, flags at +0x46, the change bit 4: only that bit counts.
    std::vector<unsigned char> rows(5 * ds::kRowSize, 0);
    assert(ds::ChangedRows(rows.data(), 5, 0x46, 4) == 0);
    rows[1 * ds::kRowSize + 0x46] = 0xFB;  // every other flag of the byte
    rows[2 * ds::kRowSize + 0x45] = 0xFF;  // the byte before
    rows[2 * ds::kRowSize + 0x47] = 0xFF;  // the byte after
    assert(ds::ChangedRows(rows.data(), 5, 0x46, 4) == 0);
    rows[0 * ds::kRowSize + 0x46] = 4;
    rows[4 * ds::kRowSize + 0x46] = 0xFF;
    assert(ds::ChangedRows(rows.data(), 5, 0x46, 4) == 2);
    assert(ds::ChangedRows(rows.data(), 4, 0x46, 4) == 1);  // the count bounds the walk

    // Held bytes: once per country, taken once, and all of them at a difference.
    ds::Shadow shadow;
    assert(shadow.held() == 0 && !shadow.Take(3) && !shadow.Take(-1));
    shadow.Hold(3);
    shadow.Hold(3);
    shadow.Hold(1800);
    shadow.Hold(-1);  // not a country
    assert(shadow.held() == 2);
    assert(shadow.Take(3) && !shadow.Take(3) && shadow.held() == 1);
    shadow.Hold(7);
    std::vector<int> given;
    shadow.TakeAll([&](int i) { given.push_back(i); });
    assert(given == std::vector<int>({7, 1800}) && shadow.held() == 0 && !shadow.Take(1800));

    // Epochs: the first scan of a country in an epoch, and of a country with no index, is the game's.
    ds::EpochMarks marks;
    assert(marks.Mark(5, 1) && !marks.Mark(5, 1) && marks.Mark(0, 1) && !marks.Mark(0, 1));
    assert(marks.Mark(5, 2) && !marks.Mark(5, 2) && marks.Mark(0, 2));
    assert(marks.Mark(-1, 2) && marks.Mark(-1, 2));

    std::puts("diplo_scan ok");
    return 0;
}
