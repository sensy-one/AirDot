#pragma once

#ifdef USE_ESP32
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>
#endif

#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"

#include <cstdint>

#include "connectivity.h"

namespace AirDot::time_weather {

inline AirDot::connectivity::RetryState &sntp_retry_state_() {
  static AirDot::connectivity::RetryState state{};
  return state;
}

inline AirDot::connectivity::RetryPolicy sntp_retry_policy_() {
  return {5000, 300000, 20};
}

inline void note_sntp_sync_failed(AirDot::connectivity::ConnectivityError error) {
  const uint32_t now = esphome::millis();
  const uint32_t delay = sntp_retry_state_().record_failure(
      now, sntp_retry_policy_(), now ^ (static_cast<uint32_t>(error) << 8U) ^ 0x54494D45U);
  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::TIME_SYNC, AirDot::connectivity::status_for_error(error), error, now,
      now + delay);
}

inline bool sntp_time_is_plausible_() {
#ifdef USE_ESP32
  const time_t now = ::time(nullptr);
  return now >= static_cast<time_t>(946684800UL);
#else
  return false;
#endif
}

inline void note_sntp_sync_success() {
  const uint32_t now = esphome::millis();
  if (!sntp_time_is_plausible_()) {
    note_sntp_sync_failed(AirDot::connectivity::ConnectivityError::INVALID_RESPONSE);
    return;
  }

  sntp_retry_state_().reset();
  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::TIME_SYNC,
      AirDot::connectivity::ConnectivityStatus::CONNECTED,
      AirDot::connectivity::ConnectivityError::NONE, now);
}

inline void request_sntp_sync() {
#ifdef USE_ESP32
  const uint32_t now = esphome::millis();
  if (!esphome::network::is_connected()) {
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::TIME_SYNC,
        AirDot::connectivity::ConnectivityStatus::OFFLINE,
        AirDot::connectivity::ConnectivityError::OFFLINE, now);
    return;
  }

  if (!sntp_retry_state_().due(now)) {
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::TIME_SYNC,
        AirDot::connectivity::ConnectivityStatus::RETRY_WAIT,
        AirDot::connectivity::ConnectivityError::TIMEOUT, now, sntp_retry_state_().next_retry_ms);
    return;
  }

  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::TIME_SYNC,
      AirDot::connectivity::ConnectivityStatus::CONNECTING,
      AirDot::connectivity::ConnectivityError::NONE, now);

  if (esp_sntp_enabled()) {
    esp_sntp_restart();
  } else {
    esp_sntp_init();
  }
#endif
}

inline void stop_sntp_sync() {
#ifdef USE_ESP32
  if (esp_sntp_enabled())
    esp_sntp_stop();
  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::TIME_SYNC,
      AirDot::connectivity::ConnectivityStatus::DISABLED,
      AirDot::connectivity::ConnectivityError::NONE, esphome::millis());
#endif
}

inline bool set_manual_time_epoch(uint32_t epoch_seconds) {
#ifdef USE_ESP32
  if (epoch_seconds < 946684800UL) {
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::TIME_SYNC,
        AirDot::connectivity::ConnectivityStatus::CONFIG_INVALID,
        AirDot::connectivity::ConnectivityError::CONFIG_INVALID, esphome::millis());
    return false;
  }
  stop_sntp_sync();
  timeval current_time{};
  current_time.tv_sec = static_cast<time_t>(epoch_seconds);
  current_time.tv_usec = 0;
  const bool applied = settimeofday(&current_time, nullptr) == 0;
  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::TIME_SYNC,
      applied ? AirDot::connectivity::ConnectivityStatus::CONNECTED
              : AirDot::connectivity::ConnectivityStatus::SERVER_ERROR,
      applied ? AirDot::connectivity::ConnectivityError::NONE
              : AirDot::connectivity::ConnectivityError::SERVER_ERROR,
      esphome::millis());
  return applied;
#else
  (void) epoch_seconds;
  return false;
#endif
}
}  // namespace AirDot::time_weather
