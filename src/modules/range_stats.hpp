#pragma once
// Counters of range_profile (diagnostic), kept apart from the hooks so they can be unit-tested.
// One observation = one call of the country range check: which country and province,
// the distance value it returned, whether that value was within the threshold, how many
// province-distance calls it made, how long it took and which code called it.

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sb {

struct RangeObservation {
    std::uintptr_t country = 0;   // country object (stable for the whole game)
    int country_id = 0;           // its tag index
    int province = 0;             // target province id
    int value = 0;                // distance value written by the check
    bool has_threshold = false;
    bool in_range = false;        // value <= threshold
    int day = 0;                  // game date in days
    int distance_calls = 0;       // 0x2D2520 calls made inside this check
    std::int64_t ticks = 0;       // QPC ticks spent in the check
    std::uint32_t caller = 0;     // RVA of the return address
};

class RangeStats {
public:
    // Upper bounds of the distance-call histogram buckets.
    static constexpr int kBuckets[] = {0, 1, 4, 16, 64, 256, 1024};

    void Add(const RangeObservation& o) {
        ++calls_;
        distance_calls_ += o.distance_calls;
        ticks_ += o.ticks;
        max_distance_calls_ = std::max(max_distance_calls_, o.distance_calls);
        ++histogram_[Bucket(o.distance_calls)];
        Day& d = days_[o.day];
        ++d.calls;
        d.distance_calls += o.distance_calls;
        d.ticks += o.ticks;
        ++callers_[o.caller];
        Pair& p = pairs_[Key(o.country, o.province)];
        if (p.calls == 0) {
            p.country_id = o.country_id;
            p.province = o.province;
        } else {
            const bool same_day = p.last_day == o.day;
            if (p.last_value != o.value) ++(same_day ? p.value_changes_same_day : p.value_changes_later);
            if (o.has_threshold && p.last_has_threshold && p.last_in_range != o.in_range) ++p.range_flips;
            if (!same_day) ++p.days;
        }
        if (p.calls == 0) p.days = 1;
        ++p.calls;
        p.distance_calls += o.distance_calls;
        p.last_value = o.value;
        p.last_in_range = o.in_range;
        p.last_has_threshold = o.has_threshold;
        p.last_day = o.day;
    }

    std::int64_t calls() const { return calls_; }
    std::int64_t distance_calls() const { return distance_calls_; }
    std::size_t pairs() const { return pairs_.size(); }

    // Pairs whose value or in-range answer changed from one day to a later day.
    std::size_t pairs_changed_later() const {
        return static_cast<std::size_t>(std::count_if(pairs_.begin(), pairs_.end(),
                                                      [](const auto& kv) { return kv.second.value_changes_later > 0; }));
    }
    std::int64_t range_flips() const {
        std::int64_t n = 0;
        for (const auto& kv : pairs_) n += kv.second.range_flips;
        return n;
    }
    std::int64_t value_changes_same_day() const {
        std::int64_t n = 0;
        for (const auto& kv : pairs_) n += kv.second.value_changes_same_day;
        return n;
    }
    std::int64_t value_changes_later() const {
        std::int64_t n = 0;
        for (const auto& kv : pairs_) n += kv.second.value_changes_later;
        return n;
    }
    // Calls a memo per (country, province) valid for one game day would not have answered:
    // the first call of each pair on each day it was seen.
    std::int64_t first_calls_of_day() const { return first_calls_per_day_estimate(); }

    void WriteSummary(std::ostream& out, double ms_per_tick) const {
        out << "key\tvalue\n";
        out << "calls\t" << calls_ << "\n";
        out << "distance_calls\t" << distance_calls_ << "\n";
        out << "distance_calls_per_check\t" << (calls_ ? static_cast<double>(distance_calls_) / calls_ : 0.0) << "\n";
        out << "max_distance_calls\t" << max_distance_calls_ << "\n";
        out << "ms_in_checks\t" << ticks_ * ms_per_tick << "\n";
        out << "days\t" << days_.size() << "\n";
        out << "calls_per_day\t" << (days_.empty() ? 0.0 : static_cast<double>(calls_) / days_.size()) << "\n";
        out << "pairs\t" << pairs_.size() << "\n";
        out << "countries\t" << Countries() << "\n";
        out << "pair_calls_first_of_day\t" << first_calls_per_day_estimate() << "\n";
        out << "value_changes_same_day\t" << value_changes_same_day() << "\n";
        out << "value_changes_later\t" << value_changes_later() << "\n";
        out << "pairs_changed_later\t" << pairs_changed_later() << "\n";
        out << "range_flips\t" << range_flips() << "\n";
        for (std::size_t i = 0; i < std::size(kBuckets) + 1; ++i) {
            out << "distance_calls_le_" << (i < std::size(kBuckets) ? std::to_string(kBuckets[i]) : "more") << "\t"
                << histogram_[i] << "\n";
        }
    }

    void WriteDays(std::ostream& out, double ms_per_tick) const {
        out << "day\tcalls\tdistance_calls\tms\n";
        for (const auto& [day, d] : days_) {
            out << day << "\t" << d.calls << "\t" << d.distance_calls << "\t" << d.ticks * ms_per_tick << "\n";
        }
    }

    void WritePairs(std::ostream& out, std::size_t limit) const {
        std::vector<const Pair*> sorted;
        sorted.reserve(pairs_.size());
        for (const auto& kv : pairs_) sorted.push_back(&kv.second);
        std::sort(sorted.begin(), sorted.end(), [](const Pair* a, const Pair* b) { return a->calls > b->calls; });
        out << "country_id\tprovince\tcalls\tdays\tdistance_calls\tvalue_changes_same_day\tvalue_changes_later\trange_flips\tlast_value\tlast_in_range\n";
        for (std::size_t i = 0; i < sorted.size() && i < limit; ++i) {
            const Pair& p = *sorted[i];
            out << p.country_id << "\t" << p.province << "\t" << p.calls << "\t" << p.days << "\t" << p.distance_calls << "\t"
                << p.value_changes_same_day << "\t" << p.value_changes_later << "\t" << p.range_flips << "\t" << p.last_value
                << "\t" << (p.last_in_range ? 1 : 0) << "\n";
        }
    }

    void WriteCallers(std::ostream& out) const {
        out << "caller_rva\tcalls\n";
        for (const auto& [rva, n] : callers_) out << "0x" << std::hex << std::uppercase << rva << std::dec << "\t" << n << "\n";
    }

private:
    struct Day {
        std::int64_t calls = 0, distance_calls = 0, ticks = 0;
    };
    struct Pair {
        int country_id = 0, province = 0;
        std::int64_t calls = 0, days = 0, distance_calls = 0;
        std::int64_t value_changes_same_day = 0, value_changes_later = 0, range_flips = 0;
        int last_value = 0, last_day = 0;
        bool last_in_range = false, last_has_threshold = false;
    };

    static std::uint64_t Key(std::uintptr_t country, int province) {
        return (static_cast<std::uint64_t>(country) << 16) ^ static_cast<std::uint16_t>(province);
    }
    static std::size_t Bucket(int n) {
        std::size_t i = 0;
        while (i < std::size(kBuckets) && n > kBuckets[i]) ++i;
        return i;
    }
    // Sum over pairs of the days each pair was seen: the calls a one-day memo could not avoid.
    std::int64_t first_calls_per_day_estimate() const {
        std::int64_t n = 0;
        for (const auto& kv : pairs_) n += kv.second.days;
        return n;
    }
    std::size_t Countries() const {
        std::map<int, int> seen;
        for (const auto& kv : pairs_) seen[kv.second.country_id] = 1;
        return seen.size();
    }

    std::int64_t calls_ = 0, distance_calls_ = 0, ticks_ = 0;
    int max_distance_calls_ = 0;
    std::int64_t histogram_[std::size(kBuckets) + 1] = {};
    std::map<int, Day> days_;
    std::unordered_map<std::uint64_t, Pair> pairs_;
    std::map<std::uint32_t, std::int64_t> callers_;
};

}  // namespace sb
