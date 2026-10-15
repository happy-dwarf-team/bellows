// Diagnostic (off by default): time spent reading ZIP members through PhysFS,
// per thread, until the ready marker. It bounds what ram_prefetch can take off
// the critical path (lab docs: docs/startup-ram-prototype.md).
//
// Wraps the read slot of the ZIP archiver's Io template (copied into every Io
// the archiver opens) and the second mz_inflate call site outside it. Nothing
// is changed: each wrapper calls the original and adds elapsed QPC time.
#include "modules.hpp"

namespace {

using ReadFn = std::int64_t (*)(void*, void*, std::uint64_t);
using InflateFn = int (*)(void*, int);

constexpr int kThreads = 48;

ReadFn g_read = nullptr;
InflateFn g_inflate = nullptr;
DWORD g_mainThread = 0;

struct ThreadRow {
    volatile LONG tid;
    volatile LONG64 readTicks, readCalls, readBytes;
    volatile LONG64 inflateTicks, inflateCalls;
};
ThreadRow g_rows[kThreads];

ThreadRow* Row() noexcept {
    const LONG tid = static_cast<LONG>(GetCurrentThreadId());
    for (auto& row : g_rows) {
        LONG seen = InterlockedCompareExchange(&row.tid, 0, 0);
        if (seen == 0) seen = InterlockedCompareExchange(&row.tid, tid, 0);  // 0 when we claimed it
        if (seen == 0 || seen == tid) return &row;
    }
    return nullptr;  // more threads than rows: not counted
}

}  // namespace

extern "C" std::int64_t SB_ProfileZipRead(void* io, void* buffer, std::uint64_t length) {
    if (sb::ReadyReached()) return g_read(io, buffer, length);
    const LONG64 start = sb::Ticks();
    const std::int64_t result = g_read(io, buffer, length);
    if (ThreadRow* row = Row()) {
        InterlockedExchangeAdd64(&row->readTicks, sb::Ticks() - start);
        InterlockedIncrement64(&row->readCalls);
        if (result > 0) InterlockedExchangeAdd64(&row->readBytes, result);
    }
    return result;
}

extern "C" int SB_ProfileInflate(void* stream, int flush) {
    if (sb::ReadyReached()) return g_inflate(stream, flush);
    const LONG64 start = sb::Ticks();
    const int result = g_inflate(stream, flush);
    if (ThreadRow* row = Row()) {
        InterlockedExchangeAdd64(&row->inflateTicks, sb::Ticks() - start);
        InterlockedIncrement64(&row->inflateCalls);
    }
    return result;
}

namespace sb {

bool InstallZipProfile(const Build& build, Mode mode) {
    if (mode == Mode::Off) return false;
    g_mainThread = GetCurrentThreadId();
    const AnvilHost* host = context().host;
    const uintptr_t expected = context().image_base + build.zip_read_fn;
    const uintptr_t replacement = reinterpret_cast<uintptr_t>(&SB_ProfileZipRead);
    AnvilResult r = host->patch_bytes(context().self, build.zip_io_read_slot, &expected, &replacement, sizeof(uintptr_t));
    if (r != ANVIL_OK) {
        Log(ANVIL_LOG_WARNING, "zip_profile: Io read slot refused (%s)", ResultName(r));
        return false;
    }
    g_read = reinterpret_cast<ReadFn>(expected);
    g_inflate = reinterpret_cast<InflateFn>(AtRva<unsigned char>(build.mz_inflate_fn));
    r = host->redirect_call(context().self, build.mz_inflate_other_site, build.mz_inflate_fn, reinterpret_cast<void*>(&SB_ProfileInflate));
    if (r != ANVIL_OK) Log(ANVIL_LOG_WARNING, "zip_profile: inflate site refused (%s)", ResultName(r));
    return true;
}

void ReportZipProfile() {
    if (g_read == nullptr) return;
    for (const auto& row : g_rows) {
        if (row.tid == 0) break;
        if (row.readCalls == 0 && row.inflateCalls == 0) continue;
        Log(ANVIL_LOG_INFO, "zip_profile: thread %ld%s: zip reads %lld (%.1f MB) %.1f ms; other inflate %lld %.1f ms",
            row.tid, static_cast<DWORD>(row.tid) == g_mainThread ? " (main)" : "", row.readCalls,
            row.readBytes / 1048576.0, TicksToMs(row.readTicks), row.inflateCalls, TicksToMs(row.inflateTicks));
    }
}

}  // namespace sb
