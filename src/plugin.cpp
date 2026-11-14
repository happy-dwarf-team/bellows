// Bellows: the Anvil plugin (API 2, C++ wrapper anvil.hpp).
#include "benchmark.hpp"
#include "common.hpp"
#include "module_switch.hpp"
#include "modules/modules.hpp"
#include "settings.hpp"

#include <anvil/anvil_game.h>

#include "settings.gen.hpp"  // cfg::<name>, written by anvil gen from plugin/settings.toml

#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

// A step that fails only stops that step. Nothing above FinishAfterReady catches, and an exception
// out of a thread ends the game; in load() it would cost the whole plugin for a note in the log.
template <class Step>
void Guarded(const char* name, Step&& step) noexcept {
    try {
        step();
    } catch (const std::exception& e) {
        sb::Log(ANVIL_LOG_WARNING, "%s: %s", name, e.what());
    } catch (...) {
        sb::Log(ANVIL_LOG_WARNING, "%s: failed", name);
    }
}

DWORD WINAPI FinishAfterReady(void*) {
    struct Step {
        const char* name;
        void (*run)();
    };
    static constexpr Step steps[] = {
        {"open_warmup finish", sb::FinishOpenWarmup},
        {"ram_prefetch finish", sb::FinishRamPrefetch},
        {"mimalloc report", sb::ReportMimalloc},
        {"verifypath_listing report", sb::ReportVerifyPathListing},
        {"listing_lookup report", sb::ReportListingLookup},
        {"exists_index report", sb::ReportExistsIndex},
        {"trigger_intern report", sb::ReportTriggerIntern},
        {"metascript_subst report", sb::ReportMetascriptSubst},
        {"metascript_blocks report", sb::ReportMetascriptBlocks},
        {"open_warmup report", sb::ReportOpenWarmup},
        {"attr_cache report", sb::ReportAttrCache},
        {"ram_prefetch report", sb::ReportRamPrefetch},
        {"zip_profile report", sb::ReportZipProfile},
        {"token_dict_profile report", sb::ReportTokenDictProfile},
        {"token_dict_cache report", sb::ReportTokenDictCache},
        {"range_profile report", sb::ReportRangeProfile},
        {"range_witness report", sb::ReportRangeWitness},
        {"frame_pacing report", sb::ReportFramePacing},
        {"pops_pacing report", sb::ReportPopsPacing},
        {"never_throttle report", sb::ReportNeverThrottle},
        {"modifier_merge report", sb::ReportModifierMerge},
        {"port_table report", sb::ReportPortTable},
        {"province_modifier_view report", sb::ReportProvinceModifierView},
        {"army_search_memo report", sb::ReportArmySearchMemo},
        {"save_compress report", sb::ReportSaveCompress},
        {"transfer_skip report", sb::ReportTransferSkip},
        {"history_add report", sb::ReportHistoryAdd},
        {"benchmark report", sb::bench::Done},  // the profiles are saved and the reports made
    };
    for (const Step& step : steps) Guarded(step.name, step.run);
    return 0;
}

// anvil-game publishes its ready event on the game's main thread. The modules
// stop at once; saving the profiles waits for the workers (up to seconds), so
// it runs on a thread of its own, as it did on Anvil's watcher thread before.
void OnGameReady() {
    if (InterlockedExchange(&sb::g_gameReady, 1) != 0) return;
    sb::CollectMimalloc("ready");  // on the main thread: mimalloc collects through a thread it knows
    HANDLE thread = CreateThread(nullptr, 0, &FinishAfterReady, nullptr, 0, nullptr);
    if (thread != nullptr) {
        SetThreadDescription(thread, L"bellows ready report");
        CloseHandle(thread);
    } else {
        FinishAfterReady(nullptr);
    }
}

// One entry per feature module of plugin.toml's [[modules]] and per profile of the Diagnostics module.
struct Module {
    const char* name;  // the module's id in plugin.toml; a profile's setting in plugin/settings.toml
    bool (*install)(anvil::Host&, sb::Mode);
    const anvil::Setting<bool>* choice = nullptr;  // a feature's "Check: ..." or a profile's setting (Diagnostics)
    bool profile = false;               // a setting of the Diagnostics module, not a module of its own
    sb::Mode mode = sb::Mode::Off;
    std::optional<anvil::Host> handle;  // the module's host; for a profile, Diagnostics'
};

Module Feature(const char* name, bool (*install)(anvil::Host&, sb::Mode), const anvil::Setting<bool>* check = nullptr) {
    return {name, install, check};
}
Module Profile(const char* name, bool (*install)(anvil::Host&, sb::Mode), const anvil::Setting<bool>& setting) {
    return {name, install, &setting, true};
}

// The handle of a module of [[modules]], or none when the core keeps it off. anvil = ">=1.23.0" means
// the host always has modules; an error (a plugin.toml out of step with plugin.cpp, which
// tests/test_module_switch.cpp checks) keeps the module off.
std::optional<anvil::Host> ModuleHandle(anvil::Host& host, const char* name) {
    try {
        return host.module(name);
    } catch (const anvil::Error& e) {
        sb::Log(ANVIL_LOG_WARNING, "%s: %s; module off", name, e.what());
        return std::nullopt;
    }
}

struct Bellows : anvil::Plugin {
    void load(anvil::Host& host) override {
        Module modules[] = {
            Feature("mimalloc", sb::InstallMimalloc),  // first: before the game's runtime allocates
            Feature("open_warmup", sb::InstallOpenWarmup),  // early: its threads must run ahead of the game
            Feature("ram_prefetch", sb::InstallRamPrefetch, &cfg::ram_prefetch_check),  // early as well
            Feature("verifypath_listing", sb::InstallVerifyPathListing, &cfg::verifypath_listing_check),
            Feature("listing_lookup", sb::InstallListingLookup, &cfg::listing_lookup_check),  // needs verifypath_listing
            Feature("exists_index", sb::InstallExistsIndex, &cfg::exists_index_check),
            Feature("trigger_intern", sb::InstallTriggerIntern, &cfg::trigger_intern_check),
            Feature("metascript_subst", sb::InstallMetascriptSubst, &cfg::metascript_subst_check),
            Feature("metascript_blocks", sb::InstallMetascriptBlocks, &cfg::metascript_blocks_check),
            Feature("attr_cache", sb::InstallAttrCache, &cfg::attr_cache_check),
            Profile("zip_profile", sb::InstallZipProfile, cfg::zip_profile),
            // token_dict_cache and token_dict_profile redirect the same call: both chain, the profile then counts
            // only the cache's misses.
            Feature("token_dict_cache", sb::InstallTokenDictCache, &cfg::token_dict_cache_check),
            Profile("token_dict_profile", sb::InstallTokenDictProfile, cfg::token_dict_profile),
            Profile("range_profile", sb::InstallRangeProfile, cfg::range_profile),
            // range_witness answers before 0x3526B0 runs: range_profile then sees only the walks it still makes.
            Feature("range_witness", sb::InstallRangeWitness, &cfg::range_witness_check),
            Feature("frame_pacing", sb::InstallFramePacing),
            Feature("pops_pacing", sb::InstallPopsPacing, &cfg::pops_pacing_check),
            Feature("never_throttle", sb::InstallNeverThrottle),
            Feature("modifier_merge", sb::InstallModifierMerge, &cfg::modifier_merge_check),
            // hooks the entry of 0x350B80; monthly_probe's hook on it chains.
            Feature("port_table", sb::InstallPortTable, &cfg::port_table_check),
            Feature("province_modifier_view", sb::InstallProvinceModifierView, &cfg::province_modifier_view_check),
            // redirects the call of 0x1022B0 in 0xEAB50; army_search_profile hooks its entry, so both chain.
            Feature("army_search_memo", sb::InstallArmySearchMemo, &cfg::army_search_memo_check),
            Feature("save_compress", sb::InstallSaveCompress, &cfg::save_compress_check),
            Feature("transfer_skip", sb::InstallTransferSkip, &cfg::transfer_skip_check),
            Feature("history_add", sb::InstallHistoryAdd, &cfg::history_add_check),
            Feature("checksum_defer", sb::InstallChecksumDefer, &cfg::checksum_defer_check),
            // its hook on 0x769EA0 sits at the entry, frame_pacing's inside the function.
            Profile("tick_profile", sb::InstallTickProfile, cfg::tick_profile),
            Profile("army_search_profile", sb::InstallArmySearchProfile, cfg::army_search_profile),
            Profile("score_probe", sb::InstallScoreProbe, cfg::score_probe),
            Profile("monthly_probe", sb::InstallMonthlyProbe, cfg::monthly_probe),
        };
        std::vector<const char*> names;
        for (const auto& m : modules) names.push_back(m.name);
        Guarded("benchmark", [&] { sb::bench::Join(host, names); });  // the Benchmark tab, not the modules

        // What the core turned on (anvil.toml, the modules' `default`), and the values Anvil validated
        // (defaults, the player's file, ANVIL_BELLOWS_* overrides).
        const std::optional<anvil::Host> diagnostics = ModuleHandle(host, "diagnostics");
        std::string turned_off;
        for (auto& m : modules) {
            const bool chosen = m.choice != nullptr && m.choice->get();
            if (m.profile) {
                m.handle = diagnostics;
                m.mode = sb::ProfileMode(diagnostics.has_value(), chosen);
                continue;
            }
            m.handle = ModuleHandle(host, m.name);
            m.mode = &m == &modules[0]
                         ? sb::MimallocMode(m.handle.has_value(), cfg::mimalloc_scope.get() == cfg::mimalloc_scope.session)
                         : sb::FeatureMode(m.handle.has_value(), diagnostics.has_value(), chosen);
            if (!m.handle) turned_off += std::string(turned_off.empty() ? "" : ", ") + m.name;
        }
        if (!turned_off.empty()) sb::Log(ANVIL_LOG_INFO, "modules off: %s", turned_off.c_str());
        if (diagnostics) sb::Log(ANVIL_LOG_INFO, "diagnostics on: the checks and profiles chosen in its settings run");
        sb::SetRamPrefetchBudget(static_cast<std::uint64_t>(cfg::ram_prefetch_budget_mb.get()));
        sb::SetFramePacingInterval(cfg::frame_pacing_interval_ms.get(),
                                   cfg::frame_pacing_input.get() == cfg::frame_pacing_input.draw);
        // restart = false: a change in the overlay applies at the next check of 0x769EA0.
        Guarded("frame_pacing settings", [&] {
            const auto apply = [] {
                sb::ChangeFramePacing(cfg::frame_pacing_interval_ms.get(),
                                      cfg::frame_pacing_input.get() == cfg::frame_pacing_input.draw);
            };
            host.subscribe_setting(cfg::frame_pacing_interval_ms, apply);
            host.subscribe_setting(cfg::frame_pacing_input, apply);
        });
        // A module without code of its own: verifypath_listing's hooks keep answering after the main menu.
        sb::SetVerifyPathSession(ModuleHandle(host, "verifypath_session").has_value());
        // On several threads; save_compress's check decodes each member again before writing it.
        sb::SetSaveCompressParallel(!cfg::save_compress_threads.get() ? 0
                                    : diagnostics && cfg::save_compress_check.get() ? 2
                                                                                    : 1);

        std::string summary;
        for (auto& m : modules) {
            bool active = false;
            try {
                // A module's changes belong to its handle; a profile's to the Diagnostics module.
                if (m.mode != sb::Mode::Off) active = m.install(*m.handle, m.mode);
            } catch (const std::exception& e) {
                // Mostly anvil::Error: Anvil refused a change (and logged why). The module's
                // sb::Undo removed its other changes; the other modules stay.
                sb::Log(ANVIL_LOG_WARNING, "%s: %s; module off", m.name, e.what());
            }
            const std::string mode = active ? sb::ModeName(m.mode) : (m.mode == sb::Mode::Off ? "off" : "failed");
            summary += std::string(summary.empty() ? "" : ", ") + m.name + "=" + (mode == "failed" ? "FAILED" : mode);
            sb::bench::SetMode(m.name, mode.c_str());
        }
        sb::Log(ANVIL_LOG_INFO, "modules: %s", summary.c_str());

        // The plugin.toml dependency makes anvil-game load first (both are early).
        host.subscribe(ANVIL_GAME_EVENT_READY, [] { OnGameReady(); });
        // A save loaded or left frees most of what mimalloc still holds in "startup" mode.
        host.subscribe(ANVIL_GAME_EVENT_SESSION, [](const void* payload) {
            const auto* session = static_cast<const AnvilGameSession*>(payload);
            const bool ended = session != nullptr && session->change == ANVIL_GAME_SESSION_ENDED;
            sb::CollectMimalloc(ended ? "session end" : "session start");
            sb::NotePopsSession(session != nullptr && !ended && session->multiplayer != 0);
            sb::NoteNeverThrottleSession(session != nullptr && !ended && session->multiplayer != 0);
        });
        // ... and so does a save loaded from the in-game menu, which publishes no session event in
        // anvil-game 1.5: a frame collects once enough was freed (a counter read otherwise).
        if (modules[0].mode == sb::Mode::Startup) {
            host.subscribe(ANVIL_GAME_EVENT_FRAME, [] { sb::CollectMimallocIfDue(); });
        }
        // A failed module only turns itself off: load() returns normally.
    }
};

}  // namespace

ANVIL_PLUGIN(Bellows)
