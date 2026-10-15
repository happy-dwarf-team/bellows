// EU4 Startup Boost — Anvil plugin entry point.
#include "build.hpp"
#include "common.hpp"
#include "config.hpp"
#include "modules/modules.hpp"

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

    const sb::Config config = sb::LoadConfig(std::filesystem::path(host->data_dir(self)));
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
