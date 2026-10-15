# Security policy

EU4 Startup Boost is an Anvil plugin: native code that runs inside the game
process and patches it in memory. Please report security problems privately,
not in public issues.

## Reporting a vulnerability

Use GitHub's private vulnerability reporting:
[Report a vulnerability](https://github.com/happy-dwarf-team/eu4-startup-boost/security/advisories/new).
Include the plugin and Anvil versions (from `anvil/anvil.log`), how to
reproduce the problem, and what an attacker gains.

This is a volunteer project; reports are handled on a best-effort basis. We
will confirm the report, agree on a fix and a disclosure date with you, and
credit you in the advisory unless you prefer otherwise.

## Supported versions

Only the latest release receives security fixes.

## In scope

- Memory safety of the patched game functions and the plugin's own code
  (archive reading in `zip_reader` / `ram_prefetch`, profile and
  `config.toml` parsing).
- Files read or written outside the game, mod and
  `anvil/data/eu4-startup-boost/` folders.
- Release integrity: release assets that do not match `SHA256SUMS.txt` or
  their build provenance attestation.

## Out of scope

- Problems in Anvil itself: report them to
  [Anvil](https://github.com/happy-dwarf-team/anvil/security/advisories/new).
- Vulnerabilities in Europa Universalis IV itself; report those to Paradox
  Interactive.
