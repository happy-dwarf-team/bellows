// ProbeMemo (score_probe counters): repeats of a full key with the same or another output, keys
// without the army that repeat, and the reset at a new day.
#include "../src/modules/score_probe_core.hpp"

#include <cassert>
#include <cstdio>

int main() {
    sb::ProbeMemo m;
    sb::ProbeCounts total;
    auto add = [&](int day, std::uint64_t full, std::uint64_t partial, std::uint64_t out) {
        const sb::ProbeCounts c = m.Add(day, full, partial, out);
        total.calls += c.calls;
        total.full_repeats += c.full_repeats;
        total.full_same += c.full_same;
        total.partial_repeats += c.partial_repeats;
        total.partial_same += c.partial_same;
    };
    add(1, 10, 100, 5);  // new
    add(1, 10, 100, 5);  // full repeat, same
    add(1, 10, 100, 6);  // full repeat, other output
    add(1, 11, 100, 5);  // new full key, partial repeat, same output
    add(1, 12, 100, 7);  // new full key, partial repeat, other output
    add(2, 10, 100, 5);  // a new day: nothing remembered
    assert(total.calls == 6);
    assert(total.full_repeats == 2 && total.full_same == 1);
    assert(total.partial_repeats == 2 && total.partial_same == 1);

    const std::uint64_t a = sb::Fnv(sb::kFnvBasis, "ab", 2), b = sb::Fnv(sb::kFnvBasis, "ba", 2);
    assert(a != b && a == sb::Fnv(sb::kFnvBasis, "ab", 2));
    std::printf("score_probe ok\n");
    return 0;
}
