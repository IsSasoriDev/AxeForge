## AxeForge v1.1.0

### Safety
- **Fixed: a failed temperature sensor read was treated as 0 °C.** That turned off the 80 °C emergency cut-off and let the auto-tuner keep raising the clock with nothing to stop it.
  - A failed read is now treated as "no reading", never as a cool one.
  - If a chip or VR sensor doesn't answer for 3 seconds, power to the ASICs is cut.
  - While a sensor is missing, the fans run at 100% and the tuner won't step up.
  - The miner refuses to power up unless its temperature sensors respond.
  - New `tempOk` field in `/api/status`.

### Security
- **Fixed: the web API accepted cross-site requests.** A malicious page open in a browser on the same network could change pool settings, reboot the miner or push firmware.
  - The API only answers when the miner is reached by IP address or a local name (`.local`, `.lan`, a bare hostname, …). This also blocks DNS-rebinding attacks.
  - Any request that changes something must include the `X-AxeForge: 1` header. Requests whose browser headers mark them as cross-site (`Origin` / `Sec-Fetch-Site`) are rejected.
  - The built-in web UI handles this automatically. **Scripts that POST to the API need to add the header**, e.g. `curl -H 'X-AxeForge: 1' ...`

### New hardware
- **NerdQaxe++ rev7 (TPS546 regulator) support.** The board revision is detected at boot, so rev6 boards behave exactly as before.
  - Rev7 limits: 1000 MHz, 120 W; the tuner stops raising the clock at a regulator temperature of 95 °C, and power is cut at 105 °C. All are adjustable in menuconfig.
  - The regulator settings are written on every power-up and never stored in the regulator's own memory.
  - Based on the rev7 profile in upstream ESP-Miner-NerdQAxePlus (develop branch).
  - ⚠️ Not yet tested on a real rev7 board. Please report boot logs and readings if you try it.

### Other
- `axeforge-factory.bin` now also resets the OTA boot selection. Flashing it over USB always boots the new firmware, even on a board that was previously updated over Wi-Fi.
- The power reading now drops to 0 W after an emergency cut-off instead of showing the last value.

### Which file?
- **axeforge-factory.bin**: first install over USB. Flash at address `0x0` (web flasher or `esptool.py write_flash 0x0 axeforge-factory.bin`).
- **axeforge.bin**: update an AxeForge miner from the web UI (*System → Firmware update*). Keeps your settings. Refresh the page after the reboot.

Supports the NerdQaxe++ (rev ≤ 6 and rev7) with the stock LilyGo T-Display-S3 or a 3.5" 480×320 screen.
