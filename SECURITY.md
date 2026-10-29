# Security

AxeForge talks to mining pools over the internet, serves a web UI on your LAN and accepts firmware updates over HTTP, so bugs in those paths can matter beyond a lost share.

## Reporting a vulnerability

Please do not open a public issue for a security problem. Use GitHub's private reporting form instead:

https://github.com/IsSasoriDev/AxeForge/security/advisories/new

Include the firmware version, the board, and what a pool, a device on the LAN or a crafted input can make the miner do. A proof of concept against your own miner is welcome; please do not test against other people's.

You should hear back within a week. Fixes ship in the next release and the report is credited in the release notes unless you ask otherwise.

## Scope

In scope: anything reachable from a pool connection (Stratum V1, V1+TLS, V2), from the web UI and API, from the OTA update path, from Swarm, and from the setup access point.

Out of scope: physical access over USB, which can rewrite the flash by design, and issues in ESP-IDF itself (report those to Espressif).

## Supported versions

Only the latest release is supported. Older releases are not patched; update instead.
