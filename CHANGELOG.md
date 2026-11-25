# Changelog

## [2.2.0](https://github.com/happy-dwarf-team/bellows/compare/v2.1.0...v2.2.0) (2026-11-22)


### Features

* **conquer_scan:** new module, off by default: the AI's list of provinces worth conquering is built from the land provinces only, on one thread or the game's pool, with the same list. Turn it on under Bellows in Anvil Manager (`bellows.conquer_scan`); in the lab it saved about 1 s a game year on Anbennar (more in the first months after 1444) and nothing on the base game ([d64ba9c](https://github.com/happy-dwarf-team/bellows/commit/d64ba9c5c85b991fc8dfb1218beda2046cf14d08))
* **diplo_scan:** new prototype module, off by default: a country's daily check for changed relations is skipped when nothing in its diplomacy changed, with the same answers. Turn it on under Bellows in Anvil Manager (`bellows.diplo_scan`); in the lab it saved about 0.9 s a game year on Anbennar and 0.1 s on the base game ([c7ad8a0](https://github.com/happy-dwarf-team/bellows/commit/c7ad8a0a3859164db34e400cbe061bf0ecb9841b))


### Bug Fixes

* **access_profile:** a `-userdir` folder that does not exist yet is now a new, empty playset, so that start no longer learns its start-up profiles from your usual playset's mods ([0a5638b](https://github.com/happy-dwarf-team/bellows/commit/0a5638bc896bea29c4157a0aefa2e442022f1d82))
* **access_profile:** a start with a `-userdir` folder that does not exist yet no longer writes the missing `dlc_load.json` as a warning to `anvil/anvil.log` ([4ac44c8](https://github.com/happy-dwarf-team/bellows/commit/4ac44c82748d9e2e987ae05c796729b747c2955d))

## [2.1.0](https://github.com/happy-dwarf-team/bellows/compare/v2.0.1...v2.1.0) (2026-11-19)


### Features

* **listing_lookup:** turn the module on by default ([#221](https://github.com/happy-dwarf-team/bellows/issues/221)) ([438d26d](https://github.com/happy-dwarf-team/bellows/commit/438d26dcf4851fb447742f62e33f0ba614f46319))
* **range_witness:** check one answer in 1024 against the game in "on" mode ([#224](https://github.com/happy-dwarf-team/bellows/issues/224)) ([c864a1e](https://github.com/happy-dwarf-team/bellows/commit/c864a1e067c1c8b35bc4a0b03bcf53b3f055cb12)), closes [#218](https://github.com/happy-dwarf-team/bellows/issues/218)


### Bug Fixes

* **army_search_profile:** forget the per-army keys at a session change ([#251](https://github.com/happy-dwarf-team/bellows/issues/251)) ([dcd644b](https://github.com/happy-dwarf-team/bellows/commit/dcd644b7311800c20ea6766f06b2681cc8ac4c1f))
* **attr_cache:** remove the W import hook when the install fails after it ([#232](https://github.com/happy-dwarf-team/bellows/issues/232)) ([bda52f0](https://github.com/happy-dwarf-team/bellows/commit/bda52f0aacf1b4572a7633f3a83d858c3549bf25))
* **diagnostics:** keep a failed path in the profilers' writers inside their thread ([#239](https://github.com/happy-dwarf-team/bellows/issues/239)) ([f758475](https://github.com/happy-dwarf-team/bellows/commit/f758475cc2e6ece3fdefcaff124282b5f5f7ce99))
* **diagnostics:** write the profilers' files after their lock is released ([#222](https://github.com/happy-dwarf-team/bellows/issues/222)) ([375f545](https://github.com/happy-dwarf-team/bellows/commit/375f54585383a4b4a6af8abe89c7b4ec430eb7d7)), closes [#216](https://github.com/happy-dwarf-team/bellows/issues/216)
* **frame_pacing:** say that an interval up to the game's 50 ms keeps the game's ([#228](https://github.com/happy-dwarf-team/bellows/issues/228)) ([ad259a8](https://github.com/happy-dwarf-team/bellows/commit/ad259a86cf406726425d8cee28cbd1637c1e145b))
* **listing_lookup:** show the module off, not failed, without verifypath_listing ([#219](https://github.com/happy-dwarf-team/bellows/issues/219)) ([8b5b681](https://github.com/happy-dwarf-team/bellows/commit/8b5b6815d48c325524ef4659713a1859a052b95a))
* **modifier_merge:** leave a dense list with fewer than 0x286 entries to the game ([#230](https://github.com/happy-dwarf-team/bellows/issues/230)) ([fdfbc9c](https://github.com/happy-dwarf-team/bellows/commit/fdfbc9cc563884ba00be9c4b8c75c240345fccc5))
* **modifier_merge:** leave a null list to the game's function ([#209](https://github.com/happy-dwarf-team/bellows/issues/209)) ([eee854f](https://github.com/happy-dwarf-team/bellows/commit/eee854f561c60c8d688fcf9f1db88ca61facad97))
* **monthly_probe:** leave a null modifier list to the game's merge ([#236](https://github.com/happy-dwarf-team/bellows/issues/236)) ([1d95271](https://github.com/happy-dwarf-team/bellows/commit/1d9527129f719dfd044af46c350806f1a0f5133d))
* **monthly_probe:** remove the random-draw hook when a later hook is refused ([#212](https://github.com/happy-dwarf-team/bellows/issues/212)) ([a25f93b](https://github.com/happy-dwarf-team/bellows/commit/a25f93b932a46ddda0084c938e294cd9249af9df))
* **open_warmup:** stop learning at ready also when there is no profile to save ([#231](https://github.com/happy-dwarf-team/bellows/issues/231)) ([621fea8](https://github.com/happy-dwarf-team/bellows/commit/621fea8352a2988791c28a1811e7fe22a09ce1f0))
* **plugin:** keep installing the other modules when one throws a non-standard exception ([#229](https://github.com/happy-dwarf-team/bellows/issues/229)) ([9d131e4](https://github.com/happy-dwarf-team/bellows/commit/9d131e40b39da0bc254fbe0cd4d2c7542dee5052))
* **plugin:** keep installing the other modules when one throws something other than std::exception ([9d131e4](https://github.com/happy-dwarf-team/bellows/commit/9d131e40b39da0bc254fbe0cd4d2c7542dee5052))
* **plugin:** let the Paradox launcher see the Workshop item as made for 1.37 ([#240](https://github.com/happy-dwarf-team/bellows/issues/240)) ([861c407](https://github.com/happy-dwarf-team/bellows/commit/861c407c99c4bb5e0fa3ebcf53f821c2b169567f))
* **plugin:** replace every module's counter file only after a successful write ([#227](https://github.com/happy-dwarf-team/bellows/issues/227)) ([eefd2b3](https://github.com/happy-dwarf-team/bellows/commit/eefd2b32c9e7445ff85e2a0a36de9f27e7b2faae))
* **plugin:** show verifypath_session off when verifypath_listing is off ([#237](https://github.com/happy-dwarf-team/bellows/issues/237)) ([4563438](https://github.com/happy-dwarf-team/bellows/commit/4563438b50e2444be1b441d0f6e111a42f3df661))
* **ram_prefetch:** let the stop at ready see every worker the starter creates ([#233](https://github.com/happy-dwarf-team/bellows/issues/233)) ([2b0518a](https://github.com/happy-dwarf-team/bellows/commit/2b0518ab1e83ede63c9a9545ea96facb0cfa4756))
* **ram_prefetch:** read the file info only of the ZIP archiver's own Io ([#246](https://github.com/happy-dwarf-team/bellows/issues/246)) ([256b578](https://github.com/happy-dwarf-team/bellows/commit/256b5781bc1dabcafb8a2c8ca150df688fda7115))
* **range_witness:** forget the witnesses when a session starts or ends ([#242](https://github.com/happy-dwarf-team/bellows/issues/242)) ([0e2c476](https://github.com/happy-dwarf-team/bellows/commit/0e2c4769d381269143a073d66283d7769aaeea20))
* **range_witness:** forget the witnesses when an earlier save is loaded ([#214](https://github.com/happy-dwarf-team/bellows/issues/214)) ([c5f7c49](https://github.com/happy-dwarf-team/bellows/commit/c5f7c490ff7f5c26288ddb567e431fcc441d7f66))
* **range_witness:** publish the exported counters while the checks run ([#235](https://github.com/happy-dwarf-team/bellows/issues/235)) ([b919be3](https://github.com/happy-dwarf-team/bellows/commit/b919be35b62f2cd8139dd9443c7a1c3a484fbf9f))
* **save_compress:** do not write a member again after the parallel way wrote part of it ([#234](https://github.com/happy-dwarf-team/bellows/issues/234)) ([c5e3405](https://github.com/happy-dwarf-team/bellows/commit/c5e3405bf573c2ddf4da042eecbd55aae25ad35f))
* **save_compress:** mark the members the check could not compare ([#244](https://github.com/happy-dwarf-team/bellows/issues/244)) ([8f2ab7d](https://github.com/happy-dwarf-team/bellows/commit/8f2ab7d844864b53e2330bf91d5e69b9fb1b9091))
* **save_compress:** say "uncompared" also after a fallback ([#248](https://github.com/happy-dwarf-team/bellows/issues/248)) ([ed2275a](https://github.com/happy-dwarf-team/bellows/commit/ed2275acb68e4d3640ea0aab28d7cc4929b2eb21))
* **start-up:** cap trigger_intern's presize, close ram_prefetch's handles ([#223](https://github.com/happy-dwarf-team/bellows/issues/223)) ([2cc7dcd](https://github.com/happy-dwarf-team/bellows/commit/2cc7dcdff83a7a97df0a568865f99cb0771264fb)), closes [#217](https://github.com/happy-dwarf-team/bellows/issues/217)
* **verifypath_listing:** know the write directory in any spelling of its separators ([#243](https://github.com/happy-dwarf-team/bellows/issues/243)) ([4e73a00](https://github.com/happy-dwarf-team/bellows/commit/4e73a00882acea33345aac6d8d892f5bf5c74cea))
* **verifypath_listing:** leave case-sensitive folders to the game ([#213](https://github.com/happy-dwarf-team/bellows/issues/213)) ([aeed4dd](https://github.com/happy-dwarf-team/bellows/commit/aeed4dd1a131deaae91d779ca4533a15b75d1ea2))
* **verifypath_listing:** leave Windows device names to the game ([#245](https://github.com/happy-dwarf-team/bellows/issues/245)) ([0e58185](https://github.com/happy-dwarf-team/bellows/commit/0e5818541250b5b0b2b18ee49c612c0b573eb5ae))


### Performance

* **verifypath_listing:** check device names without copying the component ([#247](https://github.com/happy-dwarf-team/bellows/issues/247)) ([7683da2](https://github.com/happy-dwarf-team/bellows/commit/7683da2eb02bf2862882bdcc5c4d700c0a971127))

## [2.0.1](https://github.com/happy-dwarf-team/bellows/compare/v2.0.0...v2.0.1) (2026-11-16)


### Bug Fixes

* **mimalloc_heap:** keep blocks of 1 MiB and more in the Windows heap ([#207](https://github.com/happy-dwarf-team/bellows/issues/207)) ([accacb4](https://github.com/happy-dwarf-team/bellows/commit/accacb47128d97a8f2fb2c081b68881e52c1a0b7))
* **settings:** say in the log that a leftover config.toml is no longer read ([#205](https://github.com/happy-dwarf-team/bellows/issues/205)) ([2316faa](https://github.com/happy-dwarf-team/bellows/commit/2316faa44485a1d4676f3278cfe1d32e29ca0ffd))

## [2.0.0](https://github.com/happy-dwarf-team/bellows/compare/v1.8.0...v2.0.0) (2026-11-15)


### ⚠ BREAKING CHANGES

* Bellows 2.0 needs Anvil 1.23.0 or newer; an older Anvil skips it and says why in `anvil.log`. Update Anvil first with `AnvilManager.exe` from its release page. ([#193](https://github.com/happy-dwarf-team/bellows/issues/193)) ([c397b04](https://github.com/happy-dwarf-team/bellows/commit/c397b0445e1d8c3d62c846beb0a3ae1ff7643831))
* Each feature now has one switch, its module in the Plugins tab of Anvil Manager or the overlay; the on, off and verify settings are gone. A feature you turned off in 1.x stays off. Set again: the allocator for the whole session and Compress on several threads. ([#196](https://github.com/happy-dwarf-team/bellows/issues/196)) ([9c63439](https://github.com/happy-dwarf-team/bellows/commit/9c634395b506da1e60858349f95bf2fe3d62e07b))
* Updating straight from 0.7.0 or older, or from EU4 Startup Boost, you choose the settings again, and the learned profiles are learned again at the first start of each mod set. ([#195](https://github.com/happy-dwarf-team/bellows/issues/195)) ([bf37ff9](https://github.com/happy-dwarf-team/bellows/commit/bf37ff96eb0baa9a036f4f92c22e083ade78405f))

### Features

* Bellows' 25 features are Anvil modules in five groups (Start-up, Loading, Simulation, Saving, For testing), each with a short name and one line on what you get. Turn one off if it causes trouble; a crash report names the module. ([#190](https://github.com/happy-dwarf-team/bellows/issues/190)) ([c9fbd75](https://github.com/happy-dwarf-team/bellows/commit/c9fbd75878ae76ec8aa8865a4dd7db5e16f7a405)) ([#196](https://github.com/happy-dwarf-team/bellows/issues/196)) ([9c63439](https://github.com/happy-dwarf-team/bellows/commit/9c634395b506da1e60858349f95bf2fe3d62e07b))
* A new Diagnostics module, off by default, holds the timing profiles and, under Advanced, a check of each feature against the game's own code (the old "verify"). With it off, none of that code runs. ([#196](https://github.com/happy-dwarf-team/bellows/issues/196)) ([9c63439](https://github.com/happy-dwarf-team/bellows/commit/9c634395b506da1e60858349f95bf2fe3d62e07b))

## [1.8.0](https://github.com/happy-dwarf-team/bellows/compare/v1.7.0...v1.8.0) (2026-11-12)

### Features

* With Anvil 1.20 or newer, Bellows' settings and description show in eight languages; the texts are drafts that native speakers have not checked yet. Nothing else changes. ([#184](https://github.com/happy-dwarf-team/bellows/issues/184)) ([a5d6154](https://github.com/happy-dwarf-team/bellows/commit/a5d615471ed90ec419cb0d660ba5e3f4d0b29da6))

## [1.7.0](https://github.com/happy-dwarf-team/bellows/compare/v1.6.0...v1.7.0) (2026-11-10)

### Features

* **listing_lookup:** a new start-up setting for big mod sets, "PhysFS lookups from listings". At start the game looks for each of its files in every folder that could hold it, and in a folder that overrides another, such as Anbennar's, most of these looks fail: about 28,000 failed opens per start. With `listing_lookup = "on"` Bellows answers "not there" from the folder listings it already reads for "PhysFS path checks" (`verifypath_listing`, on by default), and every file that exists is still opened by the game. Every 256th answer is checked against the game, and a difference turns the module off. In the lab a warm start of Anbennar took 18.9 s instead of 22.1 s (5 warm starts each, alternating), with nothing measurable on the base game (11.6 s either way). The game's logs and the version code on the menu were the same in every start, and the `verify` mode compared about 100,000 answers on Anbennar with the game's without a difference. Off by default: turn it on in Anvil Manager's Settings tab. Not measured yet: the first start after a reboot, other mod sets, GOG in the game, and together with `checksum_defer`. ([#178](https://github.com/happy-dwarf-team/bellows/issues/178))

Nothing changes for players who keep the default settings: the new module is off by default, and the default modules are the same as in 1.4 to 1.6. It works only at start, before the main menu, and was not tested in multiplayer.

## [1.6.0](https://github.com/happy-dwarf-team/bellows/compare/v1.5.0...v1.6.0) (2026-11-09)

### Features

* **never_throttle:** a new setting for players who minimize the game at speed 5. While a single-player game runs at speed 5 with the window minimized, the game normally waits 100 ms every frame; with `never_throttle = "on"` it skips that wait. In the lab a minimized year took 34 s instead of 83 s on the base game and 65 s instead of 107 s on Anbennar (medians of 5 games each, without `-seed`). With the window visible nothing changes. A minimized game at speed 5 uses more processor time per second (about 1.15 cores instead of 0.5–0.7). Pauses, menus, other speeds and multiplayer keep the game's waits. Off by default: turn it on in Anvil Manager's Settings tab. ([#171](https://github.com/happy-dwarf-team/bellows/issues/171))
* **checksum_defer:** a new start-up setting for big mod sets. The game reads the files for its version code (the code next to the version on the main menu) while it loads its databases, and the two slow each other down. With `checksum_defer = "on"` that code is built once the databases are loaded: the same files and the same code, ready before the main menu. In the lab this took about 1 s off a start of Anbennar (22.1 → 21.0 s and 22.6 → 21.6 s in two series), with nothing measurable on the base game. The game's logs and the version code on the menu were the same in every start. Off by default. Not measured yet: the first start after a reboot, a slow disk, other mod sets. ([#172](https://github.com/happy-dwarf-team/bellows/issues/172))

Nothing changes for players who keep the default settings. Both modules are off by default, and the default modules are the same as in 1.4 and 1.5. Neither new module was tested in multiplayer. `never_throttle` does nothing in multiplayer. With `checksum_defer` on, the game builds the same version code that multiplayer compares, so it should not matter there, but that has not been tested. The multiplayer test of the current defaults (Bellows 1.4) used the game's usual out-of-sync check, which compares the random-number count and seed, and it reported nothing in 203 days.

## [1.5.0](https://github.com/happy-dwarf-team/bellows/compare/v1.4.1...v1.5.0) (2026-11-09)

### Features

* **symbols:** Bellows runs on the GOG build 1.37.5.0, the version GOG Galaxy installs when you roll EU4 back. On that build it loads with Anvil 1.15.0 or later, which knows the build. Every address Bellows uses is the same as on GOG 1.37.5.2. Tested in the lab on a copy of a GOG 1.37.5.0 install with Tongs, Hamweard and Mei: a new campaign and one year of a 1719 save at speed 5. That was one run and not a measurement; the speed figures in the README are from Steam 1.37.5. Nothing changes on Steam or on GOG 1.37.5.2. ([#169](https://github.com/happy-dwarf-team/bellows/issues/169))

## [1.4.1](https://github.com/happy-dwarf-team/bellows/compare/v1.4.0...v1.4.1) (2026-11-09)


### Bug Fixes

* **settings:** list each group's settings together ([#167](https://github.com/happy-dwarf-team/bellows/issues/167)) ([c9d7711](https://github.com/happy-dwarf-team/bellows/commit/c9d771162301e6c093d9f0786d2716d9d3b98367))

## [1.4.0](https://github.com/happy-dwarf-team/bellows/compare/v1.3.0...v1.4.0) (2026-11-09)


### Features

* **frame_pacing:** change the interval and the input mode while the game runs ([#152](https://github.com/happy-dwarf-team/bellows/issues/152)) ([8dab270](https://github.com/happy-dwarf-team/bellows/commit/8dab2706abc860f24e29616a1cb50172d0714a41))
* **settings:** port_table, province_modifier_view, army_search_memo and pops_pacing on by default ([#142](https://github.com/happy-dwarf-team/bellows/issues/142)) ([8e68638](https://github.com/happy-dwarf-team/bellows/commit/8e6863837f51721c34a287c53dfc628d54ceb330))


### Bug Fixes

* **profiles:** drop learned entries whose file or ZIP member is gone ([#144](https://github.com/happy-dwarf-team/bellows/issues/144)) ([ee1b5b2](https://github.com/happy-dwarf-team/bellows/commit/ee1b5b214ba16c414fe2229873e1a9ca1ef15000))

## [1.3.0](https://github.com/happy-dwarf-team/bellows/compare/v1.2.0...v1.3.0) (2026-11-06)


### Features

* **army_search_memo:** more scopes, and the army's speed modifier ([#128](https://github.com/happy-dwarf-team/bellows/issues/128)) ([2468a59](https://github.com/happy-dwarf-team/bellows/commit/2468a59b52df2df191eabb28506bb3b3feedc8a6))
* **develop_search_memo:** one search per kind in the AI's walk over its provinces ([#132](https://github.com/happy-dwarf-team/bellows/issues/132)) ([7e7ab19](https://github.com/happy-dwarf-team/bellows/commit/7e7ab199453edbc82af09d22b53829daeca33ec0))
* **monthly_probe:** one row per monthly tick in monthly_probe_ticks.tsv ([#140](https://github.com/happy-dwarf-team/bellows/issues/140)) ([6bd5883](https://github.com/happy-dwarf-team/bellows/commit/6bd588349eac5c5cf28d2838aebf83ef8a770fea))
* **port_table:** the nearest-port table from one list of ports ([#139](https://github.com/happy-dwarf-team/bellows/issues/139)) ([9635e12](https://github.com/happy-dwarf-team/bellows/commit/9635e123cfc304e80a073f89bb285a69060f0585))


### Bug Fixes

* **army_search_memo:** take back the first change when the second is refused ([#127](https://github.com/happy-dwarf-team/bellows/issues/127)) ([6561395](https://github.com/happy-dwarf-team/bellows/commit/6561395a440f1bf46639937d45736f7d63f5263d))
* **history_add:** keep the game's walk for an older entry after a key that is no date ([#126](https://github.com/happy-dwarf-team/bellows/issues/126)) ([720c86d](https://github.com/happy-dwarf-team/bellows/commit/720c86d01ececb0ec9019c16de868c2538103c8d))
* **plugin:** a failed migration or report no longer costs the plugin or the game ([#138](https://github.com/happy-dwarf-team/bellows/issues/138)) ([dd5e874](https://github.com/happy-dwarf-team/bellows/commit/dd5e874dce9fb17581bfa8d3dff99c8294c9d7f3))
* read a -userdir whose path alone is in quotes ([#130](https://github.com/happy-dwarf-team/bellows/issues/130)) ([a196bd2](https://github.com/happy-dwarf-team/bellows/commit/a196bd2388ec10fa0d355c58b41df404e9cac2d2))


### Reverts

* **develop_search_memo:** the search's answer can change during a walk ([#133](https://github.com/happy-dwarf-team/bellows/issues/133)) ([b72ee5f](https://github.com/happy-dwarf-team/bellows/commit/b72ee5f839ffd53a0b64244b3f9a280a7309ff60))

## [1.2.0](https://github.com/happy-dwarf-team/bellows/compare/v1.1.0...v1.2.0) (2026-11-05)


### Features

* **army_search_memo:** the army's zone-of-control flag once per AI army search (off by default) ([#89](https://github.com/happy-dwarf-team/bellows/issues/89)) ([f07ce5f](https://github.com/happy-dwarf-team/bellows/commit/f07ce5f44d418bf3cdf51f8a95fbad1941f256ca))
* **history_add:** no walk over a dated list for an entry just read ([#121](https://github.com/happy-dwarf-team/bellows/issues/121)) ([a06f2ca](https://github.com/happy-dwarf-team/bellows/commit/a06f2ca6b7ea5c581a3f92066693866e2e3fc2ba))
* **pops_pacing:** the online-service pump every 500 ms at the fastest speed ([#119](https://github.com/happy-dwarf-team/bellows/issues/119)) ([e84eba5](https://github.com/happy-dwarf-team/bellows/commit/e84eba58255e193dcea0014aff418858397e2dc8))
* **verifypath_listing:** verifypath_session keeps the listings past the main menu ([#120](https://github.com/happy-dwarf-team/bellows/issues/120)) ([5221060](https://github.com/happy-dwarf-team/bellows/commit/5221060a06478ab7c66271b6fe93384ce560e3b3))


### Bug Fixes

* read the mods from the folder the game was started with ([#115](https://github.com/happy-dwarf-team/bellows/issues/115)) ([ee28ea7](https://github.com/happy-dwarf-team/bellows/commit/ee28ea7815a03e43acdab2d4383986b9200c13c6))

## [1.1.0](https://github.com/happy-dwarf-team/bellows/compare/v1.0.0...v1.1.0) (2026-11-04)


### Features

* **transfer_skip:** load a text save in single player without its transfer copy ([#112](https://github.com/happy-dwarf-team/bellows/issues/112)) ([31e1ae3](https://github.com/happy-dwarf-team/bellows/commit/31e1ae3511246d9cb8543bef789c5566f4564324))


### Bug Fixes

* read the mods of a GOG install from its own user folder ([#111](https://github.com/happy-dwarf-team/bellows/issues/111)) ([f3f7901](https://github.com/happy-dwarf-team/bellows/commit/f3f7901b93086afb347f4548a571c07e64e8036a))

## [1.0.0](https://github.com/happy-dwarf-team/bellows/compare/v0.19.0...v1.0.0) (2026-11-04)


### Bug Fixes

* **ram_prefetch:** wake every waiting worker when prefetch stops at ready ([#102](https://github.com/happy-dwarf-team/bellows/issues/102)) ([24972e5](https://github.com/happy-dwarf-team/bellows/commit/24972e5f931d16010d95b9e0186e605f4726522c))


### Documentation

* say Bellows is 1.0 in the README status and the design notes ([#104](https://github.com/happy-dwarf-team/bellows/issues/104)) ([ca61697](https://github.com/happy-dwarf-team/bellows/commit/ca61697c8f2ea939e1792d8146af2025e46a653b))

## [0.19.0](https://github.com/happy-dwarf-team/bellows/compare/v0.18.0...v0.19.0) (2026-11-02)


### ⚠ BREAKING CHANGES

* **symbols:** Bellows needs Anvil 1.7.0 or newer (anvil = ">=1.7.0"); an older Anvil rejects the `builds` key and skips the plugin.

### Features

* **symbols:** run on the GOG build 1.37.5.2 ([#94](https://github.com/happy-dwarf-team/bellows/issues/94)) ([c5d0530](https://github.com/happy-dwarf-team/bellows/commit/c5d053069d94d715cfe13dbc3a2f42963310c210))

## [0.18.0](https://github.com/happy-dwarf-team/bellows/compare/v0.17.0...v0.18.0) (2026-11-02)


### Features

* **save_compress:** compress saves without the game's byte-at-a-time CRC (on by default), optionally in parallel ([#85](https://github.com/happy-dwarf-team/bellows/issues/85)) ([95de7c6](https://github.com/happy-dwarf-team/bellows/commit/95de7c695d10151f11f83a4e6d04d89b5a38b1b7))

## [0.17.0](https://github.com/happy-dwarf-team/bellows/compare/v0.16.0...v0.17.0) (2026-11-01)


### Features

* **monthly_probe:** the first day of a month: segments, daily country parts and the call sites of the modifier calculation ([#80](https://github.com/happy-dwarf-team/bellows/issues/80)) ([2ff0c4c](https://github.com/happy-dwarf-team/bellows/commit/2ff0c4c0bb115e901842bfde906fc3e9401e77ba))
* **province_modifier_view:** read a province's modifier value without copying its list ([#82](https://github.com/happy-dwarf-team/bellows/issues/82)) ([07fcb65](https://github.com/happy-dwarf-team/bellows/commit/07fcb6573784a9dcde98e35816775d51c1b56761))

## [0.16.0](https://github.com/happy-dwarf-team/bellows/compare/v0.15.0...v0.16.0) (2026-11-01)


### Features

* **modifier_merge:** index the large modifier-list merges of 0x8EA420, on by default ([#78](https://github.com/happy-dwarf-team/bellows/issues/78)) ([44a841f](https://github.com/happy-dwarf-team/bellows/commit/44a841f678664701d1cd0346f7e8f04e407c0165))
* **monthly_probe:** diagnostic counters for the monthly tick ([#76](https://github.com/happy-dwarf-team/bellows/issues/76)) ([f3d136a](https://github.com/happy-dwarf-team/bellows/commit/f3d136a1bef439d5f2c4998571d5276232f276db))

## [0.15.0](https://github.com/happy-dwarf-team/bellows/compare/v0.14.0...v0.15.0) (2026-10-31)


### Features

* **frame_pacing:** on by default ([#73](https://github.com/happy-dwarf-team/bellows/issues/73)) ([cc7e4e0](https://github.com/happy-dwarf-team/bellows/commit/cc7e4e0221ba72f54f3131911ae30947b01f92c2))

## [0.14.0](https://github.com/happy-dwarf-team/bellows/compare/v0.13.0...v0.14.0) (2026-10-28)


### Features

* **metascript_blocks:** resolve [[param] ...] blocks of scripted effects in one pass ([#69](https://github.com/happy-dwarf-team/bellows/issues/69)) ([f44ca47](https://github.com/happy-dwarf-team/bellows/commit/f44ca47dffa8377fa1e4827a5ea91dbb4ce927da))


### Performance

* **metascript_blocks:** move a sampled comparison off slow calls ([#71](https://github.com/happy-dwarf-team/bellows/issues/71)) ([d1d0668](https://github.com/happy-dwarf-team/bellows/commit/d1d0668b31f7bb82dcb719fde79ec946a0f41e3f))

## [0.13.0](https://github.com/happy-dwarf-team/bellows/compare/v0.12.0...v0.13.0) (2026-10-28)


### Features

* **frame_pacing:** react at once to a waiting key press or click ([#67](https://github.com/happy-dwarf-team/bellows/issues/67)) ([1d16602](https://github.com/happy-dwarf-team/bellows/commit/1d166023b1b83bf3bc65cb2da9d156516b8b56d1))

## [0.12.0](https://github.com/happy-dwarf-team/bellows/compare/v0.11.0...v0.12.0) (2026-10-27)


### Features

* **army_search_profile:** diagnostic counters of the AI army search ([#65](https://github.com/happy-dwarf-team/bellows/issues/65)) ([b511467](https://github.com/happy-dwarf-team/bellows/commit/b511467dd55b215e474f0b79bb8f7863e0814f0a))
* **frame_pacing:** draw frames inside the daily tick less often at the fastest speed ([#61](https://github.com/happy-dwarf-team/bellows/issues/61)) ([a4614ee](https://github.com/happy-dwarf-team/bellows/commit/a4614ee41183282b80602a131c822718c5996091))
* **range_witness:** write the first verify mismatches to range_witness_mismatches.tsv ([#62](https://github.com/happy-dwarf-team/bellows/issues/62)) ([e232667](https://github.com/happy-dwarf-team/bellows/commit/e232667add6c27f6759ed47493f969d3f7052450))
* **score_probe:** diagnostic memo counters for the AI scoring functions ([#66](https://github.com/happy-dwarf-team/bellows/issues/66)) ([8d8edbf](https://github.com/happy-dwarf-team/bellows/commit/8d8edbf9401b1887f04e0c53b7dded82c536dbbd))
* **tick_profile:** diagnostic profile of the daily tick's stages ([#64](https://github.com/happy-dwarf-team/bellows/issues/64)) ([3c71a45](https://github.com/happy-dwarf-team/bellows/commit/3c71a4507ef1bfb8067112a98b0ec7b8dd4c2dde))

## [0.11.0](https://github.com/happy-dwarf-team/bellows/compare/v0.10.0...v0.11.0) (2026-10-26)


### Features

* **mimalloc:** use mimalloc only until the main menu by default ([#59](https://github.com/happy-dwarf-team/bellows/issues/59)) ([943206b](https://github.com/happy-dwarf-team/bellows/commit/943206b0c684d87b45f034d1db9e761e56802b18))

## [0.10.0](https://github.com/happy-dwarf-team/bellows/compare/v0.9.0...v0.10.0) (2026-10-26)


### Features

* report Bellows' fields to Anvil's start-up benchmark ([#51](https://github.com/happy-dwarf-team/bellows/issues/51)) ([261eed0](https://github.com/happy-dwarf-team/bellows/commit/261eed00640bcabf5cbd7e89df974984d822c7b7))


### Build

* take the Anvil SDK from the 1.4.0 release ([#49](https://github.com/happy-dwarf-team/bellows/issues/49)) ([e0205e7](https://github.com/happy-dwarf-team/bellows/commit/e0205e772b47d103da07b9a86ddd3df69910d774))

## [0.9.0](https://github.com/happy-dwarf-team/bellows/compare/v0.8.0...v0.9.0) (2026-10-21)


### Features

* publish Bellows as a Steam Workshop mod ([#32](https://github.com/happy-dwarf-team/bellows/issues/32)) ([e7280ef](https://github.com/happy-dwarf-team/bellows/commit/e7280efd3e19db5bf789a188ef553a7ecad971c6))

## [0.8.0](https://github.com/happy-dwarf-team/bellows/compare/v0.7.0...v0.8.0) (2026-10-20)


### ⚠ BREAKING CHANGES

* Bellows now needs Anvil 1.0 (plugin API 2, anvil >= 1.0.0, the anvil-game plugin); Anvil 0.2 no longer loads it. config.toml and the EU4SB_<NAME> variables are no longer read: settings live in anvil/data/bellows/settings.toml (migrated from config.toml on the first start) and the overrides are ANVIL_BELLOWS_<NAME>.

### Features

* migrate to Anvil plugin API 2 ([#29](https://github.com/happy-dwarf-team/bellows/issues/29)) ([49fbd69](https://github.com/happy-dwarf-team/bellows/commit/49fbd698e2f3fb1b3fb51d5775e96e55a292c1df)), closes [#27](https://github.com/happy-dwarf-team/bellows/issues/27)


### Build

* take the Anvil SDK from the 1.0.0 release ([#31](https://github.com/happy-dwarf-team/bellows/issues/31)) ([7112b43](https://github.com/happy-dwarf-team/bellows/commit/7112b43dcc60742ada166d8a071fa8431e6f8b13)), closes [#28](https://github.com/happy-dwarf-team/bellows/issues/28)

## [0.7.0](https://github.com/happy-dwarf-team/bellows/compare/v0.6.0...v0.7.0) (2026-10-18)


### Features

* **range_profile:** diagnostic counters for the country range check ([#22](https://github.com/happy-dwarf-team/bellows/issues/22)) ([2d3cb86](https://github.com/happy-dwarf-team/bellows/commit/2d3cb86beff06a5191b6d519f29c16f26325abec))
* **range_witness:** on by default ([#25](https://github.com/happy-dwarf-team/bellows/issues/25)) ([5193824](https://github.com/happy-dwarf-team/bellows/commit/5193824ce35686a2f761e5144381ec997427ab26))


### Performance

* **range_witness:** answer the country range check from the last in-range province ([#24](https://github.com/happy-dwarf-team/bellows/issues/24)) ([2ea17d4](https://github.com/happy-dwarf-team/bellows/commit/2ea17d4bc525042a57f9ea51e67c90b43ecb9eb5))

## [0.6.0](https://github.com/happy-dwarf-team/bellows/compare/v0.5.0...v0.6.0) (2026-10-16)


### ⚠ BREAKING CHANGES

* the plugin id, folder and DLL are now bellows; delete anvil/plugins/eu4-startup-boost/ when updating.

### Features

* rename EU4 Startup Boost to Bellows ([#20](https://github.com/happy-dwarf-team/bellows/issues/20)) ([e505902](https://github.com/happy-dwarf-team/bellows/commit/e50590242b03874a41d9745dd9395a0eff37ba8e))

## [0.5.0](https://github.com/happy-dwarf-team/eu4-startup-boost/compare/v0.4.0...v0.5.0) (2026-10-16)


### Features

* **token_dict_cache:** cache the tokenizer's keyword lookups, on by default ([#18](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/18)) ([b505985](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/b5059854e6e171db18d1346c28bdc105829e380c))
* **token_dict_profile:** diagnostic counters for the tokenizer's keyword dictionary ([#17](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/17)) ([6c8bc1c](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/6c8bc1ccdb4900aeae2eadbf8e5d31bab332be66))

## [0.4.0](https://github.com/happy-dwarf-team/eu4-startup-boost/compare/v0.3.0...v0.4.0) (2026-10-16)


### Features

* **trigger_intern:** enable by default ([#14](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/14)) ([d42d1f2](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/d42d1f21ea34f2cf47dcf6484d895f262a6cd966))


### Performance

* **trigger_intern:** cut the hook's per-call overhead ([#13](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/13)) ([474bca5](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/474bca51543dc8bd090dc3160e23a797b03d5cba))
* **trigger_intern:** faster index for a non-empty map ([#12](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/12)) ([a57751e](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/a57751ea41184d9d450c33bfc52c53cb0b048a3f))

## [0.3.0](https://github.com/happy-dwarf-team/eu4-startup-boost/compare/v0.2.0...v0.3.0) (2026-10-16)


### Features

* **ram_prefetch:** configurable memory budget, default 1 GB ([#9](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/9)) ([a6f6679](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/a6f6679d8a6aa16ee5a4228648a4216234141c32))

## [0.2.0](https://github.com/happy-dwarf-team/eu4-startup-boost/compare/v0.1.0...v0.2.0) (2026-10-15)


### Features

First public release. Each module is `on`, `off` or `verify` in
`anvil/data/eu4-startup-boost/config.toml` (created on first start):

* **mimalloc:** faster memory allocator for the game's C runtime
* **verifypath_listing:** answers PhysFS path checks from directory listings
* **exists_index:** answers "does this file exist" for the modifier icon check
* **metascript_subst:** single-pass `$parameter$` substitution in scripted effects
* **trigger_intern:** alternative trigger interning (off by default: slower than the game's own code with the shipped trigger profile)
* **open_warmup:** opens the files the game will need ahead of it on parallel threads, from a profile learned per mod set
* **ram_prefetch:** reads and unpacks the DLC archive members the game will need into RAM on worker threads (learned per mod set; up to ~1.5 GB RAM during start-up)
* **attr_cache:** remembers "file not found" answers for game and mod files during start-up (off by default)

Lab measurement (EU4 1.37.5 Steam, 10 UI/graphics/music mods, warm start to the main menu, 3 interleaved starts each): 34.96 s without plugins, 15.40 s with the first five modules; 12.24 s median with `ram_prefetch` in a separate series (15.16 s with it off). Game logs identical with and without the plugin; a verify run compared 3.6 million answers with the game's own code, 0 differences. Requires Anvil 0.2.0 or newer; supported game build: EU4 1.37.5 (Steam, Windows 64-bit), other builds are skipped by Anvil.


### Build

* **plugin:** require Anvil 0.2.0 or newer ([#3](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/3)) ([c52b80d](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/c52b80db70cdf3e913354942f032d740fc4a3b04))
* read the version from version.txt only ([#1](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/1)) ([12de7a0](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/12de7a0c57e89e8dd3b8986c924e93c30e925a8d))


### CI

* automate releases with release-please and check PR titles ([#2](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/2)) ([1ce1600](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/1ce1600390bb60ea0371953c573396633d1897b4))
