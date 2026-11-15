#pragma once

// One switch per feature (Bellows 2.0, Anvil ADR 0013): the core turns each module of plugin.toml's
// [[modules]] on or off, and nothing in settings.toml repeats it. What a module that is on runs as
// comes from the Diagnostics module: with it on, a feature's "Check: ..." setting runs the game's own
// code as well (Mode::Verify, the "verify" mode of 1.x), and a profile runs when its setting is on.
// Plain data; plugin.cpp passes what the host answered, a test passes values.

#include "common.hpp"

namespace sb {

// A feature module: off unless the core enabled it; its check only with Diagnostics on.
constexpr Mode FeatureMode(bool enabled, bool diagnostics, bool check) noexcept {
    if (!enabled) return Mode::Off;
    return diagnostics && check ? Mode::Verify : Mode::On;
}

// mimalloc: mimalloc_scope chooses until the main menu (Mode::Startup) or the whole session.
constexpr Mode MimallocMode(bool enabled, bool whole_session) noexcept {
    if (!enabled) return Mode::Off;
    return whole_session ? Mode::On : Mode::Startup;
}

// A profile of the Diagnostics module.
constexpr Mode ProfileMode(bool diagnostics, bool chosen) noexcept {
    return diagnostics && chosen ? Mode::On : Mode::Off;
}

}  // namespace sb
