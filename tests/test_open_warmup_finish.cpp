// open_warmup's end of learning at the game-ready event (FinishOpenWarmup), with the module's source
// included so its state is visible: learning stops also when there is no profile to save it to.
#include "../src/modules/open_warmup.cpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

int main() {
    namespace fs = std::filesystem;
    const fs::path file = fs::temp_directory_path() / ("sb-warmup-finish-" + std::to_string(GetCurrentProcessId()) + ".txt");
    {
        HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        assert(h != INVALID_HANDLE_VALUE);
        CloseHandle(h);
    }
    const auto open = [&] {
        HANDLE h = SB_LearnCreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        assert(h != INVALID_HANDLE_VALUE);
        CloseHandle(h);
    };

    // Learning on, as InstallOpenWarmup leaves it; the starter has not set a profile target (no
    // dlc_load.json, or its list is still being built when the game is ready).
    std::vector<LearnRecord> table(kLearnCapacity);
    g_learn = table.data();
    g_realCreateFileW = &CreateFileW;
    InterlockedExchange(&EU4SB_OpenWarmupLearn.mode, 1);
    open();
    assert(EU4SB_OpenWarmupLearn.requests == 1 && g_learnNext == 1);

    sb::FinishOpenWarmup();
    assert(g_learnStopped == 1);
    // Nothing to save to: no profile written, no failure counted.
    assert(EU4SB_OpenWarmupLearn.initialized == 0 && EU4SB_OpenWarmupLearn.cacheDisabled == 0);
    // Opens after ready are forwarded and no longer claim rows.
    open();
    open();
    assert(EU4SB_OpenWarmupLearn.requests == 1 && g_learnNext == 1);

    fs::remove(file);
    std::printf("open_warmup finish ok\n");
    return 0;
}
