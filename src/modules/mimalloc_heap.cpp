// mimalloc for the game's CRT heap (ported from the lab's startup_mimalloc_hook.inc).
//
// In eu4.exe 1.37.5 the kernel32 heap functions are imported only by the
// statically linked UCRT allocator (_malloc_base, _calloc_base, _free_base,
// _realloc_base, _msize_base; Ghidra xrefs in the lab). Replacing the four IAT
// slots moves every malloc/calloc/realloc/free/_msize of the game, and
// everything built on them, to mimalloc without patching code.
//
// Installed at the executable entry point (Anvil "early" phase): after the
// executable's TLS callbacks, before its C runtime starts. Blocks allocated
// before the switch are not owned by mimalloc and keep going to kernel32
// (counted as `misses`). Each mimalloc block carries a 16-byte header with the
// requested size so HeapSize/_msize stays exact (the UCRT's _recalloc zeroes
// from _msize). Calls for other heaps or with unsupported flags go to
// kernel32. Mode::On: active for the whole process lifetime, including gameplay.
// Mode::Startup: from the game-ready event on, new blocks come from the
// Windows heap; frees and sizes still go to the block's owner, and a realloc
// of a mimalloc block moves it to the Windows heap. The save a player loads
// later is then allocated outside mimalloc. Calls after the switch for blocks
// mimalloc does not own are not counted as misses (that is every call).
// Nothing allocates from mimalloc after the switch, so nothing runs its
// delayed purge: CollectMimalloc returns the freed pages at the ready and
// session events (the lab saw +0.37 GiB per save load without it), and
// CollectMimallocIfDue at the next frame once 64 MiB of mimalloc blocks were
// freed since the last collect (anvil-game 1.5 publishes no session event
// for a save loaded from the in-game menu; the lab saw +0.46 GiB there).
#include "modules.hpp"

#include <mimalloc.h>
#include <psapi.h>

namespace {

constexpr std::size_t kHeader = 16;
constexpr std::size_t kMagic = 0x6D6C6C6F63653475ull;

using HeapAllocFn = LPVOID(WINAPI*)(HANDLE, DWORD, SIZE_T);
using HeapFreeFn = BOOL(WINAPI*)(HANDLE, DWORD, LPVOID);
using HeapReAllocFn = LPVOID(WINAPI*)(HANDLE, DWORD, LPVOID, SIZE_T);
using HeapSizeFn = SIZE_T(WINAPI*)(HANDLE, DWORD, LPCVOID);

HeapAllocFn g_realAlloc = nullptr;
HeapFreeFn g_realFree = nullptr;
HeapReAllocFn g_realReAlloc = nullptr;
HeapSizeFn g_realSize = nullptr;
HANDLE g_processHeap = nullptr;
bool g_startupOnly = false;  // Mode::Startup

// True once Mode::Startup handed new blocks back to the Windows heap.
inline bool Switched() noexcept { return g_startupOnly && sb::ReadyReached(); }

// Bytes of mimalloc blocks freed after the switch since the last collect.
constexpr LONG64 kCollectAfterBytes = 64ll << 20;
volatile LONG64 g_freedSinceCollect = 0;

inline void CountFreed(std::size_t bytes) noexcept {
    if (Switched()) InterlockedExchangeAdd64(&g_freedSinceCollect, static_cast<LONG64>(bytes));
}

struct Header {
    std::size_t size;
    std::size_t magic;
};
static_assert(sizeof(Header) == kHeader, "header must keep 16-byte alignment");

inline bool Owned(const void* pointer) noexcept { return pointer != nullptr && mi_is_in_heap_region(pointer); }

inline void* Wrap(void* base, std::size_t size) noexcept {
    auto* header = static_cast<Header*>(base);
    header->size = size;
    header->magic = kMagic;
    return static_cast<unsigned char*>(base) + kHeader;
}

inline Header* HeaderOf(const void* pointer) noexcept {
    return reinterpret_cast<Header*>(const_cast<unsigned char*>(static_cast<const unsigned char*>(pointer)) - kHeader);
}

}  // namespace

extern "C" {
// mode = 1 when the IAT slots were replaced; misses = frees/sizes/reallocs of
// blocks mimalloc does not own (sent to kernel32); skips = calls for another
// heap or with unsupported flags; mismatches = owned blocks with a corrupt header.
__declspec(dllexport) sb::Stats EU4SB_MimallocStats = SB_STATS_INIT;
}

extern "C" LPVOID WINAPI SB_HeapAlloc(HANDLE heap, DWORD flags, SIZE_T size) {
    if (Switched()) return g_realAlloc(heap, flags, size);
    if (heap != g_processHeap || (flags & ~static_cast<DWORD>(HEAP_ZERO_MEMORY)) != 0 || size > (SIZE_MAX - kHeader)) {
        sb::Increment(&EU4SB_MimallocStats.skips);
        return g_realAlloc(heap, flags, size);
    }
    void* base = (flags & HEAP_ZERO_MEMORY) ? mi_zalloc(size + kHeader) : mi_malloc(size + kHeader);
    if (base == nullptr) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    return Wrap(base, size);
}

extern "C" BOOL WINAPI SB_HeapFree(HANDLE heap, DWORD flags, LPVOID pointer) {
    if (pointer != nullptr && heap == g_processHeap && Owned(pointer)) {
        Header* header = HeaderOf(pointer);
        if (header->magic == kMagic) {
            header->magic = 0;
            CountFreed(header->size + kHeader);
            mi_free(header);
            return TRUE;
        }
        sb::Increment(&EU4SB_MimallocStats.mismatches);
    } else if (pointer != nullptr && !Switched()) {
        sb::Increment(&EU4SB_MimallocStats.misses);
    }
    return g_realFree(heap, flags, pointer);
}

extern "C" LPVOID WINAPI SB_HeapReAlloc(HANDLE heap, DWORD flags, LPVOID pointer, SIZE_T size) {
    if (pointer != nullptr && heap == g_processHeap && Owned(pointer) &&
        (flags & ~static_cast<DWORD>(HEAP_ZERO_MEMORY)) == 0 && size <= (SIZE_MAX - kHeader)) {
        Header* header = HeaderOf(pointer);
        if (header->magic == kMagic) {
            const std::size_t old = header->size;
            if (Switched()) {
                // Out of mimalloc: HEAP_ZERO_MEMORY zeroes the new block, the copy keeps the old bytes.
                void* moved = g_realAlloc(heap, flags, size);
                if (moved == nullptr) {
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    return nullptr;  // like HeapReAlloc: the old block stays valid
                }
                std::memcpy(moved, pointer, old < size ? old : size);
                header->magic = 0;
                CountFreed(old + kHeader);
                mi_free(header);
                return moved;
            }
            void* base = mi_realloc(header, size + kHeader);
            if (base == nullptr) {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return nullptr;  // like HeapReAlloc: the old block stays valid
            }
            void* result = Wrap(base, size);
            if ((flags & HEAP_ZERO_MEMORY) && size > old) {
                std::memset(static_cast<unsigned char*>(result) + old, 0, size - old);
            }
            return result;
        }
        sb::Increment(&EU4SB_MimallocStats.mismatches);
    } else if (pointer != nullptr && !Switched()) {
        sb::Increment(&EU4SB_MimallocStats.misses);
    }
    return g_realReAlloc(heap, flags, pointer, size);
}

extern "C" SIZE_T WINAPI SB_HeapSize(HANDLE heap, DWORD flags, LPCVOID pointer) {
    if (pointer != nullptr && heap == g_processHeap && Owned(pointer)) {
        const Header* header = HeaderOf(pointer);
        if (header->magic == kMagic) return header->size;
        sb::Increment(&EU4SB_MimallocStats.mismatches);
    } else if (pointer != nullptr && !Switched()) {
        sb::Increment(&EU4SB_MimallocStats.misses);
    }
    return g_realSize(heap, flags, pointer);
}

namespace sb {

namespace {

anvil::FunctionHook<LPVOID(HANDLE, DWORD, SIZE_T)> g_allocHook;
anvil::FunctionHook<BOOL(HANDLE, DWORD, LPVOID)> g_freeHook;
anvil::FunctionHook<LPVOID(HANDLE, DWORD, LPVOID, SIZE_T)> g_reAllocHook;
anvil::FunctionHook<SIZE_T(HANDLE, DWORD, LPCVOID)> g_sizeHook;

// The pointer eu4.exe's import slot of KERNEL32.dll!<function> holds now, or 0.
uintptr_t ImportSlotValue(const char* function) {
    auto* base = reinterpret_cast<const unsigned char*>(anvil::Host{}.game().image_base);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0) return 0;
    for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name != 0; ++desc) {
        if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), "KERNEL32.dll") != 0) continue;
        const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(
            base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
        const auto* slots = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(byName->Name), function) == 0) {
                return static_cast<uintptr_t>(slots->u1.Function);
            }
        }
    }
    return 0;
}

}  // namespace

bool InstallMimalloc(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) return false;
    // All four import slots must still hold exactly the kernel32 export (no
    // other hook in between) before any is replaced: mimalloc must own every
    // block it frees.
    for (const char* name : {"HeapAlloc", "HeapFree", "HeapReAlloc", "HeapSize"}) {
        const uintptr_t expected = reinterpret_cast<uintptr_t>(GetProcAddress(kernel32, name));
        if (expected == 0 || ImportSlotValue(name) != expected) {
            Log(ANVIL_LOG_WARNING, "mimalloc: import slot of %s is not the kernel32 export; module off", name);
            return false;
        }
    }
    g_processHeap = GetProcessHeap();
    g_startupOnly = mode == Mode::Startup;
    mi_process_init();
    Undo undo;
    // Anvil enables the four together when load() returns, at the entry point
    // (only the main thread runs game code); HeapAlloc is still requested last.
    host.replace_import(g_freeHook, "KERNEL32.dll", "HeapFree", &SB_HeapFree);
    undo.add(g_freeHook);
    host.replace_import(g_reAllocHook, "KERNEL32.dll", "HeapReAlloc", &SB_HeapReAlloc);
    undo.add(g_reAllocHook);
    host.replace_import(g_sizeHook, "KERNEL32.dll", "HeapSize", &SB_HeapSize);
    undo.add(g_sizeHook);
    host.replace_import(g_allocHook, "KERNEL32.dll", "HeapAlloc", &SB_HeapAlloc);
    undo.add(g_allocHook);
    g_realFree = g_freeHook.next_pointer();
    g_realReAlloc = g_reAllocHook.next_pointer();
    g_realSize = g_sizeHook.next_pointer();
    g_realAlloc = g_allocHook.next_pointer();
    undo.keep();
    InterlockedExchange(&EU4SB_MimallocStats.mode, 1);
    InterlockedExchange(&EU4SB_MimallocStats.initialized, 1);
    return true;
}

void CollectMimalloc(const char* reason) {
    if (!g_startupOnly || EU4SB_MimallocStats.mode != 1) return;
    const auto commitMiB = [] {
        PROCESS_MEMORY_COUNTERS_EX counters{};
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters));
        return static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
    };
    const double before = commitMiB();
    const LONG64 freed = InterlockedExchange64(&g_freedSinceCollect, 0);
    const LONG64 start = Ticks();
    mi_collect(true);
    Log(ANVIL_LOG_INFO, "mimalloc: freed pages returned at %s in %.1f ms (%.0f MiB freed since the last), "
        "commit %.0f -> %.0f MiB", reason, TicksToMs(Ticks() - start), static_cast<double>(freed) / (1024.0 * 1024.0),
        before, commitMiB());
}

bool CollectMimallocIfDue() {
    if (!g_startupOnly || g_freedSinceCollect < kCollectAfterBytes) return false;
    CollectMimalloc("64 MiB freed");
    return true;
}

void ReportMimalloc() {
    LogStats("mimalloc", EU4SB_MimallocStats);
    bench::SetCounters("mimalloc", EU4SB_MimallocStats);
}

#ifdef SB_MIMALLOC_TEST
// The heap functions without the import slots, for tests/test_mimalloc_heap.cpp.
void InitMimallocForTest(Mode mode) {
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    g_realAlloc = reinterpret_cast<HeapAllocFn>(GetProcAddress(kernel32, "HeapAlloc"));
    g_realFree = reinterpret_cast<HeapFreeFn>(GetProcAddress(kernel32, "HeapFree"));
    g_realReAlloc = reinterpret_cast<HeapReAllocFn>(GetProcAddress(kernel32, "HeapReAlloc"));
    g_realSize = reinterpret_cast<HeapSizeFn>(GetProcAddress(kernel32, "HeapSize"));
    g_processHeap = GetProcessHeap();
    g_startupOnly = mode == Mode::Startup;
    mi_process_init();
    InterlockedExchange(&EU4SB_MimallocStats.mode, 1);
}
#endif

}  // namespace sb
