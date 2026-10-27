// ArmySearchStats (army_search_profile counters): first calls, repeats with the same answer, other
// answers, keys and days.
#include "../src/modules/army_search_core.hpp"

#include <cassert>
#include <cstdio>
#include <sstream>
#include <string>

namespace {

sb::ArmySearchObservation Obs(std::uintptr_t army, int result, float score, int day, int mode = 1) {
    sb::ArmySearchObservation o;
    o.army = army;
    o.mode = mode;
    o.arg = 7;
    o.result = result;
    o.score = score;
    o.day = day;
    o.ticks = 10;
    return o;
}

bool Has(const std::string& text, const std::string& line) { return text.find(line + "\n") != std::string::npos; }

}  // namespace

int main() {
    sb::ArmySearchStats s;
    s.Add(Obs(0x100, 5, 1.5f, 1));  // first
    s.Add(Obs(0x100, 5, 1.5f, 1));  // same, same day
    s.Add(Obs(0x100, 5, 1.5f, 2));  // same, next day
    s.Add(Obs(0x100, 5, 2.0f, 3));  // same result, other score
    s.Add(Obs(0x100, 6, 2.0f, 4));  // different
    s.Add(Obs(0x100, 6, 2.0f, 4, 2));  // another mode: a first call of its own key
    s.Add(Obs(0x200, 6, 2.0f, 4));  // another army: first
    assert(s.calls() == 7);

    std::ostringstream summary, days;
    s.WriteSummary(summary, 1.0, 4941);
    s.WriteDays(days, 1.0);
    const std::string t = summary.str(), d = days.str();
    std::printf("%s%s", t.c_str(), d.c_str());
    assert(Has(t, "calls\t7"));
    assert(Has(t, "total_ms\t70"));
    assert(Has(t, "armies\t2"));
    assert(Has(t, "keys\t3"));
    assert(Has(t, "first\t3"));
    assert(Has(t, "same\t2"));
    assert(Has(t, "same_day_repeats\t1"));
    assert(Has(t, "same_ms\t20"));
    assert(Has(t, "same_result_other_score\t1"));
    assert(Has(t, "different\t1"));
    assert(Has(t, "provinces\t4941"));
    assert(Has(d, "4\t3\t30"));
    std::printf("army_search ok\n");
    return 0;
}
