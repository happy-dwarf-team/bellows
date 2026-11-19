#pragma once

// Folders whose names Windows compares with case (header-only so the tests can use it).
//
// The listings of verifypath_listing, listing_lookup and exists_index key names upper-cased and build the
// path of the next folder from that spelling, which is what the platform stat does in an ordinary folder.
// Since Windows 10 1803 a folder can carry the case-sensitive flag instead: every folder created from WSL
// on a Windows drive has it, and `fsutil file setCaseSensitiveInfo` sets it. In such a folder "common" and
// "COMMON" are different names, so an upper-cased listing would answer "missing" for a file the game finds
// and "exists" for a spelling the game does not find. Its listing is therefore not used: every name in it
// goes to the game's own call.

#include <windows.h>

#include <string>

namespace sb::listing_case {

enum class Folder { CaseInsensitive, CaseSensitive, Unknown };

// Unknown when the folder cannot be opened for its attributes; CaseInsensitive when the file system has no
// such flag (FAT, exFAT, older Windows: the query is not supported) or the flag is clear.
inline Folder CaseOf(const std::wstring& directory) noexcept {
    HANDLE handle = CreateFileW(directory.c_str(), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return Folder::Unknown;
    FILE_CASE_SENSITIVE_INFO info{};
    const BOOL ok = GetFileInformationByHandleEx(handle, FileCaseSensitiveInfo, &info, sizeof(info));
    CloseHandle(handle);
    if (!ok) return Folder::CaseInsensitive;
    return (info.Flags & FILE_CS_FLAG_CASE_SENSITIVE_DIR) != 0 ? Folder::CaseSensitive : Folder::CaseInsensitive;
}

// What a listing of `directory` may be used for: only folders known to compare names without case.
inline bool UsableCase(const std::wstring& directory) noexcept {
    return CaseOf(directory) == Folder::CaseInsensitive;
}

// A path component Windows may take for a device instead of a file (CON, PRN, AUX, NUL, COM0-9, LPT0-9, CONIN$,
// CONOUT$, CLOCK$), with any extension and spaces before the dot: no listing holds it, yet the file API can find
// it (Windows 11 answers attributes for "<folder>\NUL"; Windows 10 also for "NUL.txt", "CON.txt" and the like).
// Such a name goes to the game's own call. `upper` is the component upper-cased in ASCII.
inline bool IsDeviceName(const std::string& upper) noexcept {
    std::size_t base = upper.find('.');
    if (base == std::string::npos) base = upper.size();
    while (base > 0 && upper[base - 1] == ' ') --base;
    const std::string name = upper.substr(0, base);
    if (name == "CON" || name == "PRN" || name == "AUX" || name == "NUL" || name == "CONIN$" || name == "CONOUT$" ||
        name == "CLOCK$") {
        return true;
    }
    return name.size() == 4 && (name.compare(0, 3, "COM") == 0 || name.compare(0, 3, "LPT") == 0) && name[3] >= '0' &&
           name[3] <= '9';
}

}  // namespace sb::listing_case
