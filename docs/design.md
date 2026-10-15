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
4. Memory budget for prefetch (default 1 GB, `[ram_prefetch] budget_mb`; the lab used 2 GB) and prefetch order
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

### Per-module contribution (0.2.0 work)

Leave-one-out series `sb-loo1` (all modules on, then each off; 2 interleaved
starts each; ±1–2 s run-to-run noise): all 16.69 s; without
`verifypath_listing` 25.04 s; without `mimalloc` 18.29 s; without
`exists_index` 16.58 s; without `metascript_subst` 16.71 s; without
`trigger_intern` 15.92 s.

Direct cost measurement in one start (verify mode times our code and the
game's original separately): `trigger_intern` — original 682 ms for 2.35 M
calls, ours ~1263 ms, i.e. **slower** with the real (non-empty) map;
`metascript_subst` — original 860 ms, ours ~64 ms. `trigger_intern` is
therefore **off by default** (kept with its verify mode; a cheaper
canonicalization could revisit it, but at most ~0.7 s is available).

### open_warmup and access profiles

Ported with the learned profile per mod set
(`anvil/data/eu4-startup-boost/profiles/<build>-<dlc_load hash>.txt`), the
shipped base layer `plugin/profiles/eu4-1.37.5-steam-win64.txt` (10 695 game
files, generated from a learned profile by keeping only `@\` game-folder
entries — an approximation of a start without mods, because the lab's mod set
was not changed) and the filtered "all" list for mods. Learning uses the
game's `CreateFileW` import through Anvil `replace_import` and saves at the
game-ready event. Warm starts: first start without a profile — "all" list
13 619 entries in 1.86 s; with the base layer — 11 413 entries in 78 ms;
learned profile — 11 503 entries in 76 ms; start ~15.1 s in each case. The
cold-start effect (lab: 61 → 15 s) still has to be measured with the plugin
after a reboot.

### attr_cache

Ported in the lab's "negative" policy only: `GetFileAttributesExW` and
`GetFileAttributesA` (the game's only attribute imports) through Anvil
`replace_import`; "not found" answers remembered for paths inside the game
folder and the enabled mods' folders (11 roots), never for the user folder.
Verify start `sb-attr-verify-4` (other modules at defaults): 75 389 queries
before the ready marker (the lab counted ~1.05 M before `verifypath_listing`
and `exists_index` existed), 12 491 real misses, 7 682 would-be hits —
0 mismatches — whose real API calls took **114 ms** in total across threads;
55 216 queries were outside the roots. The hook itself costs a full-path
conversion and a lookup on every query, so the net effect is about zero:
**off by default**, kept for verification.

The skipped queries are almost all PhysFS `verifyPath` component checks in the
write directory (`Documents\…\Europa Universalis IV\gfx`, `\common`,
`\history`, …), which `verifypath_listing` deliberately does not answer. The
write directory is not static: the game regenerates `gfx\flags\flagfiles_*.tga`
there and writes `shadercache`, so a "not found" cache for it would need
invalidation on the game's own directory/file creation. Not implemented;
estimated at most ~1 s summed over threads (hook time / query count), not
measured on the critical path.

### ram_prefetch

Potential first: diagnostic `zip_profile` (off by default; wraps the ZIP
archiver's Io read slot) in start `sb-zipprof-2` with all other modules on:
the game's **main thread** spent **2 748 ms in ZIP member reads (1 216 MB)**,
another thread 567 ms (274 MB).

Ported from the lab with these hooks: OpenArchive / OpenRead / CloseArchive
slots of the ZIP archiver table (`patch_bytes`, expected pointers). Workers
read and decode members into buffers; OpenRead wraps the game's Io and serves
the buffer. Same safeguards as the lab: the local header is cross-checked with
the central directory; CRC-32 is computed over the whole output; at open the
game's own entry (method, CRC, sizes) must match; anything else goes to the
original Io. Differences from the lab:

- Prefetch starts at plugin load, in first-use order, instead of when the game
  mounts an archive.
- A buffer leaves the memory budget when the game takes it; a second open of the
  same member reads through the original path.
- DEFLATE uses the game's own `tinfl_decompress` (pure miniz code, worker-owned
  state) instead of zlib, so there is no new dependency; CRC-32 comes from
  `ntdll!RtlComputeCrc32`.
- The member list is an access profile learned per mod set
  (`<key>.zip.txt`, "archive\tmember"); a game-folder base layer is shipped
  (`plugin/profiles/<build>.zip.txt`, 2 523 members of 120 DLC archives).

Results (EU4 1.37.5, the lab's mod set):

- Learning start: 2 740 opens, 2 523 unique members, the same counts as the
  lab's observer run.
- Verify start `sb-ram-verify-1`: 2 523 served, 13 474 byte comparisons with
  the game's reads, 0 mismatches; 217 second opens read by the game itself.
- On: all 1 478 MB decoded ~0.6–0.7 s after load (decode 4.7–5.3 s summed over
  8 below-normal workers). The main thread's ZIP read time fell from 2 748 ms
  to 1.2 ms (`sb-ram-on-prof-1`).
- Series `sb-ram-ab3` (3 + 3 interleaved, warm, learned profiles kept):
  **12.24 s** median (11.98 / 12.24 / 12.50) vs 15.16 s without
  `ram_prefetch` (15.14 / 15.16 / 15.66); the ranges do not overlap.
  `setup.log`, `error.log` and `system.log` are identical between the two
  variants.
- Memory: with a 2 GB budget the whole profile is resident before the game
  takes the first member (peak not-yet-taken 1 478 MB).
- Budget (`[ram_prefetch] budget_mb` in `config.toml`, 64–8192 MB, override
  `EU4SB_RAM_PREFETCH_BUDGET_MB`): workers wait while the not-yet-taken bytes
  would exceed it. Series `sb-budget-1` (3 × 3 interleaved, warm):
  - 2048 MB: 11.97 s median (11.70 / 11.97 / 12.22); everything decoded after
    669 ms.
  - 1024 MB: 11.95 s (11.70 / 11.95 / 12.22); everything decoded after 2.7 s,
    and the budget was full at its peak.
  - 512 MB: 12.23 s (11.68 / 12.23 / 12.47); 3 members were not ready in time.

  1024 and 2048 MB are equal within the noise, so the default is **1024 MB**.

Next: cold-start check with all modules. The first attempt after a reboot
(15.10, `sb-cold-all-1`, 74.6 s) ran with a stale Anvil 0.1.0 build that
skipped the plugin (manifest requires `>=0.2.0`), so it measured Anvil alone.
### Real game folder

Anvil and the plugin installed in the Steam game folder (no junction copy),
`eu4.exe` started there with Steam running and the launcher's working
directory, fresh plugin data (shipped base layers), removed after each start
(lab harness `anvil-smoke.ps1 -RealGameFolder`; warm, one start each):

| Start | Running application |
| --- | ---: |
| without Anvil (`sb-real-vanilla-1`) | 33.60 s |
| Anvil + plugin, first start (`sb-real-1`) | 14.91 s |
| Anvil + plugin, again (`sb-real-2`) | 11.91 s |

In the first start `open_warmup` took 5.5 s (these paths had not been opened
since the reboot); in the second 74 ms. All modules loaded, `ram_prefetch`
served 1 478 MB with the 1 GB budget; `setup.log`, `error.log` and
`system.log` are identical to the start without Anvil.

Still open: a start through the Paradox launcher's Play button (needs UI
control) and the cold start with the plugin.

## 7. Open questions (Q)

- Whether `mimalloc` should be its own plugin (it stays active during play,
  unlike the start-up-only modules).
