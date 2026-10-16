// Diagnostic (off by default): back-off for the modifier picture verifier's
// empty-queue spin (lab docs: docs/startup-callskip-experiments.md).
//
// The verifier's consumer loop (0x8D80D0) unlocks the queue mutex and, when it
// popped nothing, jumps straight back to the lock (je 0x8D8100 at 0x8D8155),
// so during the database phase it spins lock/unlock on the mutex the loading
// thread takes for every push. The 9 bytes "test rsi,rsi; mov rax,[rbp+67h];
// je 0x8D8100" at 0x8D814E become a jmp to a stub that re-executes them,
// continues at 0x8D8157 when an item was popped and otherwise runs a bounded
// pause loop before returning to the lock. The stub makes no calls; every item
// is still verified and shutdown still waits for an empty queue.
#include "modules.hpp"

#include <cstdlib>

namespace {

constexpr std::uint32_t kDefaultPauses = 2000;
constexpr std::size_t kStubSize = 0x50;
constexpr std::size_t kCounterOffset = 0x40;  // qword: empty-queue waits (plain increment, single consumer)

volatile LONG64* g_waits = nullptr;
std::uint32_t g_pauses = kDefaultPauses;

}  // namespace

extern "C" {
// requests = empty-queue waits (back-offs), mode = 1 on, initialized = patched.
__declspec(dllexport) sb::Stats EU4SB_VerifierBackoffStats = SB_STATS_INIT;
}

namespace sb {

bool InstallVerifierBackoff(const Build& build, Mode mode) {
    if (mode == Mode::Off) return false;
    char value[16] = {};
    const DWORD length = GetEnvironmentVariableA("EU4SB_VERIFIER_BACKOFF_PAUSES", value, sizeof(value));
    if (length > 0 && length < sizeof(value)) {
        const unsigned long pauses = std::strtoul(value, nullptr, 10);
        if (pauses >= 1 && pauses <= 1000000) g_pauses = static_cast<std::uint32_t>(pauses);
    }

    // test rsi,rsi; mov rax,[rbp+67h]; je 0x8D8100
    const unsigned char site[9] = {0x48, 0x85, 0xF6, 0x48, 0x8B, 0x45, 0x67, 0x74, 0xA9};
    const uintptr_t siteAddress = context().image_base + build.verifier_empty_branch;
    const uintptr_t itemTarget = context().image_base + build.verifier_item_continue;
    const uintptr_t lockTarget = context().image_base + build.verifier_lock_loop;
    if (siteAddress + sizeof(site) != itemTarget || std::memcmp(AtRva<unsigned char>(build.verifier_empty_branch), site, sizeof(site)) != 0) {
        Log(ANVIL_LOG_WARNING, "verifier_backoff: unexpected code at the empty-queue branch; module off");
        return false;
    }

    const AnvilHost* host = context().host;
    auto* stub = static_cast<unsigned char*>(host->alloc_near_image(context().self, kStubSize));
    if (stub == nullptr) {
        Log(ANVIL_LOG_WARNING, "verifier_backoff: no memory near the image; module off");
        return false;
    }
    std::memset(stub, 0xCC, kStubSize);
    unsigned char* p = stub;
    auto put = [&p](std::initializer_list<unsigned char> bytes) {
        for (unsigned char b : bytes) *p++ = b;
    };
    auto rel32 = [](const unsigned char* next, uintptr_t target) {
        return static_cast<std::int32_t>(static_cast<intptr_t>(target) - reinterpret_cast<intptr_t>(next));
    };
    put({0x48, 0x8B, 0x45, 0x67});  // mov rax,[rbp+67h]
    put({0x48, 0x85, 0xF6});        // test rsi,rsi
    put({0x0F, 0x85});              // jne itemTarget
    std::int32_t r = rel32(p + 4, itemTarget);
    std::memcpy(p, &r, 4), p += 4;
    put({0x48, 0xFF, 0x05});  // inc qword [rip+counter]
    r = rel32(p + 4, reinterpret_cast<uintptr_t>(stub + kCounterOffset));
    std::memcpy(p, &r, 4), p += 4;
    put({0xB9});  // mov ecx,pauses
    std::memcpy(p, &g_pauses, 4), p += 4;
    put({0xF3, 0x90, 0xFF, 0xC9, 0x75, 0xFA});  // L: pause; dec ecx; jnz L
    put({0xE9});                               // jmp lockTarget
    r = rel32(p + 4, lockTarget);
    std::memcpy(p, &r, 4), p += 4;
    std::memset(stub + kCounterOffset, 0, 8);
    g_waits = reinterpret_cast<volatile LONG64*>(stub + kCounterOffset);
    FlushInstructionCache(GetCurrentProcess(), stub, kStubSize);

    unsigned char jump[9] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90};
    r = static_cast<std::int32_t>(reinterpret_cast<intptr_t>(stub) - static_cast<intptr_t>(siteAddress + 5));
    std::memcpy(jump + 1, &r, 4);
    const AnvilResult result = host->patch_bytes(context().self, build.verifier_empty_branch, site, jump, sizeof(jump));
    if (result != ANVIL_OK) {
        Log(ANVIL_LOG_WARNING, "verifier_backoff: patch refused (%s); module off", ResultName(result));
        return false;
    }
    InterlockedExchange(&EU4SB_VerifierBackoffStats.mode, 1);
    InterlockedExchange(&EU4SB_VerifierBackoffStats.initialized, 1);
    return true;
}

void ReportVerifierBackoff() {
    if (g_waits == nullptr) return;
    InterlockedExchange64(&EU4SB_VerifierBackoffStats.requests, *g_waits);
    Log(ANVIL_LOG_INFO, "verifier_backoff: %lld empty-queue waits of %u pauses", static_cast<long long>(*g_waits), g_pauses);
}

}  // namespace sb
