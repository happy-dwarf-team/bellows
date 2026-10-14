# EU4 Startup Boost

An [Anvil](https://github.com/happy-dwarf-team/anvil) plugin that makes
**Europa Universalis IV** start faster, without changing game files or game
behaviour.

> Status: design phase — see [docs/design.md](docs/design.md).

Measured on the development machine (EU4 1.37.5, Steam, 10 UI/graphics mods),
with the same techniques as a local research build:

| Case | Vanilla | With the techniques |
| --- | ---: | ---: |
| Warm start (to the main menu) | ~31 s | ~10 s |
| First start after a reboot | ~61 s | ~15 s |

Numbers will be re-measured for the plugin itself before any release.

Supported game build: EU4 1.37.5 (Steam, Windows 64-bit). Other builds are
skipped automatically by Anvil and the game starts normally.

## License

MIT — see [LICENSE](LICENSE). Unofficial community project, not affiliated with
Paradox Interactive.
