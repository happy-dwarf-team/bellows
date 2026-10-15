#pragma once

// Addresses (RVAs) used by the modules, per supported game build. Porting to a
// new EU4 build means adding one Build entry after re-validating every
// address in the lab (Ghidra + byte checks); nothing else in the plugin
// contains game addresses.

#include <cstdint>

namespace sb {

struct Build {
    const char* id;
    uint32_t ready_marker;

    // mimalloc_heap: IAT slots of the four heap functions (imported only by the game's CRT)
    uint32_t heap_alloc_iat;
    uint32_t heap_free_iat;
    uint32_t heap_realloc_iat;
    uint32_t heap_size_iat;

    // verifypath_listing: PhysFS verifyPath stat call site (call [r9+68h]; mov ebp,eax)
    uint32_t verifypath_site;
    uint32_t dir_archiver;  // PhysFS DIR archiver function table
    uint32_t dir_stat;      // its stat entry (slot +0x68)

    // exists_index: file-system object vtable slot +0x30 and its original target
    uint32_t fs_vtable_exists_slot;
    uint32_t fs_exists_original;

    // trigger_intern: the two call sites in the loading tail and their callee
    uint32_t trigger_sites[2];
    uint32_t trigger_intern_fn;
    uint32_t trigger_registry;

    // metascript_subst: the two call sites in 0xDB0860, the callee and string assign
    uint32_t metascript_sites[2];
    uint32_t metascript_subst_fn;
    uint32_t string_assign_fn;
};

inline constexpr Build kBuilds[] = {
    {
        "eu4-1.37.5-steam-win64",
        0x233FF4E,
        0x1B66570, 0x1B663D0, 0x1B665B8, 0x1B665C8,
        0x19FB7F4, 0x1C1D7A0, 0x1A07080,
        0x1D9C968 + 0x30, 0x15CCE80,
        {0x205C0F, 0x205C3F}, 0xB9B4E0, 0x2340010,
        {0xDB090E, 0xDB0967}, 0xDB1800, 0x95110,
    },
};

inline const Build* FindBuild(const char* id) {
    if (id == nullptr) return nullptr;
    for (const auto& b : kBuilds) {
        const char* a = b.id;
        const char* c = id;
        while (*a && *a == *c) ++a, ++c;
        if (*a == 0 && *c == 0) return &b;
    }
    return nullptr;
}

}  // namespace sb
