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
