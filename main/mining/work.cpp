#include "work.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/sha256.h"
#include "mining/payout.h"

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

#if AF_ASIC_BM1397
// SHA-256 of one 64-byte block from the standard initial state: the midstate the
// BM1397 continues from. Big-endian state words.
static void sha256_midstate(const uint8_t block[64], uint8_t out[32])
{
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    static const uint32_t H0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    uint32_t W[64];
    for (int i = 0; i < 16; i++)
        W[i] = ((uint32_t) block[4 * i] << 24) | ((uint32_t) block[4 * i + 1] << 16) |
               ((uint32_t) block[4 * i + 2] << 8) | block[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(W[i - 15], 7) ^ rotr(W[i - 15], 18) ^ (W[i - 15] >> 3);
        uint32_t s1 = rotr(W[i - 2], 17) ^ rotr(W[i - 2], 19) ^ (W[i - 2] >> 10);
        W[i] = W[i - 16] + s0 + W[i - 7] + s1;
    }
    uint32_t s[8];
    memcpy(s, H0, sizeof(s));
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = s[7] + (rotr(s[4], 6) ^ rotr(s[4], 11) ^ rotr(s[4], 25)) + ((s[4] & s[5]) ^ (~s[4] & s[6])) + K[i] + W[i];
        uint32_t t2 = (rotr(s[0], 2) ^ rotr(s[0], 13) ^ rotr(s[0], 22)) + ((s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]));
        memmove(s + 1, s, 7 * sizeof(uint32_t));
        s[4] += t1;
        s[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) {
        uint32_t v = s[i] + H0[i];
        out[4 * i] = v >> 24;
        out[4 * i + 1] = v >> 16;
        out[4 * i + 2] = v >> 8;
        out[4 * i + 3] = v;
    }
}

// Up to four midstates, the version stepped through the pool's mask for each
// (one if the pool allows no rolling), as the reference firmware sends them.
static void build_midstates(const Work &w, AsicJob &cj)
{
    uint8_t hdr[64], mid[32];
    uint32_t version = w.version;
    cj.num_midstates = 0;
    memset(cj.midstate, 0, sizeof(cj.midstate));
    for (int i = 0; i < 4; i++) {
        if (i > 0) {
            if (!w.version_mask) break;
            version = work::next_version(version, w.version_mask);
        }
        memcpy(hdr, &version, 4);
        memcpy(hdr + 4, w.prev_hash, 32);
        memcpy(hdr + 36, w.merkle_root, 28);
        sha256_midstate(hdr, mid);
        for (int k = 0; k < 8; k++) memcpy(cj.midstate[i] + 4 * k, mid + 4 * (7 - k), 4);  // word order reversed
        cj.num_midstates++;
    }
}
#endif

void build(const StratumJob &sj, const char *extranonce1, uint32_t en2, int en2_len, uint32_t version_mask,
           double pool_diff, Work &w, AsicJob &cj)
{
    // extranonce2: big-endian hex of the counter, padded to en2_len bytes
    // (pools.cpp only accepts 1..16; never index the buffer with anything else)
    if (en2_len > 16) en2_len = 16;
    if (en2_len < 1) en2_len = 1;
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

#if AF_ASIC_BM1397
    memcpy(cj.merkle4, w.merkle_root + 28, 4);
    memset(cj.starting_nonce, 0, 4);
    memcpy(cj.nbits, &sj.nbits, 4);
    memcpy(cj.ntime, &sj.ntime, 4);
    build_midstates(w, cj);
#else
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
#endif
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

void describe_template(const StratumJob &sj, const char *en1, int en2_len, BlockTemplate &t, const char *user)
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
    if (en2_len < 0 || en2_len > 16) return;  // pools.cpp refuses these; never size a buffer from one
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
    // lengths come from the pool: check against what's left (an offset plus a
    // 64-bit length can wrap past any `> len` test)
    if (o > len || slen > len - o) slen = o <= len ? len - o : 0;
    size_t script = o;
    o += (size_t) slen + 4;                // script + sequence
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

    // your address (solo pools pay the one in the username) as an output script
    uint8_t mine[payout::MAX_SCRIPT];
    size_t mine_len = 0;
    payout::Net net = payout::Net::Main;
    if (user && payout::user_address(user, t.payout_addr, sizeof(t.payout_addr), &net))
        mine_len = payout::address_to_script(t.payout_addr, mine, sizeof(mine), &net);

    uint64_t outs = read_varint(cb, len, o);
    uint64_t sats = 0, to_me = 0;
    for (uint64_t i = 0; i < outs && o + 8 <= len; i++) {
        uint64_t v = 0;
        for (int b = 0; b < 8; b++) v |= (uint64_t) cb[o + b] << (8 * b);
        o += 8;
        sats += v;
        uint64_t sl = read_varint(cb, len, o);
        if (o > len || sl > len - o) break;
        bool yours = mine_len && sl == mine_len && memcmp(cb + o, mine, sl) == 0;
        if (yours) to_me += v;
        if (t.n_outputs < AF_SHOWN_OUTPUTS) {
            CoinbaseOut &co = t.outputs[t.n_outputs++];
            co.sats = v;
            co.yours = yours;
            payout::script_to_address(cb + o, sl, net, co.addr, sizeof(co.addr));
        }
        o += sl;
    }
    t.reward_btc = sats / 1e8;
    t.total_sats = sats;
    t.payout_sats = to_me;
    t.payout = !mine_len ? Payout::NotAddress : to_me ? Payout::PaysYou : Payout::NotYou;
    free(cb);
    t.valid = true;
}

} // namespace work
