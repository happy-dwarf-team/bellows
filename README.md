# Bellows

**Faster Europa Universalis IV start-up.** An
[Anvil](https://github.com/happy-dwarf-team/anvil) plugin that makes the game
start faster, without changing game files or game behaviour. Like a smith's
bellows, it gets the forge hot sooner.

Formerly **EU4 Startup Boost** (plugin id `eu4-startup-boost`, up to 0.5.0);
see [Updating from EU4 Startup Boost](#updating-from-eu4-startup-boost).

> Status: stage 2, early public releases — see
> [Releases](https://github.com/happy-dwarf-team/bellows/releases) and
> [CHANGELOG.md](CHANGELOG.md). Requires Anvil 0.2.0 or newer.
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
| `trigger_intern` | Faster trigger interning at the end of loading (~0.4 s instead of ~0.6 s for the game's own code with the shipped trigger profile; a `config.toml` written by 0.3.0 or earlier keeps `trigger_intern = "off"`, set it to `"on"` or delete the file) |
| `metascript_subst` | Single-pass `$parameter$` substitution in scripted effects |
| `open_warmup` | Opens the files the game will need ahead of it on parallel threads, from a profile learned per mod set (big gain on the first start after a reboot) |
| `ram_prefetch` | Reads and unpacks the DLC archive members the game will need into RAM on worker threads (learned per mod set; uses up to 1 GB of extra RAM during start-up, `[ram_prefetch] budget_mb`) |
| `attr_cache` | Remembers "file not found" answers for game and mod files during start-up (off by default: ~0.1 s left next to the other modules) |
| `token_dict_cache` | Caches the script tokenizer's keyword lookups (~0.4 s; on by default, also with an existing `config.toml`; add `token_dict_cache = "off"` under `[modules]` to disable) |
| `range_witness` | **Simulation, not start-up.** Answers the country range check from the province that was in range last time instead of walking all of the country's provinces again; same answers as the game's code (lab: a year at speed 5 in 20.6 s instead of 27.6 s). Off by default for now: add `range_witness = "on"` (or `"verify"` to compare every answer with the game's) under `[modules]` |

Settings: `anvil/data/bellows/config.toml` (created on first start;
each module `on`, `off` or `verify`). The plugin writes its counters to
`anvil/anvil.log` when the game is ready.

Supported game build: EU4 1.37.5 (Steam, Windows 64-bit). Other builds are
skipped automatically by Anvil and the game starts normally.

## Install / uninstall

Install [Anvil](https://github.com/happy-dwarf-team/anvil#install--uninstall)
0.2.0 or newer first. Then download `bellows-windows-x64.zip` from the
[latest release](https://github.com/happy-dwarf-team/bellows/releases/latest)
and extract it into `<EU4>/anvil/plugins/`; it contains the `bellows`
folder (`SHA256SUMS.txt` next to the archive holds its checksum). Delete that
folder to uninstall.

### Updating from EU4 Startup Boost

Delete `<EU4>/anvil/plugins/eu4-startup-boost/` and extract Bellows as above.
On its first start Bellows moves your settings and learned profiles from
`anvil/data/eu4-startup-boost/` to `anvil/data/bellows/`. While the old folder
is still in `anvil/plugins/`, Bellows changes nothing and says so in
`anvil/anvil.log`, so the two never patch the game at the same time. The
`EU4SB_<MODULE>` environment overrides and the exported `EU4SB_*` counters
keep their names.

## Building

Visual Studio 2022 (or Build Tools) with the C++ workload, CMake ≥ 3.25, Git
(fetches mimalloc 3.5.3 and toml++ 3.4.0).

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --component plugin --prefix dist
```

Contributions, the commit convention and the release process: see
[CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT — see [LICENSE](LICENSE). Bundles mimalloc (MIT, Microsoft). Unofficial
community project, not affiliated with Paradox Interactive.
