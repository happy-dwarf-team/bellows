#pragma once

// Shared plumbing for the modules: counters in the lab's 80-byte layout, the
// game-ready flag, the log, the undo list of a module's changes and small
// memory helpers. The Anvil host is anvil::Host{} (anvil.hpp), usable on any
// thread once the plugin is loading.

#include <anvil/anvil.hpp>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace sb {

// Same 80-byte layout as the lab's CacheStatsLayout, so lab tools can read the
// exported counters with GetProcAddress.
struct Stats {
    std::uint32_t structSize;
    std::uint32_t layoutVersion;
    volatile LONG64 requests;
    volatile LONG64 hits;
    volatile LONG64 misses;
    volatile LONG64 skips;
    volatile LONG64 mismatches;
    volatile LONG64 duplicateKeys;
    volatile LONG64 verifications;
    volatile LONG readyDisabled;
    volatile LONG cacheDisabled;
    volatile LONG mode;
    volatile LONG initialized;
};
static_assert(sizeof(Stats) == 80, "stats layout drift");
static_assert(offsetof(Stats, readyDisabled) == 0x40, "stats layout drift");

#define SB_STATS_INIT {sizeof(::sb::Stats), 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

inline void Increment(volatile LONG64* value) noexcept { InterlockedIncrement64(value); }

enum class Mode { Off, On, Verify };

// 1 once anvil-game published its ready event (the game finished
// initializing, the main menu is up). A plain volatile read: aligned 32-bit
// loads are atomic on x64, and the hot paths read it on every call.
inline volatile LONG g_gameReady = 0;

inline bool ReadyReached() noexcept { return g_gameReady != 0; }

template <typename T>
inline T Read(const void* base, std::size_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const unsigned char*>(base) + offset, sizeof(T));
    return value;
}

inline LONG64 Ticks() noexcept {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}
double TicksToMs(LONG64 ticks);

// Adds the time spent in a scope to *sink (QPC ticks).
struct ScopedTicks {
    volatile LONG64* sink;
    LONG64 start;
    explicit ScopedTicks(volatile LONG64* s) noexcept : sink(s), start(Ticks()) {}
    ~ScopedTicks() { InterlockedExchangeAdd64(sink, Ticks() - start); }
    ScopedTicks(const ScopedTicks&) = delete;
    ScopedTicks& operator=(const ScopedTicks&) = delete;
};

void Log(AnvilLogLevel level, const char* format, ...);
void LogStats(const char* name, const Stats& stats);

// The changes one module requested so far. A refused change throws
// anvil::Error out of the module's Install; on the way out the destructor
// removes the module's earlier changes (staged, so the game never saw them),
// and the other modules stay. keep() once every change is in.
class Undo {
public:
    Undo() = default;
    Undo(const Undo&) = delete;
    Undo& operator=(const Undo&) = delete;
    ~Undo() {
        for (auto step = steps_.rbegin(); step != steps_.rend(); ++step) (*step)();
    }

    // An anvil::FunctionHook or anvil::PointerSwap (a global of the module).
    template <class H> void add(H& hook) {
        steps_.push_back([&hook] { hook.remove(); });
    }
    void add(anvil::Change& change) {
        steps_.push_back([&change] { change.reset(); });
    }
    void keep() noexcept { steps_.clear(); }

private:
    std::vector<std::function<void()>> steps_;
};

}  // namespace sb
