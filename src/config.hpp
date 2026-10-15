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

struct Config {
    std::vector<ModuleSetting> modules;
    std::vector<std::string> notes;  // problems found while reading (logged by the caller)

    Mode mode(const char* name) const;
};

// Defaults: every module "on". Reads <data_dir>/config.toml (writes a commented
// default file when missing), then applies EU4SB_<NAME>=on|off|verify.
Config LoadConfig(const std::filesystem::path& data_dir);

std::string ModeName(Mode mode);

}  // namespace sb
