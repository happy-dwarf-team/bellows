// frame_pacing: at the fastest speed, frames drawn from inside the daily tick come less often
// (lab docs/simulation-profile.md, "Klatki rysowane w trakcie ticku").
//
// 0x769EA0 is called about 40 times between the stages of the daily tick. It draws a frame
// (0x14C25A0 with outer = 0) when the time since the last one exceeds a threshold:
// base [0x245B070] (1.0 unless the player's settings object says otherwise) times a table
// indexed by the game speed at +0x1E08 (0.02 for indexes 0-3, 0.05 for 4). In the lab these
// frames took about 16% of the main thread's time at speed 5. A mid hook after the clock read
// replaces the threshold in xmm6 for index 4 only; the game still decides, with its own clock
// and its own last-frame time, whether to draw. The simulation does not depend on these
// frames: they only draw and handle input sooner. A second mid hook on the drawing branch
// counts the frames per speed index.
#include "../settings.hpp"
#include "modules.hpp"

#include <fstream>
#include <string>

namespace {

constexpr std::int32_t kFastest = 4;  // the last entry of the 5-entry table at 0x1C33E18
constexpr int kIndexes = 6;           // 0-4, and 5 for anything else

std::int64_t g_intervalMs = 250;  // 0: keep the game's threshold, count only
float g_interval = 0.25f;         // seconds, the unit of the game's clock in 0x769EA0
std::size_t g_speedOffset = 0;
anvil::Change g_compare, g_render;
volatile LONG64 g_calls[kIndexes] = {}, g_frames[kIndexes] = {};
volatile LONG64 g_replaced = 0;
volatile LONG g_dirty = 0;
std::wstring* g_dir = nullptr;

int IndexOf(const anvil::Cpu& cpu) noexcept {
    const auto index = sb::Read<std::int32_t>(reinterpret_cast<const void*>(cpu.rsi), g_speedOffset);
    return index >= 0 && index < kIndexes - 1 ? index : kIndexes - 1;
}

void WriteFile() {
    try {
        const std::wstring path = *g_dir + L"\\frame_pacing.tsv";
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << "key\tvalue\n"
                << "interval_ms\t" << g_intervalMs << "\n"
                << "replaced\t" << g_replaced << "\n";
            for (int i = 0; i < kIndexes; ++i) {
                out << "calls_" << i << "\t" << g_calls[i] << "\n"
                    << "frames_" << i << "\t" << g_frames[i] << "\n";
            }
        }
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) {
    }
}

DWORD WINAPI WriterThread(void*) {
    for (;;) {
        Sleep(5000);
        if (InterlockedExchange(&g_dirty, 0) != 0) WriteFile();
    }
}

}  // namespace

extern "C" {
// requests = threshold checks, hits = thresholds replaced, skips = frames drawn, mode = 1 on.
__declspec(dllexport) sb::Stats EU4SB_FramePacingStats = SB_STATS_INIT;
}

namespace sb {

void SetFramePacingInterval(std::int64_t milliseconds) {
    g_intervalMs = milliseconds;
    g_interval = static_cast<float>(milliseconds) / 1000.0f;
}

bool InstallFramePacing(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    try {
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    g_speedOffset = static_cast<std::size_t>(host.value(sym::nested_frame_threshold, "speed_offset"));
    Undo undo;
    g_compare = host.mid_hook(sym::nested_frame_compare, [](anvil::Cpu& cpu) {
        const int index = IndexOf(cpu);
        InterlockedIncrement64(&g_calls[index]);
        InterlockedIncrement64(&EU4SB_FramePacingStats.requests);
        // Only ever longer than the game's own threshold: the module never draws more often than the game.
        if (index == kFastest && g_intervalMs > 0 && g_interval > cpu.xmm[6].f32[0]) {
            cpu.xmm[6].f32[0] = g_interval;
            InterlockedIncrement64(&g_replaced);
            InterlockedIncrement64(&EU4SB_FramePacingStats.hits);
        }
    });
    undo.add(g_compare);
    g_render = host.mid_hook(sym::nested_frame_render, [](anvil::Cpu& cpu) {
        InterlockedIncrement64(&g_frames[IndexOf(cpu)]);
        InterlockedIncrement64(&EU4SB_FramePacingStats.skips);
        InterlockedExchange(&g_dirty, 1);
    });
    undo.add(g_render);
    undo.keep();
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows frame_pacing writer");
        CloseHandle(thread);
    }
    InterlockedExchange(&EU4SB_FramePacingStats.mode, 1);
    InterlockedExchange(&EU4SB_FramePacingStats.initialized, 1);
    if (g_intervalMs > 0) {
        Log(ANVIL_LOG_INFO, "frame_pacing: frames inside the daily tick at most every %lld ms at the fastest speed; "
                            "counters in frame_pacing.tsv every 5 s",
            static_cast<long long>(g_intervalMs));
    } else {
        Log(ANVIL_LOG_INFO, "frame_pacing: the game's own interval, counting only; counters in frame_pacing.tsv every 5 s");
    }
    return true;
}

void ReportFramePacing() {
    if (EU4SB_FramePacingStats.initialized == 0) return;
    bench::Get().set("frame_pacing.interval_ms", g_intervalMs);
}

}  // namespace sb
