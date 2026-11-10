#pragma once

#include <cmath>
#include <cstdint>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace AirDot {

template<typename Op> bool sen66_retry(const char *name, uint8_t attempts, uint32_t retry_delay_ms, Op &&op) {
  for (uint8_t attempt = 1; attempt <= attempts; attempt++) {
    if (op())
      return true;
    ESP_LOGW("sen66", "%s failed (attempt %u/%u)", name, attempt, attempts);
    if (attempt < attempts)
      delay(retry_delay_ms);
  }
  ESP_LOGE("sen66", "%s failed after %u attempts", name, attempts);
  return false;
}

template<typename Sen66Sensor>
void sen66_apply_temperature_offset(Sen66Sensor *sensor, float offset_celsius, float slope,
                                        uint16_t time_constant_seconds, uint16_t slot) {
  if (sensor == nullptr)
    return;

  const int16_t offset_raw = static_cast<int16_t>(offset_celsius * 200.0f);
  const int16_t slope_raw = static_cast<int16_t>(slope * 10000.0f);
  const uint16_t data[4] = {
      static_cast<uint16_t>(offset_raw),
      static_cast<uint16_t>(slope_raw),
      time_constant_seconds,
      slot,
  };
  sensor->write_command(static_cast<uint16_t>(0x60B2), data, 4);
}

template<typename Sen66Sensor> bool sen66_set_voc_learning_time(Sen66Sensor *sensor, uint16_t learning_time_hours) {
  if (sensor == nullptr)
    return false;

  constexpr uint16_t VOC_TUNING_COMMAND = 0x60D0;
  uint16_t params[6] = {0, 0, 0, 0, 0, 0};
  if (!sensor->write_command(VOC_TUNING_COMMAND))
    return false;
  delay(20);
  if (!sensor->read_data(params, 6))
    return false;

  params[1] = learning_time_hours;
  params[2] = learning_time_hours;
  return sensor->write_command(VOC_TUNING_COMMAND, params, 6);
}

template<typename Sen66Sensor> bool sen66_get_voc_algorithm_state(Sen66Sensor *sensor, uint16_t *state) {
  if (sensor == nullptr || state == nullptr)
    return false;

  if (!sensor->write_command(static_cast<uint16_t>(0x6181)))
    return false;
  delay(20);
  return sensor->read_data(state, 4);
}

template<typename Sen66Sensor> bool sen66_set_voc_algorithm_state(Sen66Sensor *sensor, const uint16_t *state) {
  if (sensor == nullptr || state == nullptr)
    return false;

  return sensor->write_command(static_cast<uint16_t>(0x6181), state, 4);
}

template<typename Sen66Sensor> bool sen66_stop_measurement(Sen66Sensor *sensor) {
  if (sensor == nullptr)
    return false;

  return sensor->write_command(static_cast<uint16_t>(0x0104));
}

template<typename Sen66Sensor> bool sen66_start_measurement(Sen66Sensor *sensor) {
  if (sensor == nullptr)
    return false;

  return sensor->write_command(static_cast<uint16_t>(0x0021));
}

template<typename Sen66Sensor>
bool sen66_perform_forced_co2_recalibration(Sen66Sensor *sensor, uint16_t reference_ppm, int16_t &correction_ppm) {
  correction_ppm = 0;
  if (sensor == nullptr)
    return false;

  const uint16_t data[1] = {reference_ppm};
  if (!sensor->write_command(static_cast<uint16_t>(0x6707), data, 1))
    return false;

  delay(500);
  uint16_t response = 0;
  if (!sensor->read_data(response) || response == 0xFFFF)
    return false;

  correction_ppm = static_cast<int16_t>(static_cast<int32_t>(response) - 0x8000);
  return true;
}

template<typename Sen66Sensor> void update_sen66_ambient_pressure(Sen66Sensor *sensor, float pressure_pa, int &last_hpa) {
  if (sensor == nullptr || !std::isfinite(pressure_pa))
    return;

  const int pressure_hpa = static_cast<int>((pressure_pa + 50.0f) / 100.0f);
  if (pressure_hpa < 700 || pressure_hpa > 1200 || last_hpa == pressure_hpa)
    return;

  if (sensor->write_command(static_cast<uint16_t>(0x6720), static_cast<uint16_t>(pressure_hpa)))
    last_hpa = pressure_hpa;
}

}  // namespace AirDot
