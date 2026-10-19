// Diagnostic (off by default): time spent reading ZIP members through PhysFS,
// per thread, until the game is ready. It bounds what ram_prefetch can take off
// the critical path (lab docs: docs/startup-ram-prototype.md).
//
// Wraps the read slot of the ZIP archiver's Io template (copied into every Io
// the archiver opens; Anvil swap_pointer) and the second mz_inflate call site
// outside it (redirect_call). Nothing is changed: each wrapper calls the
// original and adds elapsed QPC time.
#include "modules.hpp"

namespace {

using InflateFn = int (*)(void*, int);

constexpr int kThreads = 48;

// The rest of the slot's chain, read on each call (Anvil rewrites it while the chain changes).
anvil::PointerSwap<std::int64_t(void*, void*, std::uint64_t)> g_readSlot;
anvil::FunctionHook<int(void*, int)> g_inflateHook;
InflateFn g_inflate = nullptr;  // the rest of the redirected call's chain
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
    if (sb::ReadyReached()) return g_readSlot.previous()(io, buffer, length);
    const LONG64 start = sb::Ticks();
    const std::int64_t result = g_readSlot.previous()(io, buffer, length);
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

bool InstallZipProfile(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_mainThread = GetCurrentThreadId();
    host.swap(g_readSlot, sym::zip_io_read_slot, &SB_ProfileZipRead);
    try {
        host.redirect(g_inflateHook, sym::mz_inflate_call, &SB_ProfileInflate);
        g_inflate = g_inflateHook.next_pointer();
    } catch (const anvil::Error& e) {
        Log(ANVIL_LOG_WARNING, "zip_profile: inflate not timed (%s)", e.what());
    }
    return true;
}

void ReportZipProfile() {
    if (!g_readSlot.active()) return;
    for (const auto& row : g_rows) {
        if (row.tid == 0) break;
        if (row.readCalls == 0 && row.inflateCalls == 0) continue;
        Log(ANVIL_LOG_INFO, "zip_profile: thread %ld%s: zip reads %lld (%.1f MB) %.1f ms; other inflate %lld %.1f ms",
            row.tid, static_cast<DWORD>(row.tid) == g_mainThread ? " (main)" : "", row.readCalls,
            row.readBytes / 1048576.0, TicksToMs(row.readTicks), row.inflateCalls, TicksToMs(row.inflateTicks));
    }
}

}  // namespace sb
