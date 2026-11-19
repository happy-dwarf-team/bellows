#pragma once
// Whether a folder is PhysFS's write directory or inside it, for verifypath_listing and exists_index (header-only so
// the tests can use it). The game creates, renames and deletes files there while it starts, so those folders always
// go to the game's own call. The two strings come from different PhysFS calls (a mounted root and
// PHYSFS_getWriteDir), so one spelling of the folder must not hide it: '/' and '\' count as the same separator, a
// run of separators as one, and ASCII letters match in either case. Anything else (non-ASCII letters, "." or "..",
// 8.3 names) is compared as written, so such a spelling counts as another folder; that only lets the listing answer
// where it would have answered before.

#include <cstddef>

namespace sb::write_root {

template <class Char>
constexpr bool IsSeparator(Char c) noexcept {
    return c == Char('\\') || c == Char('/');
}

template <class Char>
constexpr Char FoldAscii(Char c) noexcept {
    return c >= Char('a') && c <= Char('z') ? static_cast<Char>(c - Char('a') + Char('A')) : c;
}

// `path` (length `pathLength`) is `folder` (length `folderLength`) or inside it. An empty folder (or one made only
// of separators) never matches.
template <class Char>
constexpr bool IsInside(const Char* path, std::size_t pathLength, const Char* folder, std::size_t folderLength) noexcept {
    if (path == nullptr || folder == nullptr) return false;
    while (folderLength > 0 && IsSeparator(folder[folderLength - 1])) --folderLength;
    while (pathLength > 0 && IsSeparator(path[pathLength - 1])) --pathLength;
    if (folderLength == 0) return false;
    std::size_t p = 0, f = 0;
    while (f < folderLength) {
        if (p >= pathLength) return false;
        if (IsSeparator(folder[f])) {
            if (!IsSeparator(path[p])) return false;
            while (f < folderLength && IsSeparator(folder[f])) ++f;
            while (p < pathLength && IsSeparator(path[p])) ++p;
            continue;
        }
        if (FoldAscii(folder[f]) != FoldAscii(path[p])) return false;
        ++f;
        ++p;
    }
    return p == pathLength || IsSeparator(path[p]);
}

}  // namespace sb::write_root
