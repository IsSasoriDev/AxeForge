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
