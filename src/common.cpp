#include "common.hpp"

#include <cstdarg>

namespace sb {

Context& context() {
    static Context instance;
    return instance;
}

void Log(AnvilLogLevel level, const char* format, ...) {
    const Context& c = context();
    if (c.host == nullptr) return;
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    c.host->log(c.self, level, buffer);
}

void LogStats(const char* name, const Stats& s) {
    if (s.mode == 0 && s.requests == 0) return;  // module not installed
    Log(ANVIL_LOG_INFO,
        "%s: requests %lld, hits %lld, misses %lld, skips %lld, verifications %lld, mismatches %lld, extra %lld%s%s",
        name, s.requests, s.hits, s.misses, s.skips, s.verifications, s.mismatches, s.duplicateKeys,
        s.cacheDisabled ? ", DISABLED after a mismatch" : "", s.readyDisabled ? ", stopped at ready" : "");
}

const char* ResultName(AnvilResult result) {
    switch (result) {
    case ANVIL_OK: return "ok";
    case ANVIL_ERROR_INVALID_ARGUMENT: return "invalid argument";
    case ANVIL_ERROR_BYTES_MISMATCH: return "bytes mismatch";
    case ANVIL_ERROR_CONFLICT: return "conflict with another plugin";
    case ANVIL_ERROR_OUT_OF_MEMORY: return "out of memory";
    case ANVIL_ERROR_NOT_FOUND: return "not found";
    case ANVIL_ERROR_UNSUPPORTED: return "unsupported";
    case ANVIL_ERROR_WRONG_PHASE: return "wrong phase";
    case ANVIL_ERROR_ALREADY_EXISTS: return "already exists";
    case ANVIL_ERROR_INTERNAL: return "internal error";
    }
    return "unknown";
}

}  // namespace sb
