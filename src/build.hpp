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

    // ZIP archiver (PhysFS): Io template read slot and its target, mz_inflate and its call site outside the read
    uint32_t zip_io_read_slot;
    uint32_t zip_read_fn;
    uint32_t mz_inflate_fn;
    uint32_t mz_inflate_other_site;

    // ram_prefetch: ZIP archiver table slots and their targets, tinfl_decompress, ZIP entry field offsets
    uint32_t zip_open_archive_slot, zip_open_archive_fn;
    uint32_t zip_open_read_slot, zip_open_read_fn;
    uint32_t zip_close_archive_slot, zip_close_archive_fn;
    uint32_t tinfl_decompress_fn;
    uint32_t zip_entry_method, zip_entry_crc, zip_entry_compressed, zip_entry_uncompressed;

    // token_dict_profile (diagnostic): keyword dictionary, its lookup and the two lookup call sites,
    // vector of tokens added at run time (begin, end at +8)
    uint32_t token_dict, token_lookup_fn, token_lookup_tokenizer_site, token_lookup_get_or_add_site, token_dynamic_vector;

    // range_profile (diagnostic): the country range check (minimum distance from the country's
    // provinces), the province-to-province distance, and the pointer to the game root object
    // (current date at +0x1DD0)
    uint32_t range_min_distance_fn, province_distance_fn, game_root;

    // range_witness: the call of the range check in 0x3520D0, the map object (coordinates at
    // [+0x6C0] + id * 0x230 + 0x7C), the straight-line distance, the range bonus check and its
    // global value, and the two float constants of the walk's sqrt scaling
    uint32_t range_witness_site, map_global, map_line_fn, range_bonus_fn, range_bonus_global;
    uint32_t range_scale_const, range_offset_const;
    // second walk (related countries): their range bonus check and the two getters the walk
    // falls back to when the country has no modifier object at +0x1420
    uint32_t related_bonus_fn, related_bonus_default_a, related_bonus_default_b;
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
        0x1C1D840 + 0x10, 0x1A07A10, 0x1A080D0, 0x1A0B6CB,
        0x1C1D890 + 0x30, 0x1A073F0, 0x1C1D890 + 0x40, 0x1A07580, 0x1C1D890 + 0x70, 0x1A073B0,
        0x1A08470,
        0x46, 0x48, 0x50, 0x58,
        0x2326720, 0x6E4880, 0x170EA12, 0x170D47E, 0x242B8A0,
        0x3526B0, 0x2D2520, 0x233FE78,
        0x35214C, 0x233FED8, 0x88A390, 0x3F14A0, 0x233E0BC,
        0x1DC1938, 0x1D99D94,
        0x7ACEC0, 0x798DC0, 0x7A73C0,
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
