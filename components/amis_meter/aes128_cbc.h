#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace amis_meter {

/// Decrypt AES-128-CBC data using the platform crypto library.
///
/// The backend is selected the same way as in the official dlms_meter
/// component: TF-PSA / PSA crypto API when available, then the mbedtls
/// legacy API, then BearSSL (ESP8266).
///
/// `input` and `output` may point to the same buffer. `len` must be a
/// multiple of 16.
bool aes128_cbc_decrypt(const uint8_t *key, const uint8_t *iv, const uint8_t *input, uint8_t *output, size_t len);

}  // namespace amis_meter
}  // namespace esphome
