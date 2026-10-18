// Bellows — Anvil plugin entry point.
#include "build.hpp"
#include "common.hpp"
#include "config.hpp"
#include "modules/modules.hpp"
#include "rename.hpp"

#include <filesystem>
#include <string>

namespace {

void OnGameReady(const char*, const void*, void*) {
    sb::FinishOpenWarmup();
    sb::FinishRamPrefetch();
    sb::ReportMimalloc();
    sb::ReportVerifyPathListing();
    sb::ReportExistsIndex();
    sb::ReportTriggerIntern();
    sb::ReportMetascriptSubst();
    sb::ReportOpenWarmup();
    sb::ReportAttrCache();
    sb::ReportRamPrefetch();
    sb::ReportZipProfile();
    sb::ReportTokenDictProfile();
    sb::ReportTokenDictCache();
    sb::ReportRangeProfile();
    sb::ReportRangeWitness();
}

}  // namespace

extern "C" ANVIL_PLUGIN_EXPORT AnvilResult anvil_plugin_load(const AnvilHost* host, AnvilPlugin* self) {
    if (host == nullptr || host->api_version != ANVIL_API_VERSION || host->size < sizeof(AnvilHost)) {
        return ANVIL_ERROR_UNSUPPORTED;
    }
    sb::Context& c = sb::context();
    c.host = host;
    c.self = self;
    const sb::Build* build = sb::FindBuild(host->game->build_id);
    if (build == nullptr) {
        sb::Log(ANVIL_LOG_WARNING, "game build %s is not supported; nothing changed",
                host->game->build_id ? host->game->build_id : "unknown");
        return ANVIL_ERROR_UNSUPPORTED;
    }
    c.image_base = host->game->image_base;
    c.image_size = host->game->image_size;
    c.ready_marker = sb::AtRva<const uint8_t>(build->ready_marker);

    if (sb::FormerPluginInstalled(std::filesystem::path(host->plugin_dir(self)))) {
        sb::Log(ANVIL_LOG_ERROR,
                "EU4 Startup Boost (the former name of Bellows) is still installed; nothing changed. "
                "Delete the anvil/plugins/eu4-startup-boost folder to use Bellows.");
        return ANVIL_ERROR_UNSUPPORTED;
    }
    const std::filesystem::path data_dir(host->data_dir(self));
    if (const std::string note = sb::MigrateFormerData(data_dir); !note.empty()) {
        sb::Log(ANVIL_LOG_INFO, "%s", note.c_str());
    }

    const sb::Config config = sb::LoadConfig(data_dir);
    sb::SetRamPrefetchBudget(config.ram_prefetch_budget_mb);
    for (const auto& note : config.notes) sb::Log(ANVIL_LOG_WARNING, "%s", note.c_str());

    struct Entry {
        const char* name;
        bool (*install)(const sb::Build&, sb::Mode);
    } const modules[] = {
        {"mimalloc", sb::InstallMimalloc},  // first: before the game's runtime allocates
        {"open_warmup", sb::InstallOpenWarmup},  // early: its threads must run ahead of the game
        {"ram_prefetch", sb::InstallRamPrefetch},  // early as well: workers decode ahead of the game
        {"verifypath_listing", sb::InstallVerifyPathListing},
        {"exists_index", sb::InstallExistsIndex},
        {"trigger_intern", sb::InstallTriggerIntern},
        {"metascript_subst", sb::InstallMetascriptSubst},
        {"attr_cache", sb::InstallAttrCache},
        {"zip_profile", sb::InstallZipProfile},  // diagnostic, off unless EU4SB_ZIP_PROFILE=on
        {"token_dict_cache", sb::InstallTokenDictCache},  // same call site as token_dict_profile: enable one of them
        {"token_dict_profile", sb::InstallTokenDictProfile},  // diagnostic, off unless EU4SB_TOKEN_DICT_PROFILE=on
        {"range_profile", sb::InstallRangeProfile},  // diagnostic, off unless EU4SB_RANGE_PROFILE=on
        {"range_witness", sb::InstallRangeWitness},  // hooks 0x3526B0 too: enable range_profile or range_witness, not both
    };
    std::string summary;
    for (const auto& m : modules) {
        const sb::Mode mode = config.mode(m.name);
        const bool active = m.install(*build, mode);
        summary += std::string(summary.empty() ? "" : ", ") + m.name + "=" +
                   (active ? sb::ModeName(mode) : (mode == sb::Mode::Off ? "off" : "FAILED"));
    }
    sb::Log(ANVIL_LOG_INFO, "modules: %s", summary.c_str());
    if (host->game->ready_signal_known) {
        host->subscribe(self, ANVIL_EVENT_GAME_READY, OnGameReady, nullptr);
    }
    return ANVIL_OK;  // a failed module only turns itself off
}
