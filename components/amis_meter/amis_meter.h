#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/uart/uart.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

#include <string>
#include <vector>

namespace esphome {
namespace amis_meter {

static const size_t AMIS_BUFFER_SIZE = 512;
static const size_t AMIS_DECRYPTED_SIZE = 80;

class AmisMeterComponent : public Component, public uart::UARTDevice {
 public:
  void dump_config() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_decryption_key(const std::string &decryption_key);
  void set_receive_timeout(uint32_t timeout_ms) { this->receive_timeout_ms_ = timeout_ms; }

#ifdef USE_SENSOR
  void register_sensor(const std::string &obis_code, sensor::Sensor *sens) {
    this->sensors_.push_back({obis_code, sens});
  }
#endif
#ifdef USE_TEXT_SENSOR
  void register_text_sensor(const std::string &obis_code, text_sensor::TextSensor *sens) {
    this->text_sensors_.push_back({obis_code, sens});
  }
#endif

 protected:
  void process_buffer_();
  bool handle_frame_(size_t frame_len);
  bool decrypt_frame_();
  void parse_frame_();
  void consume_(size_t n);
  void reset_rx_();
  void publish_value_(const char *obis_code, float value);
  void publish_text_(const char *obis_code, const char *value);

  uint8_t buffer_[AMIS_BUFFER_SIZE];
  size_t bytes_{0};
  uint8_t decrypted_[AMIS_DECRYPTED_SIZE];
  uint8_t key_[16];
  uint32_t receive_timeout_ms_{1000};
  uint32_t last_rx_time_{0};

#ifdef USE_SENSOR
  struct SensorItem {
    std::string obis_code;
    sensor::Sensor *sensor;
  };
  std::vector<SensorItem> sensors_;
#endif
#ifdef USE_TEXT_SENSOR
  struct TextSensorItem {
    std::string obis_code;
    text_sensor::TextSensor *sensor;
  };
  std::vector<TextSensorItem> text_sensors_;
#endif
};

}  // namespace amis_meter
}  // namespace esphome
