// bellows.dll's anvil_plugin_load against a stand-in API 2 host: the settings
// it reads, the changes each module requests (none reaches a game: the image
// is this test's own executable and nothing is enabled), the per-module undo
// when Anvil refuses a change, the config.toml migration and the ready event.
//   test_plugin_load <bellows.dll> <plugin folder> ok|refuse
#include <anvil/anvil_game.h>
#include <anvil/anvil_plugin.h>

#include "settings.gen.h"
#include "symbols.gen.h"

#include <windows.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Change {
    std::string what;
    bool live = true;
};

std::mutex g_mutex;
std::vector<Change> g_changes;  // handle = index + 1
std::vector<std::string> g_log;
std::string g_scenario;
std::wstring g_pluginDir, g_dataDir;
AnvilEventCallback g_ready = nullptr;
void* g_readyContext = nullptr;
unsigned char g_symbolMemory[256] = {};  // what address_of returns for every symbol

void NeverCalled() { std::abort(); }  // what `next` and `previous` lead to: nothing runs the changes

bool Refused(const std::string& what) {
    return g_scenario == "refuse" && (what == "redirect_call " + std::to_string(SYM_TRIGGER_INTERN_CALL_B) ||
                                      what == "replace_import HeapAlloc");
}

AnvilResult Request(const std::string& what, void** next, AnvilChange** out) {
    std::lock_guard lock(g_mutex);
    if (Refused(what)) return ANVIL_ERROR_CONFLICT;
    g_changes.push_back({what});
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

AnvilResult HookFunction(AnvilPlugin*, AnvilSymbol fn, void*, void** next, AnvilChange** out) {
    return Request("hook_function " + std::to_string(fn), next, out);
}
AnvilResult MidHook(AnvilPlugin*, AnvilSymbol site, AnvilCpuCallback, void*, AnvilChange** out) {
    return Request("mid_hook " + std::to_string(site), nullptr, out);
}
AnvilResult ReplaceBlock(AnvilPlugin*, AnvilSymbol block, AnvilCpuCallback, void*, AnvilChange** out) {
    return Request("replace_block " + std::to_string(block), nullptr, out);
}
AnvilResult RedirectCall(AnvilPlugin*, AnvilSymbol site, void*, void** next, AnvilChange** out) {
    return Request("redirect_call " + std::to_string(site), next, out);
}
AnvilResult PatchBytes(AnvilPlugin*, AnvilSymbol site, const void*, size_t, AnvilChange** out) {
    return Request("patch_bytes " + std::to_string(site), nullptr, out);
}
AnvilResult SwapPointer(AnvilPlugin*, AnvilSymbol slot, void*, void** previous, AnvilChange** out) {
    return Request("swap_pointer " + std::to_string(slot), previous, out);
}
AnvilResult ReplaceImport(AnvilPlugin*, const char* module, const char* function, void*, void** next,
                          AnvilChange** out) {
    assert(std::string(module) == "KERNEL32.dll");
    return Request(std::string("replace_import ") + function, next, out);
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
int SettingBool(AnvilPlugin*, AnvilSetting) { return 0; }
int64_t SettingInt(AnvilPlugin*, AnvilSetting setting) { return setting == CFG_RAM_PREFETCH_BUDGET_MB ? 1024 : 0; }
double SettingFloat(AnvilPlugin*, AnvilSetting) { return 0.0; }
const char* SettingString(AnvilPlugin*, AnvilSetting setting) {
    switch (setting) {
    case CFG_OPEN_WARMUP:
    case CFG_RAM_PREFETCH: return "off";  // their threads would read the game folder
    case CFG_TOKEN_DICT_PROFILE: return "off";  // its ready report walks the (here fake) keyword tree
    case CFG_ATTR_CACHE: return "verify";
    default: return "on";
    }
}
AnvilResult SubscribeSetting(AnvilPlugin*, AnvilSetting, AnvilSettingCallback, void*) { return ANVIL_ERROR_WRONG_KIND; }
AnvilResult RegisterService(AnvilPlugin*, const char*, uint32_t, const void*) { return ANVIL_OK; }
const void* GetService(AnvilPlugin*, const char*, uint32_t) { return nullptr; }
AnvilResult Subscribe(AnvilPlugin*, const char* event, AnvilEventCallback callback, void* context) {
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

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    assert(argc == 4);
    g_scenario = argv[3];
    g_pluginDir = fs::absolute(argv[2]).wstring();
    const fs::path data = fs::temp_directory_path() / ("sb-load-test-" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(data);
    fs::create_directories(data);
    g_dataDir = data.wstring();
    std::ofstream(data / "config.toml", std::ios::binary) << "[modules]\nmetascript_subst = \"off\"\n";

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

    HMODULE dll = LoadLibraryW(fs::absolute(argv[1]).wstring().c_str());
    assert(dll != nullptr);
    const auto load = reinterpret_cast<AnvilResult (*)(const AnvilHost*, AnvilPlugin*)>(
        GetProcAddress(dll, "anvil_plugin_load"));
    assert(load != nullptr);
    assert(load(&host, reinterpret_cast<AnvilPlugin*>(&host)) == ANVIL_OK);  // a failed module never fails the load

    // config.toml was migrated, and its value applies to this start too.
    assert(fs::exists(data / "settings.toml") && fs::exists(data / "config.toml.migrated"));
    assert(Logged("config.toml migrated to settings.toml (1 value(s)"));

    const std::string rest = ", metascript_subst=off, attr_cache=verify, zip_profile=on, token_dict_cache=on, "
                             "token_dict_profile=off, range_profile=on, range_witness=on";
    if (g_scenario == "ok") {
        assert(Logged("modules: mimalloc=on, open_warmup=off, ram_prefetch=off, verifypath_listing=on, "
                      "exists_index=on, trigger_intern=on" + rest));
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
        assert(Live("hook_function " + std::to_string(SYM_RANGE_MIN_DISTANCE)));
        assert(Live("redirect_call " + std::to_string(SYM_RANGE_CHECK_CALL)));
    } else {
        // HeapAlloc refused: the three heap imports mimalloc already had are removed. The
        // second trigger_intern site refused: the first one is removed. The rest stays.
        assert(Logged("modules: mimalloc=FAILED, open_warmup=off, ram_prefetch=off, verifypath_listing=on, "
                      "exists_index=on, trigger_intern=FAILED" + rest));
        for (const char* f : {"HeapFree", "HeapReAlloc", "HeapSize", "HeapAlloc"}) {
            assert(!Live(std::string("replace_import ") + f));
        }
        assert(!Live("redirect_call " + std::to_string(SYM_TRIGGER_INTERN_CALL_A)));
        assert(Live("replace_import GetFileAttributesExW"));
        assert(Live("replace_block " + std::to_string(SYM_VERIFYPATH_STAT_CALL)));
    }

    // metascript_subst is off (migrated), so neither of its sites was requested.
    assert(!Live("redirect_call " + std::to_string(SYM_METASCRIPT_CALL_A)));

    // anvil-game's ready event: the modules stop and report on a thread of their own.
    assert(g_ready != nullptr);
    g_ready(ANVIL_GAME_EVENT_READY, nullptr, g_readyContext);
    for (int i = 0; i < 100 && !Logged("trigger_intern: map size"); ++i) Sleep(50);
    assert(Logged("trigger_intern: map size"));

    std::printf("plugin load (%s) ok\n", g_scenario.c_str());
    std::fflush(stdout);
    fs::remove_all(data);
    // The plugin's writer threads never end; leave without running its static destructors.
    TerminateProcess(GetCurrentProcess(), 0);
}
