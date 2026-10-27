#include "payout.h"

#include <ctype.h>
#include <string.h>
#include "mbedtls/sha256.h"

namespace {

// ---------------------------------------------------------------- base58check
const char B58[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

void dsha256(const uint8_t *d, size_t n, uint8_t out[32])
{
    uint8_t t[32];
    mbedtls_sha256(d, n, t, 0);
    mbedtls_sha256(t, 32, out, 0);
}

// Decodes into `out` (big-endian bytes, leading zeros kept); returns the length or 0.
size_t b58_decode(const char *s, uint8_t *out, size_t cap)
{
    uint8_t buf[40] = {};
    size_t len = 0;  // bytes used, little end at buf[0]
    size_t zeros = 0;
    for (; *s == '1'; s++) zeros++;
    for (; *s; s++) {
        const char *p = strchr(B58, *s);
        if (!p) return 0;
        int carry = (int) (p - B58);
        for (size_t i = 0; i < len; i++) {
            carry += buf[i] * 58;
            buf[i] = carry & 0xff;
            carry >>= 8;
        }
        while (carry) {
            if (len >= sizeof(buf)) return 0;
            buf[len++] = carry & 0xff;
            carry >>= 8;
        }
    }
    if (zeros + len > cap) return 0;
    memset(out, 0, zeros);
    for (size_t i = 0; i < len; i++) out[zeros + i] = buf[len - 1 - i];
    return zeros + len;
}

bool b58_encode(const uint8_t *d, size_t n, char *out, size_t cap)
{
    uint8_t buf[40] = {};  // base-58 digits, little end first
    size_t len = 0, zeros = 0;
    while (zeros < n && d[zeros] == 0) zeros++;
    for (size_t i = zeros; i < n; i++) {
        int carry = d[i];
        for (size_t k = 0; k < len; k++) {
            carry += buf[k] << 8;
            buf[k] = carry % 58;
            carry /= 58;
        }
        while (carry) {
            if (len >= sizeof(buf)) return false;
            buf[len++] = carry % 58;
            carry /= 58;
        }
    }
    if (zeros + len + 1 > cap) return false;
    size_t o = 0;
    for (size_t i = 0; i < zeros; i++) out[o++] = '1';
    for (size_t i = 0; i < len; i++) out[o++] = B58[buf[len - 1 - i]];
    out[o] = 0;
    return true;
}

// ---------------------------------------------------------------- bech32 / bech32m
const char B32[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
const uint32_t BECH32 = 1, BECH32M = 0x2bc830a3;

uint32_t polymod(const uint8_t *v, size_t n)
{
    static const uint32_t G[5] = {0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3};
    uint32_t c = 1;
    for (size_t i = 0; i < n; i++) {
        uint8_t top = c >> 25;
        c = ((c & 0x1ffffff) << 5) ^ v[i];
        for (int k = 0; k < 5; k++)
            if ((top >> k) & 1) c ^= G[k];
    }
    return c;
}

// hrp expanded + data, for the checksum
size_t expand(const char *hrp, const uint8_t *data, size_t dn, uint8_t *out)
{
    size_t h = strlen(hrp), o = 0;
    for (size_t i = 0; i < h; i++) out[o++] = hrp[i] >> 5;
    out[o++] = 0;
    for (size_t i = 0; i < h; i++) out[o++] = hrp[i] & 31;
    memcpy(out + o, data, dn);
    return o + dn;
}

bool convert_bits(const uint8_t *in, size_t n, int from, int to, bool pad, uint8_t *out, size_t *on, size_t cap)
{
    uint32_t acc = 0;
    int bits = 0;
    size_t o = 0;
    const uint32_t maxv = (1u << to) - 1;
    for (size_t i = 0; i < n; i++) {
        if (in[i] >> from) return false;
        acc = (acc << from) | in[i];
        bits += from;
        while (bits >= to) {
            bits -= to;
            if (o >= cap) return false;
            out[o++] = (acc >> bits) & maxv;
        }
    }
    if (pad) {
        if (bits) {
            if (o >= cap) return false;
            out[o++] = (acc << (to - bits)) & maxv;
        }
    } else if (bits >= from || ((acc << (to - bits)) & maxv)) {
        return false;
    }
    *on = o;
    return true;
}

const char *hrp_for(payout::Net net) { return net == payout::Net::Main ? "bc" : net == payout::Net::Test ? "tb" : "bcrt"; }

// Segwit address -> script. 0 if it isn't one.
size_t segwit_decode(const char *addr, uint8_t *script, size_t cap, payout::Net *net)
{
    size_t n = strlen(addr);
    if (n < 8 || n > 90) return 0;
    char low[91];
    bool has_low = false, has_up = false;
    for (size_t i = 0; i < n; i++) {
        char c = addr[i];
        if (c < 33 || c > 126) return 0;
        has_low |= islower((unsigned char) c);
        has_up |= isupper((unsigned char) c);
        low[i] = (char) tolower((unsigned char) c);
    }
    low[n] = 0;
    if (has_low && has_up) return 0;
    const char *sep = strrchr(low, '1');
    if (!sep || sep == low || low + n - sep < 8) return 0;  // version + 6 checksum chars
    char hrp[8];
    size_t hl = sep - low;
    if (hl >= sizeof(hrp)) return 0;
    memcpy(hrp, low, hl);
    hrp[hl] = 0;
    payout::Net nt;
    if (!strcmp(hrp, "bc")) nt = payout::Net::Main;
    else if (!strcmp(hrp, "tb")) nt = payout::Net::Test;
    else if (!strcmp(hrp, "bcrt")) nt = payout::Net::Regtest;
    else return 0;

    uint8_t data[90];
    size_t dn = 0;
    for (const char *p = sep + 1; *p; p++) {
        const char *q = strchr(B32, *p);
        if (!q) return 0;
        data[dn++] = (uint8_t) (q - B32);
    }
    uint8_t ex[100];
    size_t en = expand(hrp, data, dn, ex);
    uint32_t pm = polymod(ex, en);
    int ver = data[0];
    if (ver > 16) return 0;
    if (pm != (ver == 0 ? BECH32 : BECH32M)) return 0;

    uint8_t prog[40];
    size_t pn = 0;
    if (!convert_bits(data + 1, dn - 1 - 6, 5, 8, false, prog, &pn, sizeof(prog))) return 0;
    if (pn < 2 || pn > 40 || (ver == 0 && pn != 20 && pn != 32)) return 0;
    if (pn + 2 > cap) return 0;
    script[0] = ver ? (uint8_t) (0x50 + ver) : 0x00;
    script[1] = (uint8_t) pn;
    memcpy(script + 2, prog, pn);
    if (net) *net = nt;
    return pn + 2;
}

bool segwit_encode(int ver, const uint8_t *prog, size_t pn, payout::Net net, char *out, size_t cap)
{
    const char *hrp = hrp_for(net);
    uint8_t data[80];
    size_t dn = 0;
    data[dn++] = (uint8_t) ver;
    size_t cn = 0;
    if (!convert_bits(prog, pn, 8, 5, true, data + 1, &cn, sizeof(data) - 7)) return false;
    dn += cn;
    uint8_t ex[100];
    memset(data + dn, 0, 6);
    size_t en = expand(hrp, data, dn + 6, ex);
    uint32_t pm = polymod(ex, en) ^ (ver == 0 ? BECH32 : BECH32M);
    for (int i = 0; i < 6; i++) data[dn + i] = (pm >> (5 * (5 - i))) & 31;
    dn += 6;
    size_t hl = strlen(hrp);
    if (hl + 1 + dn + 1 > cap) return false;
    memcpy(out, hrp, hl);
    out[hl] = '1';
    for (size_t i = 0; i < dn; i++) out[hl + 1 + i] = B32[data[i]];
    out[hl + 1 + dn] = 0;
    return true;
}

} // namespace

namespace payout {

size_t address_to_script(const char *addr, uint8_t *script, size_t cap, Net *net)
{
    if (!addr || !*addr) return 0;
    size_t n = segwit_decode(addr, script, cap, net);
    if (n) return n;

    // legacy: version byte + 20-byte hash + 4-byte checksum
    uint8_t raw[25];
    if (strlen(addr) > 35 || b58_decode(addr, raw, sizeof(raw)) != 25) return 0;
    uint8_t h[32];
    dsha256(raw, 21, h);
    if (memcmp(h, raw + 21, 4) != 0) return 0;
    Net nt;
    bool p2sh;
    switch (raw[0]) {
    case 0x00: nt = Net::Main; p2sh = false; break;
    case 0x05: nt = Net::Main; p2sh = true; break;
    case 0x6f: nt = Net::Test; p2sh = false; break;
    case 0xc4: nt = Net::Test; p2sh = true; break;
    default: return 0;
    }
    if (p2sh) {
        if (cap < 23) return 0;
        script[0] = 0xa9;  // OP_HASH160 <20> OP_EQUAL
        script[1] = 20;
        memcpy(script + 2, raw + 1, 20);
        script[22] = 0x87;
        n = 23;
    } else {
        if (cap < 25) return 0;
        script[0] = 0x76;  // OP_DUP OP_HASH160 <20> OP_EQUALVERIFY OP_CHECKSIG
        script[1] = 0xa9;
        script[2] = 20;
        memcpy(script + 3, raw + 1, 20);
        script[23] = 0x88;
        script[24] = 0xac;
        n = 25;
    }
    if (net) *net = nt;
    return n;
}

bool script_to_address(const uint8_t *s, size_t len, Net net, char *out, size_t cap)
{
    if (cap) out[0] = 0;
    if (len >= 1 && s[0] == 0x6a) {  // OP_RETURN: witness commitment, pool data
        if (cap < 10) return false;
        strcpy(out, "OP_RETURN");
        return true;
    }
    bool test = net != Net::Main;
    if (len == 25 && s[0] == 0x76 && s[1] == 0xa9 && s[2] == 20 && s[23] == 0x88 && s[24] == 0xac) {
        uint8_t raw[25];
        raw[0] = test ? 0x6f : 0x00;
        memcpy(raw + 1, s + 3, 20);
        uint8_t h[32];
        dsha256(raw, 21, h);
        memcpy(raw + 21, h, 4);
        return b58_encode(raw, 25, out, cap);
    }
    if (len == 23 && s[0] == 0xa9 && s[1] == 20 && s[22] == 0x87) {
        uint8_t raw[25];
        raw[0] = test ? 0xc4 : 0x05;
        memcpy(raw + 1, s + 2, 20);
        uint8_t h[32];
        dsha256(raw, 21, h);
        memcpy(raw + 21, h, 4);
        return b58_encode(raw, 25, out, cap);
    }
    // witness program: OP_0 or OP_1..OP_16, then one push of 2..40 bytes
    if (len >= 4 && len <= 42 && (s[0] == 0x00 || (s[0] >= 0x51 && s[0] <= 0x60)) && s[1] == len - 2) {
        int ver = s[0] ? s[0] - 0x50 : 0;
        if (ver == 0 && len != 22 && len != 34) return false;
        return segwit_encode(ver, s + 2, len - 2, net, out, cap);
    }
    return false;
}

bool user_address(const char *user, char *addr, size_t cap, Net *net)
{
    if (!user || cap == 0) return false;
    size_t n = strcspn(user, ".");  // "address.worker"
    if (n == 0 || n >= cap || n > 90) return false;
    memcpy(addr, user, n);
    addr[n] = 0;
    uint8_t script[MAX_SCRIPT];
    if (address_to_script(addr, script, sizeof(script), net)) return true;
    addr[0] = 0;
    return false;
}

} // namespace payout
