#pragma once

// Which paths attr_cache may remember (header-only so the tests can use it).

#include <windows.h>

#include <string>
#include <vector>

namespace sb::path_rules {

inline bool IsSeparator(wchar_t c) noexcept { return c == L'\\' || c == L'/'; }

// Drive-absolute ("C:\...") paths only; device and UNC spellings are left to the real API.
inline bool IsDrivePath(const std::wstring& path) noexcept {
    return path.size() >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
           path[1] == L':' && IsSeparator(path[2]);
}

// Components that may change during start-up (same list as the lab).
inline bool HasExcludedComponent(const std::wstring& path) noexcept {
    static const wchar_t* const kExcluded[] = {L"userDocuments", L"saves", L"save", L"logs", L"log",
                                               L"cache",         L"tmp",   L"temp"};
    std::size_t start = 0;
    for (std::size_t index = 0; index <= path.size(); ++index) {
        if (index != path.size() && !IsSeparator(path[index])) continue;
        const int length = static_cast<int>(index - start);
        for (const wchar_t* name : kExcluded) {
            if (length == static_cast<int>(wcslen(name)) &&
                CompareStringOrdinal(path.data() + start, length, name, length, TRUE) == CSTR_EQUAL) {
                return true;
            }
        }
        start = index + 1;
    }
    return false;
}

// path is strictly inside root (root without a trailing separator), ignoring case.
inline bool IsUnderRoot(const std::wstring& path, const std::wstring& root) noexcept {
    if (root.empty() || path.size() <= root.size() || !IsSeparator(path[root.size()])) return false;
    return CompareStringOrdinal(path.data(), static_cast<int>(root.size()), root.data(), static_cast<int>(root.size()),
                                TRUE) == CSTR_EQUAL;
}

inline bool IsEligible(const std::wstring& fullPath, const std::vector<std::wstring>& roots) noexcept {
    if (!IsDrivePath(fullPath) || HasExcludedComponent(fullPath)) return false;
    for (const auto& root : roots) {
        if (IsUnderRoot(fullPath, root)) return true;
    }
    return false;
}

// Only stable "does not exist" answers are remembered.
inline bool IsCacheableError(DWORD error) noexcept {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

}  // namespace sb::path_rules
