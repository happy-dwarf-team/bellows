#include "common.hpp"

#include <cstdarg>

namespace sb {

double TicksToMs(LONG64 ticks) {
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    return static_cast<double>(ticks) * 1000.0 / frequency;
}

void Log(AnvilLogLevel level, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    anvil::Host{}.log(level, buffer);  // nothing before the plugin loads
}

void LogStats(const char* name, const Stats& s) {
    if (s.mode == 0 && s.requests == 0) return;  // module not installed
    Log(ANVIL_LOG_INFO,
        "%s: requests %lld, hits %lld, misses %lld, skips %lld, verifications %lld, mismatches %lld, extra %lld%s%s",
        name, s.requests, s.hits, s.misses, s.skips, s.verifications, s.mismatches, s.duplicateKeys,
        s.cacheDisabled ? ", DISABLED after a mismatch" : "", s.readyDisabled ? ", stopped at ready" : "");
}

}  // namespace sb
