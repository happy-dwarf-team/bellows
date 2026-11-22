// conquer_scan: the candidate list, the fill in index order with its prefetch positions, and
// the comparisons of a checked call.
#include "../src/modules/serial_loop_core.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

using sb::serial_loop::Pair;

int main() {
    namespace sl = sb::serial_loop;

    // Land provinces only, in index order.
    const std::vector<bool> land = {false, true, true, false, true, true, true, false, true, true, true, true};
    const auto list = sl::CandidateList(static_cast<std::int32_t>(land.size()), [&](std::int32_t i) { return land[i]; });
    assert((list == std::vector<std::int32_t>{1, 2, 4, 5, 6, 8, 9, 10, 11}));

    // The game's item for each: a value, appended only when above 0; the prefetch looks `distance` ahead.
    const auto value = [](std::int32_t i) { return i % 3 == 0 ? 0 : i * 10 - 30; };  // 0 or below for 1, 2, 3
    std::vector<Pair> out(list.size());
    std::vector<std::int32_t> ahead, half;
    const std::size_t written = sl::FillPairs(list, value, [&](std::int32_t i) { ahead.push_back(i); },
                                              [&](std::int32_t i) { half.push_back(i); }, 4, out.data());
    out.resize(written);
    assert((out == std::vector<Pair>{{4, 10}, {5, 20}, {8, 50}, {10, 70}, {11, 80}}));
    assert((ahead == std::vector<std::int32_t>{6, 8, 9, 10, 11}));  // list[k + 4] while it exists
    assert((half == std::vector<std::int32_t>{4, 5, 6, 8, 9, 10, 11}));  // list[k + 2]

    // Without a prefetch distance: the same pairs, no prefetch.
    std::vector<Pair> plain(list.size());
    ahead.clear();
    half.clear();
    plain.resize(sl::FillPairs(list, value, [&](std::int32_t i) { ahead.push_back(i); },
                               [&](std::int32_t i) { half.push_back(i); }, 0, plain.data()));
    assert(plain == out && ahead.empty() && half.empty());

    // The game's buffer comes in the pool's order; after its sort by index it is the same list.
    std::vector<Pair> game = {{10, 70}, {4, 10}, {11, 80}, {8, 50}, {5, 20}};
    std::vector<Pair> mine = out;
    assert(sl::Differences(mine, game) == 0);
    std::vector<Pair> other = {{10, 70}, {4, 11}, {11, 80}, {8, 50}};  // a value differs, one entry missing
    mine = out;
    assert(sl::Differences(mine, other) != 0);
    std::vector<Pair> sameButOne = {{4, 10}, {5, 20}, {8, 51}, {10, 70}, {11, 80}};
    mine = out;
    assert(sl::Differences(mine, sameButOne) == 1);
    std::vector<Pair> empty1, empty2;
    assert(sl::Differences(empty1, empty2) == 0);

    // Checked calls: every one in "verify", one in 256 in "on"; who runs first alternates.
    assert(sl::Checked(true, 1) && sl::Checked(true, 7));
    assert(!sl::Checked(false, 1) && !sl::Checked(false, 255) && sl::Checked(false, 256) && sl::Checked(false, 512));
    assert(sl::GameFirst(1) && !sl::GameFirst(2) && sl::GameFirst(3));
    sl::OrderedTimes t;
    t.Add(true, true, 10);
    t.Add(false, false, 4);
    t.Add(false, true, 6);
    assert(t.ticks[1][0] == 10 && t.calls[1][0] == 1 && t.ticks[0][1] == 4 && t.ticks[0][0] == 6 && t.calls[1][1] == 0);

    std::puts("serial_loop: ok");
    return 0;
}
