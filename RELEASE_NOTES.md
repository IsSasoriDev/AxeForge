# Release notes

Newest first. Each release is a `## AxeForge vX.Y.Z` section; the release workflow puts that section on the GitHub release page. Changes that are merged but not yet released go under **Unreleased**.

## Unreleased

### Network
- **The miner joins the strongest access point of its network, not the first to answer.** With a mesh or an extender broadcasting the same name, a miner could sit on a far radio at -80 dBm with a near one at -50 dBm in the same room. It now scans every channel and picks by signal, and the log says which access point and channel it joined.

## AxeForge v1.4.5

### Fixes
- **Bitaxe GT: the core voltage no longer drops seconds after power-up.** The GT, Gamma, Gamma Duo, Naja Duo and Gamma Hex now switch their regulator the way AxeOS does: by the PMBus OPERATION command alone. AxeForge also told the TPS546 to obey its CONTROL pin, and on a GT that cut the core during the clock ramp ("Core voltage lost (0.02 V of 1.21 V)") and kept it off on every retry. The Supra Hex keeps its tested setting.
- When the core voltage is lost, the log now shows every status register of the regulator, so the next report says why.

### New
- **Clean-heatsink reminder.** Over its first week the miner learns how warm the chips run and how hard the fan works at its usual power. After that it compares every week with the clean one, with the outdoor temperature from the weather page taken out, and tells you when dust is the likely cause: a Discord/Telegram message, *CLEAN HEATSINK* in place of *MINING* on the screen, and a card on the dashboard with a *Done, I cleaned it* button that starts the learning again.
- **Spatial theme.** Deep space for the web UI and the screen: a near-black indigo sky with a slow-twinkling starfield, nebula glows, a ringed planet off the corner, glass cards (see-through with a thin outline) and starlight, violet and sky-blue accents. *Display → Theme*. On the screen the stars sit behind the cards; on the 128x32 OLED boards themes don't apply.
- **Power bill & odds.** A dashboard card with the energy used today and since you started, what it cost at your electricity price (set it under *Tuning → Electricity price*, or the market price from the price mode is used), the cost per day at this power, and your chance of finding a block today, this month and this year at your hashrate.

### Overclocking, opt-in (#33) on per-board limits (#35)
- **Every board has a stock profile and, where it's known, an envelope.** The stock profile is the limits it always had (clock, voltage, tuner power ceiling). The envelope is its measured headroom above them, in `boards/<id>.toml` under `[envelope]`. Only the NerdQaxe++ rev5.1/6 (+10 % power, +50 mV, +50 MHz) and the Nexus S1 (+8 %, +50 mV, +25 MHz) have one so far, from @cbyam's measurements; a board without one can't be overclocked.
- **Tuning → Overclock** unlocks it after you type OVERCLOCK. The tuner may then climb to the envelope and the manual values may go past stock; an orange banner says so on every page for as long as it's on. Temperature ceilings and every emergency cut-off stay exactly where they were (the power cut-off stays at 115 % of stock). A weak power supply pauses it until the supply check is fine again. Turning it on or off starts a new tuner search.
- **Exact manual values.** Frequency and voltage have number fields next to the sliders, in 1 MHz and 5 mV steps, so a tuned point like 693 MHz can be typed in.

### Also fixed
- **The setup network after 5 minutes without Wi-Fi now actually appears.** Starting it (and Bluetooth setup with it) ran on the system event task, whose stack is too small for that, so the miner panicked and rebooted at the 5-minute mark instead, every 5 minutes, without ever showing `AxeForge-XXXX`. Found on a NerdQaxe++ rev6 with a router that refused the connection.
- **The Ethernet row on the Network page only appears when an adapter is actually fitted.** It showed on every NerdQAxe-family miner because the header exists; now it follows the W5500 probe at boot, and reads "adapter fitted, no cable" when one is there but unplugged. `/api/status` gains `eth.present`.

### Web UI
- **System → Device shows the board**, and on the NerdQaxe++ the API's `board` now reads "NerdQaxe++ rev7 (TPS546)" rather than the revision alone, so Swarm names it like every other board.

### Dashboard
- **One chart instead of two.** The hour chart and the "Last 24 hours" card are one chart with hashrate, temperature and power to choose from, a 1 h / 24 h switch, and a crosshair that shows the value and how long ago it was. `/api/history` now returns temperature and power next to the hashrate.

### Build
- **One file per board.** What the firmware knows about a board (chips, pins, regulator, sensors, stock point) is in `boards/<id>.toml`, turned into the old `core/board_config.h` at build time by `tools/gen_board.py`. Same macros, same binaries; adding a board or changing one no longer means finding its `#elif` in a 300-line header and its stock point in `settings.cpp`. See `boards/README.md`.

Thanks to @cbyam for #52 to #57.

## AxeForge v1.4.4

### Tested on hardware
The NerdQAxe+, NerdOctaxe+ and NerdOctaxe-γ have now run AxeForge and are marked as tested in the README and the web flasher.

### Five more Bitaxes and the HX-6
| Board | Chips | Notes |
|---|---|---|
| **Bitaxe Max** 102 | 1× BM1397 | The first Bitaxe. Its chip doesn't roll version bits itself, so the ESP32 sends it four midstates per job, as AxeOS does. 425 MHz / 1400 mV stock, 25 W limit |
| **Bitaxe Ultra** 201–207 | 1× BM1366 | DS4432U (201–205) or TPS546 (207), found at boot, like the Supra. 485 MHz / 1200 mV, 25 W |
| **Bitaxe Gamma Duo** 650 | 2× BM1370 | Two chips in parallel on one 5 V TPS546. 400 MHz / 1150 mV (XP-binned chips), 40 W |
| **Bitaxe GT** 801 | 2× BM1370 | 12 V, two-phase TPS546 stack, a diode per chip on an EMC2103. 525 MHz / 1150 mV, 60 W |
| **Bitaxe Gamma Hex** 1300 | 6× BM1370 | 12 V, four-phase TPS546 stack, two stacked domains of three chips, color screen. 690 MHz / 1200 mV, 180 W |
| **HX-6** rev B | 6× BM1370 | Open-hardware board ([HX6-Miner](https://github.com/IsSasoriDev/HX6-Miner)): the NerdQaxe++'s power wiring with six chips on a 4-phase TPS53647, I2C on 8/9, its 2.8" SPI screen showing the compact screen layout, buttons on GPIO21 and BOOT. No Ethernet (the screen uses those pins) and no touch yet. 525 MHz / 1150 mV, 130 W |

Board numbers, regulator settings and stock points come from AxeOS's board definitions (the HX-6's from its design notes). None of these five has run AxeForge yet: if you have one, please flash it and open an issue with the log (*System → Log*), working or not. Per-chip clocks are on for the Gamma Duo and the GT (their chips share one voltage) and off on the Gamma Hex (stacked domains).

## AxeForge v1.4.3

### Safety
- **Per-chip clocks are off on boards with chips stacked in series**: the Supra Hex, Ultra Hex, Naja Duo and the NerdQaxe++ rev7. There, one chip clocked higher than its partner shifts the shared voltage onto the others. They stay on for boards whose chips share one voltage (NerdQaxe++ rev5.1/6, NerdQAxe+, NerdOctaxe, NerdQX).
- **Per-chip clocks judge each chip by its hashrate too**, not only by its errors: a chip that drops work at the higher clock without reporting errors goes back a step, during the search and later in the watch.

### Setup network and updates (#38)
- **A miner that has Wi-Fi saved no longer opens an open setup network after a few failed reconnects.** It waits 5 minutes (a router restart is shorter), then opens `AxeForge-XXXX` with WPA2 and a password of its own, shown on the screen with a QR code. On first setup, with no Wi-Fi saved yet, the network stays open as before.
- **On the setup network the miner only answers the Wi-Fi form**: status, network scan and the Wi-Fi/hostname fields. Pools, logs, firmware upload and every other setting need the miner on your network.
- **Bluetooth setup asks for the same password as its PIN** when Wi-Fi was saved before.
- **A web update without an AxeForge board tag is refused.** Before, only a file for a different board was.

### Tuner
- **Best efficiency goal** (#32): J/TH now comes from the regulator's output power where the board reports it, so a fan speeding up between steps no longer looks like a less efficient clock, and a step only counts as better or worse when it differs by more than 1%. The climb no longer stops early on noise.
- Switching to manual and back keeps the per-chip clocks.

### Fixes
- The price URL is no longer sent back by the web API or written to the log (it may carry a token).
- Swarm: a miner that stopped answering is asked every 5 minutes instead of every 15 seconds, which also ends the error lines it filled the log with.
- Price mode: turning it on, or changing its source, fetches the price at once and may stop mining at once.
- A failed power-up after a price pause tries again 5 times, then hands over to fault recovery with a fresh set of retries.
- Power supply check: the settle time after power-up also applies on boards that can't read their input voltage with the chips off.

### From contributors (@cbyam)
- Live logs show clock time (#37), the Paper theme fixes (#36), Stratum V2 Noise framing as SRI does it (#39), and the repository files: SECURITY.md, issue and PR templates, CONTRIBUTING.md, Dependabot (#43).

Tested on a NerdQaxe++ rev6: per-chip hashrate check (chip 4 went back to the chain clock on its own), swarm back-off, price URL hidden. The setup network and Bluetooth changes and the efficiency goal are not tested on hardware yet.

## AxeForge v1.4.2

### New
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

### now in AxeForge too
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

## AxeForge v1.4.1

### More Bitaxes, as full firmware
AxeForge now replaces AxeOS on a Bitaxe completely: install it with the web flasher like any other board. AxeOS's Wi-Fi, pool and clock settings carry over.

| Board | Chips | Notes |
|---|---|---|
| **Bitaxe Supra Hex** 701/702 | 6× BM1368 | Tested on a 702, now a full install as well |
| **Bitaxe Supra** 400–403 | 1× BM1368 | TPS546 (402/403) or DS4432U (400/401) found at boot. **Not tested on hardware yet** |
| **Bitaxe Gamma** 600–603 | 1× BM1370 | Chip temperature from its own diode. **Not tested on hardware yet** |
| **Bitaxe Naja Duo** 1201 | 2× BM1373 | New BM1373 driver, two stacked domains, color screen. **Not tested on hardware yet** |

If you run one of the untested boards, please open an issue with your log (*System → Log*), working or not.

Hex owners who installed 1.4 through AxeOS's update page can keep updating as before; flash the full image once to switch to the standalone layout.

### Also new (@cbyam, #27-#30)
- **Ember theme:** near-black with Bitcoin orange, for the web UI and both screens. Status colours keep their meaning (green, amber, red), and mempool blocks use mempool.space's fee colours (#29).
- **Hostname setting** on the Network page. Left empty, a miner flashed from stock keeps its old name. Swarm now shows other miners' hostnames and worker names (#28).
- **Nexus S1** (BitLuck, 4× BM1373), experimental: tested on one board, built from source with `sdkconfig.defaults.nexus`, no release file yet. Its BM1373 numbers (6912 cores, hash counter 0x90) also improve the Naja Duo support (#30).
- The version in the web UI's menu links to its source on GitHub (#27).

### Security fixes (reported privately by @cbyam, thank you)
Update soon if you mine on a pool you don't run yourself, or over plain (non-TLS) Stratum V1.
- **A pool could corrupt the miner's memory** with an invalid extranonce size. Extranonce sizes from the pool (Stratum V1 and V2) are now checked, a pool that sends a bad one is disconnected, and the coinbase builder never trusts the size either.
- **A pool could stop mining until a reboot** with a crafted coinbase. Every length in it is now checked against the data that's left.
- **Deeply nested JSON could crash the miner** (from a pool, a Swarm peer on the LAN, weather/mempool replies or the web API). All of it is now refused above 16 levels of nesting before it's parsed.

### Other fixes from a code review of 1.4.0
- NerdOctaxe-γ: the regulator setup from the GPIO3 strap is now checked against the regulator actually fitted; a mismatch refuses to power up instead of running a TPS53647 with a TPS53667's limits.
- After a fault retry, the auto-tuner no longer saves a clock it never ran.
- A fault flag from one power-up can't cut the next one.
- A failed core-voltage read no longer looks like a lost rail (and the NerdAxe without ADC calibration no longer reads 0 V).
- The payout check reads the pool username safely while the web UI saves it, and handles regtest taproot addresses.
- Board sensors are read once per second instead of twice on die-sensor boards.

### Also in this release (from v1.4.0)
- NerdQAxe+, NerdOctaxe+, NerdOctaxe-γ and NerdQX support, the solo payout check, the screen-off timer, Swarm scan fixes and safer fault recovery.

### Files
- **Web flasher**: https://issasoridev.github.io/AxeForge/. Keeps your settings unless you tick *Erase*.
- Each board has an update file (`axeforge-<board>.bin`, for *System → Firmware* in the web UI) and a full image (`axeforge-<board>-factory.bin`, resets settings). The NerdQaxe++ keeps the plain names `axeforge.bin` / `axeforge-factory.bin`.
- Board names in the files: `nerdaxe`, `nerdqaxeplus`, `nerdoctaxeplus`, `nerdoctaxegamma`, `nerdqx`, `suprahex`, `supra`, `gamma`, `naja`. A file for another board is refused.


**Full Changelog**: https://github.com/IsSasoriDev/AxeForge/compare/v1.4.0...v1.4.1

## AxeForge v1.4.0

### New boards
Four more boards from the NerdQAxe family, each with its own image and web flasher card. **They are new and not yet tested on hardware**, so please report how yours runs.

| Board | Chips | Notes |
|---|---|---|
| **NerdQAxe+** | 4× BM1368 | 2-phase TPS53647, 70 W limit |
| **NerdOctaxe+** | 8× BM1368 | 3-phase TPS53647, 130 W limit |
| **NerdOctaxe-γ** | 8× BM1370 | 4-phase TPS53647 (250 W) or, from rev3.0, 6-phase TPS53667 (300 W), detected at boot. Die temperature per chip where the board has the TMP451 sensors. |
| **NerdQX** | 4× BM1370 | Die temperature per chip, up to 1000 MHz. Without its die sensors it stays at 495 MHz / 1150 mV. |

- Coming from the stock NerdQAxe firmware, these boards keep your Wi-Fi and pool settings.
- On boards with die sensors, the chip temperature is the real die reading. Before the chips have power, or if a die reading drops out, the board sensors stand in.
- The screen and web UI show chip count and model per board. With 8 chips the chip page uses two rows.

### Bitaxe Supra Hex
The first Bitaxe: the **Supra Hex 701 / 702** (6× BM1368, three stacked voltage domains, 128×32 OLED), tested on a 702.
- Installed through AxeOS's own **Update** page (rename `axeforge-suprahex.bin` to `esp-miner.bin`). AxeOS stays on the miner: going back is one upload, and three crashed boots in a row start AxeOS by itself.
- Takes over AxeOS's Wi-Fi, pool and clock settings.

### Safer fault recovery (all boards)
Found while bringing up the Hex, where a chip that stops hashing now and then made every retry fail:
- A retry after a power cut ramped the chips from the clock they ran at before, so freshly reset chips jumped from ~56 MHz to full speed in one step. On the Hex that tripped the regulator's over-current shutdown every time. Chips now ramp up from their reset clock.
- A missing chip in a stacked voltage domain (Supra Hex, NerdQaxe++ rev7) is a fault, not a smaller miner: its partner would get the whole domain's voltage.
- New cut-offs: core voltage lost while powered, and no chip hashing at all while work is being sent.
- After a fault retry at a lower clock, neither manual mode nor the tuner goes straight back up.
- Chip numbers in fault messages match the screen and web UI (*ASIC 2 stopped hashing*), and Wi-Fi disconnects log their reason.

### Solo payout check
The miner reads every job's coinbase back into addresses and checks that a found block would pay the Bitcoin address in your pool username (the part before `.worker`).
- Dashboard → *Now mining*: the payout (for example *3.14033958 BTC to your address (100%)*) and the coinbase outputs.
- Each pool card: *pays you N%*, or a red *coinbase doesn't pay your address*. That's normal on a pooled account, but on a solo pool it means a block wouldn't pay you.
- The Blocks page on the screen shows it in green or red, and the log notes every change.
- P2PKH, P2SH, P2WPKH, P2WSH and P2TR on mainnet, testnet and regtest, over Stratum V1 and V2. Tested against real mainnet coinbases.

### Also new
- **Screen off after** a set idle time (Display tab). The first press wakes it; faults, overheating and a found block wake it by themselves (@cbyam, #26).
- **Swarm scan** finds every miner on the network and the web UI keeps answering while it runs (@cbyam, #23).
- The hashrate sits centred in the gauge on the small screen (@cbyam, #25).
- Unit tests run on every push, the Supra Hex code is build-checked, and compiler warnings are listed in the CI summary (@cbyam, #22).

### Also in this release (from v1.3.2)
- Fractional pool difficulty, version rolling within the pool's mask, SV2 reconnects on a silent pool and announces the real hashrate.
- Debounced cut-offs, the last emergency kept across restarts, safer overheat recovery, pool certificate date checks.
- Auto-tune fixes, power shown with two decimals.

### Files
- **Web flasher**: https://issasoridev.github.io/AxeForge/. Keeps your settings unless you tick *Erase*.
- Each board has an update file (`axeforge-<board>.bin`, for *System → Firmware* in the web UI) and a full image (`axeforge-<board>-factory.bin`, resets settings). The NerdQaxe++ keeps the plain names `axeforge.bin` / `axeforge-factory.bin`.
- Board names in the files: `nerdaxe`, `nerdqaxeplus`, `nerdoctaxeplus`, `nerdoctaxegamma`, `nerdqx`, `suprahex`. A file for another board is refused.
- Supra Hex: `axeforge-suprahex.bin` only (rename to `esp-miner.bin` and upload in AxeOS). No full image: it would replace the partition layout AxeOS needs.


**Full Changelog**: https://github.com/IsSasoriDev/AxeForge/compare/v1.3.2...v1.4.0

## AxeForge v1.3.2

Fixes from real boards, most of them contributed by @cbyam (#6, #12–#21). Thanks!

### Mining
- **Fractional share difficulty:** the pool's difficulty is no longer cut to a whole number (1500.7 → 1500), which submitted shares the pool then rejected. At low difficulties that was up to a third of shares (#12).
- **Version rolling honours the pool's mask:** the chips only roll the bits the pool allows, and rolled versions are rebuilt and submitted correctly (#20).
- **SV2 reconnects** when the pool goes silent or closes the channel, instead of hashing its last job forever. The idle limit is now 3 minutes for V1 and SV2, down from 10 (#18).
- **SV2 announces the board's own hashrate and chip**, so a NerdAxe no longer starts at a difficulty sized for 5 TH/s (#19).

### Safety
- The over-temperature and over-power cut-offs need two readings in a row, so one bad sensor read can't shut the miner down (#14).
- The last emergency and the readings that caused it survive a restart: they're shown in the log at boot and in `/api/status` (`lastEmergency`, `emergencies`) (#14).
- Overheat recovery tries at most 3 times, 30 s apart, and only saves the lower clock if the board actually came up. Before, failed attempts kept lowering the tuned clock (#13).
- Pool certificate dates are checked: expired SV2 certificates are refused, and TLS checks dates once the clock is set over NTP (#16).
- A bad mempool URL or Swarm address can no longer crash the miner on every boot (#21).

### Auto-tune
- No +10 mV added back when the voltage trim didn't lower anything. That could push the lock over the power cap.
- Retries lower when the start point fails, instead of locking a point it never measured.
- Steps fail if any one chip goes over 2% errors (3% once locked), not just the average (#15).

### Display and web UI
- Power is shown with two decimals (e.g. 97.25 W) on the screen, the web UI and Swarm.
- NerdQaxe++ rev5.1/6 now reports the raw board sensor; only rev7 adds the +10 °C die estimate (#6).
- Swarm asks known AxeForge peers on their own API first, so they no longer log a 404 every 15 s (#17).
- Fixed the *Chips* screen widget on the NerdAxe, which read past its single chip.

### Files
- **Web flasher**: https://issasoridev.github.io/AxeForge/. Keeps your settings unless you tick *Erase*.
- NerdQaxe++: **axeforge.bin** (web update) · **axeforge-factory.bin** (full image, resets settings)
- NerdAxe: **axeforge-nerdaxe.bin** (web update) · **axeforge-nerdaxe-factory.bin** (full image, resets settings)


**Full Changelog**: https://github.com/IsSasoriDev/AxeForge/compare/v1.3.1...v1.3.2

## AxeForge v1.3.1

**Same as v1.3.0, plus NerdAxe support** — it was meant to be in 1.3.0 and got left out.

### New board: NerdAxe
- 1× BM1366 on a LilyGo T-Display-S3: core voltage through the DS4432U/TPS40305, input power from the INA260, fan and temperature from the EMC2101, core voltage read back on the ADC.
- Its own firmware image (`axeforge-nerdaxe.bin` / `axeforge-nerdaxe-factory.bin`) and its own card in the web flasher. On this board the core power enable works the other way round from the NerdQaxe++, so the images are not interchangeable.
- Coming from the NerdQAxe stock firmware, it keeps your Wi-Fi and pool settings.
- Limits: 600 MHz, 1100–1300 mV, 15 W (the 5 V supply's limit).

### Safety
- A firmware upload built for a different board is now refused (each image carries its board name). Stock firmware can still be uploaded to go back.

### Also in this release (from v1.3.0)
- **Swarm:** every miner on your network in one place — AxeForge, AxeOS (Bitaxe), NerdQAxe stock firmware — with totals.
- **Themes:** *Forge* (dark neon) and *Paper*, a minimal e-ink style for the screen and the web UI.
- **Captive portal** on the setup Wi-Fi.
- **Fans:** both NerdQaxe++ headers detected, each fan *Auto* or *Manual*.
- Fixes: no sideways scrolling in the web UI, no stale web UI after updates, the broken `°` on the chip tile.

### Files
- **Web flasher** — https://issasoridev.github.io/AxeForge/ — keeps your settings unless you tick *Erase*.
- NerdQaxe++: **axeforge.bin** (web update) · **axeforge-factory.bin** (full image, resets settings)
- NerdAxe: **axeforge-nerdaxe.bin** (web update) · **axeforge-nerdaxe-factory.bin** (full image, resets settings)


**Full Changelog**: https://github.com/IsSasoriDev/AxeForge/compare/v1.3.0...v1.3.1

## AxeForge v1.3.0

### New
- **Swarm:** see every miner on your network in one place — AxeForge, AxeOS (Bitaxe) and NerdQAxe stock firmware. *Scan network* finds them; totals for hashrate, power and best difficulty. Readings no single small miner could produce are flagged as invalid and left out of the totals.
- **Themes:** *Forge* (dark neon) and the new *Paper* — a minimal, e-ink style for both the screen and the web UI (*Display → Theme*).
- **Captive portal:** joining the `AxeForge-XXXX` setup Wi-Fi opens the setup page automatically.
- **Fans:** both NerdQaxe++ fan headers are detected (`fanCount` and a `fans` list in `/api/status`, same shape as AxeOS). Each fan can be *Auto* or *Manual*; manual fans still go to 100 % if a sensor is lost or the chips near the ceiling.

### Fixes
- The web UI no longer scrolls sideways when a table or card is wider than the window.
- After a firmware update the browser always loads the new web UI instead of a cached old one.
- The chip temperature tile showed a broken `°` sign.

### Files
- **Web flasher** — https://issasoridev.github.io/AxeForge/ — keeps your settings unless you tick *Erase*.
- **axeforge.bin** — update from the web UI (*System → Firmware update*). Keeps your settings.
- **axeforge-factory.bin** — full image for `esptool.py write_flash 0x0`; a clean install that **resets all settings**.

Supports the NerdQaxe++ (rev5.1, rev6 and rev7) with the stock LilyGo T-Display-S3 or a 3.5" 480×320 screen.


**Full Changelog**: https://github.com/IsSasoriDev/AxeForge/compare/v1.2.0...v1.3.0

## AxeForge v1.2.0

### Install from the browser
- New **web flasher**: https://issasoridev.github.io/AxeForge/ — Chrome or Edge, USB cable, no tools. It keeps your Wi-Fi and pool settings unless you tick *Erase*.

### Boards
- **NerdQaxe++ rev5.1** is listed as supported. It uses the same TPS53647 regulator and code path as rev6, and the web UI now shows `rev5.1/6 (TPS53647)`.

### Safety
- **Chip stall guard:** a chip that falls far below the others for 15 s cuts power (`Chip N stopped hashing`).
- **Fan stall detection** (@cbyam): a fan that was spinning and drops under 300 rpm cuts power. Within 3 °C of a tuner ceiling the fans go to 100 %.
- **Fixed: automatic recoveries found no chips** (@cbyam). After a fault or overheat the chips came back at 115200 baud while the ESP32 stayed at 1 Mbaud, so every retry failed.
- The fan controller is re-initialised if a supply glitch resets it.

### Mining and pools
- **Stratum V2:** each frame goes out in one write, and `TCP_NODELAY` is set on pool sockets (@cbyam). Share response went from about 53 ms to 7 ms in testing.
- Results the chain reports twice are dropped instead of being submitted as duplicates (@cbyam). Counted as `dupResults` in `/api/status`.

### Web UI and logs
- Every change made through the API is logged with the client's address, and a requested restart is remembered across the reboot (`restartedBy` in `/api/status`) (@cbyam).
- Easier-to-read secondary text on the screen and in the web UI (@cbyam). Both fan speeds are shown.

### Files
- **Web flasher** — the easiest way to install or update over USB.
- **axeforge.bin** — update an AxeForge miner from the web UI (*System → Firmware update*). Keeps your settings.
- **axeforge-factory.bin** — full image for `esptool.py write_flash 0x0 axeforge-factory.bin`. This is a clean install: it **resets all settings**.

Supports the NerdQaxe++ (rev5.1, rev6 and rev7) with the stock LilyGo T-Display-S3 or a 3.5" 480×320 screen.


**Full Changelog**: https://github.com/IsSasoriDev/AxeForge/compare/v1.1.0...v1.2.0

## AxeForge v1.1.0

### Which file?
- **axeforge-factory.bin** — first install over USB. Flash at address `0x0` (web flasher or `esptool.py write_flash 0x0 axeforge-factory.bin`).
- **axeforge.bin** — update an AxeForge miner from the web UI: *System → Firmware update*. Keeps your settings.

Supports the NerdQaxe++ (pre-rev7) with the stock LilyGo T-Display-S3 or a 3.5" 480×320 screen.


**Full Changelog**: https://github.com/IsSasoriDev/AxeForge/commits/v1.1.0

## AxeForge v1.0.0

### Which file?
- **axeforge-factory.bin** — first install over USB. Flash at address `0x0` (web flasher or `esptool.py write_flash 0x0 axeforge-factory.bin`).
- **axeforge.bin** — update an AxeForge miner from the web UI: *System → Firmware update*. Keeps your settings.

Supports the NerdQaxe++ (pre-rev7) with the stock LilyGo T-Display-S3 or a 3.5" 480×320 screen.


**Full Changelog**: https://github.com/IsSasoriDev/AxeForge/commits/v1.0.0
