// Start-up "does this virtual file exist" index (ported from the lab's
// startup_exists_index.inc).
//
// The game's file-system object (vtable 0x1D9C968) answers existence queries
// through slot +0x30 = 0x15CCE80, a thin wrapper around PHYSFS_exists. The
// modifier picture verifier asks it once per picture; every PHYSFS_exists walks
// the whole search path with several GetFileAttributesExW calls (~5 s of CPU
// during database loading in the lab).
//
// This module replaces that vtable slot (byte-checked through Anvil). A query
// is answered "exists" only when the path is found in a once-read listing of a
// *directory* entry of the PhysFS search path mounted at "/". Everything else
// (archives such as DLC zips, unusual spellings, symlinks, other mount points,
// a disabled index, after the ready marker) goes to the original function, so
// a "not found" is never invented. Every 256th positive answer is checked
// against the original ("verify": every one); a mismatch disables the index.
#include "modules.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr unsigned char kOriginalExistsPrefix[] = {
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x83, 0x7A, 0x18, 0x10, 0x72, 0x03, 0x48,
    0x8B, 0x12, 0x48, 0x85, 0xD2, 0x74, 0x18, 0x80, 0x3A, 0x00, 0x74, 0x13};

using ExistsFn = bool (*)(void*, const void*);
using GetSearchPathFn = char** (*)();
using FreeListFn = void (*)(void*);
using GetMountPointFn = const char* (*)(const char*);
using SymlinksPermittedFn = int (*)();
using GetWriteDirFn = const char* (*)();

enum class Kind : unsigned char { File, Directory, Unknown };

struct Listing {
    bool usable = false;
    std::unordered_map<std::string, Kind> names;  // upper-cased ASCII name -> kind
};

struct Root {
    std::wstring path;  // directory, no trailing separator
};

struct State {
    SRWLOCK lock = SRWLOCK_INIT;
    std::vector<Root> roots;
    // key: root index + '|' + upper-cased relative directory ("" for the root)
    std::unordered_map<std::string, std::shared_ptr<const Listing>> listings;
    bool symlinksPermitted = false;
    volatile LONG64 positiveSequence = 0;
};

ExistsFn g_originalExists = nullptr;
State* g_state = nullptr;
GetSearchPathFn g_getSearchPath = nullptr;
FreeListFn g_freeList = nullptr;
GetMountPointFn g_getMountPoint = nullptr;
SymlinksPermittedFn g_symlinksPermitted = nullptr;
GetWriteDirFn g_getWriteDir = nullptr;
bool g_verifyAll = false;

}  // namespace

extern "C" {
// requests = queries seen, hits = answered "exists" from listings, misses = sent
// to the original after a listing miss, skips = sent to the original without a
// lookup, duplicateKeys = directory listings read, verifications/mismatches =
// checks against the original, mode = 1 when installed.
__declspec(dllexport) sb::Stats EU4SB_ExistsStats = SB_STATS_INIT;
}

namespace {

// MSVC std::string: inline buffer when capacity (+0x18) < 16, else pointer at +0.
bool ReadMsvcString(const void* object, const char*& text, std::size_t& length) {
    const auto* bytes = static_cast<const unsigned char*>(object);
    const std::size_t size = *reinterpret_cast<const std::size_t*>(bytes + 0x10);
    const std::size_t capacity = *reinterpret_cast<const std::size_t*>(bytes + 0x18);
    if (size > capacity || size > 4096) return false;
    text = capacity >= 16 ? *reinterpret_cast<const char* const*>(bytes) : reinterpret_cast<const char*>(bytes);
    length = size;
    return text != nullptr;
}

// Accept only plain relative PhysFS paths "a/b/c.ext" of printable ASCII.
bool SplitVirtualPath(const char* text, std::size_t length, std::vector<std::string>& parts) {
    if (length == 0 || text[0] == '/' || text[length - 1] == '/') return false;
    std::string current;
    for (std::size_t index = 0; index < length; ++index) {
        const unsigned char value = static_cast<unsigned char>(text[index]);
        if (value < 0x20 || value > 0x7e || value == '\\' || value == ':' || value == '*' || value == '?' ||
            value == '"' || value == '<' || value == '>' || value == '|' || value == '~') {
            return false;
        }
        if (value == '/') {
            if (current.empty() || current == "." || current == "..") return false;
            parts.push_back(std::move(current));
            current.clear();
            continue;
        }
        current.push_back(static_cast<char>(value >= 'a' && value <= 'z' ? value - ('a' - 'A') : value));
    }
    if (current.empty() || current == "." || current == ".." || current.back() == '.' || current.back() == ' ') {
        return false;
    }
    parts.push_back(std::move(current));
    return true;
}

bool Utf8ToWide(const char* text, std::wstring& out) {
    const int length = static_cast<int>(std::strlen(text));
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, nullptr, 0);
    if (required <= 0) return false;
    out.assign(static_cast<std::size_t>(required), L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, out.data(), required) == required;
}

std::shared_ptr<const Listing> ReadListing(const std::wstring& directory, bool symlinksPermitted) {
    auto listing = std::make_shared<Listing>();
    const std::wstring pattern = directory + L"\\*";
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                   FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        // A missing directory is a usable, empty listing: nothing is answered from it.
        listing->usable = GetLastError() == ERROR_PATH_NOT_FOUND || GetLastError() == ERROR_FILE_NOT_FOUND;
        return listing;
    }
    bool complete = false;
    for (;;) {
        const std::size_t length = wcsnlen(data.cFileName, MAX_PATH);
        const bool dots = (length == 1 && data.cFileName[0] == L'.') ||
                          (length == 2 && data.cFileName[0] == L'.' && data.cFileName[1] == L'.');
        std::string key;
        bool ascii = true;
        for (std::size_t index = 0; index < length && ascii; ++index) {
            const wchar_t value = data.cFileName[index];
            ascii = value > 0 && value < 0x7f;
            key.push_back(static_cast<char>(value >= L'a' && value <= L'z' ? value - (L'a' - L'A') : value));
        }
        if (!dots && ascii) {
            Kind kind = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? Kind::Directory : Kind::File;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                // Junctions behave like directories for PhysFS; symbolic links only
                // when the game permits them; anything else is unknown.
                if (data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT) {
                } else if (data.dwReserved0 == IO_REPARSE_TAG_SYMLINK && symlinksPermitted) {
                } else {
                    kind = Kind::Unknown;
                }
            }
            listing->names.emplace(std::move(key), kind);
        }
        if (!FindNextFileW(find, &data)) {
            complete = GetLastError() == ERROR_NO_MORE_FILES;
            break;
        }
    }
    FindClose(find);
    listing->usable = complete;
    return listing;
}

std::shared_ptr<const Listing> GetListing(State& state, std::size_t rootIndex, const std::vector<std::string>& parts,
                                          std::size_t depth) {
    std::string key = std::to_string(rootIndex) + "|";
    std::wstring directory = state.roots[rootIndex].path;
    for (std::size_t index = 0; index < depth; ++index) {
        key += parts[index];
        key += '/';
        directory += L'\\';
        directory.append(parts[index].begin(), parts[index].end());
    }
    AcquireSRWLockShared(&state.lock);
    auto found = state.listings.find(key);
    std::shared_ptr<const Listing> existing = found == state.listings.end() ? nullptr : found->second;
    ReleaseSRWLockShared(&state.lock);
    if (existing) return existing;
    std::shared_ptr<const Listing> fresh = ReadListing(directory, state.symlinksPermitted);
    sb::Increment(&EU4SB_ExistsStats.duplicateKeys);
    AcquireSRWLockExclusive(&state.lock);
    std::shared_ptr<const Listing> result;
    try {
        result = state.listings.emplace(std::move(key), fresh).first->second;
    } catch (...) {
        result = fresh;
    }
    ReleaseSRWLockExclusive(&state.lock);
    return result;
}

bool FoundUnderRoot(State& state, std::size_t rootIndex, const std::vector<std::string>& parts) {
    for (std::size_t depth = 0; depth < parts.size(); ++depth) {
        const std::shared_ptr<const Listing> listing = GetListing(state, rootIndex, parts, depth);
        if (!listing || !listing->usable) return false;
        const auto entry = listing->names.find(parts[depth]);
        if (entry == listing->names.end() || entry->second == Kind::Unknown) return false;
        if (depth + 1 < parts.size() && entry->second != Kind::Directory) return false;
    }
    return true;
}

// Directory entries of the PhysFS search path mounted at "/", except the write
// directory and anything inside it (the game changes files there while it
// starts, so a listing could be stale).
void LoadRoots(State& state) {
    char** list = g_getSearchPath();
    if (list == nullptr) return;
    std::wstring writeDir;
    if (const char* w = g_getWriteDir ? g_getWriteDir() : nullptr) Utf8ToWide(w, writeDir);
    while (!writeDir.empty() && (writeDir.back() == L'\\' || writeDir.back() == L'/')) writeDir.pop_back();
    for (char** entry = list; *entry != nullptr; ++entry) {
        const char* mountPoint = g_getMountPoint(*entry);
        if (mountPoint == nullptr || std::strcmp(mountPoint, "/") != 0) continue;
        std::wstring path;
        if (!Utf8ToWide(*entry, path)) continue;
        while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
        if (!writeDir.empty() && path.size() >= writeDir.size() &&
            _wcsnicmp(path.c_str(), writeDir.c_str(), writeDir.size()) == 0 &&
            (path.size() == writeDir.size() || path[writeDir.size()] == L'\\' || path[writeDir.size()] == L'/')) {
            continue;
        }
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            state.roots.push_back(Root{path});
        }
    }
    g_freeList(list);
}

INIT_ONCE g_initOnce = INIT_ONCE_STATIC_INIT;

BOOL CALLBACK InitializeState(PINIT_ONCE, PVOID, PVOID*) noexcept {
    try {
        auto* state = new State();
        state->symlinksPermitted = g_symlinksPermitted != nullptr && g_symlinksPermitted() != 0;
        LoadRoots(*state);
        g_state = state;
    } catch (...) {
        g_state = nullptr;
    }
    InterlockedExchange(&EU4SB_ExistsStats.initialized, 1);
    return TRUE;
}

bool ReadyReached() noexcept {
    if (InterlockedCompareExchange(&EU4SB_ExistsStats.readyDisabled, 0, 0) != 0) return true;
    if (sb::ReadyReached()) {
        InterlockedExchange(&EU4SB_ExistsStats.readyDisabled, 1);
        return true;
    }
    return false;
}

}  // namespace

extern "C" bool SB_FileExists(void* self, const void* name) {
    sb::Increment(&EU4SB_ExistsStats.requests);
    const DWORD priorError = GetLastError();
    try {
        const char* text = nullptr;
        std::size_t length = 0;
        std::vector<std::string> parts;
        if (ReadyReached() || InterlockedCompareExchange(&EU4SB_ExistsStats.cacheDisabled, 0, 0) != 0 ||
            name == nullptr || !ReadMsvcString(name, text, length) || !SplitVirtualPath(text, length, parts)) {
            sb::Increment(&EU4SB_ExistsStats.skips);
            SetLastError(priorError);
            return g_originalExists(self, name);
        }
        InitOnceExecuteOnce(&g_initOnce, InitializeState, nullptr, nullptr);
        State* state = g_state;
        if (state == nullptr || state->roots.empty()) {
            sb::Increment(&EU4SB_ExistsStats.skips);
            SetLastError(priorError);
            return g_originalExists(self, name);
        }
        bool found = false;
        for (std::size_t root = 0; root < state->roots.size() && !found; ++root) {
            found = FoundUnderRoot(*state, root, parts);
        }
        SetLastError(priorError);
        if (!found) {
            sb::Increment(&EU4SB_ExistsStats.misses);
            return g_originalExists(self, name);
        }
        const LONG64 sequence = InterlockedIncrement64(&state->positiveSequence);
        if (g_verifyAll || (sequence & 255) == 0) {
            sb::Increment(&EU4SB_ExistsStats.verifications);
            if (!g_originalExists(self, name)) {
                sb::Increment(&EU4SB_ExistsStats.mismatches);
                if (!g_verifyAll) InterlockedExchange(&EU4SB_ExistsStats.cacheDisabled, 1);
                SetLastError(priorError);
                return false;
            }
            SetLastError(priorError);
        }
        sb::Increment(&EU4SB_ExistsStats.hits);
        return true;
    } catch (...) {
        sb::Increment(&EU4SB_ExistsStats.skips);
        SetLastError(priorError);
        return g_originalExists(self, name);
    }
}

namespace sb {

bool InstallExistsIndex(const Build& build, Mode mode) {
    if (mode == Mode::Off) return false;
    HMODULE image = reinterpret_cast<HMODULE>(context().image_base);
    g_getSearchPath = reinterpret_cast<GetSearchPathFn>(GetProcAddress(image, "PHYSFS_getSearchPath"));
    g_freeList = reinterpret_cast<FreeListFn>(GetProcAddress(image, "PHYSFS_freeList"));
    g_getMountPoint = reinterpret_cast<GetMountPointFn>(GetProcAddress(image, "PHYSFS_getMountPoint"));
    g_symlinksPermitted = reinterpret_cast<SymlinksPermittedFn>(GetProcAddress(image, "PHYSFS_symbolicLinksPermitted"));
    g_getWriteDir = reinterpret_cast<GetWriteDirFn>(GetProcAddress(image, "PHYSFS_getWriteDir"));
    if (!g_getSearchPath || !g_freeList || !g_getMountPoint || !g_getWriteDir) {
        Log(ANVIL_LOG_WARNING, "exists_index: PhysFS exports not found; module off");
        return false;
    }
    const auto* code = AtRva<const unsigned char>(build.fs_exists_original);
    if (std::memcmp(code, kOriginalExistsPrefix, sizeof(kOriginalExistsPrefix)) != 0) {
        Log(ANVIL_LOG_WARNING, "exists_index: unexpected code at the original function; module off");
        return false;
    }
    g_originalExists = reinterpret_cast<ExistsFn>(const_cast<unsigned char*>(code));
    g_verifyAll = mode == Mode::Verify;
    const uintptr_t expected = reinterpret_cast<uintptr_t>(code);
    const uintptr_t replacement = reinterpret_cast<uintptr_t>(&SB_FileExists);
    const AnvilResult r = context().host->patch_bytes(context().self, build.fs_vtable_exists_slot, &expected,
                                                       &replacement, sizeof(uintptr_t));
    if (r != ANVIL_OK) {
        Log(ANVIL_LOG_WARNING, "exists_index: vtable slot refused (%s); module off", ResultName(r));
        return false;
    }
    InterlockedExchange(&EU4SB_ExistsStats.mode, 1);
    return true;
}

void ReportExistsIndex() { LogStats("exists_index", EU4SB_ExistsStats); }

}  // namespace sb
