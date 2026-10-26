// range_witness: the country range check answered from the last province that was in range
// (lab docs: docs/simulation-profile.md, "Sprawdzenie zasięgu kraju").
//
// 0x3520D0 ("is the province within the country's range") calls 0x3526B0, which walks the
// country's provinces (list at +0x1338, count +0x1344) and, for each one that has bit 0x10
// at +0x958 and the country in its list at +0x490, computes
//   v = min(0x2D2520 distance, straight-line distance) * 1000 / (1000 + range bonus)
// and stops at the first v below the threshold; 0x3520D0 then only compares the written value
// with the threshold. In the lab 97.5% of the walk's distance calls were spent before such a
// stop, and the same province ended the walk day after day. The call at 0x35214C is
// redirected here: the witness province is checked first with the game's own functions and
// the same formula; only when it no longer qualifies does the original walk run (and a new
// witness is looked up). Mode verify always runs the original and compares the answers.
#include "../settings.hpp"
#include "modules.hpp"
#include "range_witness_core.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <string>

namespace {

using MinDistanceFn = int* (*)(void* country, int* out, void* target, void* arg4, int* threshold);
using DistanceFn = void* (*)(void* out, std::uint64_t countryInfo, void* province, void* target, void* p5,
                             std::uint64_t p6);
using LineFn = int (*)(const int* a, const int* b, int width, int height, std::uint64_t flag);
using BonusFn = bool (*)(void* country);
using RelatedBonusFn = bool (*)(void* modifiers);
using GetterFn = unsigned char* (*)();

// eu4.exe 1.37.5 layout (lab: docs/simulation-profile.md).
constexpr std::size_t kCountryInfo = 0x20;      // qword; the country id is bits 32-47
constexpr std::size_t kCountryList = 0x1338;    // int32* province ids
constexpr std::size_t kCountryCount = 0x1344;   // int32
constexpr std::size_t kRootProvinces = 0x1CA8;  // province array, 0x2E10 bytes each
constexpr std::size_t kProvinceStride = 0x2E10;
constexpr std::size_t kProvinceId = 0x20;       // int32
constexpr std::size_t kProvinceFlags = 0x958;   // byte, bit 0x10 checked by the walk
constexpr std::size_t kProvinceListBegin = 0x490, kProvinceListEnd = 0x498;  // qwords, id in bits 32-47
constexpr std::size_t kMapRecords = 0x6C0, kMapRecordStride = 0x230, kMapCoord = 0x7C;
constexpr std::size_t kMapWidth = 0x28, kMapHeight = 0x2C, kMapIntegerFlag = 0x31;
constexpr std::size_t kRootCountries = 0x1CE8;       // country*[], index = relation index
constexpr std::size_t kCountryRelations = 0x1510;    // begin/end of 0x48-byte relation records
constexpr std::size_t kRelationStride = 0x48;
constexpr std::size_t kRelationObject = 0x00, kRelationOther = 0x10;  // object*, qword with an id in bits 32-47
constexpr std::size_t kRelationList = 0x128, kRelationListCount = 0x134;  // (int32 id, pad, object*) 16 bytes
constexpr std::size_t kRelationFlagHolder = 0x40, kRelationFlag = 0x11F;
constexpr std::size_t kCountryModifiers = 0x1420;

sb::Mode g_mode = sb::Mode::Off;
anvil::FunctionHook<int*(void*, int*, void*, void*, int*)> g_site;
MinDistanceFn g_original = nullptr;  // the rest of the redirected call's chain
DistanceFn g_distance = nullptr;
LineFn g_line = nullptr;
BonusFn g_bonus = nullptr;
RelatedBonusFn g_relatedBonus = nullptr;
GetterFn g_defaultA = nullptr, g_defaultB = nullptr;
float g_scale = 0, g_offset = 0;
// Globals the walk itself reads (values of symbols.toml, taken from its code).
const unsigned char* const* g_root = nullptr;
const unsigned char* const* g_map = nullptr;
const int* g_bonusValue = nullptr;
std::size_t g_rootDate = 0;  // int32 hours since year -5000 in the game root object

// SEH-guarded parts; no C++ objects with destructors inside __try.
bool EligibleUnguarded(void* country, std::int32_t province) {
    const auto* root = *g_root;
    const auto* provinces = sb::Read<const unsigned char*>(root, kRootProvinces);
    const auto* p = provinces + static_cast<std::int64_t>(province) * kProvinceStride;
    if ((sb::Read<std::uint8_t>(p, kProvinceFlags) & 0x10) == 0) return false;
    const auto id = static_cast<std::uint16_t>(sb::Read<std::uint64_t>(country, kCountryInfo) >> 32);
    auto* it = sb::Read<const std::uint64_t*>(p, kProvinceListBegin);
    auto* end = sb::Read<const std::uint64_t*>(p, kProvinceListEnd);
    for (; it != end; ++it) {
        if (static_cast<std::uint16_t>(*it >> 32) == id) return true;
    }
    return false;
}

// The second walk's range bonus for a related country (0x352ADD-0x352B14).
bool RelatedBonus(void* owner) {
    auto* modifiers = sb::Read<unsigned char*>(owner, kCountryModifiers);
    unsigned char* fallback = modifiers != nullptr ? sb::Read<unsigned char*>(modifiers, 0x10) : g_defaultA();
    unsigned char* holder;
    if (modifiers != nullptr) {
        holder = modifiers + 0x38;
    } else if (fallback != nullptr) {
        holder = fallback + 0x78;
    } else {
        holder = g_defaultB() + 0x58;
    }
    return g_relatedBonus(holder);
}

void* RelatedUnguarded(void* country, std::int32_t index) {
    const auto* begin = sb::Read<const unsigned char*>(country, kCountryRelations);
    const auto* record = begin + static_cast<std::int64_t>(index) * kRelationStride;
    const auto* object = sb::Read<const unsigned char*>(record, kRelationObject);
    if (object == nullptr) return nullptr;
    const auto other = static_cast<std::int32_t>(static_cast<std::int16_t>(sb::Read<std::uint64_t>(record, kRelationOther) >> 32));
    const auto* it = sb::Read<const unsigned char*>(object, kRelationList);
    const auto* end = it + static_cast<std::int64_t>(sb::Read<std::int32_t>(object, kRelationListCount)) * 16;
    for (; it != end; it += 16) {
        if (sb::Read<std::int32_t>(it, 0) != other) continue;
        const auto* entry = sb::Read<const unsigned char*>(it, 8);
        if (entry == nullptr) return nullptr;
        if (sb::Read<std::uint8_t>(sb::Read<const unsigned char*>(entry, kRelationFlagHolder), kRelationFlag) == 0) return nullptr;
        const auto* root = *g_root;
        return sb::Read<void*>(sb::Read<const unsigned char*>(root, kRootCountries), static_cast<std::size_t>(index) * 8);
    }
    return nullptr;
}

bool ValueUnguarded(void* country, bool related, std::int32_t province, void* target, std::int64_t* value) {
    const auto* root = *g_root;
    auto* provinceObj = const_cast<unsigned char*>(sb::Read<const unsigned char*>(root, kRootProvinces)) +
                        static_cast<std::int64_t>(province) * kProvinceStride;
    alignas(16) unsigned char buffer[64] = {};
    g_distance(buffer, sb::Read<std::uint64_t>(country, kCountryInfo), provinceObj, target, nullptr, 1);
    const int byPath = sb::Read<int>(buffer, 0);
    const std::int32_t provinceId = sb::Read<std::int32_t>(provinceObj, kProvinceId);
    const std::int32_t targetId = sb::Read<std::int32_t>(target, kProvinceId);
    int line = 0;
    if (provinceId != targetId) {
        const auto* map = *g_map;
        const auto* records = sb::Read<const unsigned char*>(map, kMapRecords);
        const int a = sb::Read<int>(records + static_cast<std::int64_t>(static_cast<std::int16_t>(targetId)) * kMapRecordStride, kMapCoord);
        const int b = sb::Read<int>(records + static_cast<std::int64_t>(static_cast<std::int16_t>(provinceId)) * kMapRecordStride, kMapCoord);
        const std::uint8_t integer = sb::Read<std::uint8_t>(map, kMapIntegerFlag);
        const int squared = g_line(&a, &b, sb::Read<int>(map, kMapWidth), sb::Read<int>(map, kMapHeight), integer);
        if (integer != 0) {
            line = squared * 1000;
        } else {
            if (squared < 0) return false;  // the game takes another path for negative input
            const float root2 = static_cast<float>(std::sqrt(static_cast<double>(squared)));
            float scaled = root2 * g_scale;
            scaled = scaled + g_offset;
            line = static_cast<int>(scaled);  // cvttss2si
        }
    }
    const int nearest = byPath < line ? byPath : line;
    const bool bonus = related ? RelatedBonus(country) : g_bonus(country);
    const int divisor = bonus ? *g_bonusValue + 1000 : 1000;
    *value = static_cast<std::int64_t>(nearest) * 1000 / divisor;
    return true;
}

struct Game {
    int* Original(void* c, int* out, void* t, void* a4, int* thr) { return g_original(c, out, t, a4, thr); }
    const std::int32_t* ListBegin(void* c) { return sb::Read<const std::int32_t*>(c, kCountryList); }
    const std::int32_t* ListEnd(void* c) {
        return ListBegin(c) + sb::Read<std::int32_t>(c, kCountryCount);
    }
    bool Eligible(void* c, std::int32_t p) {
        __try {
            return EligibleUnguarded(c, p);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    std::int32_t RelatedCount(void* c) {
        const auto begin = reinterpret_cast<std::uintptr_t>(sb::Read<const void*>(c, kCountryRelations));
        const auto end = reinterpret_cast<std::uintptr_t>(sb::Read<const void*>(c, kCountryRelations + 8));
        return end > begin ? static_cast<std::int32_t>((end - begin) / kRelationStride) : 0;
    }
    void* Related(void* c, std::int32_t index) {
        __try {
            return RelatedUnguarded(c, index);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }
    std::int32_t Day() {
        const auto* root = *g_root;
        return root == nullptr ? 0 : static_cast<std::int32_t>((static_cast<std::int64_t>(sb::Read<std::int32_t>(root, g_rootDate)) - 43'800'000) / 24);
    }
    bool Value(void* c, bool related, std::int32_t p, void* t, std::int64_t* v) {
        __try {
            return ValueUnguarded(c, related, p, t, v);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    std::int32_t TargetId(void* t) { return sb::Read<std::int32_t>(t, kProvinceId); }
};

Game g_game;
sb::WitnessEngine<Game>* g_engine = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;
volatile LONG64 g_ticks = 0;
volatile LONG g_dirty = 0;
std::wstring* g_dir = nullptr;

void WriteFile() {
    sb::WitnessStats s;
    std::size_t size = 0;
    AcquireSRWLockShared(&g_lock);
    s = g_engine->stats;
    size = g_engine->size();
    ReleaseSRWLockShared(&g_lock);
    try {
        const std::wstring path = *g_dir + L"\\range_witness.tsv";
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << "key\tvalue\n"
                << "mode\t" << sb::ModeName(g_mode) << "\n"
                << "calls\t" << s.calls << "\n"
                << "answered\t" << s.answered << "\n"
                << "answered_related\t" << s.answered_related << "\n"
                << "lookups_skipped\t" << s.lookups_skipped << "\n"
                << "originals\t" << s.originals << "\n"
                << "witness_valid\t" << s.witness_valid << "\n"
                << "witness_lost\t" << s.witness_lost << "\n"
                << "acquired\t" << s.acquired << "\n"
                << "not_acquired\t" << s.not_acquired << "\n"
                << "out_of_range\t" << s.out_of_range << "\n"
                << "verify_agree\t" << s.verify_agree << "\n"
                << "verify_disagree\t" << s.verify_disagree << "\n"
                << "verify_value_same\t" << s.verify_value_same << "\n"
                << "verify_value_differs\t" << s.verify_value_differs << "\n"
                << "witnesses\t" << size << "\n"
                << "ms_in_checks\t" << sb::TicksToMs(g_ticks) << "\n";
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
int* SB_RangeWitness(void* country, int* out, void* target, void* arg4, int* threshold) {
    const LONG64 start = sb::Ticks();
    AcquireSRWLockExclusive(&g_lock);
    int* result = nullptr;
    try {
        result = g_engine->Check(g_game, country, out, target, arg4, threshold, g_mode == sb::Mode::Verify);
    } catch (...) {
        // Allocation failure in the witness map: answer like the game.
        ReleaseSRWLockExclusive(&g_lock);
        return g_original(country, out, target, arg4, threshold);
    }
    ReleaseSRWLockExclusive(&g_lock);
    InterlockedExchangeAdd64(&g_ticks, sb::Ticks() - start);
    InterlockedExchange(&g_dirty, 1);
    return result;
}
// requests = checks, hits = answered from the witness, skips = originals, mode = 1 on / 2 verify.
__declspec(dllexport) sb::Stats EU4SB_RangeWitnessStats = SB_STATS_INIT;
}

namespace sb {

bool InstallRangeWitness(anvil::Host& host, Mode mode) {
    if (mode == Mode::Off) return false;
    // The redirected call is the plain "call 0x3526B0" in 0x3520D0 (declared).
    try {
        g_engine = new WitnessEngine<Game>();
        g_dir = new std::wstring(host.data_dir().wstring());
    } catch (...) {
        return false;
    }
    g_mode = mode;
    // The walk calls 0x2D2520 with six arguments; the symbol declares the seven range_profile forwards.
    g_distance = reinterpret_cast<DistanceFn>(host.address(sym::province_distance));
    g_line = host.function(sym::map_line);
    g_bonus = host.function(sym::range_bonus);
    g_relatedBonus = host.function(sym::related_bonus);
    g_defaultA = host.function(sym::related_bonus_default_a);
    g_defaultB = host.function(sym::related_bonus_default_b);
    g_scale = *host.data(sym::range_scale);
    g_offset = *host.data(sym::range_offset);
    g_root = host.data(sym::game_root);
    g_map = host.data(sym::map_global);
    g_bonusValue = host.data(sym::range_bonus_global);
    g_rootDate = static_cast<std::size_t>(host.value(sym::root_and_date, "date_offset"));
    host.redirect(g_site, sym::range_check_call, &SB_RangeWitness);
    g_original = g_site.next_pointer();
    HANDLE thread = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows range_witness writer");
        CloseHandle(thread);
    }
    InterlockedExchange(&EU4SB_RangeWitnessStats.mode, mode == Mode::Verify ? 2 : 1);
    InterlockedExchange(&EU4SB_RangeWitnessStats.initialized, 1);
    Log(ANVIL_LOG_INFO, "range_witness: %s, scale %.3f offset %.3f; counters in range_witness.tsv every 5 s",
        ModeName(mode).c_str(), g_scale, g_offset);
    return true;
}

void ReportRangeWitness() {
    if (g_engine == nullptr) return;
    AcquireSRWLockShared(&g_lock);
    const WitnessStats s = g_engine->stats;
    ReleaseSRWLockShared(&g_lock);
    InterlockedExchange64(&EU4SB_RangeWitnessStats.requests, s.calls);
    InterlockedExchange64(&EU4SB_RangeWitnessStats.hits, s.answered);
    InterlockedExchange64(&EU4SB_RangeWitnessStats.skips, s.originals);
    bench::Get().set("range_witness.checks", s.calls);
    bench::Get().set("range_witness.answered", s.answered);
    bench::Get().set("range_witness.originals", s.originals);
}

}  // namespace sb
