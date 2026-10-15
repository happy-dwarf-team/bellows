#pragma once

#include "common.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace sb {

struct ModuleSetting {
    const char* name;  // key in [modules] and suffix of the EU4SB_<NAME> override
    Mode mode;
    bool supports_verify;
};

inline constexpr std::uint64_t kDefaultRamPrefetchBudgetMb = 1024;
inline constexpr std::uint64_t kMinRamPrefetchBudgetMb = 64;
inline constexpr std::uint64_t kMaxRamPrefetchBudgetMb = 8192;

struct Config {
    std::vector<ModuleSetting> modules;
    std::vector<std::string> notes;  // problems found while reading (logged by the caller)
    std::uint64_t ram_prefetch_budget_mb = kDefaultRamPrefetchBudgetMb;  // [ram_prefetch] budget_mb

    Mode mode(const char* name) const;
};

// Defaults: every module "on". Reads <data_dir>/config.toml (writes a commented
// default file when missing), then applies EU4SB_<NAME>=on|off|verify and
// EU4SB_RAM_PREFETCH_BUDGET_MB.
Config LoadConfig(const std::filesystem::path& data_dir);

std::string ModeName(Mode mode);

}  // namespace sb
