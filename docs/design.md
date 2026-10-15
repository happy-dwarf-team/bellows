# EU4 Startup Boost — design

Status: draft 2 (2026-10-15). Decisions marked **D** are agreed; **Q** are open.
Background, measurements and reverse-engineering notes come from the private
research repository (`eu4-engine-lab`); this repository contains production
code only.

## 1. Goals

- Port the start-up optimizations proven in the lab to an Anvil plugin
  (`phase = "early"`, `affects_gameplay = false`).
- Same effect as the lab build: game state, logs and loaded content identical
  to vanilla; only time changes.
- No game file is modified; all changes are in-memory patches through the
  Anvil host API, each verified against expected bytes before applying.
- Every module can be switched off individually; any failure turns that
  module off and the game continues.

## 2. Modules (D)

| Module | What it does | Lab result |
| --- | --- | --- |
| `ram_prefetch` | Reads the files the game will read (archives and loose files from a manifest) into RAM on worker threads; serves the game's zip/file reads from memory | 31 → ~24.8 s |
| `attr_cache` | Negative cache of `GetFileAttributesExW`/`A` for not-found paths during start-up (existing files always hit the real API) | part of the above |
| `exists_index` | Answers the modifier-picture verifier's "file exists" queries from directory listings | 24.6 → 19.6 s |
| `verifypath_listing` | Answers PhysFS `verifyPath` stats from directory listings | → 13.3 s |
| `mimalloc` | Replaces the game's CRT heap with mimalloc for the whole process | 13.8 → 11.8 s |
| `trigger_intern` | Skips redundant trigger-interning work at the end of loading (empty map), linear algorithm otherwise | tail −0.8 s |
| `metascript_subst` | Single-pass `$param$` substitution in scripted effects/triggers | −0.75 s |
| `open_warmup` | Opens the files the game will need on parallel threads (learned list per mod set) so first-open costs after a reboot or antivirus update are paid off the critical path | cold start 61 → 15 s |

Diagnostics from the lab (`file_trace`, deep API counters, heap sampling) are
not shipped in release builds; they may exist behind a build flag.

## 3. Changes needed when moving from the lab (D)

- The lab build patched a copy of `eu4.exe` (added import, miniz call sites)
  and ran it from a folder of junctions. In the plugin every executable change
  becomes a runtime patch through `AnvilHost` (`patch_bytes`,
  `redirect_call`, `replace_import`, `hook_function`); stubs come from the
  host's trampoline allocator instead of `int3` padding inside `eu4.exe`.
- Configuration moves from environment variables to
  `anvil/data/eu4-startup-boost/config.toml` (module switches, worker counts,
  verify modes). Environment variables stay as overrides for the lab harness.
- Writable data lives in `anvil/data/eu4-startup-boost/`. The prefetch
  manifest and the warm-up list become one **access profile** (see §3a)
  instead of a lab-generated manifest.
- The ready marker comes from `AnvilHost.game` / `on_game_ready`; modules stop
  their start-up behaviour at that point exactly as in the lab.
- Build identity: `game_builds = ["eu4-1.37.5-steam-win64"]`; all addresses are
  kept in one per-build table so a future port touches one file.

## 3a. Access profiles: shipped base layer + learning per mod set (D)

`ram_prefetch` must know which files and archive ranges the game will read;
`open_warmup` which files it will open. Both are the same knowledge, kept as
an access profile: entries (source, path or archive entry, first-use order,
bytes read), each attributed to its **source** — the base game or a specific
mod.

1. **Base layer, shipped with the plugin per game build**: generated in the
   lab from a start without mods; contains only paths, sizes and order (no game
   content). Works from the very first start on any machine.
2. **Per-mod-set profile, learned**: key = enabled mods (from `dlc_load.json`)
   + game build. Every start records what the game actually opened/read and
   merges it into the profile (new entries appended in first-use order;
   entries unused for N consecutive starts are dropped). First start of a new
   mod set: base layer + definitions-only "all" list of the mods' folders
   (filtered as in the lab warm-up), then refined by learning.
3. **Per-mod layers (later, same format)**: because each entry records its
   source, layers per mod (key: mod id + version/update time) can be extracted
   and composed for a new mod set made of known mods — base layer minus files
   the mods override, plus each mod's layer — without a learning start. A mod
   update changes its key; the old layer is used, validated by file size and
   time, until the new one is learned.
4. Memory budget for prefetch (default 2 GB, as in the lab) and prefetch order
   = first-use order.

Start with 1 and 2; keep the source attribution from day one so 3 needs no
format change.

## 4. Correctness safeguards (D)

- Modules that replace a game computation keep the lab's modes: `verify`
  (compute both, keep the game's result, count mismatches) and on (sampled
  comparison every 256th call, disable on mismatch).
- Startup caches answer only what is provably identical and fall back to the
  original otherwise; all switch off at the ready marker.
- Counters per module are written to the plugin's section of `anvil.log` at
  the ready marker (requests, hits, fallbacks, mismatches).

## 5. Testing (D)

- Native unit tests ported from the lab (`test_startup_*`): exists index,
  verifyPath listing, mimalloc hook, trigger interning, metascript
  substitution, warm-up lists.
- In-game validation with the lab harness: start-up series against vanilla and
  against the lab build, game logs compared, save loading checked, verify-mode
  runs for `trigger_intern` and `metascript_subst` before every release.

## 6. Stages

1. Skeleton plugin on Anvil stage 1 (manifest, config, logging, counters).
2. Port modules in order of independence: `mimalloc`, `verifypath_listing`,
   `exists_index`, `trigger_intern`, `metascript_subst`, `open_warmup`,
   `attr_cache`, `ram_prefetch` (largest; depends on miniz call-site patches).
3. Measure against the lab build; release candidate.

## 6a. Results of stage 2, first slice (0.1.0)

Modules ported: `mimalloc`, `verifypath_listing`, `exists_index`,
`trigger_intern`, `metascript_subst` (`src/modules/`, addresses only in
`src/build.hpp`). Patches go through Anvil: IAT slots and the vtable slot via
`patch_bytes` (expected pointer values), the verifyPath site via
`patch_bytes` + a stub in `alloc_near_image`, the trigger and metascript
sites via `redirect_call`. Plugin: 10 patches, loaded in ~42 ms at the entry
point.

Findings while porting (both also apply to the lab build):

- **`trigger_profile.txt`**: the game ships it in its root folder (332 KB);
  at start-up `0xB9B5A0` reads it and fills the trigger-interning map (6870
  entries). The lab's junction copies listed root files explicitly and missed
  it, so every lab measurement ran with an empty map. With the real file the
  module takes its hash-consing path for all 2.35 M calls; verify run: 0
  differences.
- **Write directory**: `verifypath_listing` in verify mode found 2 differences
  per start, both `logs/error.log` / `logs/error_old.log` in the PhysFS write
  directory (Documents\…\Europa Universalis IV), which the game renames during
  start-up after the listing was read. Both listing-based modules now never
  answer for the write directory or anything inside it (`PHYSFS_getWriteDir`);
  after the change: 0 differences.

Verify start (`verify` for all four modules that have it): verifypath
932,405, exists_index 16,074, trigger_intern 2,347,067, metascript_subst
320,905 comparisons, 0 mismatches.

Timing (lab smoke harness `scripts/anvil-smoke.ps1`, junction copy with the
original `eu4.exe`, loading screens pre-opened, 3 interleaved starts):
Anvil without plugins 34.96 / 35.50 / 34.70 s (median 34.96 s); with the
plugin 15.65 / 15.38 / 15.40 s (median 15.40 s). `setup.log`, `error.log`,
`system.log` identical between the two.

Next: `open_warmup` (+ access profiles), `attr_cache`, `ram_prefetch`.

## 7. Open questions (Q)

- Whether `mimalloc` should be its own plugin (it stays active during play,
  unlike the start-up-only modules).
