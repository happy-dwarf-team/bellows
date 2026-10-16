// config.toml defaults, parsing and environment overrides.
#include "config.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("sb-config-test-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(dir);

    // Missing file: defaults ("on" everywhere) and a commented default file is written.
    sb::Config c = sb::LoadConfig(dir);
    assert(c.mode("mimalloc") == sb::Mode::On && c.mode("metascript_subst") == sb::Mode::On);
    assert(c.mode("trigger_intern") == sb::Mode::On && c.mode("attr_cache") == sb::Mode::Off);  // attr_cache off by default
    assert(fs::exists(dir / "config.toml"));
    // The written default parses to the same values.
    c = sb::LoadConfig(dir);
    assert(c.notes.empty() && c.mode("exists_index") == sb::Mode::On);
    assert(c.ram_prefetch_budget_mb == sb::kDefaultRamPrefetchBudgetMb);

    // Budget: from config.toml, invalid values keep the default, the environment overrides.
    {
        std::ofstream out(dir / "config.toml", std::ios::trunc);
        out << "[ram_prefetch]\nbudget_mb = 512\n";
    }
    c = sb::LoadConfig(dir);
    assert(c.ram_prefetch_budget_mb == 512 && c.notes.empty());
    {
        std::ofstream out(dir / "config.toml", std::ios::trunc);
        out << "[ram_prefetch]\nbudget_mb = 10\n";
    }
    c = sb::LoadConfig(dir);
    assert(c.ram_prefetch_budget_mb == sb::kDefaultRamPrefetchBudgetMb && c.notes.size() == 1);
    SetEnvironmentVariableA("EU4SB_RAM_PREFETCH_BUDGET_MB", "2048");
    c = sb::LoadConfig(dir);
    assert(c.ram_prefetch_budget_mb == 2048);
    SetEnvironmentVariableA("EU4SB_RAM_PREFETCH_BUDGET_MB", "lots");
    c = sb::LoadConfig(dir);
    assert(c.ram_prefetch_budget_mb == sb::kDefaultRamPrefetchBudgetMb);
    SetEnvironmentVariableA("EU4SB_RAM_PREFETCH_BUDGET_MB", nullptr);

    {
        std::ofstream out(dir / "config.toml", std::ios::trunc);
        out << "[modules]\nmimalloc = \"verify\"\nexists_index = \"off\"\ntrigger_intern = \"verify\"\n"
               "verifypath_listing = \"maybe\"\n";
    }
    c = sb::LoadConfig(dir);
    assert(c.mode("exists_index") == sb::Mode::Off);
    assert(c.mode("trigger_intern") == sb::Mode::Verify);
    assert(c.mode("mimalloc") == sb::Mode::On);             // no verify mode -> on
    assert(c.mode("verifypath_listing") == sb::Mode::On);   // invalid value -> default
    assert(c.notes.size() == 2);
    assert(c.mode("no_such_module") == sb::Mode::Off);

    SetEnvironmentVariableA("EU4SB_EXISTS_INDEX", "verify");
    SetEnvironmentVariableA("EU4SB_METASCRIPT_SUBST", "off");
    c = sb::LoadConfig(dir);
    assert(c.mode("exists_index") == sb::Mode::Verify && c.mode("metascript_subst") == sb::Mode::Off);
    SetEnvironmentVariableA("EU4SB_EXISTS_INDEX", nullptr);
    SetEnvironmentVariableA("EU4SB_METASCRIPT_SUBST", nullptr);

    {
        std::ofstream out(dir / "config.toml", std::ios::trunc);
        out << "[modules\nbroken";
    }
    c = sb::LoadConfig(dir);
    assert(c.mode("mimalloc") == sb::Mode::On && c.notes.size() == 1);  // broken file -> defaults + note

    fs::remove_all(dir);
    std::printf("config fixture ok\n");
    return 0;
}
