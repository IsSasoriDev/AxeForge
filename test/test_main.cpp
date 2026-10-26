// Host-side unit tests for code that doesn't touch hardware: share and block
// difficulty, coinbase and merkle building, template decoding and the SV2
// message parsers. Known answers come from the Bitcoin genesis block.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mining/work.h"
#include "sv2_protocol.h"

static int s_failed = 0, s_checks = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        s_checks++;                                                     \
        if (!(cond)) {                                                  \
            s_failed++;                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                               \
    } while (0)

#define CHECK_NEAR(a, b, tol) CHECK(fabs((double) (a) - (double) (b)) <= (tol))

static void reverse_hex(const uint8_t *bin, char *hex)  // display order
{
    uint8_t r[32];
    for (int i = 0; i < 32; i++) r[i] = bin[31 - i];
    work::bin2hex(r, 32, hex);
}

// ---------------------------------------------------------------- genesis block
// Coinbase split so a 4-byte extranonce1 and 4-byte extranonce2 fall inside the
// scriptSig text, as a pool would send it.
static const char *GENESIS_COINB1 =
    "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4d04ffff001d010445";
static const char *GENESIS_EN1 = "54686520";
static const uint32_t GENESIS_EN2 = 0x54696d65;
static const char *GENESIS_COINB2 =
    "732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f"
    "722062616e6b73ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61de"
    "b649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000";

static StratumJob genesis_job()
{
    StratumJob j;
    memset(j.job_id, 0, sizeof(j.job_id));
    strcpy(j.job_id, "1");
    memset(j.prev_hash_raw, 0, 32);
    j.coinb1 = (char *) GENESIS_COINB1;
    j.coinb2 = (char *) GENESIS_COINB2;
    j.n_merkle = 0;
    j.version = 1;
    j.nbits = 0x1d00ffff;
    j.ntime = 1231006505;
    return j;
}

static void test_hex()
{
    uint8_t b[4];
    CHECK(work::hex2bin("deadBEEF", b, 4) == 4);
    CHECK(b[0] == 0xde && b[3] == 0xef);
    char h[9];
    work::bin2hex(b, 4, h);
    CHECK(strcmp(h, "deadbeef") == 0);
}

static void test_nbits()
{
    CHECK_NEAR(work::nbits_to_difficulty(0x1d00ffff), 1.0, 1e-12);
    // the worked example on the Bitcoin wiki's "Difficulty" page
    CHECK_NEAR(work::nbits_to_difficulty(0x1b0404cb), 16307.420938523983, 1e-6);
}

static void test_genesis_build_and_hash()
{
    StratumJob j = genesis_job();
    Work w;
    AsicJob cj;
    work::build(j, GENESIS_EN1, GENESIS_EN2, 4, 0x1fffe000, 1, w, cj);
    CHECK(strcmp(w.extranonce2, "54696d65") == 0);
    char hex[65];
    reverse_hex(w.merkle_root, hex);
    CHECK(strcmp(hex, "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b") == 0);

    uint8_t hash[32];
    double diff = work::nonce_difficulty(w, 2083236893, 1, hash);
    reverse_hex(hash, hex);
    CHECK(strcmp(hex, "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f") == 0);
    CHECK_NEAR(diff, 2536.4262984453, 1e-6);
    CHECK(diff >= work::nbits_to_difficulty(j.nbits));  // it was a block
    CHECK(work::nonce_difficulty(w, 2083236894, 1) < 1.0);  // the next nonce isn't
}

static void test_extranonce2_width()
{
    StratumJob j = genesis_job();
    Work w;
    AsicJob cj;
    work::build(j, GENESIS_EN1, 0x2a, 8, 0x1fffe000, 1, w, cj);
    CHECK(strcmp(w.extranonce2, "000000000000002a") == 0);
}

// A current-style coinbase: BIP34 height, pool tag, two outputs.
static void test_describe_template()
{
    StratumJob j = genesis_job();
    j.coinb1 = (char *) "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff"
                        "1a03e2d90e2f736f6c6f2d706f6f6c2d72732f";  // height 973282, "/solo-pool-rs/"
    j.coinb2 = (char *) "ffffffff02"
                        "205fa01200000000" "160014" "0000000000000000000000000000000000000000"
                        "107a070000000000" "160014" "1111111111111111111111111111111111111111"
                        "00000000";
    BlockTemplate t;
    work::describe_template(j, "00000000", 4, t);
    CHECK(t.valid);
    CHECK(t.height == 973282);
    CHECK(strcmp(t.tag, "/solo-pool-rs/") == 0);
    CHECK_NEAR(t.reward_btc, 3.125 + 0.0049, 1e-9);  // 312500000 + 490000 sat
}

// ---------------------------------------------------------------- SV2
static void le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void test_sv2_targets()
{
    uint8_t t[32] = {};
    t[26] = 0xff;  // 0x00000000ffff0000...: difficulty 1, little endian
    t[27] = 0xff;
    CHECK_NEAR(sv2_target_to_diff(t), 1.0, 1e-9);

    // difficulty 1500.5 must stay fractional (truncating it caused rejects)
    uint8_t f[32];
    work::hex2bin("0000000000000000000000000000000000000000cc9544bce9ac2b0000000000", f, 32);
    CHECK_NEAR(sv2_target_to_diff(f), 1500.5, 1e-6);
    CHECK(sv2_target_to_pdiff(f) == 1500);
}

static void test_sv2_frames()
{
    uint8_t h[6] = {0x00, 0x80, 0x21, 0x24, 0x00, 0x00};  // channel msg 0x21, 36 bytes
    sv2_frame_header_t fh;
    CHECK(sv2_parse_frame_header(h, &fh) == 0);
    CHECK(fh.msg_type == SV2_MSG_SET_TARGET && fh.msg_length == 36);

    uint8_t st[36] = {};
    le32(st, 7);
    st[4 + 27] = 0xff;
    uint32_t ch = 0;
    uint8_t target[32];
    CHECK(sv2_parse_set_target(st, sizeof(st), &ch, target) == 0);
    CHECK(ch == 7 && target[27] == 0xff);
    CHECK(sv2_parse_set_target(st, 35, &ch, target) != 0);  // short

    uint8_t ok[20] = {};
    le32(ok, 7);
    le32(ok + 4, 41);  // last sequence number
    le32(ok + 8, 3);   // newly accepted
    uint32_t n = 0;
    CHECK(sv2_parse_submit_shares_success(ok, sizeof(ok), &ch, &n) == 0);
    CHECK(ch == 7 && n == 3);
    CHECK(sv2_parse_submit_shares_success(ok, 19, &ch, &n) != 0);

    uint8_t cc[12] = {};
    le32(cc, 9);
    cc[4] = 7;
    memcpy(cc + 5, "timeout", 7);
    char reason[16];
    CHECK(sv2_parse_close_channel(cc, sizeof(cc), &ch, reason, sizeof(reason)) == 0);
    CHECK(ch == 9 && strcmp(reason, "timeout") == 0);
    CHECK(sv2_parse_close_channel(cc, 4, &ch, reason, sizeof(reason)) != 0);
}

int main()
{
    test_hex();
    test_nbits();
    test_genesis_build_and_hash();
    test_extranonce2_width();
    test_describe_template();
    test_sv2_targets();
    test_sv2_frames();
    printf("%d checks, %d failed\n", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
