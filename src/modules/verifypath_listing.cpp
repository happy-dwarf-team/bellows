// Start-up answers for the stat calls of PhysFS verifyPath (ported from the
// lab's startup_verifypath_listing.inc).
//
// PhysFS verifyPath runs whenever symbolic links are not permitted (always in
// this game). For every searched directory archive it stats each prefix of the
// path ("gfx", "gfx/interface", "gfx/interface/x.dds") through the archiver's
// stat slot (call [r9+68h]) only to learn: did the stat succeed, is the entry a
// symlink (filetype 2), and on failure is the error PHYSFS_ERR_NOT_FOUND (0xb).
//
// The call site is redirected (byte-checked, through Anvil) to a stub that
// calls SB_VerifyPathStat. For the directory archiver it answers from
// once-read directory listings exactly as the platform stat would for
// verifyPath's purposes: existing directory/junction -> rc 1, filetype 1;
// existing file -> rc 1, filetype 0; missing name or directory -> rc 0 with
// PHYSFS_setErrorCode(0xb). Symbolic links, other reparse points, unusual
// spellings, non-directory archives, unreadable listings, a disabled module
// and anything after the ready marker go to the original stat. Every 256th
// answer is compared with the original; a disagreement disables the module
// ("verify" mode compares every answer). Assumes game and mod files do not
// change while the game starts.
#include "modules.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace {

constexpr unsigned char kSiteBytes[6] = {0x41, 0xFF, 0x51, 0x68, 0x8B, 0xE8};  // call [r9+68h]; mov ebp,eax
constexpr int kPhysfsNotFound = 0xB;

using StatFn = int (*)(void*, const char*, void*);
using SetErrorFn = void (*)(int);
using GetErrorFn = int (*)();
using GetWriteDirFn = const char* (*)();

enum class Kind : unsigned char { File, Directory, Unknown };

struct Listing {
    bool usable = false;
    bool missing = false;  // the directory itself does not exist
    std::unordered_map<std::string, Kind> names;  // upper-cased ASCII names
};

struct PhysfsStat {
    std::int64_t filesize;
    std::int64_t modtime;
    std::int64_t createtime;
    std::int64_t accesstime;
    std::int32_t filetype;
    std::int32_t readonly;
};
static_assert(sizeof(PhysfsStat) == 0x28, "PHYSFS_Stat layout drift");

uintptr_t g_dirArchiver = 0;
SetErrorFn g_setError = nullptr;
GetErrorFn g_getError = nullptr;
GetWriteDirFn g_getWriteDir = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_map<std::wstring, std::shared_ptr<const Listing>>* g_listings = nullptr;
volatile LONG64 g_answerSequence = 0;
bool g_verifyAll = false;

}  // namespace

extern "C" {
// requests = calls seen, hits = answered "exists", misses = answered NOT_FOUND,
// skips = passed to the original, duplicateKeys = listings read,
// verifications/mismatches = checks against the original, mode = 1 patched,
// cacheDisabled = disabled after a mismatch.
__declspec(dllexport) sb::Stats EU4SB_VerifyPathStats = SB_STATS_INIT;
}

namespace {

std::shared_ptr<const Listing> ReadListing(const std::wstring& directory) {
    auto listing = std::make_shared<Listing>();
    const std::wstring pattern = directory + L"\\*";
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                   FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_PATH_NOT_FOUND) {
            listing->usable = true;
            listing->missing = true;
        }
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
            // The platform stat reports a symlink only for IO_REPARSE_TAG_SYMLINK;
            // a junction is a directory. Other reparse points: let the original decide.
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && data.dwReserved0 != IO_REPARSE_TAG_MOUNT_POINT) {
                kind = Kind::Unknown;
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

std::shared_ptr<const Listing> GetListing(const std::wstring& directory) {
    AcquireSRWLockShared(&g_lock);
    std::shared_ptr<const Listing> existing;
    auto found = g_listings->find(directory);
    if (found != g_listings->end()) existing = found->second;
    ReleaseSRWLockShared(&g_lock);
    if (existing) return existing;
    std::shared_ptr<const Listing> fresh = ReadListing(directory);
    sb::Increment(&EU4SB_VerifyPathStats.duplicateKeys);
    AcquireSRWLockExclusive(&g_lock);
    std::shared_ptr<const Listing> result;
    try {
        result = g_listings->emplace(directory, fresh).first->second;
    } catch (...) {
        result = fresh;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

enum class Answer { Original, Exists, NotFound };

// True when `root` is PhysFS's write directory (or inside it): the game creates,
// renames and deletes files there while it starts (logs, settings), so a listing
// read earlier can be stale (seen in the plugin's verify run: logs/error.log
// renamed during start-up). Such roots always go to the original stat.
bool IsWriteRoot(const char* root) {
    if (g_getWriteDir == nullptr) return true;
    const char* write = g_getWriteDir();  // PhysFS state lock is recursive; verifyPath already holds it
    if (write == nullptr || *write == '\0') return false;
    std::size_t wl = std::strlen(write);
    while (wl > 0 && (write[wl - 1] == '\\' || write[wl - 1] == '/')) --wl;
    std::size_t rl = std::strlen(root);
    while (rl > 0 && (root[rl - 1] == '\\' || root[rl - 1] == '/')) --rl;
    if (rl < wl || _strnicmp(root, write, wl) != 0) return false;
    return rl == wl || root[wl] == '\\' || root[wl] == '/';
}

// Mirrors the directory archiver: native path = opaque + name, '/' -> '\'.
Answer Decide(const char* root, const char* name, Kind& kind) {
    if (root == nullptr || name == nullptr || *name == '\0') return Answer::Original;
    if (IsWriteRoot(root)) return Answer::Original;
    std::wstring directory;
    const int rootLength = static_cast<int>(std::strlen(root));
    if (rootLength < 4) return Answer::Original;
    const int wide = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, root, rootLength, nullptr, 0);
    if (wide <= 0) return Answer::Original;
    directory.assign(static_cast<std::size_t>(wide), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, root, rootLength, directory.data(), wide);
    if (directory.back() != L'\\') return Answer::Original;  // the archiver keeps a trailing separator
    directory.pop_back();
    std::string component;
    for (const char* cursor = name;; ++cursor) {
        const unsigned char value = static_cast<unsigned char>(*cursor);
        if (value != '/' && value != '\0') {
            if (value < 0x20 || value > 0x7e || value == '\\' || value == ':' || value == '*' || value == '?' ||
                value == '"' || value == '<' || value == '>' || value == '|' || value == '~') {
                return Answer::Original;
            }
            component.push_back(static_cast<char>(value >= 'a' && value <= 'z' ? value - ('a' - 'A') : value));
            continue;
        }
        if (component.empty() || component == "." || component == ".." || component.back() == '.' ||
            component.back() == ' ') {
            return Answer::Original;
        }
        const std::shared_ptr<const Listing> listing = GetListing(directory);
        if (!listing || !listing->usable) return Answer::Original;
        if (listing->missing) return Answer::NotFound;
        const auto entry = listing->names.find(component);
        if (entry == listing->names.end()) return Answer::NotFound;
        if (entry->second == Kind::Unknown) return Answer::Original;
        if (value == '\0') {
            kind = entry->second;
            return Answer::Exists;
        }
        if (entry->second != Kind::Directory) return Answer::Original;  // "file/child": exact error left to the original
        directory.push_back(L'\\');
        directory.append(component.begin(), component.end());
        component.clear();
    }
}

bool Disabled() noexcept {
    if (InterlockedCompareExchange(&EU4SB_VerifyPathStats.cacheDisabled, 0, 0) != 0) return true;
    if (InterlockedCompareExchange(&EU4SB_VerifyPathStats.readyDisabled, 0, 0) != 0) return true;
    if (sb::ReadyReached()) {
        InterlockedExchange(&EU4SB_VerifyPathStats.readyDisabled, 1);
        return true;
    }
    return false;
}

}  // namespace

extern "C" int SB_VerifyPathStat(void* opaque, const char* name, void* statOut, const void* funcs) {
    sb::Increment(&EU4SB_VerifyPathStats.requests);
    const auto original = *reinterpret_cast<const StatFn*>(static_cast<const unsigned char*>(funcs) + 0x68);
    try {
        Kind kind = Kind::Unknown;
        const Answer answer = (Disabled() || reinterpret_cast<uintptr_t>(funcs) != g_dirArchiver || g_listings == nullptr)
                                  ? Answer::Original
                                  : Decide(static_cast<const char*>(opaque), name, kind);
        if (answer == Answer::Original) {
            sb::Increment(&EU4SB_VerifyPathStats.skips);
            return original(opaque, name, statOut);
        }
        const LONG64 sequence = InterlockedIncrement64(&g_answerSequence);
        if (g_verifyAll || (sequence & 255) == 0) {
            sb::Increment(&EU4SB_VerifyPathStats.verifications);
            PhysfsStat actual{};
            const int rc = original(opaque, name, &actual);
            const int error = rc ? 0 : g_getError();  // clears the code; set again below
            const bool same = answer == Answer::Exists ? (rc == 1 && actual.filetype != 2)
                                                       : (rc == 0 && error == kPhysfsNotFound);
            if (!same || g_verifyAll) {
                if (!same) {
                    const LONG64 count = InterlockedIncrement64(&EU4SB_VerifyPathStats.mismatches);
                    if (!g_verifyAll) InterlockedExchange(&EU4SB_VerifyPathStats.cacheDisabled, 1);
                    if (count <= 10) {
                        sb::Log(ANVIL_LOG_WARNING,
                                "verifypath mismatch: root '%s' name '%s': listing says %s, original rc %d type %d error %d",
                                static_cast<const char*>(opaque), name,
                                answer == Answer::Exists ? (kind == Kind::Directory ? "directory" : "file") : "not found",
                                rc, actual.filetype, error);
                    }
                }
                std::memcpy(statOut, &actual, sizeof(actual));
                if (!rc) g_setError(error);
                return rc;
            }
        }
        if (answer == Answer::NotFound) {
            sb::Increment(&EU4SB_VerifyPathStats.misses);
            g_setError(kPhysfsNotFound);
            return 0;
        }
        PhysfsStat result{};
        result.filesize = kind == Kind::Directory ? 0 : -1;  // not read by verifyPath
        result.modtime = result.createtime = result.accesstime = -1;
        result.filetype = kind == Kind::Directory ? 1 : 0;
        std::memcpy(statOut, &result, sizeof(result));
        sb::Increment(&EU4SB_VerifyPathStats.hits);
        return 1;
    } catch (...) {
        sb::Increment(&EU4SB_VerifyPathStats.skips);
        return original(opaque, name, statOut);
    }
}

namespace sb {

bool InstallVerifyPathListing(const Build& build, Mode mode) {
    if (mode == Mode::Off) return false;
    HMODULE image = reinterpret_cast<HMODULE>(context().image_base);
    g_setError = reinterpret_cast<SetErrorFn>(GetProcAddress(image, "PHYSFS_setErrorCode"));
    g_getError = reinterpret_cast<GetErrorFn>(GetProcAddress(image, "PHYSFS_getLastErrorCode"));
    g_getWriteDir = reinterpret_cast<GetWriteDirFn>(GetProcAddress(image, "PHYSFS_getWriteDir"));
    if (!g_setError || !g_getError || !g_getWriteDir) {
        Log(ANVIL_LOG_WARNING, "verifypath: PhysFS exports not found; module off");
        return false;
    }
    if (*AtRva<uintptr_t>(build.dir_archiver + 0x68) != context().image_base + build.dir_stat) {
        Log(ANVIL_LOG_WARNING, "verifypath: unexpected DIR archiver stat entry; module off");
        return false;
    }
    try {
        g_listings = new std::unordered_map<std::wstring, std::shared_ptr<const Listing>>();
    } catch (...) {
        return false;
    }
    g_dirArchiver = context().image_base + build.dir_archiver;
    g_verifyAll = mode == Mode::Verify;

    // Stub: sub rsp,28h; call [rip+16h] (slot at +0x20); add rsp,28h; mov ebp,eax; ret
    const AnvilHost* host = context().host;
    auto* stub = static_cast<unsigned char*>(host->alloc_near_image(context().self, 0x28));
    if (stub == nullptr) {
        Log(ANVIL_LOG_WARNING, "verifypath: no memory near the image; module off");
        return false;
    }
    const unsigned char code[] = {0x48, 0x83, 0xEC, 0x28, 0xFF, 0x15, 0x16, 0x00, 0x00, 0x00,
                                  0x48, 0x83, 0xC4, 0x28, 0x8B, 0xE8, 0xC3};
    std::memset(stub, 0xCC, 0x28);
    std::memcpy(stub, code, sizeof(code));
    const uintptr_t target = reinterpret_cast<uintptr_t>(&SB_VerifyPathStat);
    std::memcpy(stub + 0x20, &target, sizeof(target));
    FlushInstructionCache(GetCurrentProcess(), stub, 0x28);

    const uintptr_t site = context().image_base + build.verifypath_site;
    unsigned char call[6] = {0xE8, 0, 0, 0, 0, 0x90};
    const auto rel = static_cast<std::int32_t>(reinterpret_cast<intptr_t>(stub) - static_cast<intptr_t>(site + 5));
    std::memcpy(call + 1, &rel, sizeof(rel));
    const AnvilResult r = host->patch_bytes(context().self, build.verifypath_site, kSiteBytes, call, sizeof(call));
    if (r != ANVIL_OK) {
        Log(ANVIL_LOG_WARNING, "verifypath: call site refused (%s); module off", ResultName(r));
        return false;
    }
    InterlockedExchange(&EU4SB_VerifyPathStats.mode, 1);
    InterlockedExchange(&EU4SB_VerifyPathStats.initialized, 1);
    return true;
}

void ReportVerifyPathListing() { LogStats("verifypath_listing", EU4SB_VerifyPathStats); }

}  // namespace sb
