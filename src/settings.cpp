#include "settings.hpp"

#include <cctype>

namespace sb {

std::string ModeName(Mode mode) {
    switch (mode) {
    case Mode::Off: return "off";
    case Mode::On: return "on";
    case Mode::Verify: return "verify";
    case Mode::Startup: return "startup";
    }
    return "?";
}

std::optional<Mode> ParseMode(std::string_view text) {
    std::string lower(text);
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "on") return Mode::On;
    if (lower == "off") return Mode::Off;
    if (lower == "verify") return Mode::Verify;
    if (lower == "startup") return Mode::Startup;
    return std::nullopt;
}

}  // namespace sb
