#include "config.hpp"

#include <toml++/toml.hpp>

#include <fstream>
#include <optional>

namespace sb {
namespace {

constexpr const char* kDefaultConfig = R"(# EU4 Startup Boost settings.
# Each module: "on" (default), "off", or "verify" (also run the game's own code
# and keep its result; slower, for checking correctness — not every module).

[modules]
mimalloc = "on"            # faster memory allocator for the whole game session
verifypath_listing = "on"  # answers PhysFS path checks from directory listings
exists_index = "on"        # answers "does this file exist" for icon checks
trigger_intern = "off"     # linear trigger interning; off: with the shipped trigger_profile.txt it is slower than the game's own code
metascript_subst = "on"    # faster $parameter$ substitution in scripted effects
open_warmup = "on"         # opens game files ahead of the game (big win after a reboot)
ram_prefetch = "on"        # reads and unpacks DLC archive members into RAM ahead of the game
attr_cache = "off"         # remembers "file not found" answers for game and mod files; off: saves at most ~0.1 s next to the other modules

[ram_prefetch]
budget_mb = 1024           # RAM for unpacked archive members the game has not taken yet (64-8192)
)";

std::optional<Mode> ParseMode(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (text == "on" || text == "1" || text == "true") return Mode::On;
    if (text == "off" || text == "0" || text == "false") return Mode::Off;
    if (text == "verify") return Mode::Verify;
    return std::nullopt;
}

bool ValidBudget(long long mb) {
    return mb >= static_cast<long long>(kMinRamPrefetchBudgetMb) && mb <= static_cast<long long>(kMaxRamPrefetchBudgetMb);
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

Mode Config::mode(const char* name) const {
    for (const auto& m : modules) {
        if (std::strcmp(m.name, name) == 0) return m.mode;
    }
    return Mode::Off;
}

Config LoadConfig(const std::filesystem::path& data_dir) {
    Config config;
    config.modules = {
        {"mimalloc", Mode::On, false},
        {"verifypath_listing", Mode::On, true},
        {"exists_index", Mode::On, true},
        {"trigger_intern", Mode::Off, true},  // measured slower than the original with a non-empty map
        {"metascript_subst", Mode::On, true},
        {"open_warmup", Mode::On, false},
        {"ram_prefetch", Mode::On, true},
        {"attr_cache", Mode::Off, true},  // at most ~0.1 s left once verifypath_listing and exists_index are on
        {"zip_profile", Mode::Off, false},  // diagnostic; not in the default config.toml
    };
    const auto file = data_dir / "config.toml";
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        std::ofstream out(file, std::ios::binary);
        out << kDefaultConfig;
    } else {
        try {
            const toml::table table = toml::parse_file(file.wstring());
            if (const auto* modules = table.get_as<toml::table>("modules")) {
                for (auto& m : config.modules) {
                    if (const auto value = (*modules)[m.name].value<std::string>()) {
                        if (const auto parsed = ParseMode(*value)) {
                            m.mode = *parsed;
                        } else {
                            config.notes.push_back(std::string("config.toml: invalid value for ") + m.name);
                        }
                    }
                }
            }
            if (const auto budget = table["ram_prefetch"]["budget_mb"]) {
                const auto value = budget.value<std::int64_t>();
                if (value && ValidBudget(*value)) {
                    config.ram_prefetch_budget_mb = static_cast<std::uint64_t>(*value);
                } else {
                    config.notes.push_back("config.toml: invalid ram_prefetch.budget_mb (64-8192)");
                }
            }
        } catch (const toml::parse_error& e) {
            config.notes.push_back(std::string("config.toml ignored: ") + std::string(e.description()));
        }
    }
    for (auto& m : config.modules) {
        std::string variable = "EU4SB_";
        for (const char* p = m.name; *p; ++p) variable += static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
        char value[16] = {};
        const DWORD length = GetEnvironmentVariableA(variable.c_str(), value, sizeof(value));
        if (length > 0 && length < sizeof(value)) {
            if (const auto parsed = ParseMode(value)) {
                m.mode = *parsed;
                config.notes.push_back(variable + "=" + value + " overrides config.toml");
            }
        }
        if (m.mode == Mode::Verify && !m.supports_verify) {
            m.mode = Mode::On;
            config.notes.push_back(std::string(m.name) + " has no verify mode; using on");
        }
    }
    char budget[16] = {};
    const DWORD budgetLength = GetEnvironmentVariableA("EU4SB_RAM_PREFETCH_BUDGET_MB", budget, sizeof(budget));
    if (budgetLength > 0 && budgetLength < sizeof(budget)) {
        char* end = nullptr;
        const long long value = std::strtoll(budget, &end, 10);
        if (end != budget && *end == '\0' && ValidBudget(value)) {
            config.ram_prefetch_budget_mb = static_cast<std::uint64_t>(value);
            config.notes.push_back(std::string("EU4SB_RAM_PREFETCH_BUDGET_MB=") + budget + " overrides config.toml");
        }
    }
    return config;
}

}  // namespace sb
