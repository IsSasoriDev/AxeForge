#include "sv2_aead.h"

#include <string.h>

#include "mbedtls/chachapoly.h"

// 12-byte nonce: 4 zero bytes + 8-byte LE counter
static void build_nonce(uint64_t counter, uint8_t nonce[12])
{
    memset(nonce, 0, 4);
    for (int i = 0; i < 8; i++) {
        nonce[4 + i] = (uint8_t)(counter >> (i * 8));
    }
}

int sv2_aead_encrypt(const uint8_t key[32], uint64_t nonce_counter, const uint8_t *aad, size_t aad_len,
                     const uint8_t *plaintext, size_t pt_len, uint8_t *out)
{
    uint8_t nonce[12];
    build_nonce(nonce_counter, nonce);

    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    mbedtls_chachapoly_setkey(&ctx, key);
    int ret = mbedtls_chachapoly_encrypt_and_tag(&ctx, pt_len, nonce, aad, aad_len, plaintext, out,
                                                 out + pt_len);  // 16-byte tag appended
    mbedtls_chachapoly_free(&ctx);
    return ret;
}

int sv2_aead_decrypt(const uint8_t key[32], uint64_t nonce_counter, const uint8_t *aad, size_t aad_len,
                     const uint8_t *ciphertext, size_t ct_len, uint8_t *out)
{
    if (ct_len < SV2_NOISE_MAC_LEN) return -1;

    uint8_t nonce[12];
    build_nonce(nonce_counter, nonce);

    size_t pt_len = ct_len - SV2_NOISE_MAC_LEN;
    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    mbedtls_chachapoly_setkey(&ctx, key);
    int ret = mbedtls_chachapoly_auth_decrypt(&ctx, pt_len, nonce, aad, aad_len, ciphertext + pt_len, ciphertext,
                                              out);
    mbedtls_chachapoly_free(&ctx);
    return ret;
}

size_t sv2_noise_payload_ct_len(size_t pt_len)
{
    size_t chunks = (pt_len + SV2_NOISE_CHUNK_PT_MAX - 1) / SV2_NOISE_CHUNK_PT_MAX;
    return pt_len + chunks * SV2_NOISE_MAC_LEN;
}

int sv2_noise_seal_payload(const uint8_t key[32], uint64_t *nonce, const uint8_t *pt, size_t pt_len,
                           uint8_t *out)
{
    while (pt_len > 0) {
        size_t n = pt_len < SV2_NOISE_CHUNK_PT_MAX ? pt_len : SV2_NOISE_CHUNK_PT_MAX;
        if (sv2_aead_encrypt(key, (*nonce)++, NULL, 0, pt, n, out) != 0) return -1;
        pt += n;
        out += n + SV2_NOISE_MAC_LEN;
        pt_len -= n;
    }
    return 0;
}

int sv2_noise_open_payload(const uint8_t key[32], uint64_t *nonce, const uint8_t *ct, size_t pt_len,
                           uint8_t *out)
{
    while (pt_len > 0) {
        size_t n = pt_len < SV2_NOISE_CHUNK_PT_MAX ? pt_len : SV2_NOISE_CHUNK_PT_MAX;
        if (sv2_aead_decrypt(key, (*nonce)++, NULL, 0, ct, n + SV2_NOISE_MAC_LEN, out) != 0) return -1;
        ct += n + SV2_NOISE_MAC_LEN;
        out += n;
        pt_len -= n;
    }
    return 0;
}
