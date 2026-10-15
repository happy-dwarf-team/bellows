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
// kernel32. Active for the whole process lifetime, including gameplay.
#include "modules.hpp"

#include <mimalloc.h>

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
            mi_free(header);
            return TRUE;
        }
        sb::Increment(&EU4SB_MimallocStats.mismatches);
    } else if (pointer != nullptr) {
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
    } else if (pointer != nullptr) {
        sb::Increment(&EU4SB_MimallocStats.misses);
    }
    return g_realReAlloc(heap, flags, pointer, size);
}

extern "C" SIZE_T WINAPI SB_HeapSize(HANDLE heap, DWORD flags, LPCVOID pointer) {
    if (pointer != nullptr && heap == g_processHeap && Owned(pointer)) {
        const Header* header = HeaderOf(pointer);
        if (header->magic == kMagic) return header->size;
        sb::Increment(&EU4SB_MimallocStats.mismatches);
    } else if (pointer != nullptr) {
        sb::Increment(&EU4SB_MimallocStats.misses);
    }
    return g_realSize(heap, flags, pointer);
}

namespace sb {

bool InstallMimalloc(const Build& build, Mode mode) {
    if (mode == Mode::Off) return false;
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) return false;
    struct Slot {
        uint32_t rva;
        const char* name;
        void* replacement;
        void** original;
    } slots[] = {
        // HeapAlloc last: until it is switched no mimalloc block exists, so the
        // other three hooks only forward and a partial install stays correct.
        {build.heap_free_iat, "HeapFree", reinterpret_cast<void*>(&SB_HeapFree), reinterpret_cast<void**>(&g_realFree)},
        {build.heap_realloc_iat, "HeapReAlloc", reinterpret_cast<void*>(&SB_HeapReAlloc),
         reinterpret_cast<void**>(&g_realReAlloc)},
        {build.heap_size_iat, "HeapSize", reinterpret_cast<void*>(&SB_HeapSize), reinterpret_cast<void**>(&g_realSize)},
        {build.heap_alloc_iat, "HeapAlloc", reinterpret_cast<void*>(&SB_HeapAlloc), reinterpret_cast<void**>(&g_realAlloc)},
    };
    // All four slots must hold exactly the kernel32 export before any is replaced.
    uintptr_t expected[4] = {};
    for (int i = 0; i < 4; ++i) {
        expected[i] = reinterpret_cast<uintptr_t>(GetProcAddress(kernel32, slots[i].name));
        if (expected[i] == 0 || *AtRva<uintptr_t>(slots[i].rva) != expected[i]) {
            Log(ANVIL_LOG_WARNING, "mimalloc: IAT slot of %s is not the kernel32 export; module off", slots[i].name);
            return false;
        }
        *slots[i].original = reinterpret_cast<void*>(expected[i]);
    }
    g_processHeap = GetProcessHeap();
    mi_process_init();
    const AnvilHost* host = context().host;
    for (int i = 0; i < 4; ++i) {
        const uintptr_t replacement = reinterpret_cast<uintptr_t>(slots[i].replacement);
        const AnvilResult r = host->patch_bytes(context().self, slots[i].rva, &expected[i], &replacement, sizeof(uintptr_t));
        if (r != ANVIL_OK) {
            // Slots already replaced only forward (HeapAlloc was not switched),
            // so the game keeps using kernel32; the module counts as off.
            Log(ANVIL_LOG_WARNING, "mimalloc: patching %s refused (%s); module off", slots[i].name, ResultName(r));
            return false;
        }
    }
    InterlockedExchange(&EU4SB_MimallocStats.mode, 1);
    InterlockedExchange(&EU4SB_MimallocStats.initialized, 1);
    return true;
}

void ReportMimalloc() { LogStats("mimalloc", EU4SB_MimallocStats); }

}  // namespace sb
