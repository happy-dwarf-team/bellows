# EU4 Startup Boost

An [Anvil](https://github.com/happy-dwarf-team/anvil) plugin that makes
**Europa Universalis IV** start faster, without changing game files or game
behaviour.

> Status: 0.2.0 in progress (stage 2) — not released to players yet.
> Design: [docs/design.md](docs/design.md).

## Measured

EU4 1.37.5 (Steam, Windows 64-bit, 10 UI/graphics/music mods), warm start to
the main menu, original `eu4.exe` with Anvil, 3 interleaved starts each:

| Setup | Starts | Median |
| --- | --- | ---: |
| Anvil without plugins | 34.96 / 35.50 / 34.70 s | 34.96 s |
| Anvil + EU4 Startup Boost 0.1.0 | 15.65 / 15.38 / 15.40 s | **15.40 s** |

The game's `setup.log`, `error.log` and `system.log` are identical with and
without the plugin. In a verification start every module compared each of its
answers with the game's own code: 0 differences over 3.6 million calls.

## Modules

| Module | What it does |
| --- | --- |
| `mimalloc` | Faster memory allocator for the game's C runtime (whole session) |
| `verifypath_listing` | Answers PhysFS path checks from directory listings |
| `exists_index` | Answers "does this file exist" for the modifier icon check |
| `trigger_intern` | Alternative trigger interning (off by default: slower than the game's own code with the shipped trigger profile) |
| `metascript_subst` | Single-pass `$parameter$` substitution in scripted effects |
| `open_warmup` | Opens the files the game will need ahead of it on parallel threads, from a profile learned per mod set (big gain on the first start after a reboot) |
| `attr_cache` | Remembers "file not found" answers for game and mod files during start-up (off by default: ~0.1 s left next to the other modules) |

Settings: `anvil/data/eu4-startup-boost/config.toml` (created on first start;
each module `on`, `off` or `verify`). The plugin writes its counters to
`anvil/anvil.log` when the game is ready.

Supported game build: EU4 1.37.5 (Steam, Windows 64-bit). Other builds are
skipped automatically by Anvil and the game starts normally.

## Install (planned release layout)

Install Anvil, then copy the `eu4-startup-boost` folder into
`<EU4>/anvil/plugins/`. Remove the folder to uninstall.

## Building

Visual Studio 2022 (or Build Tools) with the C++ workload, CMake ≥ 3.25, Git
(fetches mimalloc 3.5.3 and toml++ 3.4.0).

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --component plugin --prefix dist
```

## License

MIT — see [LICENSE](LICENSE). Bundles mimalloc (MIT, Microsoft). Unofficial
community project, not affiliated with Paradox Interactive.
