// config.toml (0.7.0 and earlier) to the Anvil settings of plugin/settings.toml.
#include "settings.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

static std::string Slurp(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

static void Write(const fs::path& file, const std::string& text) { std::ofstream(file, std::ios::binary) << text; }

static const sb::SettingValue* Find(const sb::Migration& m, const char* name) {
    for (const auto& v : m.values) {
        if (v.name == name) return &v;
    }
    return nullptr;
}

int main() {
    const std::string schema = Slurp(SB_SETTINGS_SCHEMA);
    assert(!schema.empty());

    // The default file written by 0.7.0: every value is the default, nothing to write.
    sb::Migration m = sb::MigrateConfigText(
        "[modules]\nmimalloc = \"on\"\nverifypath_listing = \"on\"\nexists_index = \"on\"\ntrigger_intern = \"on\"\n"
        "metascript_subst = \"on\"\nopen_warmup = \"on\"\nram_prefetch = \"on\"\nattr_cache = \"off\"\n\n"
        "[ram_prefetch]\nbudget_mb = 1024\n",
        schema);
    assert(m.values.empty() && m.notes.empty());

    // Changed values, the old spellings, a module without verify mode, invalid and unknown entries.
    m = sb::MigrateConfigText(
        "[modules]\nexists_index = \"OFF\"\ntrigger_intern = \"verify\"\nattr_cache = \"1\"\nmimalloc = \"verify\"\n"
        "verifypath_listing = \"maybe\"\nno_such_module = \"on\"\nrange_witness = \"off\"\nzip_profile = \"true\"\n"
        "[ram_prefetch]\nbudget_mb = 2048\n",
        schema);
    assert(Find(m, "exists_index") && Find(m, "exists_index")->literal == "\"off\"");
    assert(Find(m, "trigger_intern")->literal == "\"verify\"");
    assert(Find(m, "attr_cache")->literal == "\"on\"");          // default off
    assert(Find(m, "mimalloc") == nullptr);                       // verify -> on, the default
    assert(Find(m, "verifypath_listing") == nullptr);             // invalid -> left out
    assert(Find(m, "no_such_module") == nullptr);
    assert(Find(m, "range_witness")->literal == "\"off\"");
    assert(Find(m, "zip_profile")->literal == "\"on\"");          // diagnostic, default off
    assert(Find(m, "ram_prefetch_budget_mb")->literal == "2048");
    assert(m.values.size() == 6 && m.notes.size() == 3);

    // startup is a mode of mimalloc only (its default now); another module migrates it as on.
    m = sb::MigrateConfigText("[modules]\nmimalloc = \"startup\"\nexists_index = \"startup\"\n", schema);
    assert(Find(m, "mimalloc") == nullptr);      // startup, the default
    assert(Find(m, "exists_index") == nullptr);  // on, the default
    assert(m.values.empty() && m.notes.size() == 1);
    // mimalloc = "on" was 0.7.0's default, written into every config.toml: the new default applies.
    m = sb::MigrateConfigText("[modules]\nmimalloc = \"on\"\n", schema);
    assert(m.values.empty() && m.notes.empty());
    m = sb::MigrateConfigText("[modules]\nmimalloc = \"off\"\n", schema);
    assert(Find(m, "mimalloc")->literal == "\"off\"");

    // Budget outside 64-8192 is left out; a broken file migrates nothing.
    m = sb::MigrateConfigText("[ram_prefetch]\nbudget_mb = 10\n", schema);
    assert(m.values.empty() && m.notes.size() == 1);
    m = sb::MigrateConfigText("[modules\nbroken", schema);
    assert(m.values.empty() && m.notes.size() == 1);

    // The player's file is a flat table Anvil reads.
    const std::string text = sb::PlayerSettingsText({{"exists_index", "\"off\""}, {"ram_prefetch_budget_mb", "2048"}});
    assert(text.find("\nexists_index = \"off\"\n") != std::string::npos);
    assert(text.find("\nram_prefetch_budget_mb = 2048\n") != std::string::npos);

    // The files: config.toml -> settings.toml + config.toml.migrated, once.
    const fs::path dir = fs::temp_directory_path() / ("sb-settings-test-" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    m = sb::MigrateConfigFile(dir, SB_SETTINGS_SCHEMA);  // no config.toml: nothing happens
    assert(m.values.empty() && m.notes.empty() && !fs::exists(dir / "settings.toml"));

    Write(dir / "config.toml", "[modules]\nmetascript_subst = \"off\"\n");
    m = sb::MigrateConfigFile(dir, SB_SETTINGS_SCHEMA);
    assert(m.values.size() == 1 && m.values[0].name == "metascript_subst");
    assert(!fs::exists(dir / "config.toml") && fs::exists(dir / "config.toml.migrated"));
    assert(Slurp(dir / "settings.toml").find("metascript_subst = \"off\"") != std::string::npos);
    m = sb::MigrateConfigFile(dir, SB_SETTINGS_SCHEMA);  // second start: nothing left to do
    assert(m.values.empty() && m.notes.empty());

    // Only defaults: no settings.toml is written, the old file is still renamed.
    fs::remove(dir / "settings.toml");
    Write(dir / "config.toml", "[modules]\nmimalloc = \"on\"\n");
    m = sb::MigrateConfigFile(dir, SB_SETTINGS_SCHEMA);
    assert(m.values.empty() && !fs::exists(dir / "settings.toml") && !fs::exists(dir / "config.toml"));

    // A settings.toml that exists already wins: config.toml is set aside, not migrated.
    Write(dir / "settings.toml", "exists_index = \"verify\"\n");
    Write(dir / "config.toml", "[modules]\nexists_index = \"off\"\n");
    m = sb::MigrateConfigFile(dir, SB_SETTINGS_SCHEMA);
    assert(m.values.empty() && m.notes.size() == 1 && !fs::exists(dir / "config.toml"));
    assert(Slurp(dir / "settings.toml") == "exists_index = \"verify\"\n");

    // Modes and the former variables.
    assert(sb::ParseMode("Verify") == sb::Mode::Verify && !sb::ParseMode("maybe"));
    SetEnvironmentVariableA("EU4SB_EXISTS_INDEX", "off");
    bool named = false;
    for (const auto& note : sb::FormerEnvironmentNotes()) named = named || note.find("ANVIL_BELLOWS_EXISTS_INDEX") != std::string::npos;
    assert(named);
    SetEnvironmentVariableA("EU4SB_EXISTS_INDEX", nullptr);

    fs::remove_all(dir);
    std::printf("settings fixture ok\n");
    return 0;
}
