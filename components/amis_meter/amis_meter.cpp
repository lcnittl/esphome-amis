#include "amis_meter.h"
#include "aes128_cbc.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"

#include <algorithm>
#include <cstring>

namespace esphome {
namespace amis_meter {

static const char *const TAG = "amis_meter";

// M-Bus long frame (SND_UD) layout of the AMIS TD-3511/TD-3512 telegram:
//   0: 0x68 | 1: L | 2: L repeat | 3: 0x68 | 4: C field (0x53/0x73)
//   5: primary address (0xF0) | 6: CI (0x5B) | 7-14: secondary address
//   15: access number | 16: status | 17-18: signature
//   19-98: encrypted user data (5 AES blocks) | 99: checksum | 100: 0x16
static const uint8_t AMIS_USER_DATA_LEN = 0x5F;
static const size_t OFFS_ENCRYPTED = 19;

// SND_NKE search request the meter broadcasts every minute until a reader ACKs
static const uint8_t SND_NKE[5] = {0x10, 0x40, 0xF0, 0x30, 0x16};
static const uint8_t MBUS_ACK = 0xE5;

// OBIS codes (A.B.C.D.E.F) of the values contained in the telegram
static const char *const OBIS_TIMESTAMP = "0.0.1.0.0.255";               // date + time
static const char *const OBIS_ACTIVE_ENERGY_PLUS = "1.0.1.8.0.255";      // 1.8.0, energy A+ [Wh]
static const char *const OBIS_ACTIVE_ENERGY_MINUS = "1.0.2.8.0.255";     // 2.8.0, energy A- [Wh]
static const char *const OBIS_REACTIVE_ENERGY_PLUS = "1.0.3.8.1.255";    // 3.8.1, energy R+ [varh]
static const char *const OBIS_REACTIVE_ENERGY_MINUS = "1.0.4.8.1.255";   // 4.8.1, energy R- [varh]
static const char *const OBIS_ACTIVE_POWER_PLUS = "1.0.1.7.0.255";       // 1.7.0, power P+ [W]
static const char *const OBIS_ACTIVE_POWER_MINUS = "1.0.2.7.0.255";      // 2.7.0, power P- [W]
static const char *const OBIS_REACTIVE_POWER_PLUS = "1.0.3.7.0.255";     // 3.7.0, power Q+ [var]
static const char *const OBIS_REACTIVE_POWER_MINUS = "1.0.4.7.0.255";    // 4.7.0, power Q- [var]
static const char *const OBIS_PREPAYMENT_COUNTER = "1.0.1.128.0.255";    // 1.128.0, Inkassozaehlwerk

static uint8_t dif2len(uint8_t dif) {
  switch (dif & 0x0F) {
    case 0x0:
      return 0;
    case 0x1:
      return 1;
    case 0x2:
      return 2;
    case 0x3:
      return 3;
    case 0x4:
    case 0x5:
      return 4;
    case 0x6:
      return 6;
    case 0x7:
      return 8;
    case 0x8:
      return 0;
    case 0x9:
      return 1;
    case 0xA:
      return 2;
    case 0xB:
      return 3;
    case 0xC:
      return 4;
    case 0xD:
      // variable data length, length stored in data field
      return 0;
    case 0xE:
      return 6;
    case 0xF:
      return 8;
    default:  // never reached
      return 0;
  }
}

static uint32_t read_le_uint(const uint8_t *data, size_t len) {
  uint32_t value = 0;
  for (size_t i = 0; i < len && i < 4; i++)
    value |= (uint32_t) data[i] << (8 * i);
  return value;
}

void AmisMeterComponent::set_decryption_key(const std::string &decryption_key) {
  if (!parse_hex(decryption_key, this->key_, 16)) {
    ESP_LOGE(TAG, "Decryption key must be 32 hexadecimal characters");
  }
}

void AmisMeterComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "AMIS smart meter:");
  ESP_LOGCONFIG(TAG, "  Receive timeout: %u ms", (unsigned) this->receive_timeout_ms_);
#ifdef USE_SENSOR
  for (auto &item : this->sensors_) {
    LOG_SENSOR("  ", "Sensor", item.sensor);
    ESP_LOGCONFIG(TAG, "    OBIS code: %s", item.obis_code.c_str());
  }
#endif
#ifdef USE_TEXT_SENSOR
  for (auto &item : this->text_sensors_) {
    LOG_TEXT_SENSOR("  ", "Text sensor", item.sensor);
    ESP_LOGCONFIG(TAG, "    OBIS code: %s", item.obis_code.c_str());
  }
#endif
  this->check_uart_settings(9600, 1, uart::UART_CONFIG_PARITY_EVEN, 8);
}

void AmisMeterComponent::loop() {
  const uint32_t now = millis();
  bool got_data = false;

  while (true) {
    int avail = this->available();
    if (avail <= 0)
      break;
    size_t free = sizeof(this->buffer_) - this->bytes_;
    if (free == 0)
      break;
    size_t to_read = std::min((size_t) avail, free);
    if (!this->read_array(&this->buffer_[this->bytes_], to_read))
      break;
    this->bytes_ += to_read;
    got_data = true;
  }

  if (got_data) {
    this->last_rx_time_ = now;
  } else if (this->bytes_ > 0 && (now - this->last_rx_time_) > this->receive_timeout_ms_) {
    ESP_LOGW(TAG, "Receive timeout, discarding %u buffered bytes", (unsigned) this->bytes_);
    this->reset_rx_();
    return;
  }

  this->process_buffer_();

  if (this->bytes_ == sizeof(this->buffer_)) {
    ESP_LOGW(TAG, "RX buffer full without a valid frame, clearing");
    this->reset_rx_();
  }
}

void AmisMeterComponent::process_buffer_() {
  while (this->bytes_ > 0) {
    // discard noise until a plausible frame start (0x10 short frame, 0x68 long frame)
    size_t skip = 0;
    while (skip < this->bytes_ && this->buffer_[skip] != 0x68 && this->buffer_[skip] != 0x10)
      skip++;
    if (skip > 0) {
      ESP_LOGV(TAG, "Skipping %u bytes while searching for frame start", (unsigned) skip);
      this->consume_(skip);
      continue;
    }

    if (this->buffer_[0] == 0x10) {
      if (this->bytes_ < sizeof(SND_NKE))
        return;  // wait for the rest of the short frame
      if (memcmp(this->buffer_, SND_NKE, sizeof(SND_NKE)) == 0) {
        // the meter is searching for a reader, acknowledge to start reception
        ESP_LOGD(TAG, "Received SND_NKE, sending ACK");
        this->write_byte(MBUS_ACK);
        this->consume_(sizeof(SND_NKE));
      } else {
        this->consume_(1);  // not a frame we know, resync
      }
      continue;
    }

    // long frame: 0x68 L L 0x68 ... checksum 0x16
    if (this->bytes_ < 4)
      return;  // wait for the header
    if (this->buffer_[1] != this->buffer_[2] || this->buffer_[3] != 0x68) {
      this->consume_(1);  // invalid header, resync
      continue;
    }
    size_t frame_len = (size_t) this->buffer_[1] + 6;
    if (this->bytes_ < frame_len)
      return;  // wait for the rest of the frame

    if (this->handle_frame_(frame_len)) {
      this->consume_(frame_len);
    } else {
      this->consume_(1);  // frame boundary looks wrong, resync
    }
  }
}

bool AmisMeterComponent::handle_frame_(size_t frame_len) {
  if (this->buffer_[frame_len - 1] != 0x16) {
    ESP_LOGW(TAG, "Frame stop sign missing, resyncing");
    return false;
  }

  // acknowledge reception right away, the meter waits for the ACK
  this->write_byte(MBUS_ACK);

  uint8_t checksum = 0;
  for (size_t i = 4; i < frame_len - 2; i++)
    checksum += this->buffer_[i];
  if (checksum != this->buffer_[frame_len - 2]) {
    ESP_LOGW(TAG, "Frame checksum mismatch, discarding");
    return true;
  }

  const uint8_t c_field = this->buffer_[4];
  if (c_field != 0x53 && c_field != 0x73) {
    ESP_LOGW(TAG, "Unexpected C field 0x%02X, discarding", c_field);
    return true;
  }

  if (this->buffer_[1] != AMIS_USER_DATA_LEN) {
    ESP_LOGW(TAG, "Unexpected telegram length 0x%02X (expected 0x%02X), discarding", this->buffer_[1],
             AMIS_USER_DATA_LEN);
    return true;
  }

  if (this->decrypt_frame_())
    this->parse_frame_();
  return true;
}

bool AmisMeterComponent::decrypt_frame_() {
  // The IV is built from the secondary address (offsets 7-14) and the access number (offset 15):
  // vendor id, identification number, version, medium, then the access number repeated
  uint8_t iv[16];
  iv[0] = this->buffer_[11];
  iv[1] = this->buffer_[12];
  iv[2] = this->buffer_[7];
  iv[3] = this->buffer_[8];
  iv[4] = this->buffer_[9];
  iv[5] = this->buffer_[10];
  iv[6] = this->buffer_[13];
  iv[7] = this->buffer_[14];
  for (size_t i = 8; i < 16; i++)
    iv[i] = this->buffer_[15];

  if (!aes128_cbc_decrypt(this->key_, iv, this->buffer_ + OFFS_ENCRYPTED, this->decrypted_, AMIS_DECRYPTED_SIZE)) {
    ESP_LOGE(TAG, "Decryption error");
    return false;
  }

  if (this->decrypted_[0] != 0x2F || this->decrypted_[1] != 0x2F) {
    ESP_LOGE(TAG, "Decryption failed, please check your decryption_key");
    return false;
  }
  return true;
}

void AmisMeterComponent::parse_frame_() {
  size_t i = 2;  // skip the leading 0x2F 0x2F filler bytes

  while (i < AMIS_DECRYPTED_SIZE) {
    const uint8_t dif = this->decrypted_[i];
    if (dif == 0x2F) {  // M-Bus idle filler
      i++;
      continue;
    }
    if ((dif & 0x0F) == 0x0D || dif == 0x0F || dif == 0x1F) {
      ESP_LOGW(TAG, "Variable length data not supported, stopping parse");
      break;
    }

    const uint8_t data_len = dif2len(dif);
    uint8_t dife = 0;
    while ((this->decrypted_[i] & 0x80) && i + 1 < AMIS_DECRYPTED_SIZE) {
      dife = this->decrypted_[i + 1];
      i++;
    }
    i++;
    if (i >= AMIS_DECRYPTED_SIZE)
      break;

    const uint8_t vif = this->decrypted_[i];
    uint8_t vife = 0;
    while ((this->decrypted_[i] & 0x80) && i + 1 < AMIS_DECRYPTED_SIZE) {
      vife = this->decrypted_[i + 1];
      i++;
    }
    i++;
    if (i + data_len > AMIS_DECRYPTED_SIZE)
      break;

    const uint8_t *data = &this->decrypted_[i];

    if (vif == 0x6D && data_len >= 5) {
      // date + time, M-Bus CP48 format
      if ((data[1] & 0x80) == 0x80) {
        ESP_LOGD(TAG, "Meter time invalid, skipping timestamp");
      } else {
        ESPTime time{};
        time.second = data[0] & 0x3F;
        time.minute = data[1] & 0x3F;
        time.hour = data[2] & 0x1F;
        time.day_of_month = data[3] & 0x1F;
        time.month = data[4] & 0x0F;
        time.year = 2000 + (((data[3] & 0xE0) >> 5) | ((data[4] & 0xF0) >> 1));
        time.is_dst = (data[0] & 0x40) == 0x40;
        time.recalc_timestamp_local();

        char iso[20];
        if (time.strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S") != 0) {
          ESP_LOGV(TAG, "%s: %s", OBIS_TIMESTAMP, iso);
          this->publish_text_(OBIS_TIMESTAMP, iso);
        }
        if (time.timestamp != -1)
          this->publish_value_(OBIS_TIMESTAMP, time.timestamp);
      }
    } else if (dif == 0x04 && vif == 0x03) {
      // 1.8.0
      this->publish_value_(OBIS_ACTIVE_ENERGY_PLUS, read_le_uint(data, data_len));
    } else if (dif == 0x04 && vif == 0x83 && vife == 0x3C) {
      // 2.8.0
      this->publish_value_(OBIS_ACTIVE_ENERGY_MINUS, read_le_uint(data, data_len));
    } else if (dif == 0x84 && dife == 0x10 && vif == 0xFB && vife == 0x73) {
      // 3.8.1
      this->publish_value_(OBIS_REACTIVE_ENERGY_PLUS, read_le_uint(data, data_len));
    } else if (dif == 0x84 && dife == 0x10 && vif == 0xFB && vife == 0x3C) {
      // 4.8.1
      this->publish_value_(OBIS_REACTIVE_ENERGY_MINUS, read_le_uint(data, data_len));
    } else if (dif == 0x04 && vif == 0x2B) {
      // 1.7.0
      this->publish_value_(OBIS_ACTIVE_POWER_PLUS, read_le_uint(data, data_len));
    } else if (dif == 0x04 && vif == 0xAB && vife == 0x3C) {
      // 2.7.0
      this->publish_value_(OBIS_ACTIVE_POWER_MINUS, read_le_uint(data, data_len));
    } else if (dif == 0x04 && dife == 0x00 && vif == 0xFB && vife == 0x14) {
      // 3.7.0
      this->publish_value_(OBIS_REACTIVE_POWER_PLUS, read_le_uint(data, data_len));
    } else if (dif == 0x04 && dife == 0x00 && vif == 0xFB && vife == 0x3C) {
      // 4.7.0
      this->publish_value_(OBIS_REACTIVE_POWER_MINUS, read_le_uint(data, data_len));
    } else if (dif == 0x04 && vif == 0x83 && vife == 0x04) {
      // 1.128.0 (Inkassozaehlwerk, signed)
      this->publish_value_(OBIS_PREPAYMENT_COUNTER, (int32_t) read_le_uint(data, data_len));
    } else {
      ESP_LOGV(TAG, "Unhandled data point: DIF=0x%02X DIFE=0x%02X VIF=0x%02X VIFE=0x%02X", dif, dife, vif, vife);
    }

    i += data_len;
  }

  ESP_LOGD(TAG, "Frame decoded");
}

void AmisMeterComponent::publish_value_(const char *obis_code, float value) {
  ESP_LOGV(TAG, "%s: %.0f", obis_code, value);
#ifdef USE_SENSOR
  for (auto &item : this->sensors_) {
    if (item.obis_code == obis_code)
      item.sensor->publish_state(value);
  }
#endif
}

void AmisMeterComponent::publish_text_(const char *obis_code, const char *value) {
#ifdef USE_TEXT_SENSOR
  for (auto &item : this->text_sensors_) {
    if (item.obis_code == obis_code)
      item.sensor->publish_state(value);
  }
#endif
}

void AmisMeterComponent::consume_(size_t n) {
  if (n >= this->bytes_) {
    this->bytes_ = 0;
    return;
  }
  memmove(this->buffer_, this->buffer_ + n, this->bytes_ - n);
  this->bytes_ -= n;
}

void AmisMeterComponent::reset_rx_() { this->bytes_ = 0; }

}  // namespace amis_meter
}  // namespace esphome
