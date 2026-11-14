#pragma once

// The names of the modules' modes, as plugin/settings.toml and the log write them.

#include "common.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace sb {

std::string ModeName(Mode mode);

// "on", "off", "verify", "startup" (any case).
std::optional<Mode> ParseMode(std::string_view text);

}  // namespace sb
