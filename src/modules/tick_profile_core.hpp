#pragma once

// tick_profile counters (lab docs/simulation-profile.md): the daily tick (0x758430) cut into
// segments at the calls of 0x769EA0, the "draw a frame if enough time passed" check the game
// makes about 40 times between the stages of the daily, monthly and yearly tick. A segment is
// named by the return addresses (RVAs) of the checkpoints at its two ends; time spent inside
// 0x769EA0 itself (the frames it draws) is counted per checkpoint and left out of the segments.
// Plain data, no Windows calls: the module feeds it ticks and RVAs, a test feeds it numbers.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace sb {

struct TickSpan {
    std::int64_t calls = 0;
    std::int64_t total = 0;  // ticks
    std::int64_t max = 0;

    void Add(std::int64_t ticks) {
        ++calls;
        total += ticks;
        max = std::max(max, ticks);
    }
};

struct TickDay {
    std::int32_t day = 0;          // days since 1 January of year 0 (365-day years)
    std::int64_t total = 0;        // ticks from entering to leaving 0x758430
    std::int64_t frames = 0;       // ticks inside 0x769EA0 during the day
    std::int32_t checkpoints = 0;  // calls of 0x769EA0 during the day
};

// One call of the AI pool job 0x77D8A0 (parallel_for over the per-country AI updates 0xB0FA0).
struct TickJob {
    std::int32_t day = 0;
    std::int64_t wall = 0;         // ticks inside 0x77D8A0 on the main thread
    std::int32_t items = 0;        // 0xB0FA0 calls inside it
    std::int64_t sum = 0;          // ticks of all items
    std::int64_t max_item = 0;     // the longest item
    std::int32_t threads = 0;      // threads that ran at least one item
    std::int64_t main_busy = 0;    // item ticks on the main thread
    std::int64_t max_busy = 0;     // item ticks of the busiest thread
};

class TickProfile {
public:
    static constexpr std::uint32_t kDayStart = 0;           // segment end: entering 0x758430
    static constexpr std::uint32_t kDayEnd = 0xFFFFFFFFu;  // segment end: leaving 0x758430

    void DayStart(std::int32_t day, std::int64_t now) {
        in_day_ = true;
        current_ = TickDay{day, 0, 0, 0};
        day_start_ = now;
        last_site_ = kDayStart;
        last_time_ = now;
    }

    // Entering 0x769EA0 from `site`; `now` is before the original runs.
    void CheckpointEnter(std::uint32_t site, std::int64_t now) {
        if (!in_day_) return;
        segments_[{last_site_, site}].Add(now - last_time_);
        ++current_.checkpoints;
    }

    // Leaving 0x769EA0 that was entered from `site` at `entered`.
    void CheckpointLeave(std::uint32_t site, std::int64_t entered, std::int64_t now) {
        frames_[site].Add(now - entered);
        if (!in_day_) return;
        current_.frames += now - entered;
        last_site_ = site;
        last_time_ = now;
    }

    void DayEnd(std::int64_t now) {
        if (!in_day_) return;
        segments_[{last_site_, kDayEnd}].Add(now - last_time_);
        current_.total = now - day_start_;
        days_.push_back(current_);
        in_day_ = false;
    }

    std::size_t days() const { return days_.size(); }

    // Each job is stamped with the day being run (or the last one when outside a day).
    void AddJob(TickJob job) {
        job.day = current_.day;
        jobs_.push_back(job);
    }

    std::size_t jobs() const { return jobs_.size(); }

    // Per job: the wall time, the items, and the lower bound any order of the same items on the
    // same threads could reach: max(longest item, sum / threads).
    void WriteJobs(std::ostream& out, double ms_per_tick) const {
        out << "date\twall_ms\titems\tsum_ms\tmax_item_ms\tthreads\tmain_busy_ms\tmax_busy_ms\tbound_ms\n";
        for (const auto& j : jobs_) {
            const double bound = static_cast<double>(std::max(j.max_item, j.threads > 0 ? j.sum / j.threads : j.sum));
            out << Date(j.day) << "\t" << j.wall * ms_per_tick << "\t" << j.items << "\t" << j.sum * ms_per_tick
                << "\t" << j.max_item * ms_per_tick << "\t" << j.threads << "\t" << j.main_busy * ms_per_tick << "\t"
                << j.max_busy * ms_per_tick << "\t" << bound * ms_per_tick << "\n";
        }
    }

    void WriteSegments(std::ostream& out, double ms_per_tick) const {
        out << "from\tto\tcalls\ttotal_ms\tmax_ms\n";
        for (const auto& [key, span] : segments_) {
            out << Site(key.first) << "\t" << Site(key.second) << "\t" << span.calls << "\t"
                << span.total * ms_per_tick << "\t" << span.max * ms_per_tick << "\n";
        }
    }

    void WriteFrames(std::ostream& out, double ms_per_tick) const {
        out << "site\tcalls\ttotal_ms\tmax_ms\n";
        for (const auto& [site, span] : frames_) {
            out << Site(site) << "\t" << span.calls << "\t" << span.total * ms_per_tick << "\t"
                << span.max * ms_per_tick << "\n";
        }
    }

    void WriteDays(std::ostream& out, double ms_per_tick) const {
        out << "date\ttotal_ms\tframes_ms\tcheckpoints\n";
        for (const auto& d : days_) {
            out << Date(d.day) << "\t" << d.total * ms_per_tick << "\t" << d.frames * ms_per_tick << "\t"
                << d.checkpoints << "\n";
        }
    }

    // "1720.3.1" for a day index (EU4's calendar: 365-day years, no leap days).
    static std::string Date(std::int32_t day) {
        static constexpr int kMonths[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        const int year = day / 365;
        int rest = day % 365;
        int month = 0;
        while (month < 11 && rest >= kMonths[month]) rest -= kMonths[month++];
        return std::to_string(year) + "." + std::to_string(month + 1) + "." + std::to_string(rest + 1);
    }

private:
    static std::string Site(std::uint32_t site) {
        if (site == kDayStart) return "start";
        if (site == kDayEnd) return "end";
        char text[16];
        std::snprintf(text, sizeof text, "0x%X", site);
        return text;
    }

    bool in_day_ = false;
    TickDay current_;
    std::int64_t day_start_ = 0;
    std::uint32_t last_site_ = kDayStart;
    std::int64_t last_time_ = 0;
    std::map<std::pair<std::uint32_t, std::uint32_t>, TickSpan> segments_;
    std::map<std::uint32_t, TickSpan> frames_;
    std::vector<TickDay> days_;
    std::vector<TickJob> jobs_;
};

}  // namespace sb
