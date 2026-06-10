#pragma once

#ifdef USE_ESP32
#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

#include "esphome/components/network/util.h"
#include "esphome/components/watchdog/watchdog.h"
#include "esphome/core/hal.h"

#include "connectivity.h"
#include "weather_client.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace AirDot::flight_radar {

inline constexpr uint8_t FLIGHT_RADAR_RADIUS_MIN_KM = 5;
inline constexpr uint8_t FLIGHT_RADAR_RADIUS_DEFAULT_KM = 25;
inline constexpr uint8_t FLIGHT_RADAR_RADIUS_MAX_KM = 50;
inline constexpr float FLIGHT_RADAR_RADIUS_KM = static_cast<float>(FLIGHT_RADAR_RADIUS_DEFAULT_KM);
inline constexpr uint8_t FLIGHT_RADAR_MAX_AIRCRAFT = 10;
inline constexpr uint32_t FLIGHT_RADAR_REQUEST_INTERVAL_MS = 1000UL;

struct Aircraft {
  char callsign[9]{};
  float distance_km{0.0f};
  float bearing_deg{0.0f};
  float track_deg{NAN};
  bool military{false};
};

struct Snapshot {
  bool valid{false};
  float request_latitude{0.0f};
  float request_longitude{0.0f};
  uint8_t range_km{FLIGHT_RADAR_RADIUS_DEFAULT_KM};
  uint32_t received_ms{0};
  uint8_t count{0};
  Aircraft aircraft[FLIGHT_RADAR_MAX_AIRCRAFT]{};
};

#ifdef USE_ESP32
inline constexpr int FLIGHT_RADAR_TASK_IDLE = 0;
inline constexpr int FLIGHT_RADAR_TASK_RUNNING = 1;
inline constexpr int FLIGHT_RADAR_TASK_SUCCESS = 2;
inline constexpr int FLIGHT_RADAR_TASK_FAILED = 3;
inline constexpr uint32_t FLIGHT_RADAR_HTTP_TASK_STACK_SIZE = 12288;
inline constexpr int FLIGHT_RADAR_HTTP_TIMEOUT_MS = 3500;
inline constexpr size_t FLIGHT_RADAR_MAX_RESPONSE_BYTES = 16384;
inline constexpr uint16_t FLIGHT_RADAR_MAX_PARSED_AIRCRAFT_OBJECTS = 96;
inline constexpr uint8_t FLIGHT_RADAR_LOCATION_COORDS = 0;
inline constexpr uint8_t FLIGHT_RADAR_LOCATION_IP = 1;
inline constexpr float DEG_TO_RAD = 0.017453292519943295769f;
inline constexpr float RAD_TO_DEG = 57.295779513082320876f;
inline constexpr float EARTH_RADIUS_KM = 6371.0f;
inline constexpr float KM_PER_NAUTICAL_MILE = 1.852f;

inline bool coordinates_are_valid_(float latitude, float longitude) {
  return std::isfinite(latitude) && std::isfinite(longitude) &&
         latitude >= -90.0f && latitude <= 90.0f &&
         longitude >= -180.0f && longitude <= 180.0f;
}

inline AirDot::connectivity::RetryPolicy flight_radar_retry_policy_() {
  return {2500, 120000, 20};
}

inline AirDot::connectivity::RetryState &flight_radar_retry_state_() {
  static AirDot::connectivity::RetryState state{};
  return state;
}

inline std::atomic<int> &flight_radar_task_state_() {
  static std::atomic<int> state{FLIGHT_RADAR_TASK_IDLE};
  return state;
}

inline std::atomic<int32_t> &request_latitude_e7_() {
  static std::atomic<int32_t> latitude{0};
  return latitude;
}

inline std::atomic<int32_t> &request_longitude_e7_() {
  static std::atomic<int32_t> longitude{0};
  return longitude;
}

inline std::atomic<uint8_t> &request_location_mode_() {
  static std::atomic<uint8_t> mode{FLIGHT_RADAR_LOCATION_COORDS};
  return mode;
}

inline std::atomic<uint8_t> &request_range_km_() {
  static std::atomic<uint8_t> range{FLIGHT_RADAR_RADIUS_DEFAULT_KM};
  return range;
}

inline std::atomic<uint8_t> &request_military_only_() {
  static std::atomic<uint8_t> military_only{0};
  return military_only;
}

inline uint8_t normalize_request_range_km_(uint8_t range_km) {
  return range_km >= FLIGHT_RADAR_RADIUS_MIN_KM && range_km <= FLIGHT_RADAR_RADIUS_MAX_KM
             ? range_km
             : FLIGHT_RADAR_RADIUS_DEFAULT_KM;
}

inline SemaphoreHandle_t snapshot_mutex_() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
  return mutex;
}

class SnapshotLock {
 public:
  explicit SnapshotLock(uint32_t timeout_ms = 10) {
    const auto mutex = snapshot_mutex_();
    if (mutex != nullptr)
      this->locked_ = xSemaphoreTake(mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
  }

  ~SnapshotLock() {
    if (this->locked_)
      xSemaphoreGive(snapshot_mutex_());
  }

  bool locked() const { return this->locked_; }

 private:
  bool locked_{false};
};

inline Snapshot &current_snapshot_storage_() {
  static Snapshot snapshot{};
  return snapshot;
}

inline Snapshot current_snapshot() {
  Snapshot snapshot{};
  SnapshotLock lock(2);
  if (lock.locked())
    snapshot = current_snapshot_storage_();
  return snapshot;
}

inline bool request_running() {
  return flight_radar_task_state_().load(std::memory_order_acquire) == FLIGHT_RADAR_TASK_RUNNING;
}

inline void note_flight_radar_success_() {
  flight_radar_retry_state_().reset();
  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::FLIGHT_RADAR,
      AirDot::connectivity::ConnectivityStatus::CONNECTED,
      AirDot::connectivity::ConnectivityError::NONE, esphome::millis());
}

inline void note_flight_radar_failure_(AirDot::connectivity::ConnectivityError error) {
  const uint32_t now = esphome::millis();
  auto &retry = flight_radar_retry_state_();
  const uint32_t delay = retry.record_failure(
      now, flight_radar_retry_policy_(),
      now ^ (static_cast<uint32_t>(error) << 8U) ^ 0x46524C54U);
  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::FLIGHT_RADAR,
      AirDot::connectivity::status_for_error(error), error, now, now + delay);
}

inline bool flight_radar_start_allowed_() {
  AirDot::time_weather::suppress_transport_logs_();
  const uint32_t now = esphome::millis();
  if (!esphome::network::is_connected()) {
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::FLIGHT_RADAR,
        AirDot::connectivity::ConnectivityStatus::OFFLINE,
        AirDot::connectivity::ConnectivityError::OFFLINE, now);
    return false;
  }

  if (AirDot::time_weather::http_request_active()) {
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::FLIGHT_RADAR,
        AirDot::connectivity::ConnectivityStatus::RETRY_WAIT,
        AirDot::connectivity::ConnectivityError::TIMEOUT, now, now + FLIGHT_RADAR_REQUEST_INTERVAL_MS);
    return false;
  }

  auto &retry = flight_radar_retry_state_();
  if (!retry.due(now)) {
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::FLIGHT_RADAR,
        AirDot::connectivity::ConnectivityStatus::RETRY_WAIT,
        AirDot::connectivity::ConnectivityError::TIMEOUT, now, retry.next_retry_ms);
    return false;
  }

  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::FLIGHT_RADAR,
      AirDot::connectivity::ConnectivityStatus::CONNECTING,
      AirDot::connectivity::ConnectivityError::NONE, now);
  return true;
}

inline AirDot::connectivity::ConnectivityError classify_http_error_(esp_err_t error, int status_code,
                                                                    bool overflow) {
  if (overflow)
    return AirDot::connectivity::ConnectivityError::RESOURCE_EXHAUSTED;
  if (status_code == 429)
    return AirDot::connectivity::ConnectivityError::RATE_LIMITED;
  if (status_code == 401 || status_code == 403)
    return AirDot::connectivity::ConnectivityError::AUTH_FAILED;
  if (status_code == 408 || status_code == 504)
    return AirDot::connectivity::ConnectivityError::TIMEOUT;
  if (status_code >= 500)
    return AirDot::connectivity::ConnectivityError::SERVER_ERROR;
  if (status_code >= 400)
    return AirDot::connectivity::ConnectivityError::SERVER_ERROR;
  if (error == ESP_OK)
    return AirDot::connectivity::ConnectivityError::NONE;

  const char *name = esp_err_to_name(error);
  if (name != nullptr) {
    if (std::strstr(name, "DNS") != nullptr || std::strstr(name, "HOST") != nullptr)
      return AirDot::connectivity::ConnectivityError::DNS_FAILED;
    if (std::strstr(name, "TIMEOUT") != nullptr || std::strstr(name, "EAGAIN") != nullptr)
      return AirDot::connectivity::ConnectivityError::TIMEOUT;
  }
  return AirDot::connectivity::ConnectivityError::OFFLINE;
}

struct HttpResponse {
  char *body{nullptr};
  size_t length{0};
  size_t max_length{0};
  bool overflow{false};
};

class ResponseBufferGuard {
 public:
  explicit ResponseBufferGuard(size_t size) : size_(size) {
    this->buffer_ = static_cast<char *>(heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (this->buffer_ != nullptr)
      this->buffer_[0] = '\0';
  }

  ~ResponseBufferGuard() {
    if (this->buffer_ != nullptr)
      heap_caps_free(this->buffer_);
  }

  ResponseBufferGuard(const ResponseBufferGuard &) = delete;
  ResponseBufferGuard &operator=(const ResponseBufferGuard &) = delete;

  char *data() const { return this->buffer_; }
  size_t size() const { return this->buffer_ == nullptr ? 0 : this->size_; }
  bool available() const { return this->buffer_ != nullptr; }

 private:
  char *buffer_{nullptr};
  size_t size_{0};
};

inline esp_err_t http_response_event_handler_(esp_http_client_event_t *event) {
  if (event == nullptr || event->event_id != HTTP_EVENT_ON_DATA || event->data == nullptr || event->data_len <= 0)
    return ESP_OK;

  auto *response = static_cast<HttpResponse *>(event->user_data);
  if (response == nullptr)
    return ESP_OK;

  if (response->body == nullptr || response->max_length == 0) {
    response->overflow = true;
    return ESP_OK;
  }

  const size_t available = response->length < response->max_length ? response->max_length - response->length : 0;
  const size_t incoming = static_cast<size_t>(event->data_len);
  const size_t to_copy = std::min(available, incoming);
  if (to_copy > 0) {
    std::memcpy(response->body + response->length, event->data, to_copy);
    response->length += to_copy;
    response->body[response->length] = '\0';
  }
  if (to_copy < incoming)
    response->overflow = true;
  return ESP_OK;
}

inline void skip_json_space_(const char *&cursor, const char *end) {
  while (cursor < end && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n'))
    cursor++;
}

inline bool starts_with_(const char *cursor, const char *end, const char *text) {
  const size_t length = std::strlen(text);
  return cursor + length <= end && std::strncmp(cursor, text, length) == 0;
}

inline const char *find_pattern_(const char *cursor, const char *end, const char *pattern) {
  const size_t length = std::strlen(pattern);
  if (length == 0 || cursor + length > end)
    return nullptr;

  for (; cursor + length <= end; cursor++) {
    if (std::memcmp(cursor, pattern, length) == 0)
      return cursor;
  }
  return nullptr;
}

inline const char *find_char_(const char *cursor, const char *end, char value) {
  while (cursor < end) {
    if (*cursor == value)
      return cursor;
    cursor++;
  }
  return nullptr;
}

inline bool skip_json_string_(const char *&cursor, const char *end) {
  if (cursor >= end || *cursor != '"')
    return false;
  cursor++;
  while (cursor < end) {
    if (*cursor == '\\') {
      cursor += cursor + 1 < end ? 2 : 1;
      continue;
    }
    if (*cursor == '"') {
      cursor++;
      return true;
    }
    cursor++;
  }
  return false;
}

inline bool skip_json_value_(const char *&cursor, const char *end) {
  skip_json_space_(cursor, end);
  if (cursor >= end)
    return false;

  if (*cursor == '"')
    return skip_json_string_(cursor, end);

  if (*cursor == '[' || *cursor == '{') {
    const char open = *cursor;
    const char close = open == '[' ? ']' : '}';
    uint8_t depth = 0;
    while (cursor < end) {
      if (*cursor == '"') {
        if (!skip_json_string_(cursor, end))
          return false;
        continue;
      }
      if (*cursor == open)
        depth++;
      if (*cursor == close) {
        if (depth == 0)
          return false;
        depth--;
        cursor++;
        if (depth == 0)
          return true;
        continue;
      }
      cursor++;
    }
    return false;
  }

  while (cursor < end && *cursor != ',' && *cursor != ']' && *cursor != '}')
    cursor++;
  return true;
}

inline bool parse_json_number_(const char *&cursor, const char *end, float &value) {
  skip_json_space_(cursor, end);
  if (starts_with_(cursor, end, "null")) {
    cursor += 4;
    return false;
  }

  char *number_end = nullptr;
  const float parsed = std::strtof(cursor, &number_end);
  if (number_end == cursor || number_end > end || !std::isfinite(parsed))
    return false;

  value = parsed;
  cursor = number_end;
  return true;
}

inline bool parse_json_uint_(const char *&cursor, const char *end, uint32_t &value) {
  float parsed = 0.0f;
  if (!parse_json_number_(cursor, end, parsed) || parsed < 0.0f)
    return false;
  value = static_cast<uint32_t>(parsed);
  return true;
}

inline bool parse_json_bool_(const char *&cursor, const char *end, bool &value) {
  skip_json_space_(cursor, end);
  if (starts_with_(cursor, end, "true")) {
    cursor += 4;
    value = true;
    return true;
  }
  if (starts_with_(cursor, end, "false")) {
    cursor += 5;
    value = false;
    return true;
  }
  if (starts_with_(cursor, end, "null")) {
    cursor += 4;
    return false;
  }
  return false;
}

inline bool parse_json_string_(const char *&cursor, const char *end, char *output, size_t output_size) {
  if (output_size == 0)
    return false;
  output[0] = '\0';

  skip_json_space_(cursor, end);
  if (starts_with_(cursor, end, "null")) {
    cursor += 4;
    return false;
  }
  if (cursor >= end || *cursor != '"')
    return false;

  cursor++;
  size_t written = 0;
  while (cursor < end) {
    if (*cursor == '\\') {
      cursor++;
      if (cursor >= end)
        break;
    } else if (*cursor == '"') {
      cursor++;
      while (written > 0 && output[written - 1] == ' ')
        written--;
      output[written] = '\0';
      return written > 0;
    }

    if (written + 1 < output_size)
      output[written++] = *cursor;
    cursor++;
  }
  output[0] = '\0';
  return false;
}

inline float distance_km_(float from_latitude, float from_longitude, float to_latitude, float to_longitude) {
  const float lat1 = from_latitude * DEG_TO_RAD;
  const float lat2 = to_latitude * DEG_TO_RAD;
  const float dlat = (to_latitude - from_latitude) * DEG_TO_RAD;
  const float dlon = (to_longitude - from_longitude) * DEG_TO_RAD;
  const float sin_dlat = std::sin(dlat * 0.5f);
  const float sin_dlon = std::sin(dlon * 0.5f);
  const float a = sin_dlat * sin_dlat + std::cos(lat1) * std::cos(lat2) * sin_dlon * sin_dlon;
  const float c = 2.0f * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0f, 1.0f - a)));
  return EARTH_RADIUS_KM * c;
}

inline float bearing_deg_(float from_latitude, float from_longitude, float to_latitude, float to_longitude) {
  const float lat1 = from_latitude * DEG_TO_RAD;
  const float lat2 = to_latitude * DEG_TO_RAD;
  const float dlon = (to_longitude - from_longitude) * DEG_TO_RAD;
  const float y = std::sin(dlon) * std::cos(lat2);
  const float x = std::cos(lat1) * std::sin(lat2) -
                  std::sin(lat1) * std::cos(lat2) * std::cos(dlon);
  float bearing = std::atan2(y, x) * RAD_TO_DEG;
  if (bearing < 0.0f)
    bearing += 360.0f;
  return bearing;
}

inline void append_aircraft_(Snapshot &snapshot, const Aircraft &aircraft) {
  if (snapshot.count < FLIGHT_RADAR_MAX_AIRCRAFT) {
    snapshot.aircraft[snapshot.count++] = aircraft;
    return;
  }

  uint8_t farthest = 0;
  for (uint8_t index = 1; index < snapshot.count; index++) {
    if (snapshot.aircraft[index].distance_km > snapshot.aircraft[farthest].distance_km)
      farthest = index;
  }
  if (aircraft.distance_km < snapshot.aircraft[farthest].distance_km)
    snapshot.aircraft[farthest] = aircraft;
}

inline void skip_json_value_if_needed_(const char *before, const char *&cursor, const char *end) {
  if (cursor == before)
    skip_json_value_(cursor, end);
}

inline bool read_airplanes_live_number_(const char *&cursor, const char *end, float &value) {
  const char *before = cursor;
  const bool parsed = parse_json_number_(cursor, end, value);
  skip_json_value_if_needed_(before, cursor, end);
  return parsed;
}

inline bool read_airplanes_live_uint_(const char *&cursor, const char *end, uint32_t &value) {
  const char *before = cursor;
  const bool parsed = parse_json_uint_(cursor, end, value);
  skip_json_value_if_needed_(before, cursor, end);
  return parsed;
}

inline bool read_airplanes_live_bool_(const char *&cursor, const char *end, bool &value) {
  const char *before = cursor;
  const bool parsed = parse_json_bool_(cursor, end, value);
  skip_json_value_if_needed_(before, cursor, end);
  return parsed;
}

inline bool read_airplanes_live_ground_state_(const char *&cursor, const char *end, bool &on_ground) {
  skip_json_space_(cursor, end);
  if (cursor < end && *cursor == '"') {
    char text[16]{};
    if (!parse_json_string_(cursor, end, text, sizeof(text)))
      return false;
    if (std::strcmp(text, "ground") == 0) {
      on_ground = true;
    }
    return true;
  }

  return skip_json_value_(cursor, end);
}

inline bool parse_airplanes_live_aircraft_(const char *&cursor, const char *end, float own_latitude,
                                           float own_longitude, float range_km, bool military_only,
                                           Snapshot &snapshot) {
  skip_json_space_(cursor, end);
  if (cursor >= end || *cursor != '{')
    return false;
  cursor++;

  char hex[9]{};
  char callsign[9]{};
  float latitude = NAN;
  float longitude = NAN;
  float track_deg = NAN;
  float true_heading_deg = NAN;
  float magnetic_heading_deg = NAN;
  float dir_deg = NAN;
  uint32_t db_flags = 0;
  bool on_ground = false;
  bool military = false;

  while (cursor < end) {
    skip_json_space_(cursor, end);
    if (cursor < end && *cursor == '}') {
      cursor++;
      break;
    }

    char key[24]{};
    if (!parse_json_string_(cursor, end, key, sizeof(key)))
      return false;
    skip_json_space_(cursor, end);
    if (cursor >= end || *cursor != ':')
      return false;
    cursor++;

    const char *before = cursor;
    if (std::strcmp(key, "hex") == 0) {
      parse_json_string_(cursor, end, hex, sizeof(hex));
      skip_json_value_if_needed_(before, cursor, end);
    } else if (std::strcmp(key, "flight") == 0) {
      parse_json_string_(cursor, end, callsign, sizeof(callsign));
      skip_json_value_if_needed_(before, cursor, end);
    } else if (std::strcmp(key, "lat") == 0) {
      read_airplanes_live_number_(cursor, end, latitude);
    } else if (std::strcmp(key, "lon") == 0) {
      read_airplanes_live_number_(cursor, end, longitude);
    } else if (std::strcmp(key, "alt_baro") == 0) {
      read_airplanes_live_ground_state_(cursor, end, on_ground);
    } else if (std::strcmp(key, "track") == 0) {
      read_airplanes_live_number_(cursor, end, track_deg);
    } else if (std::strcmp(key, "true_heading") == 0) {
      read_airplanes_live_number_(cursor, end, true_heading_deg);
    } else if (std::strcmp(key, "mag_heading") == 0) {
      read_airplanes_live_number_(cursor, end, magnetic_heading_deg);
    } else if (std::strcmp(key, "dir") == 0) {
      read_airplanes_live_number_(cursor, end, dir_deg);
    } else if (std::strcmp(key, "dbFlags") == 0) {
      if (read_airplanes_live_uint_(cursor, end, db_flags))
        military = military || (db_flags & 1U) != 0;
    } else if (std::strcmp(key, "mil") == 0) {
      bool mil = false;
      if (read_airplanes_live_bool_(cursor, end, mil))
        military = military || mil;
    } else {
      skip_json_value_(cursor, end);
    }

    skip_json_space_(cursor, end);
    if (cursor < end && *cursor == ',') {
      cursor++;
      continue;
    }
    if (cursor < end && *cursor == '}') {
      cursor++;
      break;
    }
  }

  if (on_ground || !coordinates_are_valid_(latitude, longitude))
    return true;
  if (military_only && !military)
    return true;

  const float distance = distance_km_(own_latitude, own_longitude, latitude, longitude);
  if (!std::isfinite(distance) || distance > range_km)
    return true;

  Aircraft aircraft{};
  std::snprintf(
      aircraft.callsign, sizeof(aircraft.callsign), "%s",
      callsign[0] != '\0' ? callsign : hex);
  aircraft.distance_km = distance;
  aircraft.bearing_deg = bearing_deg_(own_latitude, own_longitude, latitude, longitude);
  if (std::isfinite(track_deg)) {
    aircraft.track_deg = track_deg;
  } else if (std::isfinite(true_heading_deg)) {
    aircraft.track_deg = true_heading_deg;
  } else if (std::isfinite(magnetic_heading_deg)) {
    aircraft.track_deg = magnetic_heading_deg;
  } else if (std::isfinite(dir_deg)) {
    aircraft.track_deg = dir_deg;
  }
  aircraft.military = military;
  append_aircraft_(snapshot, aircraft);
  return true;
}

inline bool parse_airplanes_live_response_(const char *json, size_t json_length, float latitude, float longitude,
                                           uint8_t range_km, bool military_only, Snapshot &snapshot,
                                           bool allow_partial = false) {
  if (json == nullptr || json_length == 0)
    return false;

  const char *begin = json;
  const char *end = json + json_length;
  snapshot = Snapshot{};
  snapshot.valid = true;
  snapshot.request_latitude = latitude;
  snapshot.request_longitude = longitude;
  snapshot.range_km = normalize_request_range_km_(range_km);
  snapshot.received_ms = esphome::millis();

  const char *aircraft_key = find_pattern_(begin, end, "\"ac\"");
  if (aircraft_key == nullptr)
    return false;
  const char *colon = find_char_(aircraft_key + 4, end, ':');
  if (colon == nullptr)
    return false;

  const char *cursor = colon + 1;
  skip_json_space_(cursor, end);
  if (starts_with_(cursor, end, "null"))
    return true;
  if (cursor >= end || *cursor != '[')
    return false;
  cursor++;

  uint16_t parsed_objects = 0;
  while (cursor < end && parsed_objects < FLIGHT_RADAR_MAX_PARSED_AIRCRAFT_OBJECTS) {
    skip_json_space_(cursor, end);
    if (cursor < end && *cursor == ']')
      break;
    if (cursor < end && *cursor == '{') {
      if (!parse_airplanes_live_aircraft_(
              cursor, end, latitude, longitude, static_cast<float>(snapshot.range_km), military_only, snapshot)) {
        if (allow_partial)
          break;
        return false;
      }
      parsed_objects++;
    } else if (!skip_json_value_(cursor, end)) {
      if (allow_partial)
        break;
      return false;
    }

    skip_json_space_(cursor, end);
    if (cursor < end && *cursor == ',')
      cursor++;
  }

  std::sort(snapshot.aircraft, snapshot.aircraft + snapshot.count,
            [](const Aircraft &a, const Aircraft &b) {
              return a.distance_km < b.distance_km;
            });
  return true;
}

inline bool fetch_airplanes_live_aircraft_(float latitude, float longitude, uint8_t range_km,
                                           bool military_only, Snapshot &snapshot) {
  if (!coordinates_are_valid_(latitude, longitude)) {
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::FLIGHT_RADAR,
        AirDot::connectivity::ConnectivityStatus::CONFIG_INVALID,
        AirDot::connectivity::ConnectivityError::CONFIG_INVALID, esphome::millis());
    return false;
  }

  const uint8_t normalized_range_km = normalize_request_range_km_(range_km);
  const float distance_nm = static_cast<float>(normalized_range_km) / KM_PER_NAUTICAL_MILE;

  char url[384];
  const int written = std::snprintf(
      url, sizeof(url),
      "https://api.airplanes.live/v2/point/%.6f/%.6f/%.1f",
      latitude, longitude, distance_nm);
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(url)) {
    note_flight_radar_failure_(AirDot::connectivity::ConnectivityError::CONFIG_INVALID);
    return false;
  }

  ResponseBufferGuard response_buffer(FLIGHT_RADAR_MAX_RESPONSE_BYTES);
  if (!response_buffer.available()) {
    note_flight_radar_failure_(AirDot::connectivity::ConnectivityError::RESOURCE_EXHAUSTED);
    return false;
  }

  HttpResponse response;
  response.body = response_buffer.data();
  response.length = 0;
  response.max_length = response_buffer.size();
  response.overflow = false;
  response.body[0] = '\0';
  esp_http_client_config_t config{};
  config.url = url;
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = FLIGHT_RADAR_HTTP_TIMEOUT_MS;
  config.event_handler = http_response_event_handler_;
  config.user_data = &response;
  config.buffer_size = 512;
  config.buffer_size_tx = 256;
  config.user_agent = "";
  config.addr_type = HTTP_ADDR_TYPE_INET;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  config.crt_bundle_attach = esp_crt_bundle_attach;
#endif

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    note_flight_radar_failure_(AirDot::connectivity::ConnectivityError::RESOURCE_EXHAUSTED);
    return false;
  }

  const esp_err_t error = esp_http_client_perform(client);
  const int status_code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (error != ESP_OK || status_code != 200) {
    note_flight_radar_failure_(classify_http_error_(error, status_code, response.overflow));
    return false;
  }

  Snapshot parsed{};
  if (!parse_airplanes_live_response_(response.body, response.length, latitude, longitude,
                                      normalized_range_km, military_only, parsed, response.overflow)) {
    note_flight_radar_failure_(AirDot::connectivity::ConnectivityError::INVALID_RESPONSE);
    return false;
  }

  snapshot = parsed;
  note_flight_radar_success_();
  return true;
}

inline void flight_radar_task_(void *) {
  esphome::watchdog::WatchdogManager watchdog(25000);
  float latitude =
      static_cast<float>(request_latitude_e7_().load(std::memory_order_acquire)) / 10000000.0f;
  float longitude =
      static_cast<float>(request_longitude_e7_().load(std::memory_order_acquire)) / 10000000.0f;
  const uint8_t range_km = normalize_request_range_km_(request_range_km_().load(std::memory_order_acquire));
  const bool military_only = request_military_only_().load(std::memory_order_acquire) == 1;

  Snapshot snapshot{};
  const bool acquired =
      AirDot::time_weather::acquire_weather_http_request_slot_(AirDot::connectivity::Service::FLIGHT_RADAR);
  bool location_available = true;
  const uint8_t location_mode = request_location_mode_().load(std::memory_order_acquire);
  if (acquired && location_mode == FLIGHT_RADAR_LOCATION_IP) {
    location_available = AirDot::time_weather::fetch_ipwhois_location(latitude, longitude);
    if (!location_available) {
      const auto status = AirDot::connectivity::get_service_status(
          AirDot::connectivity::Service::WEATHER_LOCATION);
      note_flight_radar_failure_(
          status.error == AirDot::connectivity::ConnectivityError::NONE
              ? AirDot::connectivity::ConnectivityError::INVALID_RESPONSE
              : status.error);
    }
  }

  if (acquired && location_available &&
      fetch_airplanes_live_aircraft_(latitude, longitude, range_km, military_only, snapshot)) {
    SnapshotLock lock(20);
    if (lock.locked()) {
      current_snapshot_storage_() = snapshot;
      flight_radar_task_state_().store(FLIGHT_RADAR_TASK_SUCCESS, std::memory_order_release);
    } else {
      flight_radar_task_state_().store(FLIGHT_RADAR_TASK_FAILED, std::memory_order_release);
    }
  } else {
    flight_radar_task_state_().store(FLIGHT_RADAR_TASK_FAILED, std::memory_order_release);
  }

  if (acquired)
    AirDot::time_weather::release_weather_http_request_slot_();

  vTaskDelete(nullptr);
}

inline bool start_airplanes_live_request(float latitude, float longitude, uint8_t range_km = FLIGHT_RADAR_RADIUS_DEFAULT_KM,
                                         bool military_only = false) {
  if (!coordinates_are_valid_(latitude, longitude)) {
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::FLIGHT_RADAR,
        AirDot::connectivity::ConnectivityStatus::CONFIG_INVALID,
        AirDot::connectivity::ConnectivityError::CONFIG_INVALID, esphome::millis());
    return false;
  }

  if (!flight_radar_start_allowed_())
    return false;

  int expected = FLIGHT_RADAR_TASK_IDLE;
  if (!flight_radar_task_state_().compare_exchange_strong(
          expected, FLIGHT_RADAR_TASK_RUNNING, std::memory_order_acq_rel))
    return false;

  request_latitude_e7_().store(static_cast<int32_t>(latitude * 10000000.0f), std::memory_order_release);
  request_longitude_e7_().store(static_cast<int32_t>(longitude * 10000000.0f), std::memory_order_release);
  request_location_mode_().store(FLIGHT_RADAR_LOCATION_COORDS, std::memory_order_release);
  request_range_km_().store(normalize_request_range_km_(range_km), std::memory_order_release);
  request_military_only_().store(military_only ? 1 : 0, std::memory_order_release);

  const BaseType_t created = xTaskCreate(
      flight_radar_task_, "radar", FLIGHT_RADAR_HTTP_TASK_STACK_SIZE, nullptr, 1, nullptr);
  if (created == pdPASS)
    return true;

  flight_radar_task_state_().store(FLIGHT_RADAR_TASK_FAILED, std::memory_order_release);
  note_flight_radar_failure_(AirDot::connectivity::ConnectivityError::RESOURCE_EXHAUSTED);
  return false;
}

inline bool start_airplanes_live_for_current_location_request(uint8_t range_km = FLIGHT_RADAR_RADIUS_DEFAULT_KM,
                                                              bool military_only = false) {
  if (!flight_radar_start_allowed_())
    return false;

  int expected = FLIGHT_RADAR_TASK_IDLE;
  if (!flight_radar_task_state_().compare_exchange_strong(
          expected, FLIGHT_RADAR_TASK_RUNNING, std::memory_order_acq_rel))
    return false;

  request_latitude_e7_().store(0, std::memory_order_release);
  request_longitude_e7_().store(0, std::memory_order_release);
  request_location_mode_().store(FLIGHT_RADAR_LOCATION_IP, std::memory_order_release);
  request_range_km_().store(normalize_request_range_km_(range_km), std::memory_order_release);
  request_military_only_().store(military_only ? 1 : 0, std::memory_order_release);

  const BaseType_t created = xTaskCreate(
      flight_radar_task_, "radar", FLIGHT_RADAR_HTTP_TASK_STACK_SIZE, nullptr, 1, nullptr);
  if (created == pdPASS)
    return true;

  flight_radar_task_state_().store(FLIGHT_RADAR_TASK_FAILED, std::memory_order_release);
  note_flight_radar_failure_(AirDot::connectivity::ConnectivityError::RESOURCE_EXHAUSTED);
  return false;
}

inline int consume_airplanes_live_result(Snapshot &snapshot) {
  const int state = flight_radar_task_state_().load(std::memory_order_acquire);
  if (state == FLIGHT_RADAR_TASK_SUCCESS) {
    {
      SnapshotLock lock(5);
      if (!lock.locked())
        return 0;
      snapshot = current_snapshot_storage_();
    }
    flight_radar_task_state_().store(FLIGHT_RADAR_TASK_IDLE, std::memory_order_release);
    return 1;
  }

  if (state == FLIGHT_RADAR_TASK_FAILED) {
    flight_radar_task_state_().store(FLIGHT_RADAR_TASK_IDLE, std::memory_order_release);
    return -1;
  }

  return 0;
}

#else
inline Snapshot current_snapshot() { return {}; }
inline bool request_running() { return false; }
inline bool start_airplanes_live_request(float latitude, float longitude,
                                         uint8_t range_km = FLIGHT_RADAR_RADIUS_DEFAULT_KM,
                                         bool military_only = false) {
  (void) latitude;
  (void) longitude;
  (void) range_km;
  (void) military_only;
  return false;
}
inline bool start_airplanes_live_for_current_location_request(uint8_t range_km = FLIGHT_RADAR_RADIUS_DEFAULT_KM,
                                                              bool military_only = false) {
  (void) range_km;
  (void) military_only;
  return false;
}
inline int consume_airplanes_live_result(Snapshot &snapshot) {
  (void) snapshot;
  return -1;
}
#endif

}  // namespace AirDot::flight_radar
