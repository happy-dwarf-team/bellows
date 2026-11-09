// checksum_defer (PROTOTYPE, lab #66, off by default): the game's checksum task starts reading the files of
// checksum_manifest.txt only once CEU4Application::LoadDatabases has returned, instead of right before it.
//
// InitGame pushes CCreateChecksumTask to the LongTasks pool just before LoadDatabases (lab #15, #47); the task then
// competes with the databases for the VFS lock and the PhysFS state lock. Here the task's invoke (0x2178D0) waits on
// an event that the return of LoadDatabases sets, then runs as the game wrote it: same files, same order, same
// digest, same log lines (only their place in error.log moves). LongTasks has no other task during the databases
// (lab #47), so the waiting worker holds up nobody. A timeout of 120 s runs the task anyway.
//
// The counters go to anvil.log, each as ms since the process was created (the harness's clock):
// the task's start and wait, LoadDatabases' return, the task's end (its SetDone follows right after) and the first
// CEU4Application::UpdateChecksumIfNeeded that returned true (the menu has the checksum from then on).
#include "modules.hpp"

namespace {

anvil::HookOf<sym::checksum_task_invoke> g_invokeHook;
anvil::HookOf<sym::load_databases> g_loadHook;
anvil::HookOf<sym::update_checksum_if_needed> g_updateHook;
decltype(g_invokeHook.next_pointer()) g_invokeNext = nullptr;
decltype(g_loadHook.next_pointer()) g_loadNext = nullptr;
decltype(g_updateHook.next_pointer()) g_updateNext = nullptr;
HANDLE g_databasesDone = nullptr;
bool g_defer = true;  // false: "verify" only logs the times, the task starts as the game starts it
volatile LONG g_loggedUpdate = 0;
std::uint64_t g_created = 0;
constexpr DWORD kWaitMs = 120000;

double SinceCreation() noexcept {
    FILETIME now{};
    GetSystemTimePreciseAsFileTime(&now);
    const std::uint64_t t = (static_cast<std::uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    return static_cast<double>(t - g_created) / 10000.0;
}

void InvokeDetour(void* buffer) {
    const double start = SinceCreation();
    double waited = 0.0;
    DWORD outcome = WAIT_OBJECT_0;
    if (g_defer) {
        outcome = WaitForSingleObject(g_databasesDone, kWaitMs);
        waited = SinceCreation() - start;
    }
    sb::Log(ANVIL_LOG_INFO, "checksum_defer: task start %.0f ms, waited %.0f ms%s (thread %lu)", start, waited,
            outcome == WAIT_TIMEOUT ? " (TIMEOUT, ran anyway)" : "", GetCurrentThreadId());
    const double run = SinceCreation();
    g_invokeNext(buffer);
    const double end = SinceCreation();
    sb::Log(ANVIL_LOG_INFO, "checksum_defer: task done %.0f ms (ran %.0f ms)", end, end - run);
}

void LoadDatabasesDetour(void* app) {
    g_loadNext(app);
    SetEvent(g_databasesDone);
    sb::Log(ANVIL_LOG_INFO, "checksum_defer: LoadDatabases returned %.0f ms", SinceCreation());
}

bool UpdateDetour(void* app) {
    const bool got = g_updateNext(app);
    if (got && InterlockedExchange(&g_loggedUpdate, 1) == 0)
        sb::Log(ANVIL_LOG_INFO, "checksum_defer: UpdateChecksumIfNeeded took the checksum %.0f ms", SinceCreation());
    return got;
}

}  // namespace

namespace sb {

bool InstallChecksumDefer(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    g_defer = mode == Mode::On;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return false;
    g_created = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    g_databasesDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_databasesDone == nullptr) return false;
    Undo undo;
    host.hook(g_invokeHook, sym::checksum_task_invoke, &InvokeDetour);
    undo.add(g_invokeHook);
    g_invokeNext = g_invokeHook.next_pointer();
    host.hook(g_loadHook, sym::load_databases, &LoadDatabasesDetour);
    undo.add(g_loadHook);
    g_loadNext = g_loadHook.next_pointer();
    host.hook(g_updateHook, sym::update_checksum_if_needed, &UpdateDetour);
    undo.add(g_updateHook);
    g_updateNext = g_updateHook.next_pointer();
    undo.keep();
    Log(ANVIL_LOG_INFO, "checksum_defer: %s (prototype)",
        g_defer ? "the checksum task waits for LoadDatabases" : "verify: times only, the task is not moved");
    return true;
}

}  // namespace sb
