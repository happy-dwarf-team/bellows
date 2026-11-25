#pragma once

// conquer_scan: what the game's parallel loop (0x191F30) does around its loop, so a loop on the calling thread
// runs in the same state (symbols.toml, "conquer_scan").

#include "../common.hpp"

namespace sb::serial_loop {

// Resolves the symbols; true when they are there. A second call does nothing new.
bool Resolve(anvil::Host& host);

// For the lifetime of the scope, as the parallel loop for its loop: the game's "inside a parallel loop" byte is 1,
// and while the calling thread's random log is on, the log is in its collecting mode and is collected at the
// end. The parallel loop does nothing of this for an empty array; neither does the caller.
class GameLoopScope {
public:
    GameLoopScope() noexcept;
    ~GameLoopScope();
    GameLoopScope(const GameLoopScope&) = delete;
    GameLoopScope& operator=(const GameLoopScope&) = delete;

private:
    unsigned char savedFlag_;
    bool logging_ = false;
    std::uint32_t savedLogFlags_ = 0;
};

// The game's array of pointers (begin, end).
struct PointerArray {
    void* const* begin;
    void* const* end;
    std::size_t size() const noexcept { return static_cast<std::size_t>(end - begin); }
};

}  // namespace sb::serial_loop
