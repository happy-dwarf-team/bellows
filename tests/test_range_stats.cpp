// RangeStats (range_profile counters): calls, distance calls, days, per-pair changes.
#include "../src/modules/range_stats.hpp"

#include <cassert>
#include <cstdio>
#include <sstream>
#include <string>

namespace {

sb::RangeObservation Obs(std::uintptr_t country, int province, int value, int day, int distance, int threshold = 1000) {
    sb::RangeObservation o;
    o.country = country;
    o.country_id = static_cast<int>(country & 0xFF);
    o.province = province;
    o.value = value;
    o.has_threshold = true;
    o.in_range = value <= threshold;
    o.day = day;
    o.distance_calls = distance;
    o.ticks = 10;
    o.caller = 0x3520D0 + 0xA5;
    return o;
}

bool Has(const std::string& text, const std::string& line) { return text.find(line + "\n") != std::string::npos; }

}  // namespace

int main() {
    sb::RangeStats s;
    // Country 1, province 10: same value twice on day 100, then a new value on day 101 that leaves range.
    s.Add(Obs(0x1001, 10, 500, 100, 3));
    s.Add(Obs(0x1001, 10, 500, 100, 3));
    s.Add(Obs(0x1001, 10, 1500, 101, 300));
    // Country 1, province 11: changes within one day.
    s.Add(Obs(0x1001, 11, 200, 100, 0));
    s.Add(Obs(0x1001, 11, 250, 100, 1));
    // Country 2, same province id 10: a different pair.
    s.Add(Obs(0x2002, 10, 700, 101, 2000));

    assert(s.calls() == 6);
    assert(s.distance_calls() == 3 + 3 + 300 + 0 + 1 + 2000);
    assert(s.pairs() == 3);
    assert(s.value_changes_same_day() == 1);
    assert(s.value_changes_later() == 1);
    assert(s.pairs_changed_later() == 1);
    assert(s.range_flips() == 1);
    // First calls of each pair on each day: (1,10) days 100 and 101, (1,11) day 100, (2,10) day 101.
    assert(s.first_calls_of_day() == 4);

    std::ostringstream summary;
    s.WriteSummary(summary, 0.5);
    const std::string text = summary.str();
    assert(Has(text, "calls\t6"));
    assert(Has(text, "pairs\t3"));
    assert(Has(text, "countries\t2"));
    assert(Has(text, "days\t2"));
    assert(Has(text, "ms_in_checks\t30"));
    assert(Has(text, "distance_calls_le_0\t1"));
    assert(Has(text, "distance_calls_le_1\t1"));
    assert(Has(text, "distance_calls_le_4\t2"));
    assert(Has(text, "distance_calls_le_256\t0"));
    assert(Has(text, "distance_calls_le_1024\t1"));
    assert(Has(text, "distance_calls_le_more\t1"));

    std::ostringstream pairs;
    s.WritePairs(pairs, 10);
    assert(pairs.str().find("1\t10\t3\t2\t306\t0\t1\t1\t1500\t0\n") != std::string::npos);

    std::ostringstream days;
    s.WriteDays(days, 1.0);
    assert(days.str().find("100\t4\t7\t40\n") != std::string::npos);
    assert(days.str().find("101\t2\t2300\t20\n") != std::string::npos);

    std::ostringstream callers;
    s.WriteCallers(callers);
    assert(callers.str().find("0x352175\t6\n") != std::string::npos);
    std::puts("ok");
    return 0;
}
