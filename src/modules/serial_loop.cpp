// conquer_scan: the state the game's ParallelFor3 sets around its loop (serial_loop.hpp).
#include "serial_loop.hpp"

#include "modules.hpp"

namespace sb::serial_loop {

namespace {

unsigned char* g_parallelFlag = nullptr;
sym::Types::random_log_impl::Pointer g_logImpl = nullptr;
sym::Types::random_log_collect::Pointer g_logCollect = nullptr;
constexpr std::size_t kLogFlags = sym::Types::parallel_for_head::random_log_flags;
constexpr std::uint32_t kLogOn = 1, kLogCollecting = 2;

}  // namespace

bool Resolve(anvil::Host& host) {
    if (g_parallelFlag != nullptr) return true;
    g_logImpl = host.function(sym::random_log_impl);
    g_logCollect = host.function(sym::random_log_collect);
    g_parallelFlag = host.data(sym::parallel_flag);
    return true;
}

// ParallelFor3: keep the byte and set it to 1; with the thread's log on (bit 0), keep its flags and set bit 1
// (the game collects first if bit 1 was set and is now clear, which setting it cannot cause); after the loop,
// collect if bit 1 is set and was not before, and put the flags and the byte back.
GameLoopScope::GameLoopScope() noexcept : savedFlag_(*g_parallelFlag) {
    *g_parallelFlag = 1;
    unsigned char* log = static_cast<unsigned char*>(g_logImpl());
    if ((Read<std::uint8_t>(log, kLogFlags) & kLogOn) != 0) {
        logging_ = true;
        savedLogFlags_ = Read<std::uint32_t>(log, kLogFlags);
        const std::uint32_t flags = savedLogFlags_ | kLogCollecting;
        std::memcpy(log + kLogFlags, &flags, sizeof(flags));
    }
}

GameLoopScope::~GameLoopScope() {
    if (logging_) {
        unsigned char* log = static_cast<unsigned char*>(g_logImpl());
        if ((Read<std::uint32_t>(log, kLogFlags) & kLogCollecting) != 0 && (savedLogFlags_ & kLogCollecting) == 0) {
            g_logCollect(log);
        }
        std::memcpy(log + kLogFlags, &savedLogFlags_, sizeof(savedLogFlags_));
    }
    *g_parallelFlag = savedFlag_;
}

}  // namespace sb::serial_loop
