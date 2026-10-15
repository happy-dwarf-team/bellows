# Contributing to EU4 Startup Boost

The conventions are the same as in
[Anvil](https://github.com/happy-dwarf-team/anvil/blob/main/CONTRIBUTING.md);
this file lists what differs.

## Pull requests and titles

Every change goes to `main` through a squash-merged pull request whose title is
a [Conventional Commit](https://www.conventionalcommits.org/) (checked by the
`pr-title` workflow); the title becomes the commit on `main` and the changelog
entry. Types and their effect on the version:

| Type                                  | Release | Changelog                          |
| ------------------------------------- | ------- | ---------------------------------- |
| `feat`                                | minor   | Features                           |
| `fix`, `perf`, `revert`               | patch   | Bug Fixes / Performance / Reverts  |
| `build`                               | patch   | Build                              |
| `docs`, `refactor`, `test`, `ci`, `chore` | none | hidden                           |

A release is proposed only when the changelog would get a visible entry;
commits of hidden types alone never start one and ship with the next release.

Scopes: the module name (`ram_prefetch`, `attr_cache`, `open_warmup`,
`exists_index`, `trigger_intern`, `metascript_subst`, `mimalloc_heap`,
`verifypath_listing`, `zip_reader`, …), `config`, `plugin` (manifest,
packaging), `profiles`. A module that is off by default and changes game
behaviour when enabled is still a `feat`; say so in the description. Breaking
changes (a config key renamed or removed, a different Anvil requirement) take
`!` and a `BREAKING CHANGE:` paragraph; before 1.0.0 they bump the minor
version.

Performance claims in a pull request follow the measurement rules of the lab:
same save/mod set, interleaved runs, median and spread, and what was not
measured.

## Versions and releases

[release-please](https://github.com/googleapis/release-please) maintains a
`chore(main): release x.y.z` pull request that bumps `version.txt`, the
`version` in `plugin/plugin.toml` and `CHANGELOG.md`. Merging it tags `vX.Y.Z`
and creates the GitHub release; the release workflow then builds the tag and
attaches `eu4-startup-boost-windows-x64.zip` (the `eu4-startup-boost/` plugin
folder) and `SHA256SUMS.txt` with a build provenance attestation.

Do not edit `version.txt`, the manifest `version` or `CHANGELOG.md` by hand;
CMake refuses to configure when `version.txt` and `plugin/plugin.toml`
disagree. To force a version, put `Release-As: x.y.z` in a commit body.

## Building and testing

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
