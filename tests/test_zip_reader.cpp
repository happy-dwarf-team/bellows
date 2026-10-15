// ZIP reader used by ram_prefetch: central directory, local header checks,
// stored members and CRC-32 (DEFLATE needs the game's tinfl and is covered by
// in-game verify runs).
#include "modules/zip_reader.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

void Put16(std::vector<std::uint8_t>& b, unsigned v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void Put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    Put16(b, v & 0xFFFF);
    Put16(b, v >> 16);
}

struct Member {
    std::string name;
    std::string data;
    unsigned method = 0;
    std::uint32_t crcOverride = 0;  // 0 = real CRC
};

std::vector<std::uint8_t> MakeZip(const std::vector<Member>& members) {
    std::vector<std::uint8_t> zip, central;
    for (const auto& m : members) {
        const std::uint32_t crc = m.crcOverride ? m.crcOverride
                                                : sb::zip::Crc32(reinterpret_cast<const std::uint8_t*>(m.data.data()), m.data.size());
        const auto offset = static_cast<std::uint32_t>(zip.size());
        Put32(zip, 0x04034B50); Put16(zip, 20); Put16(zip, 0); Put16(zip, m.method); Put16(zip, 0); Put16(zip, 0);
        Put32(zip, crc); Put32(zip, static_cast<std::uint32_t>(m.data.size())); Put32(zip, static_cast<std::uint32_t>(m.data.size()));
        Put16(zip, static_cast<unsigned>(m.name.size())); Put16(zip, 0);
        zip.insert(zip.end(), m.name.begin(), m.name.end());
        zip.insert(zip.end(), m.data.begin(), m.data.end());
        Put32(central, 0x02014B50); Put16(central, 20); Put16(central, 20); Put16(central, 0); Put16(central, m.method);
        Put16(central, 0); Put16(central, 0); Put32(central, crc);
        Put32(central, static_cast<std::uint32_t>(m.data.size())); Put32(central, static_cast<std::uint32_t>(m.data.size()));
        Put16(central, static_cast<unsigned>(m.name.size())); Put16(central, 0); Put16(central, 0); Put16(central, 0);
        Put16(central, 0); Put32(central, 0); Put32(central, offset);
        central.insert(central.end(), m.name.begin(), m.name.end());
    }
    const auto centralOffset = static_cast<std::uint32_t>(zip.size());
    zip.insert(zip.end(), central.begin(), central.end());
    Put32(zip, 0x06054B50); Put16(zip, 0); Put16(zip, 0);
    Put16(zip, static_cast<unsigned>(members.size())); Put16(zip, static_cast<unsigned>(members.size()));
    Put32(zip, static_cast<std::uint32_t>(central.size())); Put32(zip, centralOffset); Put16(zip, 0);
    return zip;
}

HANDLE Open(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    {
        FILE* f = _wfopen(path.c_str(), L"wb");
        std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
    }
    return CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
}

bool ReadAll(HANDLE file, const std::string& name, const sb::zip::Entry& e, std::string& out) {
    out.assign(static_cast<std::size_t>(e.uncompressedSize), '\0');
    return sb::zip::ReadMember(file, name, e, nullptr, reinterpret_cast<std::uint8_t*>(out.data()));
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    assert(sb::zip::Crc32(reinterpret_cast<const std::uint8_t*>("123456789"), 9) == 0xCBF43926);  // standard CRC-32

    const fs::path dir = fs::temp_directory_path() / ("sb-zip-test-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(dir);

    const std::vector<Member> members = {
        {"gfx/a.dds", "first member"},
        {"music/b.ogg", std::string(70000, 'x')},
        {"bad/crc.txt", "corrupted", 0, 0x12345678},
        {"deflate/c.txt", "pretend", 8},
        {"dup.txt", "one"},
        {"dup.txt", "two"},
    };
    HANDLE file = Open(dir / "test.zip", MakeZip(members));
    assert(file != INVALID_HANDLE_VALUE);

    std::unordered_map<std::string, sb::zip::Entry> entries;
    assert(sb::zip::ParseCentralDirectory(file, {}, entries));
    assert(entries.size() == 5 && entries.at("dup.txt").duplicate);
    std::string out;
    assert(ReadAll(file, "gfx/a.dds", entries.at("gfx/a.dds"), out) && out == "first member");
    assert(ReadAll(file, "music/b.ogg", entries.at("music/b.ogg"), out) && out == std::string(70000, 'x'));
    assert(!ReadAll(file, "bad/crc.txt", entries.at("bad/crc.txt"), out));          // CRC mismatch
    assert(!ReadAll(file, "deflate/c.txt", entries.at("deflate/c.txt"), out));      // no decoder given
    assert(!ReadAll(file, "dup.txt", entries.at("dup.txt"), out));                  // duplicate name
    assert(!ReadAll(file, "gfx/other.dds", entries.at("gfx/a.dds"), out));          // local name differs

    // Targets: only the requested names are returned.
    assert(sb::zip::ParseCentralDirectory(file, {"music/b.ogg", "missing"}, entries));
    assert(entries.size() == 1 && entries.count("music/b.ogg") == 1);
    CloseHandle(file);

    // Truncated archive (no end record) fails.
    auto broken = MakeZip(members);
    broken.resize(broken.size() - 10);
    file = Open(dir / "broken.zip", broken);
    assert(!sb::zip::ParseCentralDirectory(file, {}, entries));
    CloseHandle(file);

    fs::remove_all(dir);
    std::printf("zip reader ok\n");
    return 0;
}
