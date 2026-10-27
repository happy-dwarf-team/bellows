#pragma once

// army_search_profile counters (lab docs/simulation-profile.md, "Co kosztuje w AI kraju"): the AI army
// search 0x1022B0 (ai_army.cpp), a priority-queue search over the province graph with a scoring functor.
// Per call: its time, and whether the same army asked with the same arguments got the same answer
// (result and score) as the last time: the share a remembered answer could give. Plain data, no
// Windows calls: the module feeds it observations, a test feeds it numbers.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <ostream>
#include <string>
#include <tuple>

namespace sb {

struct ArmySearchObservation {
    std::uintptr_t army = 0;
    std::int32_t mode = 0;     // the second argument
    std::uint64_t arg = 0;     // the fourth argument
    std::int32_t result = 0;   // the return value
    float score = 0.0f;        // *fifth argument after the call
    bool path = false;         // a path vector was asked for (twelfth argument)
    std::int32_t day = 0;
    std::int64_t ticks = 0;
};

class ArmySearchStats {
public:
    void Add(const ArmySearchObservation& o) {
        ++calls_;
        ticks_ += o.ticks;
        max_ticks_ = std::max(max_ticks_, o.ticks);
        if (o.path) ++with_path_;
        auto& day = days_[o.day];
        ++day.first;
        day.second += o.ticks;
        armies_[o.army] += 1;
        const auto key = std::make_tuple(o.army, o.mode, o.arg);
        auto it = last_.find(key);
        if (it == last_.end()) {
            ++first_;
            last_.emplace(key, Last{o.result, o.score, o.day});
            return;
        }
        Last& last = it->second;
        const bool same_result = last.result == o.result;
        const bool same_score = std::memcmp(&last.score, &o.score, sizeof o.score) == 0;
        if (same_result && same_score) {
            ++same_;
            if (last.day == o.day) ++same_day_repeats_;
            ticks_same_ += o.ticks;
        } else if (same_result) {
            ++same_result_other_score_;
        } else {
            ++different_;
        }
        last = Last{o.result, o.score, o.day};
    }

    std::int64_t calls() const { return calls_; }

    void WriteSummary(std::ostream& out, double ms_per_tick, std::int64_t provinces) const {
        out << "key\tvalue\n"
            << "calls\t" << calls_ << "\n"
            << "total_ms\t" << ticks_ * ms_per_tick << "\n"
            << "mean_us\t" << (calls_ > 0 ? ticks_ * ms_per_tick * 1000.0 / static_cast<double>(calls_) : 0.0) << "\n"
            << "max_ms\t" << max_ticks_ * ms_per_tick << "\n"
            << "with_path\t" << with_path_ << "\n"
            << "armies\t" << armies_.size() << "\n"
            << "keys\t" << last_.size() << "\n"
            << "first\t" << first_ << "\n"
            << "same\t" << same_ << "\n"
            << "same_day_repeats\t" << same_day_repeats_ << "\n"
            << "same_ms\t" << ticks_same_ * ms_per_tick << "\n"
            << "same_result_other_score\t" << same_result_other_score_ << "\n"
            << "different\t" << different_ << "\n"
            << "days\t" << days_.size() << "\n"
            << "provinces\t" << provinces << "\n";
    }

    void WriteDays(std::ostream& out, double ms_per_tick) const {
        out << "day\tcalls\ttotal_ms\n";
        for (const auto& [day, v] : days_) out << day << "\t" << v.first << "\t" << v.second * ms_per_tick << "\n";
    }

private:
    struct Last {
        std::int32_t result;
        float score;
        std::int32_t day;
    };

    std::int64_t calls_ = 0, ticks_ = 0, max_ticks_ = 0, with_path_ = 0;
    std::int64_t first_ = 0, same_ = 0, same_day_repeats_ = 0, ticks_same_ = 0;
    std::int64_t same_result_other_score_ = 0, different_ = 0;
    std::map<std::tuple<std::uintptr_t, std::int32_t, std::uint64_t>, Last> last_;
    std::map<std::uintptr_t, std::int64_t> armies_;
    std::map<std::int32_t, std::pair<std::int64_t, std::int64_t>> days_;  // calls, ticks
};

}  // namespace sb
