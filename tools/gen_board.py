#!/usr/bin/env python3
"""Turn boards/<id>.toml into core/board_config.h and board.cmake.

Run by main/CMakeLists.txt at configure time; no dependencies beyond Python 3.
A board file names a family (boards/families/<family>.toml) whose values it
inherits; its own values win. See boards/README.md for the fields.
"""
import os
import re
import sys

# ---------------------------------------------------------------- a small TOML reader
# Enough TOML for the board files: comments, [tables], [dotted.tables], ints
# (decimal and 0x), floats, booleans, "strings", and [arrays] of those.
_NUM = re.compile(r"^-?(0x[0-9a-fA-F]+|\d+(\.\d+)?)$")


def _value(text, where):
    text = text.strip()
    if text in ("true", "false"):
        return text == "true"
    if text.startswith('"') and text.endswith('"') and len(text) >= 2:
        return bytes(text[1:-1], "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")
    if text.startswith("[") and text.endswith("]"):
        inner = text[1:-1].strip()
        return [_value(v, where) for v in inner.split(",")] if inner else []
    if _NUM.match(text):
        if text.lower().startswith(("0x", "-0x")):
            return int(text, 16)
        return float(text) if "." in text else int(text)
    raise SystemExit(f"{where}: can't read value {text!r}")


def read_toml(path):
    doc, table = {}, None
    with open(path, encoding="utf-8") as f:
        for n, raw in enumerate(f, 1):
            line = raw.split("#", 1)[0].strip() if not raw.strip().startswith('"') else raw.strip()
            # a '#' inside a string would break the comment strip above; the board files don't use one
            if not line:
                continue
            where = f"{path}:{n}"
            if line.startswith("[") and line.endswith("]"):
                table = doc
                for part in line[1:-1].split("."):
                    table = table.setdefault(part.strip(), {})
                continue
            if "=" not in line:
                raise SystemExit(f"{where}: expected key = value")
            key, _, val = line.partition("=")
            (table if table is not None else doc)[key.strip()] = _value(val, where)
    return doc


def merge(base, over):
    out = dict(base)
    for k, v in over.items():
        out[k] = merge(out[k], v) if isinstance(v, dict) and isinstance(out.get(k), dict) else v
    return out


# ---------------------------------------------------------------- the macros
# (table, key, macro). A key that is absent leaves its macro undefined, so the
# code's own #ifndef defaults apply. Booleans become 1 (false leaves the macro
# undefined, as the hand-written header did). Floats get an f suffix.
FIELDS = [
    ("asic", "count", "AF_NUM_ASICS"),
    ("asic", "diff_min", "AF_ASIC_DIFF_MIN"),
    ("asic", "diff_max", "AF_ASIC_DIFF_MAX"),
    ("asic", "job_interval_ms", "AF_JOB_INTERVAL_MS"),
    ("asic", "min_freq_mhz", "AF_MIN_FREQ_MHZ"),
    ("asic", "voltage_domains", "AF_VOLTAGE_DOMAINS"),
    ("asic", "en_active_high", "AF_ASIC_EN_ACTIVE_HIGH"),
    ("asic", "bm1373_shufps_init", "AF_BM1373_SHUFPS_INIT"),
    ("pins", "i2c_sda", "AF_PIN_I2C_SDA"),
    ("pins", "i2c_scl", "AF_PIN_I2C_SCL"),
    ("pins", "button_1", "AF_PIN_BTN_1"),
    ("display", "lcd", "AF_HAS_LCD"),
    ("display", "color_board", "AF_COLOR_BOARD"),
    ("display", "spi_st7789", "AF_LCD_SPI_ST7789"),
    ("regulator", "phases", "AF_VR_PHASES"),
    ("regulator", "imax_a", "AF_VR_IMAX_A"),
    ("regulator", "ifault_a", "AF_VR_IFAULT_A"),
    ("regulator", "init_mv", "AF_INIT_MV"),
    ("regulator", "detect_rev7", "AF_DETECT_REV7"),
    ("regulator", "detect_tps53667", "AF_DETECT_TPS53667"),
    ("regulator", "ds4432u", "AF_HAS_DS4432U"),
    ("regulator", "ds4432u_diode", "AF_DS4432U_DIODE"),
    ("regulator", "temp_from_controller", "AF_VR_TEMP_FROM_CONTROLLER"),
    ("regulator", "fan_target_c", "AF_VR_FAN_TARGET_C"),
    ("regulator", "version_rolling_hz", "AF_VR_FREQ_HZ"),
    ("regulator", "no_mux_max_freq_mhz", "AF_NO_MUX_MAX_FREQ"),
    ("regulator", "no_mux_max_mv", "AF_NO_MUX_MAX_MV"),
    ("sensors", "emc2101", "AF_EMC2101"),
    ("sensors", "emc2103", "AF_EMC2103"),
    ("sensors", "emc_ideality", "AF_EMC_IDEALITY"),
    ("sensors", "emc_beta", "AF_EMC_BETA"),
    ("sensors", "emc_flip", "AF_EMC_FLIP"),
    ("sensors", "emc_direct_pwm", "AF_EMC_DIRECT_PWM"),
    ("sensors", "tmp451_muxes", "AF_TMP451_MUXES"),
    ("sensors", "tmp451_pin_a1", "AF_TMP451_PIN_A1"),
    ("sensors", "proxy_die_offset_c", "AF_PROXY_DIE_OFFSET"),
    ("sensors", "power_offset_w", "AF_POWER_OFFSET_W"),
    ("stock", "freq_mhz", "AF_STOCK_FREQ_MHZ"),
    ("stock", "mv", "AF_STOCK_MV"),
    ("stock", "display_type", "AF_STOCK_DISPLAY"),
    ("stock", "rotate_180", "AF_STOCK_ROTATE_180"),
    ("stock", "fan_min_pct", "AF_STOCK_FAN_MIN_PCT"),
    ("envelope", "power_pct", "AF_ENV_POWER_PCT"),
    ("envelope", "mv", "AF_ENV_MV"),
    ("envelope", "freq_mhz", "AF_ENV_FREQ_MHZ"),
]
# keys whose false value still has to be a 0, because the code tests the value
ZERO_IF_FALSE = {"AF_HAS_LCD", "AF_HAS_VR_SENSOR", "AF_STOCK_ROTATE_180"}
FAMILY_MACRO = {"nerdq": "AF_NQ_FAMILY", "hex": "AF_HEX_FAMILY", "bitaxe": "AF_BITAXE_FAMILY"}

TAIL = """
// Embedded in every image so a firmware upload for another board is refused
// (their pinouts disagree about which level switches the core power on).
#define AF_BOARD_TAG_PREFIX "AXEFORGE-BOARD:"
#define AF_BOARD_TAG AF_BOARD_TAG_PREFIX AF_BOARD_ID ";"

#ifndef AF_HAS_VR_SENSOR
#define AF_HAS_VR_SENSOR 1
#endif
#ifndef AF_VR_FAN_TARGET_C
#define AF_VR_FAN_TARGET_C 65.0f  // the VR fan's PI target
#endif
#ifndef AF_JOB_INTERVAL_MS
#define AF_JOB_INTERVAL_MS 500
#endif
#ifndef AF_ASIC_DIFF_MIN
#define AF_ASIC_DIFF_MIN 512
#define AF_ASIC_DIFF_MAX 2048
#endif
#ifndef AF_TMP451_MUXES
#define AF_TMP451_MUXES 0
#endif
#ifndef AF_PROXY_DIE_OFFSET
#define AF_PROXY_DIE_OFFSET 0.0f
#endif
#ifndef AF_VOLTAGE_DOMAINS
#define AF_VOLTAGE_DOMAINS 1
#endif
#ifndef AF_MIN_FREQ_MHZ
#define AF_MIN_FREQ_MHZ 400  // lowest clock the tuner and recoveries go to
#endif
#ifndef AF_STOCK_FREQ_MHZ
#define AF_STOCK_FREQ_MHZ 600
#endif
#ifndef AF_STOCK_MV
#define AF_STOCK_MV 1150
#endif
#ifndef AF_STOCK_DISPLAY
#define AF_STOCK_DISPLAY 0
#endif
#ifndef AF_STOCK_ROTATE_180
#define AF_STOCK_ROTATE_180 0
#endif

// All boards share these
#define AF_PIN_ASIC_RST 1
#define AF_PIN_UART_TX 17
#define AF_PIN_UART_RX 18
"""


def c_string(s):
    out = []
    for b in s.encode("utf-8"):
        out.append(chr(b) if 32 <= b < 127 and chr(b) not in '"\\' else "\\%03o" % b)
    return '"' + "".join(out) + '"'


def c_value(v):
    if isinstance(v, bool):
        return "1" if v else "0"
    if isinstance(v, float):
        return f"{v}f"
    if isinstance(v, str):
        return c_string(v)
    return str(v)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: gen_board.py boards/<id>.toml <out dir>")
    path, out_dir = sys.argv[1], sys.argv[2]
    board = read_toml(path)
    family = board.get("family")
    if family:
        fam_path = os.path.join(os.path.dirname(os.path.abspath(path)), "families", family + ".toml")
        board = merge(read_toml(fam_path), board)
    for need in ("name", "id", "kconfig"):
        if need not in board:
            raise SystemExit(f"{path}: missing {need}")

    lines = [
        "#pragma once",
        f"// Generated from boards/{os.path.basename(path)} by tools/gen_board.py: edit the board file, not this.",
        "// Compile-time description of the target board. One firmware image per board:",
        "// the pinouts overlap, and a pin that is an LCD data line on one board can be a",
        "// regulator enable on another.",
        "",
        "#include \"sdkconfig.h\"",
        "",
        f"#define AF_BOARD_NAME {c_string(board['name'])}",
        f"#define AF_BOARD_ID {c_string(board['id'])}",
    ]
    if family in FAMILY_MACRO:
        lines.append(f"#define {FAMILY_MACRO[family]} 1")
    chip = board.get("asic", {}).get("chip")
    if not chip:
        raise SystemExit(f"{path}: asic.chip is required")
    lines.append(f"#define AF_ASIC_{chip.upper()} 1")
    if board.get("keep_stock_nvs"):
        lines.append("#define AF_KEEP_STOCK_NVS 1")
    if board.get("no_ethernet"):
        lines.append("#define AF_NO_ETH 1")
    if board.get("regulator", {}).get("vr_sensor") is False:
        lines.append("#define AF_HAS_VR_SENSOR 0")

    reg = board.get("regulator", {})
    # the NerdQaxe-style regulators: current limits follow the phase count unless given
    if "phases" in reg:
        reg.setdefault("imax_a", reg["phases"] * 30)
        reg.setdefault("ifault_a", reg["imax_a"] + 5)

    for table, key, macro in FIELDS:
        v = board.get(table, {}).get(key)
        if v is None:
            continue
        if v is False and macro not in ZERO_IF_FALSE:
            continue
        lines.append(f"#define {macro} {c_value(v)}")
    lines.append(TAIL)

    os.makedirs(os.path.join(out_dir, "core"), exist_ok=True)
    with open(os.path.join(out_dir, "core", "board_config.h"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    with open(os.path.join(out_dir, "board.cmake"), "w") as f:
        f.write(f"# Generated from {os.path.basename(path)}\n")
        f.write(f"set(AF_BOARD_ID {board['id']})\n")
        f.write(f"set(AF_HAS_LCD {'1' if board.get('display', {}).get('lcd') else '0'})\n")
        f.write(f"set(AF_EXPERIMENTAL {'1' if board.get('experimental') else '0'})\n")


if __name__ == "__main__":
    main()
