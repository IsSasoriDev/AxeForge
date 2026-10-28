## AxeForge v1.4.2

### New, and not in AxeOS or NerdOS
- **Per-chip clocks**: after the tuner locks, each chip that can go faster gets its own higher clock (12 MHz steps, up to +48 MHz), judged by its own error count. A chip that slips later goes down a step on its own instead of taking the whole chain with it. Boards with more than one chip; re-tune to use it.
- **Tuning goals**: most hashrate, most hashrate under a power limit you set, or best efficiency (J/TH).
- **Electricity price mode**: stops mining while power costs more than your limit. Energy-Charts day-ahead prices for European bidding zones (no key needed), or any URL that returns a price in JSON.
- **Power supply check**: how far the input voltage drops under load, an estimate of supply-plus-cable resistance, and a warning before a weak supply starts causing faults.
- **Update rollback**: a web update that crashes at start-up twice, or doesn't mine within 10 minutes, goes back to the previous firmware by itself. Works with the stock Bitaxe bootloader too.

Tested on a NerdQaxe++: update rollback (a deliberately crashing update went back to the previous firmware on its own after two crashes; a good one was kept after 2 minutes of mining), the power supply check, and the price mode (Energy-Charts, three stop/start cycles back to full hashrate). Per-chip clocks and the power-limit and efficiency goals are new in the tuner and still being tested. None of these have run on a Bitaxe yet.

### Bitaxe Supra tested
The Bitaxe Supra (v1.4.1) has now been run on hardware and is marked as tested in the README and the web flasher.

### Fixes
- NerdQAxe family: when a chip missed the count while powering back up (after a fault or a price pause), it kept hashing without an address of its own and clashed with chip 1 (repeated nonces, a nonsense hashrate). The miner now power-cycles and counts again, up to 3 times, before running with fewer chips.

### What AxeOS and NerdOS had, now in AxeForge too
- **Notifications** to Discord and Telegram: found block, new best share, faults and power cuts, no pool for 5 minutes, miner started (*System → Notifications*).
- **Two-factor login** (optional): changes need a 6-digit code from an authenticator app. Setup shows a QR code; a login lasts 12 hours.
- **Self-test**: sensors, regulator, chips and fans, then a minute of hashrate, error and power measurement, while mining.
- **Best shares**: the 10 best, with time, pool and chip, kept across reboots.
- **24-hour charts**: hashrate, chip temperature and power, one point per 5 minutes.
- **Payout check that can act**: optionally send no work to a pool whose coinbase doesn't pay your address or keeps more than your fee limit.
- **Wi-Fi scan** on the Network page, and **Bluetooth setup** with the free "ESP BLE Provisioning" app while the setup network is open.
- **Ethernet** with the W5500 adapter on the NerdQAxe-family header, found at boot.
- **Accessory port** on the Bitaxe (AxeOS's BAP protocol), for displays and controllers made for AxeOS.
- **Advanced chip timing**: job interval, and version-rolling speed on BM1370 boards.
- **API documentation**: `docs/openapi.yaml` describes every endpoint.

Tested on a NerdQaxe++: self-test, two-factor login, best shares, Wi-Fi scan, live version-rolling change. Not tested on hardware yet: Bluetooth setup, Ethernet (no adapter here), the accessory port, and message delivery to Discord/Telegram.

### NerdOctaxe-γ with 6 phases
Boards with the TPS53667 regulator (rev3.0 and later, found by the GPIO3 strap) now start at the stock firmware's 6-phase point, 700 MHz / 1210 mV, unless you have set your own.

### Bitaxe Ultra Hex
| Board | Chips | Notes |
|---|---|---|
| **Bitaxe Ultra Hex** 302/303 | 6× BM1366 | Same board as the Supra Hex with the older chip: three stacked domains, TPS546, 128×32 OLED. Stock point 485 MHz / 1200 mV, limits 575 MHz and 90 W. **Not tested on hardware yet** |

Install it with the web flasher like the other Bitaxes; AxeOS's Wi-Fi, pool and clock settings carry over. If you run one, please open an issue with your log (*System → Log*), working or not.

- BM1366 chains now space chip addresses 256 / chips apart, as the reference firmware does. Nothing changes on the NerdAxe (one chip).

### New web flasher layout
Pick your miner from a compact grid, filtered by family or searched by name, chip or board number (try `303`), then install from one panel that also lists the board's file names. Links like `…/AxeForge/#ultrahex` open with that board chosen.

### Web UI on phones (@cbyam, #31)
All eight tabs now fit across the bottom bar on a phone, the Swarm table keeps to name, hashrate, temperature and power, and the bar clears the iPhone home indicator. Wider screens are unchanged.

### Also in this release (from v1.4.1)
- Bitaxe Supra, Gamma and Naja Duo as full firmware, Nexus S1 (experimental), the Ember theme, the hostname setting and the security fixes reported by @cbyam.

### Files
- **Web flasher**: https://issasoridev.github.io/AxeForge/. Keeps your settings unless you tick *Erase*.
- Each board has an update file (`axeforge-<board>.bin`, for *System → Firmware* in the web UI) and a full image (`axeforge-<board>-factory.bin`, resets settings). The NerdQaxe++ keeps the plain names `axeforge.bin` / `axeforge-factory.bin`.
- Board names in the files: `nerdaxe`, `nerdqaxeplus`, `nerdoctaxeplus`, `nerdoctaxegamma`, `nerdqx`, `suprahex`, `ultrahex`, `supra`, `gamma`, `naja`. A file for another board is refused.
