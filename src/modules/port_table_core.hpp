#pragma once

// port_table: the game's table of the nearest port (lab docs: docs/simulation-profile.md, "Tabela portów").
//
// 0x350B80 rebuilds a country's table when the country's flag +0x2887 is set: for every province of the
// map, the nearest port among the country's own and those of the countries whose ports it may use. For
// every province it walks the country's whole province list, then the country's whole row of relations,
// and walks the province list of each friend it finds. None of those walks depends on the province it is
// filling in, so this keeps the result of one pass and goes over only that list. The ports stay in the
// game's order and the comparison stays strict, so a tie ends on the port the game ends on. The distance
// and the range come from the game's own functions.
//
// Two rebuilds in three leave the table as it was: the flag is set by far more than what moves a port.
// WorldPrint hashes everything the game's loop and its distance read, so a rebuild with the same print
// over the same table can be left out. What the distance reads, from the decompiled 0x2D2520: of every
// province whether it has an owner, whether that owner is this country or a subject of it (not who it
// is, or any war elsewhere on the map would change every country's print), the group +0x338 with its
// two member lists, whether +0x550 is set and the port flag. The rest (the map's coordinates and its
// table of distances, a province's neighbours) does not change during a game.
//
// Plain memory reads at the game's offsets, no Windows calls, so a test can lay out its own structures.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace sb::port {

using Bytes = const unsigned char*;

// The game root.
constexpr std::size_t kRootProvinceIdsBegin = 0x1C90, kRootProvinceIdsEnd = 0x1C98;  // gives the province count
constexpr std::size_t kRootProvinces = 0x1CA8, kRootCountries = 0x1CE8;
constexpr std::size_t kProvinceStride = 0x2E10;

// A country.
constexpr std::size_t kCountryId = 0x20, kCountryType = 0x26, kCountryProvinces = 0x1338, kCountryProvinceCount = 0x1344;
constexpr std::size_t kCountryPortCount = 0x1C78, kCountryWars = 0x1468, kCountryOverlord = 0x18F0;
constexpr std::size_t kCountryRows = 0x1510, kCountryRowsEnd = 0x1518, kRowStride = 0x48, kRowId = 0x10;
constexpr std::size_t kCountryTable = 0x2320, kCountryTableEnd = 0x2328, kCountryDirty = 0x2887;
constexpr std::uint8_t kTypeWithoutProvinces = 7;

// A relation row: its object holds two lists of 16-byte entries, the key in the first int.
constexpr std::size_t kListA = 0xE0, kListACount = 0xEC, kListB = 0x128, kListBCount = 0x134;
constexpr std::size_t kEntryPointer = 8, kEntrySize = 16, kEntryObjectId = 0x18;

// A province.
constexpr std::size_t kProvinceSelf = 0x20, kProvinceClass = 0x28, kProvinceAccess = 0x5E0;
constexpr std::size_t kProvinceOwner = 0x468, kProvinceController = 0x470, kProvinceFriends = 0x490, kProvinceFriendsEnd = 0x498;
constexpr std::size_t kProvinceBuilding = 0x550, kProvinceFlags = 0x958, kProvinceFlagsHigh = 0x959;
constexpr std::size_t kClassFlags = 0x228;
constexpr std::size_t kProvinceGroup = 0x338, kGroupList = 8, kGroupNodeNext = 0x10, kGroupArray = 0x40, kGroupCount = 0x4C;
constexpr std::uint8_t kFlagPort = 0x10, kFlagBlocked = 0x40, kFlagHighRestricted = 0x10, kClassClosed = 8;

constexpr std::int32_t kFarAway = 0x5F5DD18;
constexpr std::int32_t kWarState = 3;
constexpr std::size_t kWarNodeNext = 0x10, kWarStateOffset = 0x40;  // a list node: its data at +0, the next at +0x10

template <class T>
inline T At(Bytes base, std::size_t offset) noexcept {
    T value;
    std::memcpy(&value, base + offset, sizeof(T));
    return value;
}

inline std::int16_t TagOf(std::uint64_t id) noexcept { return static_cast<std::int16_t>(id >> 32); }

// What the game checks on a province before its distance counts. `tag` is the country that must be
// on the province's list: the searching country for its own ports, the owner for a friend's.
inline bool PortCounts(Bytes province, std::int16_t tag) noexcept {
    const auto flags = At<std::uint8_t>(province, kProvinceFlags);
    if ((flags & kFlagPort) == 0) return false;
    const auto owner = At<std::uint64_t>(province, kProvinceOwner);
    if (TagOf(At<std::uint64_t>(province, kProvinceController)) != TagOf(owner)) return false;
    if ((owner >> 56) == 0 || At<const void*>(province, kProvinceBuilding) == nullptr) return false;
    auto entry = At<const std::uint64_t*>(province, kProvinceFriends);
    const auto end = At<const std::uint64_t*>(province, kProvinceFriendsEnd);
    for (; entry != end; ++entry) {
        std::uint64_t id;
        std::memcpy(&id, entry, sizeof id);
        if (TagOf(id) == tag) return (flags & kFlagBlocked) == 0;
    }
    return false;
}

// The country of row `row` lends its ports to the searching country.
inline bool RowLendsPorts(Bytes row) noexcept {
    const auto relation = At<Bytes>(row, 0);
    const std::int16_t tag = TagOf(At<std::uint64_t>(row, kRowId));
    auto entry = At<Bytes>(relation, kListA);
    for (std::int32_t i = 0, n = At<std::int32_t>(relation, kListACount); i < n; ++i, entry += kEntrySize) {
        if (At<std::int32_t>(entry, 0) != tag) continue;
        const auto object = At<Bytes>(entry, kEntryPointer);
        if (object != nullptr && TagOf(At<std::uint64_t>(object, kEntryObjectId)) == tag) return true;
        break;
    }
    entry = At<Bytes>(relation, kListB);
    for (std::int32_t i = 0, n = At<std::int32_t>(relation, kListBCount); i < n; ++i, entry += kEntrySize) {
        if (At<std::int32_t>(entry, 0) == tag) return true;
    }
    return false;
}

inline Bytes ProvinceAt(Bytes provinces, std::int32_t id) noexcept {
    return provinces + static_cast<std::int64_t>(id) * static_cast<std::int64_t>(kProvinceStride);
}

// The ports the game would look at for every province, in the order it looks at them.
inline void CollectPorts(Bytes root, Bytes country, std::vector<Bytes>& out) {
    const auto provinces = At<Bytes>(root, kRootProvinces);
    const std::int16_t tag = TagOf(At<std::uint64_t>(country, kCountryId));
    auto own = At<const std::int32_t*>(country, kCountryProvinces);
    for (std::int32_t i = 0, n = At<std::int32_t>(country, kCountryProvinceCount); i < n; ++i) {
        std::int32_t id;
        std::memcpy(&id, own + i, sizeof id);
        Bytes province = ProvinceAt(provinces, id);
        if (PortCounts(province, tag)) out.push_back(province);
    }
    const auto rows = At<Bytes>(country, kCountryRows);
    const std::int64_t rowCount = (At<Bytes>(country, kCountryRowsEnd) - rows) / static_cast<std::int64_t>(kRowStride);
    const auto countries = At<Bytes>(root, kRootCountries);
    for (std::int64_t r = 0; r < rowCount; ++r) {
        if (!RowLendsPorts(rows + r * kRowStride)) continue;
        const auto other = At<Bytes>(countries, static_cast<std::size_t>(r) * 8);
        auto list = At<const std::int32_t*>(other, kCountryProvinces);
        for (std::int32_t i = 0, n = At<std::int32_t>(other, kCountryProvinceCount); i < n; ++i) {
            std::int32_t id;
            std::memcpy(&id, list + i, sizeof id);
            Bytes province = ProvinceAt(provinces, id);
            if (PortCounts(province, TagOf(At<std::uint64_t>(province, kProvinceOwner)))) out.push_back(province);
        }
    }
}

// True when the country is in a state the game treats as at war: every province then counts.
inline bool OpenToAllProvinces(Bytes country) noexcept {
    for (auto node = At<Bytes>(country, kCountryWars); node != nullptr; node = At<Bytes>(node, kWarNodeNext)) {
        if (At<std::int32_t>(At<Bytes>(node, 0), kWarStateOffset) == kWarState) return true;
    }
    return false;
}

// Whether the game fills in the province at all (the check on top of its loop).
inline bool TargetCounts(Bytes target, std::uint64_t countryId, bool open) noexcept {
    if ((At<std::uint8_t>(At<Bytes>(target, kProvinceClass), kClassFlags) & kClassClosed) != 0) return false;
    if (open) return true;
    if ((At<std::uint8_t>(target, kProvinceFlagsHigh) & kFlagHighRestricted) == 0) return true;
    if ((countryId >> 56) == 0 || static_cast<std::uint8_t>(countryId >> 48) == kTypeWithoutProvinces) return true;
    const auto index = static_cast<std::uint32_t>(static_cast<std::int32_t>(TagOf(countryId)));
    const auto word = At<const std::uint32_t*>(target, kProvinceAccess) + (index >> 5);
    std::uint32_t bits;
    std::memcpy(&bits, word, sizeof bits);
    return ((bits >> (index & 31)) & 1) != 0;
}

inline std::uint64_t Mix(std::uint64_t hash, std::uint64_t value) noexcept {
    hash = (hash ^ value) * 0x9E3779B97F4A7C15ULL;
    return hash ^ (hash >> 29);
}

inline std::uint64_t TablePrint(const std::int16_t* table, std::size_t cells) noexcept {
    std::uint64_t hash = cells;
    for (std::size_t i = 0; i < cells; ++i) hash = Mix(hash, static_cast<std::uint16_t>(table[i]));
    return hash;
}

// Which tags count as this country in the distance: itself and its subjects, by tag.
inline void MarkOwnTags(Bytes root, Bytes country, std::vector<std::uint8_t>& own) {
    own.assign(0x8000, 0);
    const std::int16_t tag = TagOf(At<std::uint64_t>(country, kCountryId));
    if (tag >= 0) own[static_cast<std::size_t>(tag)] = 1;
    const auto rows = At<Bytes>(country, kCountryRows);
    const std::int64_t rowCount = (At<Bytes>(country, kCountryRowsEnd) - rows) / static_cast<std::int64_t>(kRowStride);
    const auto countries = At<Bytes>(root, kRootCountries);
    for (std::int64_t r = 0; r < rowCount; ++r) {
        const auto other = At<Bytes>(countries, static_cast<std::size_t>(r) * 8);
        const std::int16_t otherTag = TagOf(At<std::uint64_t>(other, kCountryId));
        if (otherTag >= 0 && TagOf(At<std::uint64_t>(other, kCountryOverlord)) == tag) own[static_cast<std::size_t>(otherTag)] = 1;
    }
}

// Everything a rebuild of this country reads. `groups` and `own` are scratch space.
inline std::uint64_t WorldPrint(Bytes root, Bytes country, const std::vector<Bytes>& ports, std::int32_t reach,
                                std::vector<Bytes>& groups, std::vector<std::uint8_t>& own) {
    const auto id = At<std::uint64_t>(country, kCountryId);
    const bool open = OpenToAllProvinces(country);
    std::uint64_t hash = Mix(Mix(Mix(0x50525431, id), open), static_cast<std::uint32_t>(reach));
    for (Bytes port : ports) hash = Mix(hash, reinterpret_cast<std::uintptr_t>(port));
    const auto provinces = At<Bytes>(root, kRootProvinces);
    const auto count = static_cast<std::int32_t>((At<Bytes>(root, kRootProvinceIdsEnd) - At<Bytes>(root, kRootProvinceIdsBegin)) >> 3);
    MarkOwnTags(root, country, own);
    groups.clear();
    for (std::int32_t p = 0; p < count; ++p) {
        Bytes province = ProvinceAt(provinces, p);
        const auto owner = At<std::uint64_t>(province, kProvinceOwner);
        const std::int16_t tag = TagOf(owner);
        const auto group = At<Bytes>(province, kProvinceGroup);
        hash = Mix(hash, reinterpret_cast<std::uintptr_t>(group));
        if (group != nullptr) groups.push_back(group);
        std::uint64_t bits = (At<std::uint8_t>(province, kProvinceFlags) & kFlagPort) != 0;
        bits |= At<const void*>(province, kProvinceBuilding) != nullptr ? 2u : 0u;
        bits |= TargetCounts(province, id, open) ? 4u : 0u;
        bits |= (owner >> 56) != 0 ? 8u : 0u;
        bits |= tag >= 0 && own[static_cast<std::size_t>(tag)] != 0 ? 16u : 0u;
        hash = Mix(hash, bits);
    }
    std::sort(groups.begin(), groups.end());
    groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
    for (Bytes group : groups) {
        const auto members = At<std::int32_t>(group, kGroupCount);
        hash = Mix(hash, static_cast<std::uint32_t>(members));
        const auto array = At<Bytes>(group, kGroupArray);
        for (std::int32_t i = 0; i < members; ++i) hash = Mix(hash, At<std::uint64_t>(array, static_cast<std::size_t>(i) * 8));
        for (auto node = At<Bytes>(group, kGroupList); node != nullptr; node = At<Bytes>(node, kGroupNodeNext)) {
            hash = Mix(hash, At<std::uint64_t>(node, 0));
        }
    }
    return hash;
}

// True when Fill can stand in for the game's function on this country: a table of the right size that
// the game would fill in. Everything else (the first build of the table, the empty country) goes to the
// game's function, which is short for those anyway.
inline bool Applies(Bytes root, Bytes country) noexcept {
    const auto begin = At<Bytes>(country, kCountryTable), end = At<Bytes>(country, kCountryTableEnd);
    const auto count = (At<Bytes>(root, kRootProvinceIdsEnd) - At<Bytes>(root, kRootProvinceIdsBegin)) >> 3;
    if (count <= 1 || (end - begin) / 2 != count) return false;
    if (At<std::int32_t>(country, kCountryProvinceCount) == 0 && At<std::uint8_t>(country, kCountryType) != kTypeWithoutProvinces)
        return false;
    return At<std::int32_t>(country, kCountryPortCount) != 0;
}

// The loop of 0x350B80 over the list `ports`: `distance(port, target)` and `reach` are the game's.
// `table` starts as the game's table: a province the game skips keeps its value.
template <class Distance>
void Fill(Bytes root, Bytes country, const std::vector<Bytes>& ports, std::int16_t* table, Distance&& distance, std::int32_t reach) {
    const auto provinces = At<Bytes>(root, kRootProvinces);
    const auto count = static_cast<std::int32_t>((At<Bytes>(root, kRootProvinceIdsEnd) - At<Bytes>(root, kRootProvinceIdsBegin)) >> 3);
    const auto id = At<std::uint64_t>(country, kCountryId);
    const bool open = OpenToAllProvinces(country);
    for (std::int32_t p = 1; p < count; ++p) {
        Bytes target = ProvinceAt(provinces, p);
        if (!TargetCounts(target, id, open)) continue;
        table[p] = 0;
        std::int32_t best = kFarAway;
        Bytes nearest = nullptr;
        for (Bytes port : ports) {
            const std::int32_t d = distance(port, target);
            if (d < best) {
                best = d;
                nearest = port;
            }
        }
        if (nearest != nullptr) {
            const auto self = At<std::int16_t>(nearest, kProvinceSelf);
            table[p] = reach < best ? static_cast<std::int16_t>(-self) : self;
        }
    }
}

}  // namespace sb::port
