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
