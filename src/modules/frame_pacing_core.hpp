#pragma once

// frame_pacing's choice of threshold for one check of 0x769EA0 at the fastest speed. Plain data; the
// module passes the registers and a probe of the input queues, a test passes numbers.

namespace sb {

// What waits for the game's next frame.
enum class Waiting { Nothing, Motion, Press };

// Shortest time between two frames drawn for a waiting press (a frame takes the press out of the queue).
constexpr double kPressGap = 0.010;

// The game's own threshold unless the module's interval is longer. With draw_on_input, while input
// waits: a key press, click or wheel turn gets a frame at once (at most every kPressGap, the only case
// where the module draws sooner than the game), mouse motion the game's own threshold. waiting() runs
// only when the module's interval is longer and at most once per check.
template <class InputWaiting>
float PacedThreshold(float game, float interval, double elapsed, bool draw_on_input, InputWaiting&& waiting) {
    if (!(interval > game)) return game;
    if (!draw_on_input) return interval;
    switch (waiting()) {
    case Waiting::Press:
        return elapsed >= kPressGap ? 0.0f : interval;
    case Waiting::Motion:
        return game;
    default:
        return interval;
    }
}

}  // namespace sb
