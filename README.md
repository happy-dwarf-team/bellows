# Bellows

**Faster Europa Universalis IV start-up.** An
[Anvil](https://github.com/happy-dwarf-team/anvil) plugin that makes the game
start faster, without changing game files or game behaviour. Like a smith's
bellows, it gets the forge hot sooner.

Formerly **EU4 Startup Boost** (plugin id `eu4-startup-boost`, up to 0.5.0);
see [Updating from EU4 Startup Boost](#updating-from-eu4-startup-boost).

> Status: stage 2, early public releases — see
> [Releases](https://github.com/happy-dwarf-team/bellows/releases) and
> [CHANGELOG.md](CHANGELOG.md). The next release is a plugin for Anvil's
> plugin API 2 and requires **Anvil 1.0** (released together with it); 0.7.0
> and earlier need Anvil 0.2.
> Design: [docs/design.md](docs/design.md).

## Measured

EU4 1.37.5 (Steam, Windows 64-bit, 10 UI/graphics/music mods), warm start to
the main menu, original `eu4.exe` with Anvil, 3 interleaved starts each:

| Setup | Starts | Median |
| --- | --- | ---: |
| Anvil without plugins | 34.96 / 35.50 / 34.70 s | 34.96 s |
| Anvil + EU4 Startup Boost 0.1.0 | 15.65 / 15.38 / 15.40 s | **15.40 s** |

With `ram_prefetch` (0.2.0 work, separate interleaved series): 12.24 s median
(11.98 / 12.24 / 12.50) vs 15.16 s with every other module on and
`ram_prefetch` off; game logs identical.

The game's `setup.log`, `error.log` and `system.log` are identical with and
without the plugin. In a verification start every module compared each of its
answers with the game's own code: 0 differences over 3.6 million calls.

## Modules

| Module | What it does |
| --- | --- |
| `mimalloc` | Faster memory allocator for the game's C runtime (whole session) |
| `verifypath_listing` | Answers PhysFS path checks from directory listings |
| `exists_index` | Answers "does this file exist" for the modifier icon check |
| `trigger_intern` | Faster trigger interning at the end of loading (~0.4 s instead of ~0.6 s for the game's own code with the shipped trigger profile; a `config.toml` written by 0.3.0 or earlier said `trigger_intern = "off"`, and the migration keeps that: delete the line from `settings.toml` to use the default `"on"`) |
| `metascript_subst` | Single-pass `$parameter$` substitution in scripted effects |
| `open_warmup` | Opens the files the game will need ahead of it on parallel threads, from a profile learned per mod set (big gain on the first start after a reboot) |
| `ram_prefetch` | Reads and unpacks the DLC archive members the game will need into RAM on worker threads (learned per mod set; uses up to 1 GB of extra RAM during start-up, setting `ram_prefetch_budget_mb`) |
| `attr_cache` | Remembers "file not found" answers for game and mod files during start-up (off by default: ~0.1 s left next to the other modules) |
| `token_dict_cache` | Caches the script tokenizer's keyword lookups (~0.4 s) |
| `range_witness` | **Simulation, not start-up.** Answers the country range check from the province that was in range last time instead of walking all of the country's provinces again; same answers as the game's code (lab: a year at speed 5 in 20.6 s instead of 27.6 s). `"verify"` compares every answer with the game's |

Diagnostics, off by default: `zip_profile`, `token_dict_profile`,
`range_profile`.

Supported game build: EU4 1.37.5 (Steam, Windows 64-bit). Every game address
is declared in [`plugin/symbols.toml`](plugin/symbols.toml); Anvil checks
them against `eu4.exe` before it loads Bellows, and on another build (or any
mismatch) skips the plugin with the reason in `anvil/anvil.log` and the game
starts normally.

## Settings

Each module is a setting `on`, `off` or (where the module has it) `verify`,
plus `ram_prefetch_budget_mb` (64–8192); the defaults, labels and
descriptions are in [`plugin/settings.toml`](plugin/settings.toml). Your
values go into `anvil/data/bellows/settings.toml`, one line each, only those
that differ from the default, for example:

```toml
attr_cache = "on"
ram_prefetch_budget_mb = 2048
```

Every setting applies at the next start. For one start, an environment
variable overrides it: `ANVIL_BELLOWS_<NAME>`, e.g.
`ANVIL_BELLOWS_EXISTS_INDEX=verify` (Anvil logs each override). The plugin
writes its counters to `anvil/anvil.log` when the game is ready.

**From 0.7.0 or earlier:** on its first start Bellows moves your
`anvil/data/bellows/config.toml` to `settings.toml` (values that differ from
the defaults; `[ram_prefetch] budget_mb` becomes `ram_prefetch_budget_mb`)
and keeps the old file as `config.toml.migrated`; `anvil/anvil.log` says what
was moved or left out. The `EU4SB_<NAME>` variables are no longer read: use
`ANVIL_BELLOWS_<NAME>` (a set `EU4SB_` variable is named in the log).

## Install / uninstall

Install [Anvil](https://github.com/happy-dwarf-team/anvil#install--uninstall)
1.0 or newer first (it includes the `anvil-game` plugin Bellows needs). Then
download `bellows-windows-x64.zip` from the
[latest release](https://github.com/happy-dwarf-team/bellows/releases/latest)
and extract it into `<EU4>/anvil/plugins/`; it contains the `bellows`
folder (`SHA256SUMS.txt` next to the archive holds its checksum). Delete that
folder to uninstall.

### Updating from EU4 Startup Boost

Delete `<EU4>/anvil/plugins/eu4-startup-boost/` and extract Bellows as above.
On its first start Bellows moves your settings and learned profiles from
`anvil/data/eu4-startup-boost/` to `anvil/data/bellows/` (and the settings
from `config.toml` to `settings.toml`, see [Settings](#settings)). Anvil 1.0
does not load the old plugin (plugin API 1); Bellows mentions the leftover
folder in `anvil/anvil.log`. The exported `EU4SB_*` counters keep their
names.

## Building

Visual Studio 2022 (or Build Tools) with the C++ workload, CMake ≥ 3.28, Git
(fetches mimalloc 3.5.3, toml++ 3.4.0 and the Anvil SDK).

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --component plugin --prefix dist
```

The plugin folder as Anvil loads it is `build/plugin/bellows/`. The Anvil SDK
(the `AnvilPlugin` CMake package, its headers and `anvil.exe`) is the SDK
archive of the Anvil release `SB_ANVIL_VERSION` in `CMakeLists.txt` (1.0.0),
downloaded by the first configure and checked against its SHA-256. Use an
unpacked SDK archive, or one installed from a local Anvil checkout, with
`-DAnvilPlugin_DIR=<sdk>/cmake`. After changing
`plugin/symbols.toml`, see [CONTRIBUTING.md](CONTRIBUTING.md#game-addresses).

Contributions, the commit convention and the release process: see
[CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT — see [LICENSE](LICENSE). Bundles mimalloc (MIT, Microsoft). Unofficial
community project, not affiliated with Paradox Interactive.
