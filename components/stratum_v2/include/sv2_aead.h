#ifndef SV2_AEAD_H
#define SV2_AEAD_H

// Noise transport encryption (ChaCha20-Poly1305) for SV2 frames. Pure code with
// no ESP-IDF dependency, so the host tests can run it.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SV2_NOISE_MAC_LEN 16
// A frame payload is encrypted in chunks of at most 65535 bytes each, MAC
// included, every chunk under the next nonce (as SRI's codec_sv2 does).
#define SV2_NOISE_CHUNK_PT_MAX (65535 - SV2_NOISE_MAC_LEN)

// One AEAD block. out receives pt_len + 16 bytes (tag appended).
// Returns 0, or the mbedtls error code.
int sv2_aead_encrypt(const uint8_t key[32], uint64_t nonce, const uint8_t *aad, size_t aad_len,
                     const uint8_t *plaintext, size_t pt_len, uint8_t *out);

// One AEAD block. ciphertext ends in the 16-byte tag; out receives ct_len - 16 bytes.
// Returns 0, -1 when ct_len is shorter than the tag, or the mbedtls error code.
int sv2_aead_decrypt(const uint8_t key[32], uint64_t nonce, const uint8_t *aad, size_t aad_len,
                     const uint8_t *ciphertext, size_t ct_len, uint8_t *out);

// Encrypted size of a frame payload of pt_len bytes: one MAC per chunk.
size_t sv2_noise_payload_ct_len(size_t pt_len);

// Encrypt a frame payload chunk by chunk, advancing *nonce once per chunk.
// out must hold sv2_noise_payload_ct_len(pt_len) bytes.
int sv2_noise_seal_payload(const uint8_t key[32], uint64_t *nonce, const uint8_t *pt, size_t pt_len,
                           uint8_t *out);

// Decrypt a frame payload of pt_len plaintext bytes from
// sv2_noise_payload_ct_len(pt_len) bytes of ciphertext, advancing *nonce once per chunk.
int sv2_noise_open_payload(const uint8_t key[32], uint64_t *nonce, const uint8_t *ct, size_t pt_len,
                           uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SV2_AEAD_H */
