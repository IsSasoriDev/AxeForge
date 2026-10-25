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
