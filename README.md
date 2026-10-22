# AxeForge

New firmware for the **NerdQaxe++** (4× BM1370, TPS53647 regulator) driving a **3.5" 480×320 display** (8-bit parallel or SPI, ST7796S or ILI9488). It has a built-in auto-tuner that finds the highest stable hashrate, multi-pool mining over Stratum V1, V1+TLS or Stratum V2, and a screen layout you can customize.

> ⚠️ **Young firmware.** It runs on a real NerdQaxe++ (pre-rev7). The mining math is checked against a real Bitcoin block (`tools/verify_work.py`). Stratum V2 has been tested up to the encrypted handshake and connection setup, but not share submission. Keep a backup of your stock flash so you can go back.

## What it does

- **Mining:** version rolling, 500 ms job rotation, every nonce re-hashed on the ESP32 before it's submitted, and per-chip hashrate and hardware-error counters read from the BM1370s.
- **Up to 4 pools**, each one using any of these protocols:
  - **Stratum V1**
  - **V1 over TLS**: the certificate is checked against the built-in CA bundle.
  - **Stratum V2**: Noise-NX encrypted, extended channel. You can add the pool's authority key to verify its identity.

  There are two pool modes:
  - **Split**: each pool gets a percentage of the jobs.
  - **Failover**: the first healthy pool in the list gets all the work.

  Pools reconnect live when you save; no reboot is needed. Pools can't be assigned to individual chips: the 4 BM1370s share one serial chain and every job goes to all of them. A 25%-per-pool split gives the same result.
- **Live data:**
  - The block template you're mining: height, reward, coinbase tag, previous hash.
  - The latest share hash, with its leading zeros highlighted.
  - mempool.space (or your own node's mempool instance): recently mined blocks, projected next blocks and fees.
- **Auto-tuner (max stable hashrate):**
  1. **Climb:** starts at 600 MHz / 1150 mV and adds 12 MHz per step. Each step gets a 2 min warm-up and then a 5 min measurement.
  2. If the hardware error rate goes above 1%, or hashrate drops below 92% of the theoretical value, it adds 10 mV (at most 4 times per frequency).
  3. It stops at the first wall: errors that voltage can't fix, the chip/VR temperature ceiling, the power ceiling, or the max frequency.
  4. **Trim:** at the best frequency, it lowers the voltage while the miner stays stable, then adds 10 mV back as a safety margin. This keeps the same hashrate with lower temps.
  5. **Watch:** it saves the result to flash, re-checks every 10 min and drops 12 MHz if the miner drifts unstable (for example, when the room warms up).
  - A full search takes about 1–2 hours. To start it again, hold **BTN2** or use the web UI.
- **Safety (cannot be overridden from the UI):** the limits are compiled in through `idf.py menuconfig → AxeForge → Hard safety limits`. Defaults:

  | Limit | Default |
  |---|---|
  | Max frequency | 750 MHz |
  | Core voltage | 1100–1300 mV |
  | Tuner chip-temp ceiling | 68 °C |
  | Emergency chip-temp cut | 80 °C |
  | Tuner VR-temp ceiling | 85 °C |
  | Emergency VR-temp cut | 100 °C |
  | Max power | 100 W |

  - The emergency cut turns the core voltage off, waits until the miner cools 20 °C, then restarts 50 MHz lower.
  - Regulator faults (over-current, VOUT fault, PSU fault) also cut power. After a fault it retries 3 times.
  - The monitor that handles all of this runs on its own task, separate from the tuner.
- **Fans:** a PI controller keeps the hottest chip at the target you set (default 60 °C). The VR fan follows the hotter of the two loops.

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
- Long-press **BTN2** restarts the auto-tune search.
- A red banner shows faults and overheating.
- If you ever find a block, the banner flashes **BLOCK FOUND!**

## Web UI

Open `http://<miner-ip>/`. The tabs are:
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
  - A live log console.
  - Firmware update by uploading `axeforge.bin`, so later updates don't need USB.

## Screens

Two screens are supported by the same firmware. Pick one under **Display → Screen fitted** in the web UI; the miner reboots to switch. If you switched to the wrong one and see nothing, hold the **bottom button (GPIO14)** while powering on to flip back.

| Screen | Resolution | Notes |
|---|---|---|
| 3.5" ST7796S / ILI9488 | 480×320 | All pages, plus the two custom layout pages |
| LilyGo T-Display-S3 (the stock NerdQaxe++ screen) | 320×170 | Compact versions of every page except the custom layouts |

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

## Flashing

1. Hold the BOOT button and connect USB.
2. Then either:
   - use the web flasher at https://espressif.github.io/esptool-js/ and write `release/axeforge-factory.bin` at address `0x0`, or
   - run:

     ```bash
     python -m esptool --chip esp32s3 -b 921600 write_flash 0x0 axeforge-factory.bin
     ```

If the miner has ever been updated through the web UI, also clear the OTA boot marker. Otherwise it keeps booting the last web-installed version instead of the factory image:

```bash
python -m esptool --chip esp32s3 erase_region 0xc10000 0x2000
```

Later updates don't need USB: upload `release/axeforge.bin` under **System → Firmware update**. That keeps your settings.

Flashing the factory image wipes the stock settings. On first boot:
1. Join the `AxeForge-XXXX` Wi-Fi network (or scan the QR code on the screen).
2. Open http://192.168.4.1.
3. Enter your Wi-Fi, pool and worker (BTC address), then save.

## Building

You need ESP-IDF v5.3.x. Keep the project on a short path on Windows.

```bash
idf.py set-target esp32s3
idf.py menuconfig   # optional: pins / limits
idf.py build
```

## Not supported (yet)

- **Rev7 boards with the TPS546 regulator.** The firmware detects them and stays powered down instead of guessing.
- Stratum V2 share submission is implemented but has not been tested end to end (that needs an SV2 pool account).
- One pool per ASIC: not possible on this hardware (see above).

## Credits / license

The BM1370 register sequences, TPS53647 setup, board pinout and the Stratum V2 Noise/protocol component (`components/stratum_v2`) are based on the open-source [ESP-Miner-NerdQAxePlus](https://github.com/shufps/ESP-Miner-NerdQAxePlus) (GPL-3.0). For that reason AxeForge is also released under GPL-3.0.
