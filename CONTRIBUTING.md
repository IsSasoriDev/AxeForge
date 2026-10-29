# Contributing

Bug reports, test results from boards marked "not tested" in the README, and pull requests are all welcome.

## Before you start

Open an issue for anything bigger than a bug fix so the approach can be agreed first. Tuner behaviour and board support in particular are discussed in issues before code.

## Building

ESP-IDF v5.3.x, as in the README. Without a local toolchain, the Espressif Docker image works:

```bash
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.2 idf.py build
```

Other boards build from their `sdkconfig.defaults.<board>` as described in the README under Building.

## Tests

The host tests need `g++`, `make`, `libmbedtls-dev` and `libcjson-dev`:

```bash
make -C test
```

They cover the code that does not touch hardware: difficulty, coinbase and merkle building, template decoding, the payout check, SV2 framing and Noise. Add a test when you change any of that. CI runs them on every pull request, then builds the firmware for every board.

## Pull requests

- One change per PR. Base it on `main`.
- Title prefix: `fix:` for bugs, `enhance:` for features, `security:` for security fixes.
- Say in the description what the change does and why, in plain words.
- End with what you ran it on, for example "Tested on a NerdQaxe++ rev6 on v1.4.2". If you could not test on hardware, say so; a build-only PR is still useful.
- Keep the README's board table and the release notes in step if the change affects users.

## Code

C++17 on ESP-IDF, C for the SV2 component. Match the style of the file you are in. Comments explain why, not what. No new dependencies without discussion.
