#pragma once

#ifdef USE_ESP32
#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include "esphome/components/network/util.h"
#include "esphome/components/watchdog/watchdog.h"
#include "esphome/core/hal.h"

#include "connectivity.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace AirDot::time_weather {
#ifdef USE_ESP32
inline constexpr int WEATHER_TASK_IDLE = 0;
inline constexpr int WEATHER_TASK_RUNNING = 1;
inline constexpr int WEATHER_TASK_SUCCESS = 2;
inline constexpr int WEATHER_TASK_FAILED = 3;
inline constexpr uint32_t WEATHER_HTTP_TASK_STACK_SIZE = 12288;

inline bool coordinates_are_valid_(float latitude, float longitude) {
  return std::isfinite(latitude) && std::isfinite(longitude) &&
         latitude >= -90.0f && latitude <= 90.0f &&
         longitude >= -180.0f && longitude <= 180.0f;
}

inline AirDot::connectivity::RetryPolicy weather_retry_policy_() {
  return {5000, 300000, 20};
}

inline AirDot::connectivity::RetryState &ipwhois_location_retry_state_() {
  static AirDot::connectivity::RetryState state{};
  return state;
}

inline AirDot::connectivity::RetryState &open_meteo_weather_retry_state_() {
  static AirDot::connectivity::RetryState state{};
  return state;
}

inline std::atomic<bool> &weather_http_request_active_() {
  static std::atomic<bool> active{false};
  return active;
}

inline bool http_request_active() {
  return weather_http_request_active_().load(std::memory_order_acquire);
}

inline void suppress_transport_logs_() {
  static bool configured = false;
  if (configured)
    return;
  configured = true;
  esp_log_level_set("esp-tls-mbedtls", ESP_LOG_NONE);
  esp_log_level_set("esp-tls", ESP_LOG_NONE);
  esp_log_level_set("transport_base", ESP_LOG_NONE);
  esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE);
}

inline bool acquire_weather_http_request_slot_(AirDot::connectivity::Service service) {
  bool expected = false;
  if (weather_http_request_active_().compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    return true;

  const uint32_t now = esphome::millis();
  AirDot::connectivity::set_service_status(
      service, AirDot::connectivity::ConnectivityStatus::RETRY_WAIT,
      AirDot::connectivity::ConnectivityError::TIMEOUT, now, now + 5000);
  return false;
}

inline void release_weather_http_request_slot_() {
  weather_http_request_active_().store(false, std::memory_order_release);
}

inline AirDot::connectivity::RetryState &retry_state_for_service_(AirDot::connectivity::Service service) {
  switch (service) {
    case AirDot::connectivity::Service::WEATHER_LOCATION:
      return ipwhois_location_retry_state_();
    case AirDot::connectivity::Service::WEATHER:
    default:
      return open_meteo_weather_retry_state_();
  }
}

inline void note_weather_service_success_(AirDot::connectivity::Service service) {
  retry_state_for_service_(service).reset();
  AirDot::connectivity::set_service_status(
      service, AirDot::connectivity::ConnectivityStatus::CONNECTED,
      AirDot::connectivity::ConnectivityError::NONE, esphome::millis());
}

inline void note_weather_service_failure_(AirDot::connectivity::Service service,
                                          AirDot::connectivity::ConnectivityError error) {
  const uint32_t now = esphome::millis();
  auto &retry = retry_state_for_service_(service);
  const uint32_t delay = retry.record_failure(
      now, weather_retry_policy_(), now ^ (static_cast<uint32_t>(service) << 16U) ^
                                      (static_cast<uint32_t>(error) << 8U) ^ 0x57454154U);
  AirDot::connectivity::set_service_status(service, AirDot::connectivity::status_for_error(error), error, now,
                                           now + delay);
}

inline bool weather_service_start_allowed_(AirDot::connectivity::Service service) {
  suppress_transport_logs_();
  const uint32_t now = esphome::millis();
  if (!esphome::network::is_connected()) {
    AirDot::connectivity::set_service_status(
        service, AirDot::connectivity::ConnectivityStatus::OFFLINE,
        AirDot::connectivity::ConnectivityError::OFFLINE, now);
    return false;
  }

  auto &retry = retry_state_for_service_(service);
  if (!retry.due(now)) {
    AirDot::connectivity::set_service_status(
        service, AirDot::connectivity::ConnectivityStatus::RETRY_WAIT,
        AirDot::connectivity::ConnectivityError::TIMEOUT, now, retry.next_retry_ms);
    return false;
  }

  AirDot::connectivity::set_service_status(
      service, AirDot::connectivity::ConnectivityStatus::CONNECTING,
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
  std::string body;
  size_t max_length{256};
  bool overflow{false};
};

inline esp_err_t http_response_event_handler_(esp_http_client_event_t *event) {
  if (event == nullptr || event->event_id != HTTP_EVENT_ON_DATA || event->data == nullptr || event->data_len <= 0)
    return ESP_OK;

  auto *response = static_cast<HttpResponse *>(event->user_data);
  if (response == nullptr)
    return ESP_OK;

  if (response->body.size() + static_cast<size_t>(event->data_len) > response->max_length) {
    response->overflow = true;
    return ESP_OK;
  }

  response->body.append(static_cast<const char *>(event->data), static_cast<size_t>(event->data_len));
  return ESP_OK;
}

inline bool extract_json_number_(const std::string &json, const char *key, float &value) {
  size_t search_index = 0;
  while (true) {
    const size_t key_index = json.find(key, search_index);
    if (key_index == std::string::npos)
      return false;

    const size_t colon_index = json.find(':', key_index + std::strlen(key));
    if (colon_index == std::string::npos)
      return false;

    const char *number_start = json.c_str() + colon_index + 1;
    while (*number_start == ' ' || *number_start == '\t' || *number_start == '\r' || *number_start == '\n' ||
           *number_start == '[')
      number_start++;

    char *number_end = nullptr;
    const float parsed = std::strtof(number_start, &number_end);
    if (number_end != number_start && std::isfinite(parsed)) {
      value = parsed;
      return true;
    }

    search_index = key_index + std::strlen(key);
  }
}

inline bool fetch_ipwhois_location_(esp_http_client_addr_type_t address_type,
                                    float &latitude, float &longitude) {
  HttpResponse response;
  esp_http_client_config_t config{};
  config.url = "https://ipwho.is/?fields=latitude,longitude";
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = address_type == HTTP_ADDR_TYPE_INET6 ? 2500 : 4000;
  config.event_handler = http_response_event_handler_;
  config.user_data = &response;
  config.buffer_size = 512;
  config.buffer_size_tx = 512;
  config.user_agent = "";
  config.addr_type = address_type;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  config.crt_bundle_attach = esp_crt_bundle_attach;
#endif

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    note_weather_service_failure_(
        AirDot::connectivity::Service::WEATHER_LOCATION,
        AirDot::connectivity::ConnectivityError::RESOURCE_EXHAUSTED);
    return false;
  }

  const esp_err_t error = esp_http_client_perform(client);
  const int status_code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (error != ESP_OK || status_code != 200 || response.overflow) {
    const auto classified_error = classify_http_error_(error, status_code, response.overflow);
    note_weather_service_failure_(AirDot::connectivity::Service::WEATHER_LOCATION, classified_error);
    return false;
  }

  float parsed_latitude = 0.0f;
  float parsed_longitude = 0.0f;
  if (!extract_json_number_(response.body, "\"latitude\"", parsed_latitude) ||
      !extract_json_number_(response.body, "\"longitude\"", parsed_longitude)) {
    note_weather_service_failure_(
        AirDot::connectivity::Service::WEATHER_LOCATION,
        AirDot::connectivity::ConnectivityError::INVALID_RESPONSE);
    return false;
  }
  if (!coordinates_are_valid_(parsed_latitude, parsed_longitude)) {
    note_weather_service_failure_(
        AirDot::connectivity::Service::WEATHER_LOCATION,
        AirDot::connectivity::ConnectivityError::INVALID_RESPONSE);
    return false;
  }

  latitude = parsed_latitude;
  longitude = parsed_longitude;
  note_weather_service_success_(AirDot::connectivity::Service::WEATHER_LOCATION);
  return true;
}

inline std::atomic<bool> &ipwhois_ipv6_location_suppressed_() {
  static std::atomic<bool> suppressed{false};
  return suppressed;
}

inline bool has_preferred_global_ipv6_address_() {
#if CONFIG_LWIP_IPV6
  esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (sta_netif == nullptr)
    return false;

  esp_ip6_addr_t global_ipv6{};
  return esp_netif_get_ip6_global(sta_netif, &global_ipv6) == ESP_OK;
#else
  return false;
#endif
}

inline void reset_ipwhois_ipv6_location_probe() {
  ipwhois_ipv6_location_suppressed_().store(false, std::memory_order_release);
}

inline bool fetch_ipwhois_location(float &latitude, float &longitude) {
  if (!ipwhois_ipv6_location_suppressed_().load(std::memory_order_acquire) && has_preferred_global_ipv6_address_()) {
    if (fetch_ipwhois_location_(HTTP_ADDR_TYPE_INET6, latitude, longitude))
      return true;

    ipwhois_ipv6_location_suppressed_().store(true, std::memory_order_release);
  }

  return fetch_ipwhois_location_(HTTP_ADDR_TYPE_INET, latitude, longitude);
}

inline bool fetch_ipwhois_ipv6_location(float &latitude, float &longitude) {
  esphome::watchdog::WatchdogManager watchdog(15000);
  return fetch_ipwhois_location(latitude, longitude);
}

struct OpenMeteoWeather {
  float temperature_c{0.0f};
  float humidity_percent{0.0f};
  float pressure_hpa{0.0f};
  float high_temperature_c{0.0f};
  float low_temperature_c{0.0f};
  int weather_code{-1};
};

inline bool fetch_open_meteo_weather(float latitude, float longitude, OpenMeteoWeather &weather) {
  if (!coordinates_are_valid_(latitude, longitude)) {
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::WEATHER,
        AirDot::connectivity::ConnectivityStatus::CONFIG_INVALID,
        AirDot::connectivity::ConnectivityError::CONFIG_INVALID, esphome::millis());
    return false;
  }

  char url[384];
  const int written = std::snprintf(
      url, sizeof(url),
      "https://api.open-meteo.com/v1/forecast?latitude=%.7f&longitude=%.7f"
      "&current=temperature_2m,relative_humidity_2m,pressure_msl,weather_code"
      "&daily=temperature_2m_max,temperature_2m_min,weather_code"
      "&temperature_unit=celsius&timezone=auto&forecast_days=1",
      latitude, longitude);
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(url)) {
    return false;
  }

  HttpResponse response;
  response.max_length = 2048;
  esp_http_client_config_t config{};
  config.url = url;
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = 5000;
  config.event_handler = http_response_event_handler_;
  config.user_data = &response;
  config.buffer_size = 768;
  config.buffer_size_tx = 512;
  config.user_agent = "";
  config.addr_type = HTTP_ADDR_TYPE_INET;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  config.crt_bundle_attach = esp_crt_bundle_attach;
#endif

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    note_weather_service_failure_(
        AirDot::connectivity::Service::WEATHER,
        AirDot::connectivity::ConnectivityError::RESOURCE_EXHAUSTED);
    return false;
  }

  const esp_err_t error = esp_http_client_perform(client);
  const int status_code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (error != ESP_OK || status_code != 200 || response.overflow) {
    const auto classified_error = classify_http_error_(error, status_code, response.overflow);
    note_weather_service_failure_(AirDot::connectivity::Service::WEATHER, classified_error);
    return false;
  }

  float temperature_c = 0.0f;
  float humidity_percent = 0.0f;
  float pressure_hpa = 0.0f;
  float high_temperature_c = 0.0f;
  float low_temperature_c = 0.0f;
  float weather_code = -1.0f;
  if (!extract_json_number_(response.body, "\"temperature_2m\"", temperature_c) ||
      !extract_json_number_(response.body, "\"relative_humidity_2m\"", humidity_percent) ||
      !extract_json_number_(response.body, "\"pressure_msl\"", pressure_hpa) ||
      !extract_json_number_(response.body, "\"temperature_2m_max\"", high_temperature_c) ||
      !extract_json_number_(response.body, "\"temperature_2m_min\"", low_temperature_c)) {
    note_weather_service_failure_(
        AirDot::connectivity::Service::WEATHER,
        AirDot::connectivity::ConnectivityError::INVALID_RESPONSE);
    return false;
  }
  extract_json_number_(response.body, "\"weather_code\"", weather_code);

  weather.temperature_c = temperature_c;
  weather.humidity_percent = humidity_percent;
  weather.pressure_hpa = pressure_hpa;
  weather.high_temperature_c = high_temperature_c;
  weather.low_temperature_c = low_temperature_c;
  weather.weather_code = static_cast<int>(weather_code);
  note_weather_service_success_(AirDot::connectivity::Service::WEATHER);
  return true;
}

inline std::atomic<int> &ipwhois_location_task_state_() {
  static std::atomic<int> state{WEATHER_TASK_IDLE};
  return state;
}

inline std::atomic<int32_t> &ipwhois_location_latitude_e7_() {
  static std::atomic<int32_t> latitude{0};
  return latitude;
}

inline std::atomic<int32_t> &ipwhois_location_longitude_e7_() {
  static std::atomic<int32_t> longitude{0};
  return longitude;
}

inline void ipwhois_location_task_(void *) {
  esphome::watchdog::WatchdogManager watchdog(20000);
  float latitude = 0.0f;
  float longitude = 0.0f;
  const bool acquired = acquire_weather_http_request_slot_(AirDot::connectivity::Service::WEATHER_LOCATION);
  if (acquired && fetch_ipwhois_location(latitude, longitude)) {
    ipwhois_location_latitude_e7_().store(static_cast<int32_t>(latitude * 10000000.0f), std::memory_order_release);
    ipwhois_location_longitude_e7_().store(static_cast<int32_t>(longitude * 10000000.0f), std::memory_order_release);
    ipwhois_location_task_state_().store(WEATHER_TASK_SUCCESS, std::memory_order_release);
  } else {
    ipwhois_location_task_state_().store(WEATHER_TASK_FAILED, std::memory_order_release);
  }
  if (acquired)
    release_weather_http_request_slot_();

  vTaskDelete(nullptr);
}

inline bool start_ipwhois_location_request() {
  if (!weather_service_start_allowed_(AirDot::connectivity::Service::WEATHER_LOCATION))
    return false;

  int expected = WEATHER_TASK_IDLE;
  if (!ipwhois_location_task_state_().compare_exchange_strong(expected, WEATHER_TASK_RUNNING, std::memory_order_acq_rel))
    return false;

  const BaseType_t created = xTaskCreate(
      ipwhois_location_task_, "location", WEATHER_HTTP_TASK_STACK_SIZE, nullptr, 1, nullptr);
  if (created == pdPASS)
    return true;

  ipwhois_location_task_state_().store(WEATHER_TASK_FAILED, std::memory_order_release);
  note_weather_service_failure_(
      AirDot::connectivity::Service::WEATHER_LOCATION,
      AirDot::connectivity::ConnectivityError::RESOURCE_EXHAUSTED);
  return false;
}

inline int consume_ipwhois_location_result(float &latitude, float &longitude) {
  const int state = ipwhois_location_task_state_().load(std::memory_order_acquire);
  if (state == WEATHER_TASK_SUCCESS) {
    latitude = static_cast<float>(ipwhois_location_latitude_e7_().load(std::memory_order_acquire)) / 10000000.0f;
    longitude = static_cast<float>(ipwhois_location_longitude_e7_().load(std::memory_order_acquire)) / 10000000.0f;
    ipwhois_location_task_state_().store(WEATHER_TASK_IDLE, std::memory_order_release);
    return 1;
  }

  if (state == WEATHER_TASK_FAILED) {
    ipwhois_location_task_state_().store(WEATHER_TASK_IDLE, std::memory_order_release);
    return -1;
  }

  return 0;
}

inline std::atomic<int> &open_meteo_weather_task_state_() {
  static std::atomic<int> state{WEATHER_TASK_IDLE};
  return state;
}

inline std::atomic<int32_t> &open_meteo_request_latitude_e7_() {
  static std::atomic<int32_t> latitude{0};
  return latitude;
}

inline std::atomic<int32_t> &open_meteo_request_longitude_e7_() {
  static std::atomic<int32_t> longitude{0};
  return longitude;
}

inline std::atomic<int32_t> &open_meteo_temperature_c_e2_() {
  static std::atomic<int32_t> temperature{0};
  return temperature;
}

inline std::atomic<int32_t> &open_meteo_humidity_percent_e2_() {
  static std::atomic<int32_t> humidity{0};
  return humidity;
}

inline std::atomic<int32_t> &open_meteo_pressure_hpa_e2_() {
  static std::atomic<int32_t> pressure{0};
  return pressure;
}

inline std::atomic<int32_t> &open_meteo_high_temperature_c_e2_() {
  static std::atomic<int32_t> temperature{0};
  return temperature;
}

inline std::atomic<int32_t> &open_meteo_low_temperature_c_e2_() {
  static std::atomic<int32_t> temperature{0};
  return temperature;
}

inline std::atomic<int> &open_meteo_weather_code_() {
  static std::atomic<int> weather_code{-1};
  return weather_code;
}

inline void open_meteo_weather_task_(void *) {
  esphome::watchdog::WatchdogManager watchdog(20000);
  const float latitude = static_cast<float>(open_meteo_request_latitude_e7_().load(std::memory_order_acquire)) /
                         10000000.0f;
  const float longitude = static_cast<float>(open_meteo_request_longitude_e7_().load(std::memory_order_acquire)) /
                          10000000.0f;

  OpenMeteoWeather weather;
  const bool acquired = acquire_weather_http_request_slot_(AirDot::connectivity::Service::WEATHER);
  if (acquired && fetch_open_meteo_weather(latitude, longitude, weather)) {
    open_meteo_temperature_c_e2_().store(static_cast<int32_t>(weather.temperature_c * 100.0f),
                                         std::memory_order_release);
    open_meteo_humidity_percent_e2_().store(static_cast<int32_t>(weather.humidity_percent * 100.0f),
                                            std::memory_order_release);
    open_meteo_pressure_hpa_e2_().store(static_cast<int32_t>(weather.pressure_hpa * 100.0f),
                                        std::memory_order_release);
    open_meteo_high_temperature_c_e2_().store(static_cast<int32_t>(weather.high_temperature_c * 100.0f),
                                              std::memory_order_release);
    open_meteo_low_temperature_c_e2_().store(static_cast<int32_t>(weather.low_temperature_c * 100.0f),
                                             std::memory_order_release);
    open_meteo_weather_code_().store(weather.weather_code, std::memory_order_release);
    open_meteo_weather_task_state_().store(WEATHER_TASK_SUCCESS, std::memory_order_release);
  } else {
    open_meteo_weather_task_state_().store(WEATHER_TASK_FAILED, std::memory_order_release);
  }
  if (acquired)
    release_weather_http_request_slot_();

  vTaskDelete(nullptr);
}

inline bool start_open_meteo_weather_request(float latitude, float longitude) {
  if (!coordinates_are_valid_(latitude, longitude)) {
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::WEATHER,
        AirDot::connectivity::ConnectivityStatus::CONFIG_INVALID,
        AirDot::connectivity::ConnectivityError::CONFIG_INVALID, esphome::millis());
    return false;
  }

  if (!weather_service_start_allowed_(AirDot::connectivity::Service::WEATHER))
    return false;

  int expected = WEATHER_TASK_IDLE;
  if (!open_meteo_weather_task_state_().compare_exchange_strong(
          expected, WEATHER_TASK_RUNNING, std::memory_order_acq_rel))
    return false;

  open_meteo_request_latitude_e7_().store(static_cast<int32_t>(latitude * 10000000.0f), std::memory_order_release);
  open_meteo_request_longitude_e7_().store(static_cast<int32_t>(longitude * 10000000.0f), std::memory_order_release);

  const BaseType_t created = xTaskCreate(
      open_meteo_weather_task_, "weather", WEATHER_HTTP_TASK_STACK_SIZE, nullptr, 1, nullptr);
  if (created == pdPASS)
    return true;

  open_meteo_weather_task_state_().store(WEATHER_TASK_FAILED, std::memory_order_release);
  note_weather_service_failure_(
      AirDot::connectivity::Service::WEATHER,
      AirDot::connectivity::ConnectivityError::RESOURCE_EXHAUSTED);
  return false;
}

inline bool open_meteo_weather_request_matches(float latitude, float longitude) {
  if (!coordinates_are_valid_(latitude, longitude))
    return false;

  return open_meteo_request_latitude_e7_().load(std::memory_order_acquire) ==
             static_cast<int32_t>(latitude * 10000000.0f) &&
         open_meteo_request_longitude_e7_().load(std::memory_order_acquire) ==
             static_cast<int32_t>(longitude * 10000000.0f);
}

inline int consume_open_meteo_weather_result(float &temperature_c, float &humidity_percent, float &pressure_hpa,
                                             float &high_temperature_c, float &low_temperature_c, int &weather_code) {
  const int state = open_meteo_weather_task_state_().load(std::memory_order_acquire);
  if (state == WEATHER_TASK_SUCCESS) {
    temperature_c = static_cast<float>(open_meteo_temperature_c_e2_().load(std::memory_order_acquire)) / 100.0f;
    humidity_percent =
        static_cast<float>(open_meteo_humidity_percent_e2_().load(std::memory_order_acquire)) / 100.0f;
    pressure_hpa = static_cast<float>(open_meteo_pressure_hpa_e2_().load(std::memory_order_acquire)) / 100.0f;
    high_temperature_c =
        static_cast<float>(open_meteo_high_temperature_c_e2_().load(std::memory_order_acquire)) / 100.0f;
    low_temperature_c =
        static_cast<float>(open_meteo_low_temperature_c_e2_().load(std::memory_order_acquire)) / 100.0f;
    weather_code = open_meteo_weather_code_().load(std::memory_order_acquire);
    open_meteo_weather_task_state_().store(WEATHER_TASK_IDLE, std::memory_order_release);
    return 1;
  }

  if (state == WEATHER_TASK_FAILED) {
    open_meteo_weather_task_state_().store(WEATHER_TASK_IDLE, std::memory_order_release);
    return -1;
  }

  return 0;
}
#else
inline bool fetch_ipwhois_location(float &latitude, float &longitude) {
  (void) latitude;
  (void) longitude;
  return false;
}

inline bool http_request_active() { return false; }

inline bool fetch_ipwhois_ipv6_location(float &latitude, float &longitude) {
  (void) latitude;
  (void) longitude;
  return false;
}

inline bool start_ipwhois_location_request() { return false; }

inline int consume_ipwhois_location_result(float &latitude, float &longitude) {
  (void) latitude;
  (void) longitude;
  return -1;
}

inline void reset_ipwhois_ipv6_location_probe() {}

inline bool start_open_meteo_weather_request(float latitude, float longitude) {
  (void) latitude;
  (void) longitude;
  return false;
}

inline bool open_meteo_weather_request_matches(float latitude, float longitude) {
  (void) latitude;
  (void) longitude;
  return false;
}

inline int consume_open_meteo_weather_result(float &temperature_c, float &humidity_percent, float &pressure_hpa,
                                             float &high_temperature_c, float &low_temperature_c, int &weather_code) {
  (void) temperature_c;
  (void) humidity_percent;
  (void) pressure_hpa;
  (void) high_temperature_c;
  (void) low_temperature_c;
  (void) weather_code;
  return -1;
}
#endif

}  // namespace AirDot::time_weather
