// attr_cache path rules and mod folder discovery.
#include "modules/access_profile.hpp"
#include "modules/path_rules.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

int main() {
    namespace rules = sb::path_rules;
    const std::vector<std::wstring> roots = {L"C:\\Games\\EU4", L"D:\\workshop\\236850\\123"};

    assert(rules::IsEligible(L"C:\\Games\\EU4\\gfx\\a.dds", roots));
    assert(rules::IsEligible(L"c:\\games\\eu4\\GFX\\a.dds", roots));        // case-insensitive root
    assert(rules::IsEligible(L"D:\\workshop\\236850\\123\\common\\x.txt", roots));
    assert(!rules::IsEligible(L"C:\\Games\\EU4", roots));                    // the root itself
    assert(!rules::IsEligible(L"C:\\Games\\EU4x\\a.dds", roots));            // prefix, not a folder
    assert(!rules::IsEligible(L"C:\\Games\\other\\a.dds", roots));
    assert(!rules::IsEligible(L"C:\\Games\\EU4\\logs\\error.log", roots));   // excluded component
    assert(rules::IsEligible(L"C:\\Games\\EU4\\save games\\x", roots));     // "save games" is not "save"
    assert(!rules::IsEligible(L"C:\\Games\\EU4\\Saves\\x.eu4", roots));
    assert(!rules::IsEligible(L"C:\\Games\\EU4\\gfx\\Cache\\a", roots));
    assert(rules::IsEligible(L"C:\\Games\\EU4\\gfx\\cached\\a", roots));     // whole components only
    assert(!rules::IsEligible(L"\\\\?\\C:\\Games\\EU4\\a.dds", roots));     // device spelling
    assert(!rules::IsEligible(L"gfx\\a.dds", roots));

    assert(rules::IsCacheableError(ERROR_FILE_NOT_FOUND) && rules::IsCacheableError(ERROR_PATH_NOT_FOUND));
    assert(!rules::IsCacheableError(ERROR_ACCESS_DENIED) && !rules::IsCacheableError(ERROR_INVALID_NAME));

    // ModFolders: "path" (absolute or relative to the user folder) or the folder of "archive".
    namespace fs = std::filesystem;
    const fs::path user = fs::temp_directory_path() / ("sb-rules-test-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(user / "mod");
    std::ofstream(user / "mod" / "a.mod") << "name=\"A\"\npath=\"D:/workshop/236850/123/\"\n";
    std::ofstream(user / "mod" / "b.mod") << "name=\"B\"\npath=\"mod/b\"\n";
    std::ofstream(user / "mod" / "c.mod") << "name=\"C\"\narchive=\"mod/c/c.zip\"\n";
    const auto folders = sb::profile::ModFolders(user.wstring(), {"mod/a.mod", "mod/b.mod", "mod/c.mod", "mod/missing.mod"});
    assert(folders.size() == 3);
    assert(folders[0] == L"D:\\workshop\\236850\\123");
    assert(folders[1] == user.wstring() + L"\\mod\\b");
    assert(folders[2] == user.wstring() + L"\\mod\\c");
    fs::remove_all(user);

    std::printf("path rules ok\n");
    return 0;
}
