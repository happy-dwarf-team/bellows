**Your game starts in about 13 seconds instead of 37. The forge gets hot sooner.**

![](https://i.imgur.com/Uf8Z21n.png)

## Overview
EU4 spends a long time staring at its own files before you get to the main menu. Bellows helps it along: it fetches what the game is about to need before the game asks, and answers the boring questions faster. Nothing in your game is changed, it just starts sooner.

On our test machine (10 mods) the trip to the main menu went from about 37 seconds to about 13. Your mileage may vary, but it should not get worse, and you can measure it on your own computer (see below).

![](https://i.imgur.com/Uf8Z21n.png)

## Before you subscribe
- **[Anvil](https://anvil.happydwarf.dev/) 1.3 or newer** must be installed in the game folder (1.5 or newer if you want to measure the gain yourself). Get it from [anvil.happydwarf.dev](https://anvil.happydwarf.dev/) and press Download. The Workshop can't install it for you: a dwarf has to carry it in by hand, once.
- **EU4 1.37.5** (Steam, Windows). On any other version Bellows politely stays out of the way and the game starts as usual.

![](https://i.imgur.com/Uf8Z21n.png)

## Getting it running
1. Subscribe and enable Bellows in your playset.
2. Start the game. Anvil will tell you a new plugin is waiting: press **Ctrl+Shift+F12**, open **Mods** and approve Bellows. You can also approve it before playing, in Anvil Manager.
3. Restart. Enjoy the shorter wait.

Anvil asks again after every update. Until you approve, the version you trusted before keeps working.

![](https://i.imgur.com/Uf8Z21n.png)

## Measure it yourself
Don't take our word for it. With Anvil 1.5 or newer and Bellows enabled and approved, open Anvil Manager (`AnvilManager.exe` in the game folder), go to the **Benchmark** tab, pick Bellows and start the series. The game then starts several times on its own, without Bellows and with it in turn, and closes each time it reaches the main menu. At the end you see the typical start time of both, side by side. Your saves, mods and settings stay as they were.

Keep Steam running and leave the computer alone while it works: about ten minutes with the default settings. Afterwards you can send the report to us, after seeing exactly what it contains ([what we collect](https://github.com/happy-dwarf-team/anvil/blob/main/docs/benchmark-privacy.md)), or copy it into a [performance report](https://github.com/happy-dwarf-team/bellows/issues/new?template=performance_report.yml). A dwarf trusts the scales, not the boasting.

![](https://i.imgur.com/Uf8Z21n.png)

## What it doesn't touch
- No game files: the files the game checks stay the same, so ironman achievements stay available.
- No gameplay changes, so it's fine in multiplayer.
- Unsubscribe and it's gone. Nothing is left in your saves.

![](https://i.imgur.com/Uf8Z21n.png)

## More from Happy Dwarf Team
- **[Anvil](https://anvil.happydwarf.dev/)**: the free loader that lets plugins like Bellows into EU4, with a Manager to approve them, keep them updated and measure what they give you.
- **[Tongs](https://steamcommunity.com/sharedfiles/filedetails/?id=3826695093)**: for mod testers and tool makers, let your scripts and AI agents see the game and press its buttons.
- **[Etch](https://etch.happydwarf.dev/)**: for interface modders, see your `.gui` window as the game draws it, right in VS Code.
- Everything we make: [happydwarf.dev](https://happydwarf.dev/)

![](https://i.imgur.com/Uf8Z21n.png)

### Alternative download
[GitHub](https://github.com/happy-dwarf-team/bellows)

![](https://i.imgur.com/Uf8Z21n.png)

## FAQ

### Why do I have to approve it?
Bellows is a real program running inside your game, not just a pile of text files. Anvil makes sure nothing like that runs without your nod. Trust, but verify. Dwarf law.

### My game didn't get any faster.
Some of Bellows' tricks learn what your mod set needs during one start and pay off from the next, so give it two runs. Still slow? The **Mods** tab in Anvil's panel tells you if Bellows was skipped and why. For a fair number, run the benchmark from **Measure it yourself** above and attach the report to a [performance report](https://github.com/happy-dwarf-team/bellows/issues/new?template=performance_report.yml).

### Does it work with big overhaul mods?
Yes. It doesn't care what your mods contain, it only helps the game read them faster. Bigger mod sets usually gain the most.

### Will it break my saves or my achievements?
No. It changes how fast the game starts, not what the game does.

### What is that little dwarf head on the thumbnail?
If you check the mod's GitHub page, you will notice the repository belongs to Happy Dwarf Team.

I like dwarves.
I like being happy.
I am, in fact, a happy dwarf myself.

![](https://i.imgur.com/Uf8Z21n.png)

## Credits
**Anvil** for letting a dwarf into the forge.
**mimalloc** by Microsoft for the faster memory handling.
**Everyone who waited 37 seconds for the main menu**: never again.
