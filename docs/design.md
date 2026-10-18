# Bellows — design

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
  `anvil/data/bellows/config.toml` (module switches, worker counts,
  verify modes). Environment variables stay as overrides for the lab harness.
- Writable data lives in `anvil/data/bellows/`. The prefetch
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
`metascript_subst` — original 860 ms, ours ~64 ms. `trigger_intern` was
therefore **off by default** (kept with its verify mode; a cheaper
canonicalization could revisit it, but at most ~0.7 s is available).

Cheaper index (on by default since this change; a `config.toml` written by
an earlier version keeps its `"off"`): the structure table holds only
the map keys and their substructures (arena + open addressing, built with the
index); a node's shape is only looked up, and a miss marks it "foreign"
(answer 0, and its parents are foreign too), since a key consists solely of
key substructures. Inner nodes are memoized in a flat pointer table presized
from the registry's two set sizes (+0x10, +0x50); leaves skip the memo.
Measured in the game (lab junction copy, warm starts): verify 0 mismatches
over 2,347,067 calls; our part ~487 ms against 628 ms for the original; in
on mode 417–436 ms summed per call. An interleaved on/off series of start-ups
(5 vs 7 starts) gave 12.25 ± 0.50 s vs 12.37 ± 0.36 s to `Running
application`: consistent with ~0.2 s, but within the noise.

Hook overhead: timing every call (two QPC reads and a locked add) and the
interlocked hot-path counters cost ~0.1–0.2 s offline. A locked instruction
waits for the walk's outstanding cache misses, and a timed call cannot overlap
its misses with its neighbours', so per-call timing also inflated the result.
The hook now counts with plain updates (the game interns from one thread
under its own mutex) and logs the span from the first to the last call
(QPC at every 16th entry; the game's loop overhead included). Offline 474 →
360 ms; in the game the span is 381–395 ms (verify: 0 mismatches). The index
is released at the ready report.

### open_warmup and access profiles

Ported with the learned profile per mod set
(`anvil/data/bellows/profiles/<build>-<dlc_load hash>.txt`), the
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

### token_dict_cache

The script tokenizer (`0x170E700`) looks every word token up in a global
case-insensitive ternary search tree (`0x2326720`, `0x6E4880`): one CRT
`tolower` and one node per character. `token_dict_profile` (diagnostic)
measured ~8.7 M lookups per start, 16.7 nodes per successful lookup and a
215 k-node tree that grows during loading (get-or-add appends to the run-time
token vector `0x242B8A0`). The tokenizer's call site is redirected to a
per-thread open-addressing cache keyed by the token with ASCII letters
lowercased; hits stay valid until the tree is rebuilt, misses only for the
run-time token count read before the lookup. On by default and kept out of the
written `config.toml`, so existing files get it too.

Lab: verify start 0 mismatches over 8,447,519 cache answers; offline on the
real keys 261–304 vs 704–723 ms (shuffled) and 137–142 vs 233–246 ms (runs);
two interleaved start-up series −0.43 s (8/8 pairs) and −0.32 s (5/8), −0.38 s
over 16 pairs (Anvil ready time); game logs identical in all starts.

### range_profile (diagnostic, simulation)

The first module aimed at the simulation, not the start. In the lab's profile
of a year at speed 5 (1720, Russia 1719 save) 17% of the main thread was one
chain: the province day (`0x9F52E0`) checks whether a country can still reach
a province (`0x3532C0` → `0x3520D0`), and `0x3526B0` takes the minimum
distance from the country's flagged provinces to the target, calling the
province distance `0x2D2520` for each, with an early exit below the threshold.

`range_profile` (off by default, not in the written `config.toml`) hooks
`0x3526B0` and `0x2D2520` through Anvil `hook_function` only to observe: every
check is recorded with its country, province, value, in-range answer, the
distance calls inside it, its time and its caller. The game is usually closed
without a clean exit, so a thread writes `range_profile*.tsv` to the data
folder every 5 s: a summary, per game day, the 5000 busiest (country,
province) pairs and the callers.

Lab, `sim-range-8-a` trial 2 (only `range_profile` on, 8 workers per pool):
the year 1720 took 27.73 s, of which 5.07 s (18.3%) inside the checks; 143,482
checks (393 per day), 12.5 M distance calls (87 per check, up to 1497); every
check came from `0x3520D0`. Over the whole session (655 game days) 1597 pairs
of 85 countries were checked, each about once a day; their values changed only
65 times (52 pairs) and the in-range answer twice. A memo per pair would
answer almost every check if it can tell when the inputs change: the country's
province list, the province flags and lists the check reads, the range
modifier and the range bonus.

### range_witness (simulation)

`range_profile` showed what dominated the check: 97.5% of the distance calls
went into walks that ended "in range", after 92.6 provinces on average, and the
walk usually ended at the same province day after day. The answer 0x3520D0
uses is only `value <= threshold`, and the walk stops at the first province
below the threshold; so any province below the threshold gives the same answer.

`range_witness` redirects the call at `0x35214C` (in `0x3520D0`). Per
(country, target province, threshold) it keeps the province that ended the last
walk (the witness), from the country's own list or from a related country (the
second walk over the relation records at `+0x1510`, parallel to the country
array at `root+0x1CE8`, qualified through the record's list at `+0x128` and the
flag `+0x11F`, with the related country's own bonus through `+0x1420` and
`0x7ACEC0`). A check first evaluates only the witness with the game's own
functions and formula: still in the list, bit `0x10` at `+0x958`, the country in
the province's list at `+0x490`, `min(0x2D2520, straight line) * 1000 / (1000 +
bonus) < threshold`. Then it writes the value the original would write when
stopping there. Otherwise the original walk runs and a new witness is looked
up; when none exists (the answer came from a value exactly at the threshold),
the lookup waits 30 game days. Nothing is kept across loads beyond what the
session builds. `verify` always runs the original and compares.

Lab (EU4 1.37.5, Russia 1719 save, 8 workers per pool, only this module on,
Tongs-driven year 1720 at speed 5; lab `docs/simulation-profile.md`):

- `verify`, two full sessions with the first walk only: 361,622 witness
  answers, all equal to the original's, and in 359,338 of them the original
  stopped at the witness and wrote the same value; 0 differences. With the
  second walk (one session, ~200 game days): 72,233 answers, 0 differences,
  72,042 same values.
- `on`: 92% of the checks answered from the witness (236,981 of 258,100 in a
  session, 56,197 from a related country); time inside the checks 1.2 s per
  session instead of ~9 s.
- Interleaved series (ABBA, one launch per trial): `on` 20.68, 20.48, 21.02,
  20.05 s for the year, `off` 27.29, 27.58, 27.91 s (one `off` trial rejected
  on an event pause): −7.0 s (−25.5%), no overlap. Main-thread CPU 19.1 s
  instead of 25.5 s per year.

- `verify`, one session from 17 March 1719 to 1 January 1727 (both walks, the
  game driven year by year at speed 5, two event pauses resumed): 1,106,039
  checks, 1,021,812 witness answers, 0 disagreements; in 1,016,682 the
  original stopped at the witness and wrote the same value, 0 different.

On by default since then, like `token_dict_cache` (kept out of the written
`config.toml`, so existing files get it too). Still open: other saves, mod
sets and much later dates.

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

Through the Paradox launcher (`sb-launcher-1`, `-3`, `-4`, `-5`): Steam opened
the launcher and Play was clicked through UI automation. `eu4.exe` was started
by `Paradox Launcher` and the plugin loaded with all default modules each
time, with fresh plugin data (shipped base layers). Starts are timed from the
game process's creation:

| Start | Running application | `open_warmup` | `ram_prefetch` workers |
| --- | ---: | ---: | ---: |
| `sb-launcher-1` | 17.77 s | 6 093 ms | 6 865 ms |
| `sb-launcher-3` | 17.27 s | 5 556 ms | 6 709 ms |
| `sb-launcher-4` | 13.81 s | 100 ms | 5 381 ms |
| `sb-launcher-5` | 13.72 s | 89 ms | 5 449 ms |

The game logs are identical to the start without Anvil, and the launcher left
`dlc_load.json` unchanged.

- **Slow warm-up:** the slow `open_warmup` runs were the first starts after a
  break. A direct start shows the same (`sb-real-1`: 5.5 s), so it is not
  launcher-specific.
- **Launcher slower with warm files:** 13.7–14.8 s, against ~11.9 s for direct
  starts. Launcher starts load the Steam overlay (`gameoverlayrenderer64.dll`,
  `steamclient64.dll`, checked 8 s after the process appears); a direct start
  (`sb-real-3`, 11.97 s) loads neither. That the overlay causes the difference
  is a hypothesis.
- **Plugin gain through the launcher:** without Anvil 30.87 s and 30.08 s
  (`sb-launcher-van-1`, `-van-2`), interleaved with `sb-launcher-6` at
  14.79 s; the game logs are identical.
- **Click timing:** a Play click while the launcher was still syncing at
  start-up did not launch the game (`sb-launcher-2`).

### Gameplay

`sb-play-2`: Anvil and the plugin in the game folder, the 1719 Russia save,
five in-game years at speed 5 played through UI automation (37 popups
answered), then exit through the game menu. The game wrote 5 yearly autosaves
and showed no crash, error dialog or broken graphics. `error.log` holds only
the mod-script errors already seen without the plugin. `sb-load-1`: the
1724.1.1 autosave written in that session loads correctly. The starting save
was unchanged after both sessions, and the game folder was clean.

Still open: the cold start with the plugin.

## 7. Open questions (Q)

- Whether `mimalloc` should be its own plugin (it stays active during play,
  unlike the start-up-only modules).
