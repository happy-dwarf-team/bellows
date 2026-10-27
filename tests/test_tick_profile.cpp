// TickProfile (tick_profile counters): segments between checkpoints, frame time left out,
// day rows, checkpoints outside a day, dates.
#include "../src/modules/tick_profile_core.hpp"

#include <cassert>
#include <cstdio>
#include <sstream>
#include <string>

namespace {

bool Has(const std::string& text, const std::string& line) { return text.find(line + "\n") != std::string::npos; }

}  // namespace

int main() {
    sb::TickProfile p;
    // A checkpoint outside a day: frame time counted, no segment, no day.
    p.CheckpointEnter(0x100, 0);
    p.CheckpointLeave(0x100, 0, 5);

    // Day 1720.1.1: start at 100, checkpoint A at 130 (frame until 150), B at 170 (no frame), end at 200.
    const std::int32_t day = 1720 * 365;
    p.DayStart(day, 100);
    p.CheckpointEnter(0xA, 130);
    p.CheckpointLeave(0xA, 130, 150);
    p.CheckpointEnter(0xB, 170);
    p.CheckpointLeave(0xB, 170, 170);
    p.DayEnd(200);
    // The next day: the same path, longer first segment.
    p.DayStart(day + 31, 300);
    p.CheckpointEnter(0xA, 360);
    p.CheckpointLeave(0xA, 360, 361);
    p.CheckpointEnter(0xB, 371);
    p.CheckpointLeave(0xB, 371, 371);
    p.DayEnd(381);
    // Leaving a day twice changes nothing.
    p.DayEnd(999);
    assert(p.days() == 2);

    std::ostringstream segments, frames, days;
    p.WriteSegments(segments, 1.0);
    p.WriteFrames(frames, 1.0);
    p.WriteDays(days, 1.0);
    const std::string s = segments.str(), f = frames.str(), d = days.str();
    std::printf("%s%s%s", s.c_str(), f.c_str(), d.c_str());

    assert(Has(s, "start\t0xA\t2\t90\t60"));  // 30 + 60
    assert(Has(s, "0xA\t0xB\t2\t30\t20"));    // 20 + 10: the frame time is not in the segment
    assert(Has(s, "0xB\tend\t2\t40\t30"));    // 30 + 10
    assert(s.find("0x100") == std::string::npos);
    assert(Has(f, "0x100\t1\t5\t5"));
    assert(Has(f, "0xA\t2\t21\t20"));
    assert(Has(d, "1720.1.1\t100\t20\t2"));
    assert(Has(d, "1720.2.1\t81\t1\t2"));

    assert(sb::TickProfile::Date(0) == "0.1.1");
    assert(sb::TickProfile::Date(1720 * 365 + 364) == "1720.12.31");
    assert(sb::TickProfile::Date(1720 * 365 + 59) == "1720.3.1");
    std::printf("tick_profile ok\n");
    return 0;
}
