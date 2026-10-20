# Bellows

**Faster Europa Universalis IV start-up, without changing game files or game behaviour.**

Bellows is a plugin for [Anvil](https://github.com/happy-dwarf-team/anvil), the EU4 plugin loader. It does the game's own start-up work faster: a faster memory allocator, file checks answered from directory listings, and the files and DLC archive members the game will need read ahead of it on worker threads, from a profile it learns for your mod set. In our measurements (EU4 1.37.5, 10 mods) the start to the main menu took about 15 seconds instead of 35. The game's logs are the same with and without it.

This mod holds only the plugin: no game files. It does not change the game's checksum, so ironman achievements stay available.

## Requirements

- EU4 1.37.5 (Steam, Windows 64-bit). On another game build Anvil skips Bellows and the game starts as usual.
- **Anvil 1.3 or newer installed in the game folder.** The Workshop cannot install Anvil for you: follow the [Anvil install guide](https://github.com/happy-dwarf-team/anvil#install--uninstall) first.

## Approving the plugin

Bellows is native code, so Anvil does not load it until you approve it:

1. Subscribe, enable the mod in the launcher's playset and start the game.
2. Anvil shows a notice: open its panel with Ctrl+Shift+F12, go to the **Mods** tab and approve Bellows. You can also approve it before you start, in Anvil Manager's **Mods** tab.
3. Bellows loads from the next start.

Anvil asks again for **each new version**. Until you approve an update, the version you approved before keeps running. If you also installed Bellows by hand in anvil/plugins, that copy wins over the mod.

## Links

- [Source, settings and releases](https://github.com/happy-dwarf-team/bellows)
- [Report a problem](https://github.com/happy-dwarf-team/bellows/issues)

Unofficial community project, not affiliated with Paradox Interactive.
