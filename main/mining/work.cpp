#include "work.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/sha256.h"

// 0x00000000FFFF0000...0000 as a double
static const double TRUEDIFFONE = 26959535291011309493156476344723991336010898738574164086137773096960.0;

static uint8_t hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static void dsha256(const uint8_t *data, size_t len, uint8_t out[32])
{
    uint8_t tmp[32];
    mbedtls_sha256(data, len, tmp, 0);
    mbedtls_sha256(tmp, 32, out, 0);
}

static void swap_words(const uint8_t *in, uint8_t *out, size_t len)
{
    for (size_t i = 0; i < len; i += 4) {
        out[i + 0] = in[i + 3];
        out[i + 1] = in[i + 2];
        out[i + 2] = in[i + 1];
        out[i + 3] = in[i + 0];
    }
}

static void reverse(uint8_t *d, size_t len)
{
    for (size_t i = 0; i < len / 2; i++) {
        uint8_t t = d[i];
        d[i] = d[len - 1 - i];
        d[len - 1 - i] = t;
    }
}

static double le256_to_double(const uint8_t *h)
{
    double r = 0;
    for (int i = 31; i >= 0; i--) r = r * 256.0 + h[i];
    return r;
}

namespace work {

size_t hex2bin(const char *hex, uint8_t *bin, size_t bin_len)
{
    size_t n = 0;
    while (hex[0] && hex[1] && n < bin_len) {
        bin[n++] = (hexval(hex[0]) << 4) | hexval(hex[1]);
        hex += 2;
    }
    return n;
}

void bin2hex(const uint8_t *bin, size_t len, char *hex)
{
    static const char *digits = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        hex[2 * i] = digits[bin[i] >> 4];
        hex[2 * i + 1] = digits[bin[i] & 0xf];
    }
    hex[2 * len] = 0;
}

void build(const StratumJob &sj, const char *extranonce1, uint32_t en2, int en2_len, uint32_t version_mask,
           double pool_diff, Work &w, AsicJob &cj)
{
    // extranonce2: big-endian hex of the counter, padded to en2_len bytes
    if (en2_len > 16) en2_len = 16;
    snprintf(w.extranonce2, sizeof(w.extranonce2), "%0*lx", en2_len * 2, (unsigned long) en2);
    w.extranonce2[en2_len * 2] = 0;

    // coinbase = coinb1 + en1 + en2 + coinb2
    size_t l1 = strlen(sj.coinb1), le1 = strlen(extranonce1), le2 = strlen(w.extranonce2), l2 = strlen(sj.coinb2);
    size_t bin_len = (l1 + le1 + le2 + l2) / 2;
    uint8_t *cb = (uint8_t *) malloc(bin_len);
    size_t o = 0;
    o += hex2bin(sj.coinb1, cb + o, bin_len - o);
    o += hex2bin(extranonce1, cb + o, bin_len - o);
    o += hex2bin(w.extranonce2, cb + o, bin_len - o);
    o += hex2bin(sj.coinb2, cb + o, bin_len - o);

    uint8_t pair[64];
    dsha256(cb, o, pair);
    free(cb);
    for (int i = 0; i < sj.n_merkle; i++) {
        memcpy(pair + 32, sj.merkle[i], 32);
        dsha256(pair, 64, pair);
    }
    // pair[0..31] is the merkle root in natural (header) byte order
    memcpy(w.merkle_root, pair, 32);

    strncpy(w.job_id, sj.job_id, sizeof(w.job_id) - 1);
    w.job_id[sizeof(w.job_id) - 1] = 0;
    w.version = sj.version;
    w.version_mask = version_mask;
    w.ntime = sj.ntime;
    w.nbits = sj.nbits;
    w.pool_diff = pool_diff;
    swap_words(sj.prev_hash_raw, w.prev_hash, 32);
    w.valid = true;

    // chip job: merkle/prevhash word-swapped and fully reversed
    uint8_t tmp[32];
    swap_words(w.merkle_root, tmp, 32);
    reverse(tmp, 32);
    memcpy(cj.merkle_root, tmp, 32);
    memcpy(tmp, sj.prev_hash_raw, 32);
    reverse(tmp, 32);
    memcpy(cj.prev_block_hash, tmp, 32);
    memset(cj.starting_nonce, 0, 4);
    memcpy(cj.nbits, &sj.nbits, 4);
    memcpy(cj.ntime, &sj.ntime, 4);
    memcpy(cj.version, &sj.version, 4);
}

double nonce_difficulty(const Work &w, uint32_t nonce, uint32_t version, uint8_t *hash_out)
{
    uint8_t hdr[80];
    memcpy(hdr, &version, 4);
    memcpy(hdr + 4, w.prev_hash, 32);
    memcpy(hdr + 36, w.merkle_root, 32);
    memcpy(hdr + 68, &w.ntime, 4);
    memcpy(hdr + 72, &w.nbits, 4);
    memcpy(hdr + 76, &nonce, 4);
    uint8_t h[32];
    dsha256(hdr, 80, h);
    if (hash_out) memcpy(hash_out, h, 32);
    double v = le256_to_double(h);
    return v > 0 ? TRUEDIFFONE / v : 0;
}

double nbits_to_difficulty(uint32_t nbits)
{
    int exp = nbits >> 24;
    double mant = nbits & 0xffffff;
    if (!mant) return 0;
    double target = mant * pow(256.0, exp - 3);
    return TRUEDIFFONE / target;
}

uint32_t block_height_from_coinb1(const char *coinb1)
{
    // version(4) + vin count(1) + prevout(36) + script len(1) => push opcode
    if (strlen(coinb1) < 86 + 8) return 0;
    uint8_t push;
    hex2bin(coinb1 + 84, &push, 1);
    if (push < 1 || push > 4) return 0;
    uint8_t b[4] = {};
    hex2bin(coinb1 + 86, b, push);
    return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t) b[3] << 24);
}

static uint64_t read_varint(const uint8_t *b, size_t len, size_t &o)
{
    if (o >= len) return 0;
    uint8_t f = b[o++];
    if (f < 0xfd) return f;
    int n = f == 0xfd ? 2 : f == 0xfe ? 4 : 8;
    uint64_t v = 0;
    for (int i = 0; i < n && o < len; i++) v |= (uint64_t) b[o++] << (8 * i);
    return v;
}

void describe_template(const StratumJob &sj, const char *en1, int en2_len, BlockTemplate &t)
{
    t.height = block_height_from_coinb1(sj.coinb1);
    t.ntime = sj.ntime;
    t.nbits = sj.nbits;
    t.version = sj.version;
    t.branches = (uint8_t) sj.n_merkle;
    // prev hash for display: header order reversed
    uint8_t ph[32];
    swap_words(sj.prev_hash_raw, ph, 32);
    reverse(ph, 32);
    bin2hex(ph, 32, t.prev_hash);

    // rebuild the coinbase with a zero extranonce2 and walk its outputs
    size_t l1 = strlen(sj.coinb1) / 2, le1 = strlen(en1) / 2, l2 = strlen(sj.coinb2) / 2;
    size_t len = l1 + le1 + en2_len + l2;
    uint8_t *cb = (uint8_t *) calloc(1, len);
    if (!cb) return;
    hex2bin(sj.coinb1, cb, l1);
    hex2bin(en1, cb + l1, le1);
    hex2bin(sj.coinb2, cb + l1 + le1 + en2_len, l2);

    size_t o = 4;                          // version
    read_varint(cb, len, o);               // input count (1)
    o += 36;                               // prevout
    uint64_t slen = read_varint(cb, len, o);
    size_t script = o;
    o += slen + 4;                         // script + sequence
    // printable coinbase text, skipping the height push
    // keep only runs of 4+ printable characters so random bytes don't leak in
    int tl = 0;
    size_t start = script + 1 + (script < len ? cb[script] : 0);
    size_t run = 0;
    for (size_t i = start; i <= script + slen && i <= len; i++) {
        bool printable = i < script + slen && i < len && cb[i] >= 0x20 && cb[i] < 0x7f;
        if (printable) {
            run++;
            continue;
        }
        if (run >= 4) {
            if (tl && tl < (int) sizeof(t.tag) - 1) t.tag[tl++] = ' ';
            for (size_t k = i - run; k < i && tl < (int) sizeof(t.tag) - 1; k++) t.tag[tl++] = (char) cb[k];
        }
        run = 0;
    }
    t.tag[tl] = 0;

    uint64_t outs = read_varint(cb, len, o);
    uint64_t sats = 0;
    for (uint64_t i = 0; i < outs && o + 8 <= len; i++) {
        uint64_t v = 0;
        for (int b = 0; b < 8; b++) v |= (uint64_t) cb[o + b] << (8 * b);
        o += 8;
        sats += v;
        o += read_varint(cb, len, o);
    }
    t.reward_btc = sats / 1e8;
    free(cb);
    t.valid = true;
}

} // namespace work
