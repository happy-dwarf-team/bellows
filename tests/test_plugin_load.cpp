// bellows.dll's anvil_plugin_load against a stand-in API 2 host: the settings
// it reads, the changes each module requests (none reaches a game: the image
// is this test's own executable and nothing is enabled), the per-module undo
// when Anvil refuses a change, the ready event and
// the start-up benchmark fields (a stand-in anvil.benchmark that checks every
// call as Anvil 1.4 does; "old" is an Anvil without the service), and the module switches (Anvil
// ADR 0011, 0013): in every scenario the core keeps open_warmup, ram_prefetch and metascript_subst off,
// and turns Diagnostics on with every profile but token_dict_profile and the check of attr_cache; in
// "modules" the player also turned exists_index, verifypath_session and Diagnostics off and left never_throttle
// off; in "nolisting" verifypath_listing is off, so listing_lookup is off too (not failed); in
// "checks0".."checks4" a different set of the features' checks is on, and each feature must run as "verify"
// exactly when its own check is. Each change records the handle that requested it: a feature's changes belong
// to its own module, a profile's to Diagnostics.
//   test_plugin_load <bellows.dll> <plugin folder> ok|refuse|old|modules|nolisting|checks0..checks4
#include <anvil/anvil_benchmark.h>
#include <anvil/anvil_game.h>
#include <anvil/anvil_plugin.h>

#include "settings.gen.h"
#include "symbols.gen.h"

#include <windows.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Change {
    std::string what;
    std::string owner;  // the module whose handle requested it; "bellows" for the plugin's own
    bool live = true;
};

std::mutex g_mutex;
std::vector<Change> g_changes;  // handle = index + 1
std::map<const void*, std::string> g_owners;  // module handles (and the plugin's own) -> module id
std::vector<std::string> g_log;
std::string g_scenario;
std::wstring g_pluginDir, g_dataDir;
AnvilEventCallback g_ready = nullptr;
void* g_readyContext = nullptr;
AnvilEventCallback g_session = nullptr;
void* g_sessionContext = nullptr;
AnvilEventCallback g_frame = nullptr;
void* g_frameContext = nullptr;
unsigned char g_symbolMemory[256] = {};  // what address_of returns for every symbol

void NeverCalled() { std::abort(); }  // what `next` and `previous` lead to: nothing runs the changes

bool Refused(const std::string& what) {
    return g_scenario == "refuse" && (what == "redirect_call " + std::to_string(SYM_TRIGGER_INTERN_CALL_B) ||
                                      what == "replace_import HeapAlloc" ||
                                      what == "hook_function " + std::to_string(SYM_COUNTRY_MODIFIERS));
}

AnvilResult Request(AnvilPlugin* self, const std::string& what, void** next, AnvilChange** out) {
    std::lock_guard lock(g_mutex);
    if (Refused(what)) return ANVIL_ERROR_CONFLICT;
    const auto owner = g_owners.find(self);
    g_changes.push_back({what, owner != g_owners.end() ? owner->second : "unknown"});
    if (next != nullptr) *next = reinterpret_cast<void*>(&NeverCalled);
    if (out != nullptr) *out = reinterpret_cast<AnvilChange*>(g_changes.size());
    return ANVIL_OK;
}

void Log(AnvilPlugin*, AnvilLogLevel, const char* message) {
    std::lock_guard lock(g_mutex);
    g_log.emplace_back(message);
    std::printf("log: %s\n", message);
}

const wchar_t* PluginDir(AnvilPlugin*) { return g_pluginDir.c_str(); }
const wchar_t* DataDir(AnvilPlugin*) { return g_dataDir.c_str(); }

AnvilResult HookFunction(AnvilPlugin* self, AnvilSymbol fn, void*, void** next, AnvilChange** out) {
    return Request(self, "hook_function " + std::to_string(fn), next, out);
}
AnvilResult MidHook(AnvilPlugin* self, AnvilSymbol site, AnvilCpuCallback, void*, AnvilChange** out) {
    return Request(self, "mid_hook " + std::to_string(site), nullptr, out);
}
AnvilResult ReplaceBlock(AnvilPlugin* self, AnvilSymbol block, AnvilCpuCallback, void*, AnvilChange** out) {
    return Request(self, "replace_block " + std::to_string(block), nullptr, out);
}
AnvilResult RedirectCall(AnvilPlugin* self, AnvilSymbol site, void*, void** next, AnvilChange** out) {
    return Request(self, "redirect_call " + std::to_string(site), next, out);
}
AnvilResult PatchBytes(AnvilPlugin* self, AnvilSymbol site, const void*, size_t, AnvilChange** out) {
    return Request(self, "patch_bytes " + std::to_string(site), nullptr, out);
}
AnvilResult SwapPointer(AnvilPlugin* self, AnvilSymbol slot, void*, void** previous, AnvilChange** out) {
    return Request(self, "swap_pointer " + std::to_string(slot), previous, out);
}
AnvilResult ReplaceImport(AnvilPlugin* self, const char* module, const char* function, void*, void** next,
                          AnvilChange** out) {
    assert(std::string(module) == "KERNEL32.dll" || std::string(module) == "pops_api.dll");
    return Request(self, std::string("replace_import ") + function, next, out);
}
AnvilResult Unsupported() { return ANVIL_ERROR_UNSUPPORTED; }
AnvilResult HookFunctionAt(AnvilPlugin*, void*, const void*, size_t, void*, void**, AnvilChange**) { return Unsupported(); }
AnvilResult MidHookAt(AnvilPlugin*, void*, const void*, size_t, AnvilCpuCallback, void*, AnvilChange**) {
    return Unsupported();
}
AnvilResult Remove(AnvilPlugin*, AnvilChange* change) {
    std::lock_guard lock(g_mutex);
    const auto index = reinterpret_cast<std::uintptr_t>(change);
    if (index == 0 || index > g_changes.size() || !g_changes[index - 1].live) return ANVIL_ERROR_NOT_FOUND;
    g_changes[index - 1].live = false;
    return ANVIL_OK;
}
AnvilResult WrongPhase(AnvilPlugin*) { return ANVIL_ERROR_WRONG_PHASE; }
void NoGroup(AnvilPlugin*) {}
void* AddressOf(AnvilPlugin*, AnvilSymbol) { return g_symbolMemory; }
AnvilResult SymbolValue(AnvilPlugin*, AnvilSymbol, const char*, uint64_t* out) {
    *out = 0x40;  // an RVA inside this executable's headers: readable
    return ANVIL_OK;
}
// Each feature's "Check: ..." setting, as plugin/settings.toml names it. In "checks<k>" the check at index i
// is on when bit k of i + 1 is set: over checks0..checks4 every check has its own pattern, so a Feature(...)
// of src/plugin.cpp that reads another module's check shows that module's pattern in some scenario.
const std::pair<const char*, AnvilSetting> kChecks[] = {
    {"ram_prefetch", CFG_RAM_PREFETCH_CHECK},
    {"verifypath_listing", CFG_VERIFYPATH_LISTING_CHECK},
    {"listing_lookup", CFG_LISTING_LOOKUP_CHECK},
    {"exists_index", CFG_EXISTS_INDEX_CHECK},
    {"trigger_intern", CFG_TRIGGER_INTERN_CHECK},
    {"metascript_subst", CFG_METASCRIPT_SUBST_CHECK},
    {"metascript_blocks", CFG_METASCRIPT_BLOCKS_CHECK},
    {"attr_cache", CFG_ATTR_CACHE_CHECK},
    {"token_dict_cache", CFG_TOKEN_DICT_CACHE_CHECK},
    {"loc_index", CFG_LOC_INDEX_CHECK},
    {"range_witness", CFG_RANGE_WITNESS_CHECK},
    {"pops_pacing", CFG_POPS_PACING_CHECK},
    {"modifier_merge", CFG_MODIFIER_MERGE_CHECK},
    {"port_table", CFG_PORT_TABLE_CHECK},
    {"province_modifier_view", CFG_PROVINCE_MODIFIER_VIEW_CHECK},
    {"army_search_memo", CFG_ARMY_SEARCH_MEMO_CHECK},
    {"conquer_scan", CFG_CONQUER_SCAN_CHECK},
    {"diplo_scan", CFG_DIPLO_SCAN_CHECK},
    {"save_compress", CFG_SAVE_COMPRESS_CHECK},
    {"transfer_skip", CFG_TRANSFER_SKIP_CHECK},
    {"history_add", CFG_HISTORY_ADD_CHECK},
    {"checksum_defer", CFG_CHECKSUM_DEFER_CHECK},
};
int ChecksBit() { return g_scenario.starts_with("checks") ? g_scenario.back() - '0' : -1; }
bool CheckOn(std::size_t index, int bit) { return (((index + 1) >> bit) & 1) != 0; }

int SettingBool(AnvilPlugin*, AnvilSetting setting) {
    if (const int bit = ChecksBit(); bit >= 0) {
        for (std::size_t i = 0; i < std::size(kChecks); ++i) {
            if (kChecks[i].second == setting) return CheckOn(i, bit) ? 1 : 0;
        }
    }
    switch (setting) {
    // token_dict_profile stays off: its ready report walks the (here fake) keyword tree.
    case CFG_ZIP_PROFILE:
    case CFG_RANGE_PROFILE:
    case CFG_TICK_PROFILE:
    case CFG_ARMY_SEARCH_PROFILE:
    case CFG_SCORE_PROBE:
    case CFG_MONTHLY_PROBE:
    case CFG_ATTR_CACHE_CHECK: return 1;
    default: return 0;
    }
}
// mimalloc_scope 0 is "menu" (Mode::Startup, also a benchmark field value); frame_pacing_input 0 is "draw".
int64_t SettingInt(AnvilPlugin*, AnvilSetting setting) { return setting == CFG_RAM_PREFETCH_BUDGET_MB ? 1024 : 0; }
double SettingFloat(AnvilPlugin*, AnvilSetting) { return 0.0; }
const char* SettingString(AnvilPlugin*, AnvilSetting) { return ""; }
AnvilResult SubscribeSetting(AnvilPlugin*, AnvilSetting, AnvilSettingCallback, void*) { return ANVIL_ERROR_WRONG_KIND; }
// --- the stand-in module() of Anvil 1.22 and later ----------------------------
std::vector<std::string> g_moduleCalls;
std::set<std::string> g_manifestModules;  // the ids of plugin.toml's [[modules]], as Anvil knows them
unsigned char g_moduleHandles[64] = {};  // a distinct address per module; the stand-in calls ignore `self`

// The `id` of each [[modules]] table of the built plugin's plugin.toml.
std::set<std::string> ManifestModules(const fs::path& manifest) {
    std::set<std::string> out;
    std::ifstream in(manifest);
    bool in_module = false;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("[")) in_module = line == "[[modules]]";
        if (in_module && line.starts_with("id = \"") && line.ends_with("\"")) out.insert(line.substr(6, line.size() - 7));
    }
    return out;
}

AnvilResult Module(AnvilPlugin*, const char* module, AnvilPlugin** out) {
    std::lock_guard lock(g_mutex);
    *out = nullptr;
    g_moduleCalls.push_back(module);
    const std::string id = module;
    // Anvil answers ANVIL_ERROR_NOT_DECLARED for an id plugin.toml does not declare: a typo in a
    // Feature("...") of src/plugin.cpp would leave that feature off at every start.
    if (!g_manifestModules.contains(id)) return ANVIL_ERROR_NOT_DECLARED;
    // open_warmup's and ram_prefetch's threads would read the game folder.
    if (id == "open_warmup" || id == "ram_prefetch" || id == "metascript_subst") return ANVIL_ERROR_DISABLED;
    if (g_scenario == "modules" && (id == "exists_index" || id == "never_throttle" || id == "diagnostics" ||
                                    id == "verifypath_session")) {
        return ANVIL_ERROR_DISABLED;
    }
    if (g_scenario == "nolisting" && id == "verifypath_listing") return ANVIL_ERROR_DISABLED;
    *out = reinterpret_cast<AnvilPlugin*>(&g_moduleHandles[g_moduleCalls.size() % sizeof(g_moduleHandles)]);
    g_owners[*out] = id;
    return ANVIL_OK;
}
AnvilResult RegisterService(AnvilPlugin*, const char*, uint32_t, const void*) { return ANVIL_OK; }
// --- the stand-in anvil.benchmark ---------------------------------------------
struct BenchField {
    uint32_t kind = 0;
    std::vector<std::string> options;
};
bool g_loading = false;  // inside anvil_plugin_load
std::map<std::string, BenchField> g_fields;
std::map<std::string, std::string> g_values;  // field -> value as text
std::vector<std::string> g_benchErrors;       // calls Anvil would refuse
int g_declares = 0, g_dones = 0;
std::string g_learning;
bool g_openCost = false;

// `group.field` or `field`, identifiers of [a-z0-9_], at most 64 characters.
bool BenchName(const char* name) {
    if (name == nullptr) return false;
    const std::string text = name;
    if (text.empty() || text.size() > ANVIL_BENCHMARK_MAX_NAME || text.front() == '.' || text.back() == '.') return false;
    int dots = 0;
    for (char c : text) {
        if (c == '.') {
            ++dots;
        } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return dots <= 1;
}

AnvilResult BenchRefuse(const std::string& why) {  // under g_mutex
    g_benchErrors.push_back(why);
    std::printf("benchmark refused: %s\n", why.c_str());
    return ANVIL_ERROR_INVALID_ARGUMENT;
}

int BenchActive() { return 1; }
AnvilResult BenchDeclare(AnvilPlugin*, const AnvilBenchmarkField* fields, uint32_t count) {
    std::lock_guard lock(g_mutex);
    ++g_declares;
    if (!g_loading) return BenchRefuse("declare outside anvil_plugin_load");
    if (g_fields.size() + count > ANVIL_BENCHMARK_MAX_FIELDS) return BenchRefuse("more than 128 fields");
    std::map<std::string, BenchField> added;  // all or none
    for (uint32_t i = 0; i < count; ++i) {
        const AnvilBenchmarkField& f = fields[i];
        const std::string name = f.name != nullptr ? f.name : "";
        if (!BenchName(f.name)) return BenchRefuse("bad name '" + name + "'");
        if (g_fields.contains(name) || added.contains(name)) return BenchRefuse("declared twice: " + name);
        if (f.kind < ANVIL_BENCHMARK_INT || f.kind > ANVIL_BENCHMARK_FLAG) return BenchRefuse("bad kind: " + name);
        if (f.unit > ANVIL_BENCHMARK_UNIT_PERCENT) return BenchRefuse("bad unit: " + name);
        if ((f.kind == ANVIL_BENCHMARK_ENUM || f.kind == ANVIL_BENCHMARK_FLAG) && f.unit != ANVIL_BENCHMARK_UNIT_NONE) {
            return BenchRefuse("a unit on an enum or a flag: " + name);
        }
        if (f.label != nullptr && std::strlen(f.label) > ANVIL_BENCHMARK_MAX_LABEL) return BenchRefuse("long label: " + name);
        BenchField field{f.kind, {}};
        if (f.kind == ANVIL_BENCHMARK_ENUM) {
            if (f.options == nullptr || f.option_count == 0 || f.option_count > ANVIL_BENCHMARK_MAX_OPTIONS) {
                return BenchRefuse("bad option list: " + name);
            }
            for (uint32_t k = 0; k < f.option_count; ++k) {
                if (!BenchName(f.options[k]) || std::strchr(f.options[k], '.') != nullptr) {
                    return BenchRefuse("bad option of " + name);
                }
                field.options.emplace_back(f.options[k]);
            }
        } else if (f.options != nullptr || f.option_count != 0) {
            return BenchRefuse("options on a field that is not an enum: " + name);
        }
        added.emplace(name, std::move(field));
    }
    g_fields.merge(added);
    return ANVIL_OK;
}
AnvilResult BenchSet(const char* field, uint32_t kind, const std::string& value) {
    std::lock_guard lock(g_mutex);
    const auto found = field != nullptr ? g_fields.find(field) : g_fields.end();
    if (found == g_fields.end()) return BenchRefuse(std::string("not declared: ") + (field ? field : "(null)"));
    if (found->second.kind != kind) return BenchRefuse(std::string("wrong kind: ") + field);
    if (g_dones != 0) return BenchRefuse(std::string("set after done(): ") + field);
    const auto& options = found->second.options;
    if (kind == ANVIL_BENCHMARK_ENUM && std::find(options.begin(), options.end(), value) == options.end()) {
        return BenchRefuse(std::string("not an option of ") + field + ": " + value);
    }
    g_values[field] = value;
    return ANVIL_OK;
}
AnvilResult BenchSetInt(AnvilPlugin*, const char* field, int64_t value) {
    return BenchSet(field, ANVIL_BENCHMARK_INT, std::to_string(value));
}
AnvilResult BenchSetNumber(AnvilPlugin*, const char* field, double value) {
    if (!std::isfinite(value)) {
        std::lock_guard lock(g_mutex);
        return BenchRefuse(std::string("not finite: ") + (field ? field : "(null)"));
    }
    return BenchSet(field, ANVIL_BENCHMARK_NUMBER, std::to_string(value));
}
AnvilResult BenchSetEnum(AnvilPlugin*, const char* field, const char* option) {
    return BenchSet(field, ANVIL_BENCHMARK_ENUM, option != nullptr ? option : "");
}
AnvilResult BenchSetFlag(AnvilPlugin*, const char* field, int value) {
    return BenchSet(field, ANVIL_BENCHMARK_FLAG, value ? "true" : "false");
}
AnvilResult BenchDone(AnvilPlugin*) {
    std::lock_guard lock(g_mutex);
    ++g_dones;
    return ANVIL_OK;
}
AnvilResult BenchLearning(AnvilPlugin*, const char* reason) {
    std::lock_guard lock(g_mutex);
    if (!BenchName(reason) || std::strchr(reason, '.') != nullptr) return BenchRefuse("bad learning reason");
    if (g_dones != 0) return BenchRefuse("learning() after done()");
    g_learning = reason;
    return ANVIL_OK;
}
AnvilResult BenchOpenCost(AnvilPlugin*, uint64_t, double ms) {
    std::lock_guard lock(g_mutex);
    if (!std::isfinite(ms) || ms < 0) return BenchRefuse("bad open cost");
    if (g_dones != 0) return BenchRefuse("open_cost() after done()");
    g_openCost = true;
    return ANVIL_OK;
}
const AnvilBenchmarkService g_benchmark = {sizeof(AnvilBenchmarkService), BenchActive, BenchDeclare,
                                           BenchSetInt, BenchSetNumber, BenchSetEnum, BenchSetFlag,
                                           BenchDone, BenchLearning, BenchOpenCost};

// An Anvil older than 1.4 (scenario "old") has no anvil.benchmark: get_service returns NULL.
const void* GetService(AnvilPlugin*, const char* name, uint32_t version) {
    if (g_scenario == "old" || std::string(name) != ANVIL_BENCHMARK_SERVICE || version != ANVIL_BENCHMARK_SERVICE_VERSION) {
        return nullptr;
    }
    return &g_benchmark;
}
AnvilResult Subscribe(AnvilPlugin*, const char* event, AnvilEventCallback callback, void* context) {
    if (std::string(event) == ANVIL_GAME_EVENT_SESSION) {
        g_session = callback;
        g_sessionContext = context;
        return ANVIL_OK;
    }
    if (std::string(event) == ANVIL_GAME_EVENT_FRAME) {
        g_frame = callback;
        g_frameContext = context;
        return ANVIL_OK;
    }
    assert(std::string(event) == ANVIL_GAME_EVENT_READY);
    g_ready = callback;
    g_readyContext = context;
    return ANVIL_OK;
}
AnvilResult Publish(AnvilPlugin*, const char*, const void*) { return ANVIL_OK; }

bool Logged(const std::string& text) {
    std::lock_guard lock(g_mutex);
    for (const auto& line : g_log) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

bool Live(const std::string& what) {
    for (const auto& c : g_changes) {
        if (c.live && c.what == what) return true;
    }
    return false;
}

// Whether a live change `what` was requested through the handle of `module` (two modules may chain one hook).
bool OwnedBy(const std::string& what, const std::string& module) {
    for (const auto& c : g_changes) {
        if (c.live && c.what == what && c.owner == module) return true;
    }
    std::printf("%s: no live change requested by %s\n", what.c_str(), module.c_str());
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    assert(argc == 4);
    g_scenario = argv[3];
    g_pluginDir = fs::absolute(argv[2]).wstring();
    g_manifestModules = ManifestModules(fs::absolute(argv[2]) / "plugin.toml");
    const fs::path data = fs::temp_directory_path() / ("sb-load-test-" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(data);
    fs::create_directories(data);
    g_dataDir = data.wstring();
    // A 0.7.0 file left in the data folder ("ok" only): 2.0 does not read it, but says so.
    if (g_scenario == "ok") std::ofstream(data / "config.toml", std::ios::binary) << "[modules]\nmetascript_subst = \"off\"\n";

    AnvilGameInfo game{};
    game.size = sizeof(game);
    game.build_id = "eu4-1.37.5-steam-win64";
    game.image_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    game.image_size = 0x1000;
    AnvilHost host{};
    host.size = sizeof(host);
    host.api_version = ANVIL_API_VERSION;
    host.anvil_version = "1.0.0";
    host.game = &game;
    host.log = Log;
    host.plugin_dir = PluginDir;
    host.data_dir = DataDir;
    host.hook_function = HookFunction;
    host.mid_hook = MidHook;
    host.replace_block = ReplaceBlock;
    host.redirect_call = RedirectCall;
    host.patch_bytes = PatchBytes;
    host.swap_pointer = SwapPointer;
    host.replace_import = ReplaceImport;
    host.hook_function_at = HookFunctionAt;
    host.mid_hook_at = MidHookAt;
    host.remove = Remove;
    host.begin_group = WrongPhase;
    host.commit_group = WrongPhase;
    host.abort_group = NoGroup;
    host.address_of = AddressOf;
    host.symbol_value = SymbolValue;
    host.setting_bool = SettingBool;
    host.setting_int = SettingInt;
    host.setting_float = SettingFloat;
    host.setting_string = SettingString;
    host.subscribe_setting = SubscribeSetting;
    host.register_service = RegisterService;
    host.get_service = GetService;
    host.subscribe = Subscribe;
    host.publish = Publish;
    host.module = Module;

    HMODULE dll = LoadLibraryW(fs::absolute(argv[1]).wstring().c_str());
    assert(dll != nullptr);
    const auto load = reinterpret_cast<AnvilResult (*)(const AnvilHost*, AnvilPlugin*)>(
        GetProcAddress(dll, "anvil_plugin_load"));
    assert(load != nullptr);
    g_owners[&host] = "bellows";
    g_loading = true;
    assert(load(&host, reinterpret_cast<AnvilPlugin*>(&host)) == ANVIL_OK);  // a failed module never fails the load
    g_loading = false;

    // config.toml is named in the log only when it is there, and left as it is.
    assert(Logged("anvil/data/bellows/config.toml (Bellows 0.7.0 and earlier) is no longer read") == (g_scenario == "ok"));
    assert(fs::exists(data / "config.toml") == (g_scenario == "ok") && !fs::exists(data / "settings.toml"));

    if (g_scenario == "nolisting") {
        // listing_lookup depends on verifypath_listing: with that module off it is off as well, not "FAILED",
        // in the log and in the benchmark's mode field, and its code did not run.
        assert(Logged("listing_lookup: needs verifypath_listing, which is off; module off"));
        std::string line;
        {
            std::lock_guard lock(g_mutex);
            for (const auto& l : g_log) {
                if (l.starts_with("modules: ")) line = l;
            }
        }
        assert(line.starts_with("modules: mimalloc=startup, open_warmup=off, ram_prefetch=off, verifypath_listing=off, "
                                "listing_lookup=off, exists_index=on"));
        // verifypath_session has nothing to keep answering without verifypath_listing, so it shows off too.
        assert(Logged("verifypath_session: needs verifypath_listing, which is off; module off"));
        assert(line.ends_with(", verifypath_session=off"));
        // listing_lookup's own blocks (realDir's stat, the directory archiver's openRead) were never requested.
        assert(!Live("replace_block " + std::to_string(SYM_REALDIR_STAT_CALL)));
        assert(!Live("replace_block " + std::to_string(SYM_DIR_OPENREAD_CALL)));
        assert(!Live("replace_block " + std::to_string(SYM_VERIFYPATH_STAT_CALL)));
        {
            std::lock_guard lock(g_mutex);
            assert(g_values.at("listing_lookup.mode") == "off" && g_values.at("verifypath_listing.mode") == "off");
        }
        std::printf("plugin load (%s) ok\n", g_scenario.c_str());
        std::fflush(stdout);
        fs::remove_all(data);
        TerminateProcess(GetCurrentProcess(), 0);
    }

    if (const int bit = ChecksBit(); bit >= 0) {
        // With Diagnostics on, each feature runs as "verify" exactly when its own check is on.
        std::map<std::string, std::string> logged;
        {
            std::lock_guard lock(g_mutex);
            for (const auto& line : g_log) {
                if (!line.starts_with("modules: ")) continue;
                std::size_t start = 9;
                while (start < line.size()) {
                    std::size_t end = line.find(", ", start);
                    if (end == std::string::npos) end = line.size();
                    const std::string entry = line.substr(start, end - start);
                    const std::size_t eq = entry.find('=');
                    assert(eq != std::string::npos);
                    logged[entry.substr(0, eq)] = entry.substr(eq + 1);
                    start = end + 2;
                }
            }
        }
        for (std::size_t i = 0; i < std::size(kChecks); ++i) {
            const std::string module = kChecks[i].first;
            // The core keeps these two off in every scenario.
            const bool off = module == "ram_prefetch" || module == "metascript_subst";
            const std::string expected = off ? "off" : CheckOn(i, bit) ? "verify" : "on";
            std::printf("%s=%s (expected %s)\n", module.c_str(), logged[module].c_str(), expected.c_str());
            assert(logged.at(module) == expected);
        }
        std::printf("plugin load (%s) ok\n", g_scenario.c_str());
        std::fflush(stdout);
        fs::remove_all(data);
        TerminateProcess(GetCurrentProcess(), 0);
    }

    const bool modules = g_scenario == "modules";
    // Diagnostics on: the profiles chosen in its settings run, and attr_cache's check makes it "verify".
    const std::string diag = modules ? "off" : "on";
    const std::string rest = ", metascript_subst=off, metascript_blocks=on, attr_cache=" + std::string(modules ? "on" : "verify") +
                             ", zip_profile=" + diag + ", token_dict_cache=on, token_dict_profile=off, loc_index=on, range_profile=" + diag +
                             ", range_witness=on, frame_pacing=on, pops_pacing=on, never_throttle=" + diag +
                             ", modifier_merge=on, port_table=on, province_modifier_view=on, army_search_memo=on, conquer_scan=on, diplo_scan=on, save_compress=on, "
                             "transfer_skip=on, history_add=on, checksum_defer=on, tick_profile=" + diag + ", army_search_profile=" + diag +
                             ", score_probe=" + diag + ", monthly_probe=" + (g_scenario == "refuse" ? "FAILED" : diag) +
                             ", verifypath_session=" + (modules ? "off" : "on");
    {
        // Every module of [[modules]] asked once, Diagnostics and verifypath_session included, none of the profiles.
        std::lock_guard lock(g_mutex);
        // The code asks for exactly plugin.toml's modules (28), each once.
        assert(g_manifestModules.size() == 28 && g_moduleCalls.size() == 28);
        const std::set<std::string> asked(g_moduleCalls.begin(), g_moduleCalls.end());
        assert(asked == g_manifestModules && asked.contains("mimalloc") && asked.contains("checksum_defer") &&
               asked.contains("diagnostics") && asked.contains("verifypath_session"));
        for (const char* d : {"zip_profile", "token_dict_profile", "range_profile", "tick_profile", "army_search_profile",
                              "score_probe", "monthly_probe"}) {
            assert(!asked.contains(d));
        }
    }
    if (modules) {
        // exists_index turned off, never_throttle not turned on, Diagnostics off: their code did not run.
        assert(Logged("modules: mimalloc=startup, open_warmup=off, ram_prefetch=off, verifypath_listing=on, "
                      "listing_lookup=on, exists_index=off, trigger_intern=on" + rest));
        assert(!Live("swap_pointer " + std::to_string(SYM_FS_EXISTS_SLOT)));
        assert(!Logged("diagnostics on"));
        // verifypath_session off: verifypath_listing stops answering at the ready event (no writer thread).
        assert(Live("replace_block " + std::to_string(SYM_VERIFYPATH_STAT_CALL)));
        assert(!Logged("verifypath_listing: also after the game is ready"));
        // The profiles' own hooks (the daily tick is also frame_pacing's and pops_pacing's).
        for (auto s : {SYM_ARMY_SEARCH, SYM_SCORE_MOVE, SYM_MONTHLY_TICK, SYM_COUNTRY_MONTH, SYM_RANGE_MIN_DISTANCE}) {
            assert(!Live("hook_function " + std::to_string(s)));
        }
        assert(!Live("swap_pointer " + std::to_string(SYM_ZIP_IO_READ_SLOT)));
    } else if (g_scenario != "refuse") {
        assert(Logged("diagnostics on"));
        assert(Logged("verifypath_listing: also after the game is ready"));
        assert(Logged("modules: mimalloc=startup, open_warmup=off, ram_prefetch=off, verifypath_listing=on, "
                      "listing_lookup=on, exists_index=on, trigger_intern=on" + rest));
        for (const char* f : {"HeapFree", "HeapReAlloc", "HeapSize", "HeapAlloc", "GetFileAttributesExW",
                              "GetFileAttributesA"}) {
            assert(Live(std::string("replace_import ") + f));
        }
        assert(Live("replace_block " + std::to_string(SYM_VERIFYPATH_STAT_CALL)));
        assert(Live("swap_pointer " + std::to_string(SYM_FS_EXISTS_SLOT)));
        assert(Live("swap_pointer " + std::to_string(SYM_ZIP_IO_READ_SLOT)));
        assert(Live("redirect_call " + std::to_string(SYM_TRIGGER_INTERN_CALL_A)));
        assert(Live("redirect_call " + std::to_string(SYM_TRIGGER_INTERN_CALL_B)));
        assert(Live("redirect_call " + std::to_string(SYM_TOKEN_LOOKUP_TOKENIZER_CALL)));
        assert(Live("redirect_call " + std::to_string(SYM_METASCRIPT_BLOCKS_CALL)));
        assert(Live("hook_function " + std::to_string(SYM_RANGE_MIN_DISTANCE)));
        assert(Live("redirect_call " + std::to_string(SYM_RANGE_CHECK_CALL)));
        assert(Live("mid_hook " + std::to_string(SYM_NESTED_FRAME_COMPARE)));
        assert(Live("mid_hook " + std::to_string(SYM_NESTED_FRAME_RENDER)));
        assert(Live("hook_function " + std::to_string(SYM_DAILY_TICK)));
        assert(Live("hook_function " + std::to_string(SYM_NESTED_FRAME_CHECK)));
        assert(Live("hook_function " + std::to_string(SYM_AI_JOB)));
        assert(Live("hook_function " + std::to_string(SYM_AI_COUNTRY)));
        assert(Live("hook_function " + std::to_string(SYM_ARMY_SEARCH)));
        assert(Live("hook_function " + std::to_string(SYM_SCORE_MOVE)));
        assert(Live("hook_function " + std::to_string(SYM_SCORE_STRENGTH)));
        assert(Live("hook_function " + std::to_string(SYM_SCORE_TABLE)));
        assert(Live("hook_function " + std::to_string(SYM_MONTHLY_TICK)));
        assert(Live("hook_function " + std::to_string(SYM_COUNTRY_MONTH)));
        assert(Live("hook_function " + std::to_string(SYM_PROVINCE_MONTH)));
        assert(Live("hook_function " + std::to_string(SYM_UNITS_MONTH)));
        assert(Live("hook_function " + std::to_string(SYM_PORT_TABLE)));
        assert(Live("hook_function " + std::to_string(SYM_MODIFIER_MERGE)));
        assert(Live("hook_function " + std::to_string(SYM_GAME_RANDOM)));
        assert(Live("hook_function " + std::to_string(SYM_COUNTRY_DAY)));
        assert(Live("hook_function " + std::to_string(SYM_COUNTRY_MODIFIERS)));
        // A feature's changes belong to its own module's handle, a profile's to Diagnostics'.
        assert(OwnedBy("replace_block " + std::to_string(SYM_VERIFYPATH_STAT_CALL), "verifypath_listing"));
        assert(OwnedBy("swap_pointer " + std::to_string(SYM_FS_EXISTS_SLOT), "exists_index"));
        assert(OwnedBy("redirect_call " + std::to_string(SYM_TRIGGER_INTERN_CALL_A), "trigger_intern"));
        assert(OwnedBy("redirect_call " + std::to_string(SYM_METASCRIPT_BLOCKS_CALL), "metascript_blocks"));
        assert(OwnedBy("hook_function " + std::to_string(SYM_PORT_TABLE), "port_table"));
        assert(OwnedBy("hook_function " + std::to_string(SYM_MODIFIER_MERGE), "modifier_merge"));
        assert(OwnedBy("swap_pointer " + std::to_string(SYM_ZIP_IO_READ_SLOT), "diagnostics"));
        for (auto s : {SYM_ARMY_SEARCH, SYM_SCORE_MOVE, SYM_MONTHLY_TICK, SYM_COUNTRY_MONTH, SYM_RANGE_MIN_DISTANCE}) {
            assert(OwnedBy("hook_function " + std::to_string(s), "diagnostics"));
        }
    } else {
        // HeapAlloc refused: the three heap imports mimalloc already had are removed. The
        // second trigger_intern site refused: the first one is removed. The rest stays.
        assert(Logged("modules: mimalloc=FAILED, open_warmup=off, ram_prefetch=off, verifypath_listing=on, "
                      "listing_lookup=on, exists_index=on, trigger_intern=FAILED" + rest));
        for (const char* f : {"HeapFree", "HeapReAlloc", "HeapSize", "HeapAlloc"}) {
            assert(!Live(std::string("replace_import ") + f));
        }
        assert(!Live("redirect_call " + std::to_string(SYM_TRIGGER_INTERN_CALL_A)));
        // monthly_probe's last hook refused: none of its hooks stays, the random draw's included (its
        // detour would call a null `next`).
        for (auto s : {SYM_GAME_RANDOM, SYM_MONTHLY_TICK, SYM_COUNTRY_MONTH, SYM_PROVINCE_MONTH, SYM_UNITS_MONTH,
                       SYM_COUNTRY_DAY, SYM_COUNTRY_DAY_ARMIES}) {
            assert(!Live("hook_function " + std::to_string(s)));
        }
        assert(Live("replace_import GetFileAttributesExW"));
        assert(Live("replace_block " + std::to_string(SYM_VERIFYPATH_STAT_CALL)));
    }

    // metascript_subst is off, so neither of its sites was requested.
    assert(!Live("redirect_call " + std::to_string(SYM_METASCRIPT_CALL_A)));

    // anvil-game's ready event: the modules stop and report on a thread of their own.
    assert(g_ready != nullptr);
    g_ready(ANVIL_GAME_EVENT_READY, nullptr, g_readyContext);
    for (int i = 0; i < 100 && !Logged("trigger_intern: map size"); ++i) Sleep(50);
    assert(Logged("trigger_intern: map size"));

    // mimalloc = "startup" returns freed pages at the ready event and at each session event.
    assert(g_session != nullptr);
    const AnvilGameSession started = {sizeof(AnvilGameSession), ANVIL_GAME_SESSION_STARTED, 0, 0};
    g_session(ANVIL_GAME_EVENT_SESSION, &started, g_sessionContext);
    const bool collects = g_scenario != "refuse";  // refuse: mimalloc failed
    assert(Logged("mimalloc: freed pages returned at ready") == collects);
    assert(Logged("mimalloc: freed pages returned at session start") == collects);
    // The frame handler (mimalloc = "startup" only) collects nothing while little was freed.
    assert(g_frame != nullptr);
    const AnvilGameFrame frame = {sizeof(AnvilGameFrame), 0, 1};
    g_frame(ANVIL_GAME_EVENT_FRAME, &frame, g_frameContext);
    assert(!Logged("mimalloc: freed pages returned at 64 MiB freed"));

    // The benchmark: fields declared in load(), every call one Anvil accepts, done() at the end of the report.
    if (g_scenario == "old") {
        assert(!Logged("benchmark"));  // Bellows runs as before; the log has nothing about the missing service
    } else {
        for (int i = 0; i < 100; ++i) {
            {
                std::lock_guard lock(g_mutex);
                if (g_dones != 0) break;
            }
            Sleep(50);
        }
        std::lock_guard lock(g_mutex);
        std::printf("benchmark: %zu fields declared, %zu set\n", g_fields.size(), g_values.size());
        assert(g_benchErrors.empty());
        assert(g_declares >= 1 && g_dones == 1);
        const bool ok = g_scenario == "ok" || g_scenario == "modules";
        std::map<std::string, std::string> modes = {
            {"mimalloc", ok ? "startup" : "failed"}, {"open_warmup", "off"}, {"ram_prefetch", "off"},
            {"verifypath_listing", "on"}, {"exists_index", "on"}, {"trigger_intern", ok ? "on" : "failed"},
            {"metascript_subst", "off"}, {"metascript_blocks", "on"}, {"attr_cache", modules ? "on" : "verify"},
            {"token_dict_cache", "on"}, {"loc_index", "on"},
            {"range_witness", "on"}, {"frame_pacing", "on"}, {"pops_pacing", "on"}, {"never_throttle", "on"}, {"modifier_merge", "on"}, {"port_table", "on"}, {"province_modifier_view", "on"}, {"army_search_memo", "on"}, {"save_compress", "on"}, {"transfer_skip", "on"}, {"history_add", "on"}, {"checksum_defer", "on"},
        };
        if (modules) modes["exists_index"] = modes["never_throttle"] = "off";
        for (const auto& [module, mode] : modes) assert(g_values.at(module + ".mode") == mode);
        assert(!g_fields.contains("tick_profile.mode"));  // the diagnostics have no mode field
        for (const char* field : {"exists_index.requests", "exists_index.listings", "verifypath_listing.mismatches",
                                  "attr_cache.verifications", "token_dict_cache.entries", "range_profile.checks",
                                  "range_witness.answered"}) {
            // exists_index and Diagnostics (range_profile) are off in "modules": their fields stay null.
            const std::string f = field;
            assert(g_values.contains(field) == !(modules && (f.starts_with("exists_index.") || f.starts_with("range_profile."))));
        }
        // open_warmup and ram_prefetch are off: no open cost, no profile being built, their fields stay null.
        assert(!g_openCost && g_learning.empty());
        assert(g_fields.contains("open_warmup.opened") && !g_values.contains("open_warmup.opened"));
        assert(!g_values.contains("metascript_subst.requests"));
    }

    std::printf("plugin load (%s) ok\n", g_scenario.c_str());
    std::fflush(stdout);
    fs::remove_all(data);
    // The plugin's writer threads never end; leave without running its static destructors.
    TerminateProcess(GetCurrentProcess(), 0);
}
