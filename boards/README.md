# Boards

One file per board. Everything the firmware knows about a board at compile time is in `boards/<id>.toml` and nowhere else; `tools/gen_board.py` turns it into `core/board_config.h` when the project is configured, so the C++ sees the same `AF_*` macros as before.

Adding a board means: a `boards/<id>.toml`, a `sdkconfig.defaults.<id>` with its `CONFIG_AF_BOARD_*=y`, the Kconfig choice entry and its limits, a line in the Build workflow's board list and a README row. Changing a board's pins or chips touches only its file.

A board names a `family` (`boards/families/<family>.toml`) and inherits its values; the board's own values win. A key left out leaves its macro undefined, and the code's own defaults apply.

## Keys

Top level: `name` (shown in the UI), `id` (file names and the board tag an update is checked against), `kconfig` (the `CONFIG_AF_BOARD_*` choice, without `CONFIG_`), `family`, `experimental` (built on every push, no release file), `keep_stock_nvs` (take over the stock firmware's Wi-Fi, pools and board settings on first boot), `no_ethernet`.

| Table | Key | Macro | Meaning |
|---|---|---|---|
| asic | chip | `AF_ASIC_<CHIP>` | `bm1366`, `bm1368`, `bm1370`, `bm1373` or `bm1397` |
| asic | count | `AF_NUM_ASICS` | chips in the chain |
| asic | diff_min, diff_max | `AF_ASIC_DIFF_MIN/MAX` | ticket difficulty range |
| asic | job_interval_ms | `AF_JOB_INTERVAL_MS` | how often a new job is sent (default 500) |
| asic | min_freq_mhz | `AF_MIN_FREQ_MHZ` | lowest clock the tuner and recoveries go to (default 400) |
| asic | voltage_domains | `AF_VOLTAGE_DOMAINS` | chips stacked in series share a voltage (default 1) |
| asic | en_active_high | `AF_ASIC_EN_ACTIVE_HIGH` | chip enable polarity |
| asic | bm1373_shufps_init | `AF_BM1373_SHUFPS_INIT` | init the BM1373 as shufps does, not as bitaxe |
| pins | i2c_sda, i2c_scl | `AF_PIN_I2C_SDA/SCL` | |
| pins | button_1 | `AF_PIN_BTN_1` | user button, where it isn't the default |
| display | lcd | `AF_HAS_LCD` | an LCD (true) or the 128x32 OLED (false) |
| display | color_board | `AF_COLOR_BOARD` | Bitaxe "color" layout: ST7789, no accessory header |
| display | spi_st7789 | `AF_LCD_SPI_ST7789` | the HX-6's 2.8in SPI panel |
| regulator | phases | `AF_VR_PHASES` | TPS536x7 phases; `imax_a` defaults to 30 per phase and `ifault_a` to `imax_a` + 5 |
| regulator | imax_a, ifault_a | `AF_VR_IMAX_A`, `AF_VR_IFAULT_A` | |
| regulator | init_mv | `AF_INIT_MV` | power up here, then move to the target (0 = straight there) |
| regulator | detect_rev7 | `AF_DETECT_REV7` | NerdQaxe++ rev7's TPS546 stack, found at boot |
| regulator | detect_tps53667 | `AF_DETECT_TPS53667` | NerdOctaxe-γ rev3.0+ |
| regulator | ds4432u, ds4432u_diode | `AF_HAS_DS4432U`, `AF_DS4432U_DIODE` | the 5 V Bitaxe regulator path |
| regulator | temp_from_controller | `AF_VR_TEMP_FROM_CONTROLLER` | the regulator's own sensor, no TMP1075 at the VR |
| regulator | vr_sensor | `AF_HAS_VR_SENSOR` | false when nothing reads the regulator temperature |
| regulator | fan_target_c | `AF_VR_FAN_TARGET_C` | VR fan PI target (default 65) |
| regulator | version_rolling_hz | `AF_VR_FREQ_HZ` | |
| regulator | no_mux_max_freq_mhz, no_mux_max_mv | `AF_NO_MUX_MAX_FREQ/MV` | NerdQX without its TMP451 mux |
| sensors | emc2101, emc2103 | `AF_EMC2101`, `AF_EMC2103` | fan and diode controller |
| sensors | emc_ideality, emc_beta | `AF_EMC_IDEALITY/BETA` | diode model registers |
| sensors | emc_flip, emc_direct_pwm | `AF_EMC_FLIP`, `AF_EMC_DIRECT_PWM` | |
| sensors | tmp451_muxes, tmp451_pin_a1 | `AF_TMP451_MUXES`, `AF_TMP451_PIN_A1` | per-chip die sensors |
| sensors | proxy_die_offset_c | `AF_PROXY_DIE_OFFSET` | board sensor + offset where there is no die sensor |
| sensors | power_offset_w | `AF_POWER_OFFSET_W` | added to the measured input power |
| stock | freq_mhz, mv | `AF_STOCK_FREQ_MHZ`, `AF_STOCK_MV` | the operating point a fresh miner starts at |
| stock | display_type, rotate_180 | `AF_STOCK_DISPLAY`, `AF_STOCK_ROTATE_180` | 0 = 3.5in 480x320, 1 = 320x170 |
| stock | fan_min_pct | `AF_STOCK_FAN_MIN_PCT` | where the default 30% isn't enough |

The safety limits (max clock, voltage, power, temperatures) still live in `main/Kconfig.projbuild` as `default X if AF_BOARD_*` chains, so they can be changed in menuconfig; moving them here is the next step.
