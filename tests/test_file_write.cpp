// sb::ReplaceFileWith (src/file_write.hpp): a failed write keeps the last good file.
#include "../src/file_write.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("sb_file_write_" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path file = dir / "stats.tsv";

    // A new file, then a replacement.
    assert(sb::ReplaceFileWith(file.wstring(), "a\t1\n"));
    assert(ReadAll(file) == "a\t1\n");
    assert(sb::ReplaceFileWith(file.wstring(), "a\t2\nb\t3\n"));
    assert(ReadAll(file) == "a\t2\nb\t3\n");
    assert(!fs::exists(dir / "stats.tsv.tmp"));

    // The temp file cannot be opened (a folder of that name): the old file stays as it was.
    fs::create_directory(dir / "stats.tsv.tmp");
    assert(!sb::ReplaceFileWith(file.wstring(), "lost\n"));
    assert(ReadAll(file) == "a\t2\nb\t3\n");
    fs::remove(dir / "stats.tsv.tmp");

    // A folder that does not exist: nothing is written, nothing is left behind.
    assert(!sb::ReplaceFileWith((dir / "missing" / "x.tsv").wstring(), "x\n"));
    assert(!fs::exists(dir / "missing"));

    // An empty text is a valid file.
    assert(sb::ReplaceFileWith(file.wstring(), ""));
    assert(fs::file_size(file) == 0);

    fs::remove_all(dir);
    std::puts("file_write: ok");
    return 0;
}
