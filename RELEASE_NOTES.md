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
