# Security and legal policy

## Game files

- ARMR never ships, downloads, or links to game packages, assets, or game
  libraries. Users import their own legally obtained copies.
- Everything derived from a game stays out of git: packages, extracted
  payloads (`apps/`), save data, run logs and traces (`logs/`, `.runtime/`),
  and frame captures. `.gitignore` covers these paths; do not force-add them.
  The one exception is the handful of screenshots in `docs/media/` that the
  maintainer picked for the README; add new ones only with their approval.
- Do not paste game files, disassembly, logs, or screenshots into issues,
  pull requests, or hosted AI tools. Describe the failure using ARMR's own
  diagnostic lines instead.

## Running untrusted code

A game's native code runs inside the ARMR process. The JIT confines guest
loads and stores to the guest address space, and every host service a game
can reach goes through the import registry. That is a compatibility layer,
not a security sandbox: only run games from sources you trust.

## Online services

ARMR does not implement Google Play Services, billing, or device attestation,
and it does not try to bypass anti-cheat or anti-tamper protection. Using
ARMR with online games may violate their terms of service.

## Reporting a vulnerability

Open a GitHub security advisory on this repository rather than a public
issue.
