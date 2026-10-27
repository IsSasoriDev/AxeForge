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
