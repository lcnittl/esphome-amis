#include "aes128_cbc.h"

#include "esphome/core/defines.h"
#include "esphome/core/log.h"

#include <cstring>

#if __has_include(<psa/crypto.h>)
#define AMIS_METER_CRYPTO_TFPSA
#include <psa/crypto.h>
#elif !defined(USE_ESP8266) && __has_include(<mbedtls/aes.h>)
#define AMIS_METER_CRYPTO_MBEDTLS
#if __has_include(<mbedtls/esp_config.h>)
#include <mbedtls/esp_config.h>
#endif
#include <mbedtls/aes.h>
#elif __has_include(<bearssl/bearssl.h>)
#define AMIS_METER_CRYPTO_BEARSSL
#include <bearssl/bearssl.h>
#endif

namespace esphome {
namespace amis_meter {

static const char *const TAG = "amis_meter";

// the AMIS telegram carries 80 bytes (5 AES blocks) of encrypted user data
static const size_t MAX_DATA_LEN = 80;

#if defined(AMIS_METER_CRYPTO_TFPSA)

bool aes128_cbc_decrypt(const uint8_t *key, const uint8_t *iv, const uint8_t *input, uint8_t *output, size_t len) {
  if (len > MAX_DATA_LEN || (len % 16) != 0)
    return false;

  psa_status_t status = psa_crypto_init();
  if (status != PSA_SUCCESS) {
    ESP_LOGE(TAG, "psa_crypto_init failed: %d", (int) status);
    return false;
  }

  psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
  psa_set_key_bits(&attributes, 128);
  psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DECRYPT);
  psa_set_key_algorithm(&attributes, PSA_ALG_CBC_NO_PADDING);

  psa_key_id_t key_id = 0;
  status = psa_import_key(&attributes, key, 16, &key_id);
  if (status != PSA_SUCCESS) {
    ESP_LOGE(TAG, "psa_import_key failed: %d", (int) status);
    return false;
  }

  // the one-shot API expects the IV prepended to the ciphertext
  uint8_t iv_and_data[16 + MAX_DATA_LEN];
  memcpy(iv_and_data, iv, 16);
  memcpy(iv_and_data + 16, input, len);

  size_t output_len = 0;
  status = psa_cipher_decrypt(key_id, PSA_ALG_CBC_NO_PADDING, iv_and_data, 16 + len, output, len, &output_len);
  psa_destroy_key(key_id);
  if (status != PSA_SUCCESS || output_len != len) {
    ESP_LOGE(TAG, "psa_cipher_decrypt failed: %d", (int) status);
    return false;
  }
  return true;
}

#elif defined(AMIS_METER_CRYPTO_MBEDTLS)

bool aes128_cbc_decrypt(const uint8_t *key, const uint8_t *iv, const uint8_t *input, uint8_t *output, size_t len) {
  if (len > MAX_DATA_LEN || (len % 16) != 0)
    return false;

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  if (mbedtls_aes_setkey_dec(&ctx, key, 128) != 0) {
    mbedtls_aes_free(&ctx);
    ESP_LOGE(TAG, "mbedtls_aes_setkey_dec failed");
    return false;
  }
  uint8_t iv_copy[16];
  memcpy(iv_copy, iv, 16);
  int ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, len, iv_copy, input, output);
  mbedtls_aes_free(&ctx);
  if (ret != 0) {
    ESP_LOGE(TAG, "mbedtls_aes_crypt_cbc failed: %d", ret);
    return false;
  }
  return true;
}

#elif defined(AMIS_METER_CRYPTO_BEARSSL)

bool aes128_cbc_decrypt(const uint8_t *key, const uint8_t *iv, const uint8_t *input, uint8_t *output, size_t len) {
  if (len > MAX_DATA_LEN || (len % 16) != 0)
    return false;

  br_aes_big_cbcdec_keys ctx;
  br_aes_big_cbcdec_init(&ctx, key, 16);
  uint8_t iv_copy[16];
  memcpy(iv_copy, iv, 16);
  if (output != input)
    memcpy(output, input, len);
  br_aes_big_cbcdec_run(&ctx, iv_copy, output, len);  // decrypts in place
  return true;
}

#else

// fallback for platforms without a supported crypto library
bool aes128_cbc_decrypt(const uint8_t *key, const uint8_t *iv, const uint8_t *input, uint8_t *output, size_t len) {
  ESP_LOGE(TAG, "No supported crypto library available on this platform");
  return false;
}

#endif

}  // namespace amis_meter
}  // namespace esphome
