# Bellows: repository rules

- Anvil plugin that makes EU4 1.37.5 start and run faster. Every module gives the game's own
  answers faster (`affects_gameplay = false` in `plugin/plugin.toml`); a module that could
  change the simulation is a different plugin, not a Bellows module. Design in
  [docs/design.md](docs/design.md).
- [CONTRIBUTING.md](CONTRIBUTING.md) lists what differs from Anvil's conventions: titles and
  module scopes, releases (a draft release that a human publishes), the build and tests, and
  `plugin/symbols.toml` with its lock checked against the real `eu4.exe`. Run its build and
  `ctest` before finishing a change; CI runs them.
- Performance claims follow the measurement rules in CONTRIBUTING.md: same save and mod set,
  interleaved runs, median and spread, and what was not measured.
- English in code and docs. Do not imply affiliation with Paradox Interactive.
- The agent owns the pull request flow: it works on a branch with Conventional Commits,
  pushes it and opens the PR as a draft. Once ready (green CI, the change tested as above,
  no blocking review findings, not put on hold by the user) it marks the PR ready and
  squash-merges it; GitHub does not allow auto-merge on a draft. Pull requests from other
  contributors get a review and a recommendation; the maintainer decides on merging them.
  Do not bypass CI or branch protection, and do not change repository settings beyond what
  auto-merge needs.
