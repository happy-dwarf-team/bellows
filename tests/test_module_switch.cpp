// One switch per feature (Bellows 2.0, Anvil ADR 0013): what a module runs as, from the core's switch
// and the Diagnostics module; and plugin.toml's [[modules]] against plugin/settings.toml: every module in
// a group of the README's order, short names and descriptions, every setting a module's, the checks and
// profiles in Diagnostics, and each mode setting of 1.x retired into its module.
#include "module_switch.hpp"

#include <toml++/toml.hpp>

#include <cassert>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using sb::Mode;

namespace {

// Words of a name; a number ("speed 5") is not one.
std::size_t Words(const std::string& text) {
    std::size_t words = 0, start = 0;
    while (start < text.size()) {
        std::size_t end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        const std::string word = text.substr(start, end - start);
        if (!word.empty() && word.find_first_not_of("0123456789") != std::string::npos) ++words;
        start = end + 1;
    }
    return words;
}

std::set<std::string> Strings(const toml::node_view<const toml::node>& node) {
    std::set<std::string> out;
    if (const auto* array = node.as_array()) {
        for (const auto& value : *array) out.insert(value.value<std::string>().value());
    }
    return out;
}

}  // namespace

int main() {
    // The core's switch decides; a check needs Diagnostics.
    assert(sb::FeatureMode(false, true, true) == Mode::Off);
    assert(sb::FeatureMode(true, false, false) == Mode::On);
    assert(sb::FeatureMode(true, false, true) == Mode::On);  // a check without Diagnostics does nothing
    assert(sb::FeatureMode(true, true, false) == Mode::On);
    assert(sb::FeatureMode(true, true, true) == Mode::Verify);
    assert(sb::MimallocMode(false, true) == Mode::Off);
    assert(sb::MimallocMode(true, false) == Mode::Startup);
    assert(sb::MimallocMode(true, true) == Mode::On);
    assert(sb::ProfileMode(false, true) == Mode::Off);
    assert(sb::ProfileMode(true, false) == Mode::Off);
    assert(sb::ProfileMode(true, true) == Mode::On);

    const toml::table manifest = toml::parse_file(SB_PLUGIN_MANIFEST);
    const toml::table schema = toml::parse_file(SB_SETTINGS_SCHEMA);
    const toml::table& settings = *schema["setting"].as_table();
    const toml::table& retired = *schema["retired"].as_table();
    const toml::array& modules = *manifest["modules"].as_array();
    assert(manifest["affects_gameplay"].value<bool>() == false);

    const std::vector<std::string> order = {"Start-up", "Loading", "Simulation", "Saving", "For testing"};
    std::vector<std::string> groups;  // in the order of their first module
    std::map<std::string, bool> declared;  // id -> default
    std::map<std::string, std::string> names;
    for (const auto& node : modules) {
        const toml::table& m = *node.as_table();
        const std::string id = m["id"].value<std::string>().value();
        const std::string name = m["name"].value<std::string>().value();
        const std::string description = m["description"].value<std::string>().value();
        const std::string group = m["group"].value<std::string>().value();
        const bool on = m["default"].value_or(true);
        assert(declared.emplace(id, on).second);
        names[id] = name;
        if (groups.empty() || groups.back() != group) groups.push_back(group);
        assert(m["affects_gameplay"].value<bool>() == false);
        assert(!m.contains("allow_ironman") && !m.contains("allow_multiplayer"));
        // ui-guidelines §6: two to four words, one sentence of about 100 characters.
        if (Words(name) > 4 || description.size() > 110) std::printf("module %s: name or description too long\n", id.c_str());
        assert(Words(name) <= 4 && description.size() <= 110);
        assert(description.find("Simulation:") == std::string::npos && description.find("Verify") == std::string::npos);
    }
    assert(groups == order);  // each group once, in the README's order
    assert(declared.size() == 25 && declared.size() <= 32);  // Anvil's limit is 32 modules
    assert(declared.at("diagnostics") == false);

    // Every setting belongs to a declared module and repeats no switch (ADR 0013, point 5).
    std::set<std::string> checks;
    for (const auto& [key, node] : settings) {
        const toml::table& s = *node.as_table();
        const std::string name(key.str());
        const std::string module = s["module"].value<std::string>().value_or("");
        if (!declared.contains(module)) std::printf("setting %s: module '%s' not declared\n", name.c_str(), module.c_str());
        assert(declared.contains(module));
        assert(!declared.contains(name));
        assert(s["description"].value<std::string>().value().size() <= 110);
        if (module != "diagnostics") {
            assert(!s["advanced"].value_or(false));
            continue;
        }
        // Diagnostics: bools, off until chosen; the checks are advanced and name a feature module.
        assert(s["type"].value<std::string>() == "bool" && s["default"].value<bool>() == false);
        if (name.ends_with("_check")) {
            const std::string feature = name.substr(0, name.size() - 6);
            assert(declared.contains(feature) && feature != "diagnostics");
            assert(s["advanced"].value_or(false));
            assert(s["label"].value<std::string>() == "Check: " + names.at(feature));
            checks.insert(feature);
        } else {
            assert(!s["advanced"].value_or(false) && s["group"].value<std::string>() == "Profiles");
        }
    }
    assert(checks.size() == 19);  // the features that had a "verify" mode in 1.x

    // Each module but Diagnostics was a mode setting of 1.x (verifypath_session an on / off one): its
    // "off" turns the module off, and for a module off by default its "on" turns it on.
    for (const auto& [id, on] : declared) {
        if (id == "diagnostics") {
            assert(!retired.contains(id));
            continue;
        }
        const toml::table* r = retired[id].as_table();
        if (r == nullptr) std::printf("module %s: no [retired.%s]\n", id.c_str(), id.c_str());
        assert(r != nullptr);
        assert((*r)["module"].value<std::string>() == id);
        assert(Strings((*r)["off"]) == std::set<std::string>{"off"});
        assert(r->contains("on") == !on);
        if (!on) assert(Strings((*r)["on"]).contains("on"));
    }
    // Any other retired name is a renamed setting (save_compress_parallel): its entry names the module
    // and only removes the old value.
    for (const auto& [key, node] : retired) {
        assert(!settings.contains(key.str()));
        if (declared.contains(std::string(key.str()))) continue;
        const toml::table& r = *node.as_table();
        assert(declared.contains(r["module"].value_or(std::string())));
        assert(!r.contains("off") && !r.contains("on"));
    }

    std::printf("module_switch ok (%zu modules, %zu checks)\n", declared.size(), checks.size());
    return 0;
}
