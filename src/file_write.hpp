#pragma once

// Replacing a data file in one step (header-only so the tests can use it).
//
// The text goes to "<path>.tmp" first. The temp file replaces the old file only once it was written and closed
// without an error, so a failed write (a full disk, a file held by another program) leaves the last good file in
// place instead of an empty or cut one.

#include <windows.h>

#include <fstream>
#include <string>

namespace sb {

inline bool ReplaceFileWith(const std::wstring& path, const std::string& text) noexcept {
    try {
        const std::wstring temp = path + L".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            out.close();
            if (!out) {
                DeleteFileW(temp.c_str());
                return false;
            }
        }
        if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
        DeleteFileW(temp.c_str());
        return false;
    } catch (...) {
        return false;
    }
}

}  // namespace sb
