#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome::ltr303als {

class LTR303ALSSensor : public sensor::Sensor, public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void update() override;
  void set_window_correction(float window_correction) { this->window_correction_ = window_correction; }

 protected:
  bool write_register_(uint8_t reg, uint8_t value);
  bool read_registers_(uint8_t reg, uint8_t *data, size_t length);
  float calculate_lux_(uint16_t ch0, uint16_t ch1) const;
  float window_correction_{1.0f};
};

}  // namespace esphome::ltr303als
