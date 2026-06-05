#include "ltr303als.h"

#include <cmath>

namespace esphome::ltr303als {

static constexpr uint8_t REG_ALS_CONTR = 0x80;
static constexpr uint8_t REG_ALS_MEAS_RATE = 0x85;
static constexpr uint8_t REG_ALS_DATA_CH1_0 = 0x88;
static constexpr uint8_t REG_ALS_STATUS = 0x8C;

static constexpr uint8_t ALS_CONTR_GAIN_1X_ACTIVE = 0x01;
static constexpr uint8_t ALS_MEAS_100MS_500MS = 0x03;
static constexpr uint8_t ALS_STATUS_DATA_INVALID = 0x80;

void LTR303ALSSensor::setup() {
  if (!this->write_register_(REG_ALS_MEAS_RATE, ALS_MEAS_100MS_500MS)) {
    this->mark_failed();
    return;
  }

  if (!this->write_register_(REG_ALS_CONTR, ALS_CONTR_GAIN_1X_ACTIVE)) {
    this->mark_failed();
  }
}

void LTR303ALSSensor::update() {
  uint8_t status = 0;
  if (!this->read_registers_(REG_ALS_STATUS, &status, 1)) {
    this->status_set_warning();
    this->publish_state(NAN);
    return;
  }

  if ((status & ALS_STATUS_DATA_INVALID) != 0) {
    this->status_set_warning();
    this->publish_state(NAN);
    return;
  }

  uint8_t data[4] = {};
  if (!this->read_registers_(REG_ALS_DATA_CH1_0, data, sizeof(data))) {
    this->status_set_warning();
    this->publish_state(NAN);
    return;
  }

  const uint16_t ch1 = (static_cast<uint16_t>(data[1]) << 8) | data[0];
  const uint16_t ch0 = (static_cast<uint16_t>(data[3]) << 8) | data[2];

  this->status_clear_warning();
  this->publish_state(this->calculate_lux_(ch0, ch1) * this->window_correction_);
}

bool LTR303ALSSensor::write_register_(uint8_t reg, uint8_t value) {
  return this->write_register(reg, &value, 1) == i2c::ERROR_OK;
}

bool LTR303ALSSensor::read_registers_(uint8_t reg, uint8_t *data, size_t length) {
  return this->read_register(reg, data, length) == i2c::ERROR_OK;
}

float LTR303ALSSensor::calculate_lux_(uint16_t ch0, uint16_t ch1) const {
  const uint32_t total = static_cast<uint32_t>(ch0) + ch1;
  if (total == 0) {
    return 0.0f;
  }

  const float ratio = static_cast<float>(ch1) / static_cast<float>(total);
  if (ratio < 0.45f) {
    return 1.7743f * ch0 + 1.1059f * ch1;
  }
  if (ratio < 0.64f) {
    return 4.2785f * ch0 - 1.9548f * ch1;
  }
  if (ratio < 0.85f) {
    return 0.5926f * ch0 + 0.1185f * ch1;
  }
  return 0.0f;
}

}  // namespace esphome::ltr303als
