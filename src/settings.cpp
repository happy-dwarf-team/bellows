#include "settings.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace sb {
namespace {

namespace fs = std::filesystem;

// The part of a setting of plugin/settings.toml the migration needs.
struct Declared {
    std::string type;
    std::string default_text;          // enum: the option's name
    std::int64_t default_int = 0;
    std::vector<std::string> options;  // enum
    std::int64_t min = INT64_MIN, max = INT64_MAX;
};

std::optional<Declared> FindDeclared(const toml::table& schema, std::string_view name) {
    const auto* setting = schema["setting"][name].as_table();
    if (setting == nullptr) return std::nullopt;
    Declared d;
    d.type = (*setting)["type"].value_or(std::string());
    if (d.type == "enum") {
        d.default_text = (*setting)["default"].value_or(std::string());
        if (const auto* values = (*setting)["values"].as_array()) {
            for (const auto& option : *values) {
                if (const auto* table = option.as_table()) d.options.push_back((*table)["name"].value_or(std::string()));
            }
        }
    } else if (d.type == "int") {
        d.default_int = (*setting)["default"].value_or(std::int64_t{0});
        d.min = (*setting)["min"].value_or(INT64_MIN);
        d.max = (*setting)["max"].value_or(INT64_MAX);
    }
    return d;
}

// An enum option's name ([a-z0-9_]) as a TOML string.
std::string Quoted(const std::string& text) { return "\"" + text + "\""; }

std::string Upper(std::string_view text) {
    std::string out(text);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// [modules] <name> = "<mode>"
void MigrateModule(const toml::table& schema, const std::string& name, const toml::node& node, Migration& m) {
    const auto declared = FindDeclared(schema, name);
    if (!declared || declared->type != "enum") {
        m.notes.push_back("config.toml: unknown module '" + name + "' left out");
        return;
    }
    const auto text = node.value<std::string>();
    const std::optional<Mode> mode = text ? ParseMode(*text) : std::nullopt;
    if (!mode) {
        m.notes.push_back("config.toml: invalid value for " + name + " left out (the default applies)");
        return;
    }
    std::string option = ModeName(*mode);
    if (std::find(declared->options.begin(), declared->options.end(), option) == declared->options.end()) {
        m.notes.push_back("config.toml: " + name + " has no " + option + " mode; migrated as on");
        option = "on";
    }
    if (option != declared->default_text) m.values.push_back({name, Quoted(option)});
}

// [ram_prefetch] budget_mb = <MB>
void MigrateBudget(const toml::table& schema, const toml::node& node, Migration& m) {
    constexpr const char* kName = "ram_prefetch_budget_mb";
    const auto declared = FindDeclared(schema, kName);
    const auto value = node.value<std::int64_t>();
    if (!declared || declared->type != "int" || !value || *value < declared->min || *value > declared->max) {
        m.notes.push_back("config.toml: invalid ram_prefetch.budget_mb left out (the default applies)");
        return;
    }
    if (*value != declared->default_int) m.values.push_back({kName, std::to_string(*value)});
}

}  // namespace

std::string ModeName(Mode mode) {
    switch (mode) {
    case Mode::Off: return "off";
    case Mode::On: return "on";
    case Mode::Verify: return "verify";
    }
    return "?";
}

std::optional<Mode> ParseMode(std::string_view text) {
    std::string lower(text);
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "on" || lower == "1" || lower == "true") return Mode::On;
    if (lower == "off" || lower == "0" || lower == "false") return Mode::Off;
    if (lower == "verify") return Mode::Verify;
    return std::nullopt;
}

Migration MigrateConfigText(std::string_view config, std::string_view schema_text) {
    Migration m;
    toml::table schema;
    try {
        schema = toml::parse(schema_text);
    } catch (const toml::parse_error& e) {
        m.notes.push_back("settings.toml of the plugin does not parse (" + std::string(e.description()) +
                          "); config.toml not migrated");
        return m;
    }
    toml::table table;
    try {
        table = toml::parse(config);
    } catch (const toml::parse_error& e) {
        m.notes.push_back("config.toml does not parse (" + std::string(e.description()) +
                          "); nothing migrated, the defaults apply");
        return m;
    }
    for (const auto& [key, node] : table) {
        const std::string section(key.str());
        if (section == "modules" && node.is_table()) {
            for (const auto& [name, value] : *node.as_table()) MigrateModule(schema, std::string(name.str()), value, m);
        } else if (section == "ram_prefetch" && node.is_table()) {
            for (const auto& [name, value] : *node.as_table()) {
                if (name.str() == "budget_mb") {
                    MigrateBudget(schema, value, m);
                } else {
                    m.notes.push_back("config.toml: unknown key ram_prefetch." + std::string(name.str()) + " left out");
                }
            }
        } else {
            m.notes.push_back("config.toml: unknown key '" + section + "' left out");
        }
    }
    return m;
}

std::string PlayerSettingsText(const std::vector<SettingValue>& values) {
    std::string out = "# Bellows settings that differ from the defaults (migrated from config.toml).\n";
    for (const auto& v : values) out += v.name + " = " + v.literal + "\n";
    return out;
}

Migration MigrateConfigFile(const fs::path& data_dir, const fs::path& schema_file) {
    Migration m;
    std::error_code ec;
    const fs::path config = data_dir / "config.toml";
    if (!fs::is_regular_file(config, ec)) return m;
    const fs::path settings = data_dir / "settings.toml";
    const fs::path kept = data_dir / "config.toml.migrated";
    if (fs::exists(settings, ec)) {
        m.notes.push_back("config.toml is no longer read and settings.toml exists already: config.toml not "
                          "migrated, kept as config.toml.migrated");
    } else {
        auto read = [](const fs::path& file, std::string& text) {
            std::ifstream in(file, std::ios::binary);
            if (!in) return false;
            std::ostringstream buffer;
            buffer << in.rdbuf();
            text = buffer.str();
            return true;
        };
        std::string config_text, schema_text;
        if (!read(config, config_text) || !read(schema_file, schema_text)) {
            m.notes.push_back("config.toml or the plugin's settings.toml could not be read; config.toml not migrated");
            return m;
        }
        m = MigrateConfigText(config_text, schema_text);
        if (!m.values.empty()) {
            const fs::path temp = data_dir / "settings.toml.tmp";
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                out << PlayerSettingsText(m.values);
                if (!out) {
                    m.notes.push_back("settings.toml could not be written; config.toml kept, its values apply to this "
                                      "start only");
                    return m;
                }
            }
            fs::rename(temp, settings, ec);
            if (ec) {
                fs::remove(temp, ec);
                m.notes.push_back("settings.toml could not be written; config.toml kept, its values apply to this "
                                  "start only");
                return m;
            }
        }
        m.notes.push_back("config.toml migrated to settings.toml (" + std::to_string(m.values.size()) +
                          " value(s) differ from the defaults); the old file is kept as config.toml.migrated");
    }
    fs::rename(config, kept, ec);  // replaces an older config.toml.migrated
    if (ec) m.notes.push_back("config.toml could not be renamed: " + ec.message());
    return m;
}

std::vector<std::string> FormerEnvironmentNotes() {
    std::vector<std::string> notes;
    wchar_t* block = GetEnvironmentStringsW();
    if (block == nullptr) return notes;
    for (const wchar_t* entry = block; *entry != L'\0'; entry += wcslen(entry) + 1) {
        if (_wcsnicmp(entry, L"EU4SB_", 6) != 0) continue;
        const wchar_t* equals = wcschr(entry, L'=');
        if (equals == nullptr) continue;
        std::string name;
        for (const wchar_t* c = entry + 6; c < equals; ++c) name.push_back(static_cast<char>(*c & 0x7F));
        notes.push_back("EU4SB_" + name + " is no longer read; use ANVIL_BELLOWS_" + Upper(name));
    }
    FreeEnvironmentStringsW(block);
    return notes;
}

}  // namespace sb
