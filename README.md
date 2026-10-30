# AxeForge

[![Build](https://github.com/IsSasoriDev/AxeForge/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/IsSasoriDev/AxeForge/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/IsSasoriDev/AxeForge?display_name=tag)](https://github.com/IsSasoriDev/AxeForge/releases/latest)
[![License: GPL-3.0](https://img.shields.io/github/license/IsSasoriDev/AxeForge)](LICENSE)
[![Flash in browser](https://img.shields.io/badge/flash-in%20browser-blue)](https://issasoridev.github.io/AxeForge/)

Alternative firmware for the NerdQAxe family and the Bitaxe Supra Hex, Ultra Hex, Supra, Gamma and Naja Duo (see [Supported boards](#supported-boards)). On a Bitaxe it replaces AxeOS completely. It has a built-in auto-tuner that finds the highest stable hashrate, multi-pool mining over Stratum V1, V1+TLS or Stratum V2, and a screen layout you can customize, on the stock LilyGo T-Display-S3 or a 3.5" 480×320 display.

> **Install it from the browser:** https://issasoridev.github.io/AxeForge/ (Chrome or Edge, USB cable). If that page is down, see [Flashing without the web flasher](#flashing-without-the-web-flasher).

> ⚠️ **Young firmware.** The NerdQaxe++, the NerdAxe and the Supra Hex run it every day, and the Bitaxe Supra has been tested; the newer boards in the table below have not been tested on hardware yet. The mining math is checked against a real Bitcoin block (`tools/verify_work.py`). Keep a backup of your stock flash so you can go back.

## Supported boards

| Board | Chips | Regulator | Power limit | Firmware file | Status |
|---|---|---|---|---|---|
| **NerdQaxe++** rev5.1 / rev6 | 4× BM1370 | TPS53647, 3-phase | 100 W | `axeforge.bin` | ✅ Tested (rev6; rev5.1 runs the same code) |
| **NerdQaxe++** rev7 | 4× BM1370 | TPS546 stack | 120 W | `axeforge.bin` | ✅ Tested by contributors |
| **NerdAxe** | 1× BM1366 | DS4432U + TPS40305 | 15 W | `axeforge-nerdaxe.bin` | ✅ Tested |
| **NerdQAxe+** | 4× BM1368 | TPS53647, 2-phase | 70 W | `axeforge-nerdqaxeplus.bin` | ⚠️ Not tested on hardware yet |
| **NerdOctaxe+** | 8× BM1368 | TPS53647, 3-phase | 130 W | `axeforge-nerdoctaxeplus.bin` | ⚠️ Not tested on hardware yet |
| **NerdOctaxe-γ** | 8× BM1370 | TPS53647 4-phase, or TPS53667 6-phase from rev3.0 | 250 W / 300 W | `axeforge-nerdoctaxegamma.bin` | ⚠️ Not tested on hardware yet |
| **NerdQX** | 4× BM1370 | TPS53647, 3-phase | 240 W | `axeforge-nerdqx.bin` | ⚠️ Not tested on hardware yet |
| **Bitaxe Supra Hex** 701 / 702 | 6× BM1368 | TPS546, 3 stacked domains | 120 W | `axeforge-suprahex.bin` | ✅ Tested (702) |
| **Bitaxe Ultra Hex** 302 / 303 | 6× BM1366 | TPS546, 3 stacked domains | 90 W | `axeforge-ultrahex.bin` | ⚠️ Not tested on hardware yet |
| **Bitaxe Supra** 400–403 | 1× BM1368 | TPS546 (402/403) or DS4432U (400/401), detected at boot | 40 W | `axeforge-supra.bin` | ✅ Tested |
| **Bitaxe Gamma** 600–603 | 1× BM1370 | TPS546 | 40 W | `axeforge-gamma.bin` | ⚠️ Not tested on hardware yet |
| **Bitaxe Naja Duo** 1201 | 2× BM1373 | TPS546 stack, 2 stacked domains | 60 W | `axeforge-naja.bin` | ⚠️ Not tested on hardware yet |
| **Bitaxe Max** 102 | 1× BM1397 | DS4432U + TPS40305 | 25 W | `axeforge-max.bin` | ⚠️ Not tested on hardware yet |
| **Bitaxe Ultra** 201–207 | 1× BM1366 | DS4432U (201–205) or TPS546 (207), detected at boot | 25 W | `axeforge-ultra.bin` | ⚠️ Not tested on hardware yet |
| **Bitaxe Gamma Duo** 650 | 2× BM1370 | TPS546 | 40 W | `axeforge-gammaduo.bin` | ⚠️ Not tested on hardware yet |
| **Bitaxe GT** 801 | 2× BM1370 | TPS546 stack, 2-phase | 60 W | `axeforge-gt.bin` | ⚠️ Not tested on hardware yet |
| **Bitaxe Gamma Hex** 1300 | 6× BM1370 | TPS546 stack, 4-phase, 2 stacked domains | 180 W | `axeforge-gammahex.bin` | ⚠️ Not tested on hardware yet |
| **Nexus S1** (BitLuck) | 4× BM1373 | TPS53647, 4-phase | 170 W | build from source | ⚠️ Experimental (one board tested) |

- Each board has its own firmware file. A file for another board is refused, because the boards switch their core power on differently.
- The NerdQaxe++ revision and the NerdOctaxe-γ regulator are detected at boot.
- The NerdQX and NerdOctaxe-γ read the real die temperature of every chip. The other boards use temperature sensors on the PCB next to the chips.
- A NerdQX without its die sensors is treated as an unknown board and stays at 495 MHz / 1150 mV.
- On a Bitaxe, AxeForge replaces AxeOS (see [Bitaxe](#bitaxe)). The Supra Hex, Ultra Hex, Supra and Gamma use the 128×32 OLED; the Naja Duo's color screen gets the full small-screen UI.
- The Gamma and the Naja Duo read the chip's own temperature diode once it has power; the Supra 400/401 uses the fan controller's sensor next to the chip, as AxeOS does.
- The Nexus S1 is experimental. BitLuck publishes no source or schematics, so its pins and power sequence were read from the stock firmware. Its limits stay close to stock (500 MHz, 1000 mV, 170 W), and there is no release file yet: build it with `sdkconfig.defaults.nexus`.
- If you run one of the untested boards, please open an issue with your log (*System → Log*), working or not.
- Other Bitaxe models (Ultra, Max, GT, Gamma Duo, Gamma Hex) are not supported yet.

## What it does

- **Mining:** version rolling (within the mask the pool allows), a new job for the chips every 0.5–1.5 s depending on the chip, every nonce re-hashed on the ESP32 before it's submitted, and per-chip hashrate and hardware-error counters read from the chips. A result the chain reports twice is dropped instead of being submitted again.
- **Up to 4 pools**, each one using any of these protocols:
  - **Stratum V1**
  - **V1 over TLS**: the certificate is checked against the built-in CA bundle.
  - **Stratum V2**: Noise-NX encrypted, extended channel. You can add the pool's authority key to verify its identity (see [Stratum V2](#stratum-v2)).

  There are two pool modes:
  - **Split**: each pool gets a percentage of the jobs.
  - **Failover**: the first healthy pool in the list gets all the work.

  Pools reconnect live when you save; no reboot is needed. Pools can't be assigned to individual chips: all chips share one serial chain and every job goes to all of them. A 25%-per-pool split gives the same result.
- **Solo payout check:** the miner reads the coinbase of every job back into addresses and checks whether a block would pay the Bitcoin address in your pool username (the part before `.worker`). Dashboard → *Now mining* shows the result and the coinbase outputs, each pool card shows *pays you N%* or a red *coinbase doesn't pay your address*, and the Blocks page on the screen turns green or red. On a pooled account (a username that isn't an address) it just shows who the block would pay. It understands P2PKH, P2SH, P2WPKH, P2WSH and P2TR on mainnet, testnet and regtest, and works over Stratum V1 and V2.
- **Live data:**
  - The block template you're mining: height, reward, coinbase tag, previous hash, coinbase outputs.
  - The latest share hash, with its leading zeros highlighted.
  - mempool.space (or your own node's mempool instance): recently mined blocks, projected next blocks and fees.
- **Auto-tuner (max stable hashrate):**
  1. **Climb:** starts at the board's stock point (600 MHz / 1150 mV on the NerdQaxe++) and adds 12 MHz per step. Each step gets a 2 min warm-up and then a 5 min measurement.
  2. If the hardware error rate goes above 1%, or hashrate drops below 92% of the theoretical value, it adds 10 mV (at most 4 times per frequency).
  3. It stops at the first wall: errors that voltage can't fix, the chip/VR temperature ceiling, the power ceiling, or the max frequency.
  4. **Trim:** at the best frequency, it lowers the voltage while the miner stays stable, then adds 10 mV back as a safety margin. This keeps the same hashrate with lower temps.
  5. **Per-chip clocks** (boards with more than one chip, except the Nexus): once locked, every chip that can go faster steps up 12 MHz at a time, up to +48 MHz over the chain clock. All chips are tried together and each one is judged by its own error count, so the weakest chip no longer holds the others back.
  6. **Watch:** it saves the result to flash, re-checks every 10 min and drops 12 MHz if the miner drifts unstable (for example, when the room warms up). If a raised chip is the one with the errors, only that chip goes down a step.
  - **Goal** (*Tuning → Mode*): most hashrate (the steps above), most hashrate under a power limit you set (the limit replaces the board's power ceiling), or best efficiency, which holds the voltage, climbs while J/TH keeps falling, then trims the voltage.
  - A full search takes about 1–2 hours, plus up to 4 rounds of 5 min for the per-chip step. To start it again, hold **BTN2** or use the web UI.
- **Electricity price** (*Tuning → Electricity price*): the chips switch off while the price is above your limit and start again when it drops, at most one change every 10 minutes. Prices come from the Energy-Charts day-ahead market for your bidding zone (Europe, no account or key), or from any URL that returns the price in JSON. If the price can't be read, the miner keeps mining.
- **Power supply check:** the input voltage under load is compared with the supply's nominal 5 or 12 V. *System → Device* shows the drop and an estimate of the supply-plus-cable resistance; 5% low counts as weak and 8% as bad, with a notification once per boot. A failing supply or a warm barrel jack shows up here before it causes faults.
- **Update rollback:** after a web update the new firmware is on probation. If it crashes at start-up twice, or is online but not mining 10 minutes after boot, the previous firmware starts again. It is kept after 2 minutes of normal mining. It works with any bootloader, including the stock Bitaxe one, and can be turned off in *System → Firmware update*.
- **Safety (cannot be overridden from the UI):** the limits are compiled in through `idf.py menuconfig → AxeForge → Hard safety limits`. Each board has its own power limit (see [Supported boards](#supported-boards)). The NerdQaxe++ defaults:

  | Limit | Rev ≤ 6 | Rev7 |
  |---|---|---|
  | Max frequency | 750 MHz | 1000 MHz |
  | Core voltage | 1100–1300 mV | 1100–1300 mV |
  | Tuner chip-temp ceiling | 68 °C | 68 °C |
  | Emergency chip-temp cut | 80 °C | 80 °C |
  | Tuner VR-temp ceiling | 85 °C (board sensor) | 95 °C (regulator die) |
  | Emergency VR-temp cut | 100 °C | 105 °C |
  | Max power | 100 W | 120 W |

  The board revision is detected at boot from the regulator's device ID.

  - The emergency cut turns the core voltage off, waits until the miner cools 20 °C, then restarts 50 MHz lower.
  - Regulator faults (over-current, VOUT fault, PSU fault) also cut power. After a fault it retries 3 times.
  - A temperature sensor that stops answering for 3 seconds also cuts power. A failed read never counts as a cool reading, and the miner won't power up without working sensors.
  - A fan that has been spinning but drops under 300 rpm at 30 % duty or more for 10 s cuts power (`Fan N stalled`).
  - A chip that falls far below the others (or reports nonsense) for 15 s cuts power (`ASIC N stopped hashing`). On boards whose chips sit in series voltage domains, a chip that stops drawing current lets its domain's voltage climb.
  - Power is also cut if the core voltage drops below half its setting while powered (`Core voltage lost`), or no chip returns anything for 15 s while work is being sent (`No chip is hashing`).
  - On boards with stacked voltage domains (Supra Hex, Ultra Hex, Naja Duo, NerdQaxe++ rev7), the miner won't run with a chip missing: its partner would get the whole domain's voltage.
  - Automatic recoveries (fault retries, overheat cool-down) re-detect the chips at the right baud rate, ramp them up from their reset clock, and stay at the lower clock afterwards until you change the settings.
  - The monitor that handles all of this runs on its own task, separate from the tuner.
- **Swarm:** one page with every miner on your network (AxeForge, AxeOS/Bitaxe, NerdQAxe stock firmware): hashrate, temperature, power, shares, best difficulty and uptime, plus totals. *Scan network* finds them; you can also add an address by hand. This miner polls the others itself, so the API protection stays strict and no CORS is needed.
- **Themes:** *Forge* (dark neon), *Paper* (minimal, e-ink style: ink on paper, hairline rules, no glow) or *Ember* (dark, Bitcoin orange). One setting changes both the screen and the web UI.
- **Fans:** both fan headers are detected on boards that have two (one on the NerdAxe); a header counts as connected once its fan has been seen spinning. Each fan can run on *Auto* or a fixed *Manual* speed. A PI controller keeps the hottest chip at the target you set (default 60 °C). The VR fan follows the hotter of the two loops. Within 3 °C of a tuner ceiling the fans go to 100 % regardless of the target, and both fan speeds are shown.
- **Payout check that can act:** by default a coinbase that doesn't pay you is only shown in red. Under *Pools → Solo payout check* you can tell the miner to send that pool no work at all, and set the highest pool fee you accept. The other pools carry on; the pool gets work again as soon as a template pays you.
- **Best shares and 24-hour charts:** the 10 best shares (difficulty, time, pool, chip), kept across reboots, and hashrate, chip temperature and power for the last 24 hours, one point per 5 minutes.
- **Notifications:** Discord (webhook) and Telegram (bot) messages for a found block, a new best share, faults and power cuts, no pool for 5 minutes, and the miner starting. Pick which ones under *System → Notifications*.
- **Self-test:** *System → Self-test* checks sensors, regulator, chips and fans, then measures hashrate, hardware errors and power for a minute while mining.
- **Two-factor login:** optional. With it on, every change (settings, pools, firmware upload) needs a 6-digit code from an authenticator app; a login lasts 12 hours or until a reboot. If you lose the app, reflash with *Erase*.
- **Setup without typing:** *Network* lists the Wi-Fi networks in range. While the setup network is open, the miner can also be set up over Bluetooth with Espressif's free "ESP BLE Provisioning" app (it shows up as `PROV_AXEFORGE_XXXX`). Bluetooth turns off and gives its memory back once Wi-Fi works.
- **Ethernet (NerdQAxe family):** the W5500 Ethernet adapter that plugs onto the NerdQAxe header is found at boot. On the NerdQX and NerdOctaxe-γ with die sensors the header pins are taken, so Ethernet is skipped there.
- **Accessory port (Bitaxe):** the AxeOS accessory protocol on the Bitaxe's accessory header (GPIO39/40, 115200 baud), so displays and controllers made for AxeOS work with AxeForge. Unlike AxeOS, it never sends the Wi-Fi password out.
- **Advanced chip timing:** job interval and, on BM1370 boards, the version-rolling speed, under *Tuning*. Leave them at 0 for the board's defaults.

## Screen

The pages and their order are set in the web UI (Display → Pages & order). Short-press **BTN1** for the next page and **BTN2** for the previous one.

| Page | What's on it |
|---|---|
| Dashboard | Hashrate gauge, temp/power/fan tiles, auto-tune strip, 1-hour chart |
| Clock & weather | Large clock, date, current weather, 3-day forecast |
| Blocks | Mempool-style block cubes (upcoming and mined), the block you're mining now, the live share hash |
| Pools | Totals, plus a card per pool: protocol, share of work, estimated hashrate, accepted/rejected shares, latency |
| ASIC chips | Per-chip hashrate, temperature and error rate, plus auto-tune details |
| Custom 1 / 2 | Your own layouts from the web editor (12×8 grid, 24 widget types) |
| System | QR code for Wi-Fi setup or for the web UI |

Other controls and alerts:
- Long-press **BTN1** turns the backlight on or off.
- *Screen off after* (Display tab: never, 1, 5, 15, 30 or 60 minutes) turns the backlight off when no button was pressed and no setting saved for that long. The first press only wakes the screen. Faults, overheating and a found block wake it by themselves.
- Long-press **BTN2** restarts the auto-tune search.
- A red banner shows faults and overheating.
- If you ever find a block, the banner flashes **BLOCK FOUND!**

## Web UI

Open `http://<miner-ip>/`. The API only answers when it's reached by IP address or a LAN name (`.local`, `.lan`, a bare hostname, etc.), and any change has to come from the AxeForge page itself, so other web sites can't change settings or push firmware. Scripts that POST to the API need to send the header `X-AxeForge: 1` (and `X-AxeForge-Session` with two-factor login on). The whole API is documented in [`docs/openapi.yaml`](docs/openapi.yaml). The tabs are:
- **Dashboard**
- **Pools**
- **Tuning**
- **Display**:
  - A live capture of the real LCD.
  - A page manager.
  - A drag-and-drop layout editor.
- **Clock & data**: weather and the mempool source.
- **Network**
- **System**:
  - A live log console. Every change made through the API is logged with the client's address, and the next boot says who asked for the last restart.
  - Firmware update by uploading your board's `.bin` (see [Flashing](#flashing)), so later updates don't need USB.
  - Self-test, notifications and two-factor login.

## Screens

Two screens are supported by the same firmware. Pick one under **Display → Screen fitted** in the web UI; the miner reboots to switch. If you switched to the wrong one and see nothing, hold the **bottom button (GPIO14)** while powering on to flip back.

| Screen | Resolution | Notes |
|---|---|---|
| 3.5" ST7796S / ILI9488 | 480×320 | All pages, plus the two custom layout pages |
| LilyGo T-Display-S3 (the stock screen on most NerdQAxe boards) | 320×170 | Compact versions of every page except the custom layouts |

Both connect to the same 8-bit parallel header.

## Display wiring

Both screens use the **8-bit parallel (i80)** stock display header:

| Signal | GPIO |
|---|---|
| D0–D7 | 39, 40, 41, 42, 45, 46, 47, 48 |
| WR (write clock) | 8 |
| RD (held high) | 9 |
| CS | 6 |
| DC | 7 |
| RST | 5 |
| Backlight | 38 |
| Power enable (driven high) | 15 |
| Buttons | 0 (top), 14 (bottom) |

The defaults are an ST7796S controller with a 20 MHz write clock, inverted colors and BGR order. All of these can be changed in `menuconfig → AxeForge → Big display`, which also has an SPI option and an ILI9488 option.

If the screen looks wrong:
- Colors wrong → toggle *Invert colors* and/or *BGR*.
- Image upside down → enable *Rotate 180*.
- Garbage on screen → lower the write clock to 10000 kHz.
- Blank screen → try the ILI9488 controller option.

## Stratum V2

Pick **Stratum V2** as the pool's protocol and enter the pool's SV2 host and port (for example `v2.stratum.braiins.com`, port `3336`) and your worker name.

- **Encryption:** every SV2 connection is encrypted (Noise NX).
- **Pool identity:** paste the pool's **authority public key** (base58, from the pool's docs) to check you are talking to the real pool. Without it the connection is still encrypted, but the pool isn't verified. The pool card shows *Connected (SV2, verified)* or *Connected (SV2, encrypted)*.
- **Certificate dates:** with a key set, an expired or not-yet-valid pool certificate is refused once the clock is set over NTP.
- **Mining:** the miner opens an extended channel, announces its real hashrate and chip model, rolls the version bits the pool allows and keeps the pool's difficulty exactly (no rounding).
- **Reconnects:** if the pool goes silent for 3 minutes or closes the channel, the miner reconnects (or fails over to the next pool).
- **Tested so far:** a contributor mined on a NerdQaxe++ rev6 against solo-pool-rs over SV2. Shares were accepted with none rejected, certificate dates were checked, and the miner reconnected after the pool went silent. Not tested yet: other SV2 pools such as Braiins, and a pool closing the channel (none of the pools tried sends that message).

## Flashing

**Easiest:** open the web flasher at https://issasoridev.github.io/AxeForge/ in Chrome or Edge, connect the miner's USB-C port, pick your board and press *Install*. It writes the firmware pieces separately, so saved Wi-Fi and pool settings are kept unless you tick *Erase*. If no serial port shows up, hold BOOT, tap RESET and release BOOT.

Later updates don't need USB: upload your board's file under **System → Firmware update**. That keeps your settings. A file for another board is refused.

### Flashing without the web flasher

If the web flasher is down (or you'd rather not use a browser), flash over USB with Espressif's `esptool`. Download your board's files from the [Releases](https://github.com/IsSasoriDev/AxeForge/releases) page first:

| Board | Update file | Full image |
|---|---|---|
| NerdQaxe++ | `axeforge.bin` | `axeforge-factory.bin` |
| NerdAxe | `axeforge-nerdaxe.bin` | `axeforge-nerdaxe-factory.bin` |
| NerdQAxe+ | `axeforge-nerdqaxeplus.bin` | `axeforge-nerdqaxeplus-factory.bin` |
| NerdOctaxe+ | `axeforge-nerdoctaxeplus.bin` | `axeforge-nerdoctaxeplus-factory.bin` |
| NerdOctaxe-γ | `axeforge-nerdoctaxegamma.bin` | `axeforge-nerdoctaxegamma-factory.bin` |
| NerdQX | `axeforge-nerdqx.bin` | `axeforge-nerdqx-factory.bin` |
| Bitaxe Supra Hex | `axeforge-suprahex.bin` | `axeforge-suprahex-factory.bin` |
| Bitaxe Ultra Hex | `axeforge-ultrahex.bin` | `axeforge-ultrahex-factory.bin` |
| Bitaxe Supra | `axeforge-supra.bin` | `axeforge-supra-factory.bin` |
| Bitaxe Gamma | `axeforge-gamma.bin` | `axeforge-gamma-factory.bin` |
| Bitaxe Naja Duo | `axeforge-naja.bin` | `axeforge-naja-factory.bin` |

1. Install esptool (needs Python 3):

   ```bash
   python -m pip install esptool
   ```

2. Connect the miner's USB-C port and find its serial port: `COM3`, `COM4`, … on Windows (Device Manager → Ports), `/dev/ttyACM0` on Linux, `/dev/cu.usbmodem…` on macOS. If none shows up, hold **BOOT**, tap **RESET** and release BOOT.

3. Optional, but recommended the first time: back up the stock firmware (16 MB, takes a few minutes):

   ```bash
   python -m esptool --chip esp32s3 -p COM3 -b 921600 read_flash 0x0 0x1000000 stock-backup.bin
   ```

4. Write the full image for your board. This **resets all settings**, because the image covers the settings area too:

   ```bash
   python -m esptool --chip esp32s3 -p COM3 -b 921600 write_flash 0x0 axeforge-factory.bin
   ```

   Use your board's full image from the table, for example `axeforge-nerdqx-factory.bin`.

5. Press **RESET** (or unplug and replug). The miner starts in setup mode, see [First boot](#first-boot).

To keep your settings, write the separate pieces of a build instead of the full image. You get them from a local build (`build/` or `build_<board>/`) or from the web flasher's folder on GitHub Pages:

```bash
python -m esptool --chip esp32s3 -p COM3 -b 921600 write_flash 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 axeforge.bin 0xc10000 ota_data_initial.bin
```

Both ways reset the OTA boot marker, so the miner boots the version you just wrote. To go back to stock, write your backup (or the stock factory image) at `0x0` the same way.

**Windows without Python:** Espressif's *Flash Download Tool* also works. Choose ESP32-S3 and USB, add the full image at address `0x0`, pick the COM port and press *Start*.

### Bitaxe

On a Bitaxe, AxeForge is a full replacement for AxeOS: install it with the web flasher (or esptool) like any other board. It brings its own partition layout, and AxeOS is gone afterwards. Without *Erase*, AxeForge takes over AxeOS's Wi-Fi, pool and clock settings on first boot.

To go back to AxeOS, flash a stock factory image from the [ESP-Miner releases](https://github.com/bitaxeorg/ESP-Miner/releases) (or your backup) at `0x0` with esptool.

**Installed AxeForge 1.4 through AxeOS's update page?** That still works and keeps updating normally: AxeForge sits in AxeOS's spare slot, and three crashed boots in a row start AxeOS by itself. Flash the full image once to switch to the standalone layout.

### First boot

Installed over stock with the web flasher (without *Erase*), boards other than the NerdQaxe++ take over the stock firmware's Wi-Fi and pool settings and go straight to mining. The full image wipes them.

Coming from the stock firmware, or after a clean install, on first boot:
1. Join the `AxeForge-XXXX` Wi-Fi network (or scan the QR code on the screen). Phones and laptops open the setup page by themselves (captive portal). On first setup the network is open. If a miner that already has Wi-Fi saved can't connect for 5 minutes, it opens the same network with a password, shown on its screen, and on that network it only offers the Wi-Fi form.
2. Open http://192.168.4.1.
3. Enter your Wi-Fi, pool and worker (BTC address), then save.

## Building

You need ESP-IDF v5.3.x. Keep the project on a short path on Windows.

```bash
idf.py set-target esp32s3
idf.py menuconfig   # optional: pins / limits
idf.py build        # NerdQaxe++
```

Every other board builds in its own directory from `sdkconfig.defaults.<board>` (`nerdaxe`, `nerdqaxeplus`, `nerdoctaxeplus`, `nerdoctaxegamma`, `nerdqx`, `suprahex`, `ultrahex`, `supra`, `gamma`, `naja`):

```bash
idf.py -B build_nerdqx -D SDKCONFIG=build_nerdqx/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.nerdqx" build
```

Unit tests for the code that doesn't touch hardware (difficulty, coinbase and merkle building, template decoding, the payout check, SV2 parsers) run on a PC with the normal compiler, no ESP-IDF needed. They need `g++`, `make` and `libmbedtls-dev`:

```bash
make -C test
```

## Releases (GitHub Actions)

`.github/workflows/build.yml` runs the unit tests and builds every board on every push and pull request. The `.bin` files are attached to the run as an artifact, and the run summary lists any compiler warnings.

To publish a release, push a version tag:

```bash
git tag -a v1.4.0 -m "AxeForge v1.4.0"
git push origin v1.4.0
```

The workflow writes the tag into `version.txt`, builds every board with ESP-IDF v5.3.2 and creates a GitHub Release with each board's update file and full image (see the table under Flashing) and `SHA256SUMS.txt`.

Tags with a dash (for example `v1.5.0-beta1`) are marked as pre-releases.

A release tag also publishes the web flasher (`flasher/`) to GitHub Pages with that version's firmware. This needs a one-time setting: **Settings → Pages → Source: GitHub Actions**. Pre-releases don't replace the public flasher.

## Not supported yet

- One pool per ASIC: not possible on this hardware (see above).
- Bitaxe models other than the Supra Hex, Ultra Hex, Supra, Gamma and Naja Duo (Ultra, Max, GT, Gamma Duo, Gamma Hex).

## Credits / license

The BM1366/BM1368/BM1370 register sequences, TPS53647/TPS53667 setup, the Bitaxe board definitions, TPS546 profiles, EMC2101/EMC2103 setup and BM1373 support (from [ESP-Miner](https://github.com/bitaxeorg/ESP-Miner), GPL-3.0), TMP451 die sensor handling, board pinouts and limits and the Stratum V2 Noise/protocol component (`components/stratum_v2`) are based on the open-source [ESP-Miner-NerdQAxePlus](https://github.com/shufps/ESP-Miner-NerdQAxePlus) (GPL-3.0). The rev7 TPS546 profile and the W5500 Ethernet adapter's pin map come from the same project. The accessory port speaks AxeOS's BAP protocol. For that reason AxeForge is also released under GPL-3.0.

Thanks to [@cbyam](https://github.com/cbyam) for many fixes and features since v1.2.0: SV2 latency and reconnects, fan safety, recovery, duplicate results, pool difficulty and version rolling, the tuner, the Swarm scan, the screen-off timer and the unit tests.
