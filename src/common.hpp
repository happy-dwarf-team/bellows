#pragma once

// Shared plumbing for the modules: the Anvil host, counters in the lab's
// 80-byte layout, the ready marker and small memory helpers.

#include <anvil/anvil_plugin.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

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

struct Context {
    const AnvilHost* host = nullptr;
    AnvilPlugin* self = nullptr;
    uintptr_t image_base = 0;
    uint32_t image_size = 0;
    volatile const uint8_t* ready_marker = nullptr;  // becomes 1 when the game finished initializing
};

Context& context();

inline bool ReadyReached() noexcept {
    const auto* marker = context().ready_marker;
    return marker != nullptr && *marker == 1;
}

template <typename T>
inline T* AtRva(uint32_t rva) noexcept {
    return reinterpret_cast<T*>(context().image_base + rva);
}

template <typename T>
inline T Read(const void* base, std::size_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const unsigned char*>(base) + offset, sizeof(T));
    return value;
}

void Log(AnvilLogLevel level, const char* format, ...);
void LogStats(const char* name, const Stats& stats);

const char* ResultName(AnvilResult result);

}  // namespace sb
