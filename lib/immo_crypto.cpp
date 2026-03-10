#include "immo_crypto.h"
#include <nrf_soc.h>
#include <string.h>

#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

namespace immo {
namespace {

void le32_write(uint8_t out[4], uint32_t x) {
  out[0] = static_cast<uint8_t>(x & 0xFF);
  out[1] = static_cast<uint8_t>((x >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((x >> 16) & 0xFF);
  out[3] = static_cast<uint8_t>((x >> 24) & 0xFF);
}

bool aes128_ecb_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  nrf_ecb_hal_data_t ecb{};
  memcpy(ecb.key, key, 16);
  memcpy(ecb.cleartext, in, 16);
  const uint32_t err = sd_ecb_block_encrypt(&ecb);
  if (err != NRF_SUCCESS) return false;
  memcpy(out, ecb.ciphertext, 16);
  return true;
}

void xor_block(uint8_t dst[16], const uint8_t a[16], const uint8_t b[16]) {
  for (size_t i = 0; i < 16; i++) dst[i] = a[i] ^ b[i];
}

}  // namespace

void build_nonce(uint32_t counter, uint8_t nonce[NONCE_LEN]) {
  le32_write(&nonce[0], counter);
  for (size_t i = 4; i < NONCE_LEN; i++) nonce[i] = 0;
}

void build_msg(uint32_t counter, Command command, uint8_t msg[MSG_LEN]) {
  le32_write(&msg[0], counter);
  msg[4] = static_cast<uint8_t>(command);
}

bool ccm_auth_encrypt(const uint8_t key[16], const uint8_t nonce[NONCE_LEN], const uint8_t* msg, size_t msg_len, size_t aad_len, uint8_t* out_ct, uint8_t out_mic[MIC_LEN]) {
  if (msg_len > 0xFFFFu || aad_len > msg_len) return false;

  const uint8_t L = 2;
  const uint8_t M = MIC_LEN;
  const uint8_t flags_b0 = static_cast<uint8_t>(((M - 2) / 2) << 3) | static_cast<uint8_t>(L - 1);

  uint8_t b0[16]{};
  b0[0] = flags_b0;
  memcpy(&b0[1], nonce, NONCE_LEN);
  b0[14] = static_cast<uint8_t>((msg_len >> 8) & 0xFF);
  b0[15] = static_cast<uint8_t>(msg_len & 0xFF);

  uint8_t x[16]{};
  uint8_t tmp[16]{};
  xor_block(tmp, x, b0);
  if (!aes128_ecb_encrypt(key, tmp, x)) return false;

  size_t offset = 0;
  while (offset < msg_len) {
    uint8_t block[16]{};
    const size_t n = min(static_cast<size_t>(16), msg_len - offset);
    memcpy(block, msg + offset, n);
    xor_block(tmp, x, block);
    if (!aes128_ecb_encrypt(key, tmp, x)) return false;
    offset += n;
  }

  uint8_t a0[16]{};
  a0[0] = static_cast<uint8_t>(L - 1);
  memcpy(&a0[1], nonce, NONCE_LEN);
  a0[14] = 0;
  a0[15] = 0;

  uint8_t s0[16]{};
  if (!aes128_ecb_encrypt(key, a0, s0)) return false;

  for (size_t i = 0; i < M; i++) out_mic[i] = static_cast<uint8_t>(x[i] ^ s0[i]);

  for (size_t i = 0; i < aad_len; i++) {
    out_ct[i] = msg[i];
  }

  size_t offset_enc = aad_len;
  for (uint16_t ctr_i = 1; offset_enc < msg_len; ctr_i++) {
    uint8_t ai[16]{};
    ai[0] = static_cast<uint8_t>(L - 1);
    memcpy(&ai[1], nonce, NONCE_LEN);
    ai[14] = static_cast<uint8_t>((ctr_i >> 8) & 0xFF);
    ai[15] = static_cast<uint8_t>(ctr_i & 0xFF);

    uint8_t si[16]{};
    if (!aes128_ecb_encrypt(key, ai, si)) return false;

    const size_t n = min(static_cast<size_t>(16), msg_len - offset_enc);
    for (size_t j = 0; j < n; j++) {
      out_ct[offset_enc + j] = msg[offset_enc + j] ^ si[j];
    }
    offset_enc += n;
  }

  return true;
}

bool ccm_auth_decrypt(const uint8_t key[16], const uint8_t nonce[NONCE_LEN], const uint8_t* ct, size_t ct_len, size_t aad_len, uint8_t* out_msg, uint8_t out_mic[MIC_LEN]) {
  if (ct_len > 0xFFFFu || aad_len > ct_len) return false;

  for (size_t i = 0; i < aad_len; i++) {
    out_msg[i] = ct[i];
  }

  const uint8_t L = 2;
  const uint8_t M = MIC_LEN;
  size_t offset_enc = aad_len;
  for (uint16_t ctr_i = 1; offset_enc < ct_len; ctr_i++) {
    uint8_t ai[16]{};
    ai[0] = static_cast<uint8_t>(L - 1);
    memcpy(&ai[1], nonce, NONCE_LEN);
    ai[14] = static_cast<uint8_t>((ctr_i >> 8) & 0xFF);
    ai[15] = static_cast<uint8_t>(ctr_i & 0xFF);

    uint8_t si[16]{};
    if (!aes128_ecb_encrypt(key, ai, si)) return false;

    const size_t n = min(static_cast<size_t>(16), ct_len - offset_enc);
    for (size_t j = 0; j < n; j++) {
      out_msg[offset_enc + j] = ct[offset_enc + j] ^ si[j];
    }
    offset_enc += n;
  }

  const uint8_t flags_b0 = static_cast<uint8_t>(((M - 2) / 2) << 3) | static_cast<uint8_t>(L - 1);
  uint8_t b0[16]{};
  b0[0] = flags_b0;
  memcpy(&b0[1], nonce, NONCE_LEN);
  b0[14] = static_cast<uint8_t>((ct_len >> 8) & 0xFF);
  b0[15] = static_cast<uint8_t>(ct_len & 0xFF);

  uint8_t x[16]{};
  uint8_t tmp[16]{};
  xor_block(tmp, x, b0);
  if (!aes128_ecb_encrypt(key, tmp, x)) return false;

  size_t offset = 0;
  while (offset < ct_len) {
    uint8_t block[16]{};
    const size_t n = min(static_cast<size_t>(16), ct_len - offset);
    memcpy(block, out_msg + offset, n);
    xor_block(tmp, x, block);
    if (!aes128_ecb_encrypt(key, tmp, x)) return false;
    offset += n;
  }

  uint8_t a0[16]{};
  a0[0] = static_cast<uint8_t>(L - 1);
  memcpy(&a0[1], nonce, NONCE_LEN);
  a0[14] = 0;
  a0[15] = 0;

  uint8_t s0[16]{};
  if (!aes128_ecb_encrypt(key, a0, s0)) return false;

  for (size_t i = 0; i < M; i++) out_mic[i] = static_cast<uint8_t>(x[i] ^ s0[i]);

  return true;
}

bool constant_time_eq(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t diff = 0;
  for (size_t i = 0; i < n; i++) diff |= (a[i] ^ b[i]);
  return diff == 0;
}

}  // namespace immo
