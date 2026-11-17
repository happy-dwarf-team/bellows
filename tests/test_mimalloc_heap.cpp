// The mimalloc heap functions without the import slots: what each mode does
// before and after the game-ready event.
#include "../src/common.hpp"

#include <mimalloc.h>
#include <psapi.h>

#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

extern "C" {
LPVOID WINAPI SB_HeapAlloc(HANDLE heap, DWORD flags, SIZE_T size);
BOOL WINAPI SB_HeapFree(HANDLE heap, DWORD flags, LPVOID pointer);
LPVOID WINAPI SB_HeapReAlloc(HANDLE heap, DWORD flags, LPVOID pointer, SIZE_T size);
SIZE_T WINAPI SB_HeapSize(HANDLE heap, DWORD flags, LPCVOID pointer);
extern sb::Stats EU4SB_MimallocStats;
}

namespace sb {
void InitMimallocForTest(Mode mode);
void CollectMimalloc(const char* reason);
bool CollectMimallocIfDue();
}

namespace {

const HANDLE kHeap = GetProcessHeap();

bool Owned(const void* p) { return mi_is_in_heap_region(p); }

unsigned char* Filled(std::size_t size, unsigned char value) {
    auto* p = static_cast<unsigned char*>(SB_HeapAlloc(kHeap, 0, size));
    assert(p != nullptr);
    for (std::size_t i = 0; i < size; ++i) p[i] = value;
    return p;
}

bool All(const unsigned char* p, std::size_t from, std::size_t to, unsigned char value) {
    for (std::size_t i = from; i < to; ++i) {
        if (p[i] != value) return false;
    }
    return true;
}

void StartupMode() {
    sb::g_gameReady = 0;
    sb::InitMimallocForTest(sb::Mode::Startup);

    // Before ready: mimalloc, exact sizes, zeroing.
    unsigned char* kept = Filled(100, 0xA1);
    unsigned char* grown = Filled(100, 0xB2);
    unsigned char* shrunk = Filled(100, 0xC3);
    auto* zeroed = static_cast<unsigned char*>(SB_HeapAlloc(kHeap, HEAP_ZERO_MEMORY, 64));
    assert(Owned(kept) && Owned(grown) && Owned(shrunk) && Owned(zeroed));
    assert(All(zeroed, 0, 64, 0));
    assert(SB_HeapSize(kHeap, 0, kept) == 100);
    std::vector<void*> early;
    for (int i = 0; i < 4000; ++i) early.push_back(Filled(32 + i % 200, 0x5A));

    sb::g_gameReady = 1;

    // After ready: new blocks come from the Windows heap.
    auto* late = static_cast<unsigned char*>(SB_HeapAlloc(kHeap, HEAP_ZERO_MEMORY, 300));
    assert(late != nullptr && !Owned(late));
    assert(All(late, 0, 300, 0));
    assert(SB_HeapSize(kHeap, 0, late) == 300);
    late = static_cast<unsigned char*>(SB_HeapReAlloc(kHeap, 0, late, 600));
    assert(late != nullptr && !Owned(late) && SB_HeapSize(kHeap, 0, late) == 600);
    assert(SB_HeapFree(kHeap, 0, late));

    // Blocks from before ready keep their owner for sizes and frees.
    assert(SB_HeapSize(kHeap, 0, kept) == 100 && All(kept, 0, 100, 0xA1));

    // A realloc moves a mimalloc block to the Windows heap, keeping its bytes.
    grown = static_cast<unsigned char*>(SB_HeapReAlloc(kHeap, HEAP_ZERO_MEMORY, grown, 250));
    assert(grown != nullptr && !Owned(grown));
    assert(SB_HeapSize(kHeap, 0, grown) == 250);
    assert(All(grown, 0, 100, 0xB2) && All(grown, 100, 250, 0));
    shrunk = static_cast<unsigned char*>(SB_HeapReAlloc(kHeap, 0, shrunk, 40));
    assert(shrunk != nullptr && !Owned(shrunk));
    assert(SB_HeapSize(kHeap, 0, shrunk) == 40 && All(shrunk, 0, 40, 0xC3));

    // Frees of early blocks on other threads, next to late blocks of their own.
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&early, t] {
            for (std::size_t i = t; i < early.size(); i += 4) {
                void* own = SB_HeapAlloc(kHeap, 0, 48);
                assert(own != nullptr && !Owned(own));
                assert(SB_HeapFree(kHeap, 0, early[i]));
                assert(SB_HeapFree(kHeap, 0, own));
            }
        });
    }
    for (auto& thread : threads) thread.join();

    for (void* p : {static_cast<void*>(kept), static_cast<void*>(grown), static_cast<void*>(shrunk),
                    static_cast<void*>(zeroed)}) {
        assert(SB_HeapFree(kHeap, 0, p));
    }
    // Calls for Windows-heap blocks after the switch are not misses.
    assert(EU4SB_MimallocStats.misses == 0);
    assert(EU4SB_MimallocStats.mismatches == 0);
}

double CommitMiB() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    return static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
}

// After the switch nothing allocates from mimalloc, so blocks freed later stay committed
// until a collect: here ~260 MiB from threads that stay alive, freed on a thread that ends;
// the frame-time check collects once 64 MiB were freed, and not before.
void StartupModeReturnsMemory() {
    sb::g_gameReady = 0;
    sb::InitMimallocForTest(sb::Mode::Startup);
    constexpr int kThreads = 4;
    constexpr std::size_t kPerThread = 120000;
    std::vector<std::vector<void*>> blocks(kThreads);
    std::atomic<int> allocated{0};
    std::atomic<bool> leave{false};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (std::size_t i = 0; i < kPerThread; ++i) blocks[t].push_back(Filled(32 + (i * 37) % 900, 1));
            ++allocated;
            while (!leave) Sleep(1);  // alive, like the game's workers
        });
    }
    while (allocated < kThreads) Sleep(1);
    void* last = Filled(64, 2);
    sb::g_gameReady = 1;
    sb::CollectMimalloc("ready");  // as plugin.cpp does; nothing was freed yet
    assert(!sb::CollectMimallocIfDue());
    std::thread([&] {
        for (auto& list : blocks) {
            for (void* p : list) assert(SB_HeapFree(kHeap, 0, p));
        }
    }).join();
    const double freed = CommitMiB();
    assert(sb::CollectMimallocIfDue());
    const double collected = CommitMiB();
    std::printf("mimalloc_heap: commit after the frees %.0f MiB, after the frame-time collect %.0f MiB\n", freed,
                collected);
    // Wine reports no private usage (PrivateUsage stays 0), so the size of the return cannot be measured there;
    // the collect itself still ran (CollectMimallocIfDue above) and the checks after it still apply.
    if (freed == 0.0 && collected == 0.0) {
        std::printf("mimalloc_heap: skipped the size check, the process reports no private usage (Wine)\n");
    } else {
        assert(freed - collected > 150.0);
    }
    assert(SB_HeapFree(kHeap, 0, last));  // a small free afterwards: no collect
    assert(!sb::CollectMimallocIfDue());
    leave = true;
    for (auto& thread : threads) thread.join();
}

void WholeSessionMode() {
    sb::g_gameReady = 1;
    sb::InitMimallocForTest(sb::Mode::On);
    unsigned char* p = Filled(100, 0x11);
    assert(Owned(p));
    p = static_cast<unsigned char*>(SB_HeapReAlloc(kHeap, 0, p, 400));
    assert(p != nullptr && Owned(p) && SB_HeapSize(kHeap, 0, p) == 400 && All(p, 0, 100, 0x11));
    assert(SB_HeapFree(kHeap, 0, p));

    // A Windows-heap block still goes to kernel32, counted as a miss.
    void* foreign = HeapAlloc(kHeap, 0, 16);
    const LONG64 misses = EU4SB_MimallocStats.misses;
    assert(SB_HeapFree(kHeap, 0, foreign));
    assert(EU4SB_MimallocStats.misses == misses + 1);
}

// Blocks of 1 MiB and more stay in the Windows heap in both modes (anvil#426: the game reads past
// the end of its province array, which must not end where mimalloc's committed memory ends).
void LargeBlocks() {
    constexpr std::size_t kLarge = std::size_t{1} << 20;
    sb::g_gameReady = 0;
    sb::InitMimallocForTest(sb::Mode::Startup);
    const LONG64 large = EU4SB_MimallocStats.duplicateKeys;

    void* below = SB_HeapAlloc(kHeap, 0, kLarge - 1);
    assert(below != nullptr && Owned(below));
    auto* zeroed = static_cast<unsigned char*>(SB_HeapAlloc(kHeap, HEAP_ZERO_MEMORY, kLarge));
    assert(zeroed != nullptr && !Owned(zeroed));
    assert(All(zeroed, 0, kLarge, 0) && SB_HeapSize(kHeap, 0, zeroed) == kLarge);

    // A mimalloc block grown to the limit moves out, keeping its bytes and zeroing the rest.
    unsigned char* grown = Filled(1000, 0x77);
    assert(Owned(grown));
    grown = static_cast<unsigned char*>(SB_HeapReAlloc(kHeap, HEAP_ZERO_MEMORY, grown, 2 * kLarge));
    assert(grown != nullptr && !Owned(grown) && SB_HeapSize(kHeap, 0, grown) == 2 * kLarge);
    assert(All(grown, 0, 1000, 0x77) && All(grown, 1000, 2 * kLarge, 0));
    // Shrunk again, it stays in the Windows heap.
    grown = static_cast<unsigned char*>(SB_HeapReAlloc(kHeap, 0, grown, 500));
    assert(grown != nullptr && !Owned(grown) && SB_HeapSize(kHeap, 0, grown) == 500 && All(grown, 0, 500, 0x77));
    assert(EU4SB_MimallocStats.duplicateKeys == large + 2);
    for (void* p : {below, static_cast<void*>(zeroed), static_cast<void*>(grown)}) assert(SB_HeapFree(kHeap, 0, p));

    // The whole-session mode keeps them out as well.
    sb::g_gameReady = 1;
    sb::InitMimallocForTest(sb::Mode::On);
    void* session = SB_HeapAlloc(kHeap, 0, 4 * kLarge);
    assert(session != nullptr && !Owned(session) && SB_HeapSize(kHeap, 0, session) == 4 * kLarge);
    assert(SB_HeapFree(kHeap, 0, session));
    void* small = SB_HeapAlloc(kHeap, 0, 4096);
    assert(small != nullptr && Owned(small) && SB_HeapFree(kHeap, 0, small));
    assert(EU4SB_MimallocStats.mismatches == 0);
}

// HeapAlloc returns MEMORY_ALLOCATION_ALIGNMENT (16 bytes on x64) for every size, and the game's CRT
// passes that on to malloc and operator new. mimalloc keeps it only because MI_MAX_ALIGN_SIZE (16)
// rounds its small bins to two words (MI_ALIGN2W in page-queue.c); bins of 24, 40 or 56 bytes would
// return 8-byte aligned blocks for requests of 1-8, 17-24 and 33-40 bytes with the 16-byte header.
// This pins it across mimalloc updates, for allocations and reallocs.
void Alignment() {
    sb::g_gameReady = 0;
    sb::InitMimallocForTest(sb::Mode::On);
    std::vector<void*> blocks;
    for (std::size_t size = 0; size <= 1024; ++size) {
        for (int i = 0; i < 4; ++i) {
            void* p = SB_HeapAlloc(kHeap, i % 2 ? HEAP_ZERO_MEMORY : 0, size);
            assert(p != nullptr && Owned(p));
            assert(reinterpret_cast<std::uintptr_t>(p) % MEMORY_ALLOCATION_ALIGNMENT == 0);
            assert(SB_HeapSize(kHeap, 0, p) == size);
            blocks.push_back(p);
        }
    }
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        void* p = SB_HeapReAlloc(kHeap, 0, blocks[i], (i * 7) % 300);
        assert(p != nullptr && Owned(p));
        assert(reinterpret_cast<std::uintptr_t>(p) % MEMORY_ALLOCATION_ALIGNMENT == 0);
        assert(SB_HeapSize(kHeap, 0, p) == (i * 7) % 300);
        assert(SB_HeapFree(kHeap, 0, p));
    }
    assert(EU4SB_MimallocStats.mismatches == 0);
}

}  // namespace

int main() {
    Alignment();
    StartupModeReturnsMemory();
    StartupMode();
    WholeSessionMode();
    LargeBlocks();
    std::puts("mimalloc_heap: ok");
    return 0;
}
