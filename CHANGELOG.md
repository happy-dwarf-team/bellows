# Changelog

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
