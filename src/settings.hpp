#pragma once

// Module modes and the move from config.toml (Bellows 0.7.0 and earlier) to
// Anvil settings (plugin/settings.toml, design §11).
//
// Anvil reads the player's values (anvil/data/bellows/settings.toml) and the
// ANVIL_BELLOWS_<NAME> overrides before the plugin loads. The first start
// after the update still finds the old anvil/data/bellows/config.toml: its
// values that differ from the defaults are written to settings.toml, the old
// file is renamed to config.toml.migrated, and the same values apply to that
// start as well (unless an ANVIL_BELLOWS_<NAME> variable overrides them).

#include "common.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sb {

std::string ModeName(Mode mode);

// "on", "off", "verify"; config.toml also accepted 1/0/true/false in any case.
std::optional<Mode> ParseMode(std::string_view text);

// One line of the player's settings file: `name = literal` (a TOML literal).
struct SettingValue {
    std::string name;
    std::string literal;
};

struct Migration {
    std::vector<SettingValue> values;  // those that differ from the defaults of plugin/settings.toml
    std::vector<std::string> notes;    // for the log
};

// config.toml text to setting values, checked against the schema (the text
// of plugin/settings.toml): [modules] <name> = mode becomes <name>, and
// [ram_prefetch] budget_mb becomes ram_prefetch_budget_mb. Invalid or
// unknown values are left out with a note; "verify" for a module without a
// verify mode becomes "on", as config.toml treated it.
Migration MigrateConfigText(std::string_view config, std::string_view schema);

// The player's settings file for these values (a flat TOML table).
std::string PlayerSettingsText(const std::vector<SettingValue>& values);

// The first start after the update: data_dir/config.toml to
// data_dir/settings.toml (only when that does not exist yet), and
// config.toml renamed to config.toml.migrated. Returns the migrated values,
// which also apply to this start. Nothing happens without config.toml.
Migration MigrateConfigFile(const std::filesystem::path& data_dir, const std::filesystem::path& schema_file);

// EU4SB_<NAME> variables (read up to 0.7.0) that are still set, each with
// its replacement ANVIL_BELLOWS_<NAME>.
std::vector<std::string> FormerEnvironmentNotes();

}  // namespace sb
