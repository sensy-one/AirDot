#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/hal.h"

#include "display_runtime.h"
#include "setup_settings.h"
#include "setup_runtime.h"
#include "setup_page_renderer.h"
#include "time_weather_client.h"

namespace AirDot::onboarding {

class SetupHandler : public AsyncWebHandler {
 public:
  SetupHandler(int *active, int *saved, uint32_t *last_activity_ms, int *setup_pending_wifi_save, std::string *setup_pending_wifi_ssid,
               std::string *setup_pending_wifi_password, int *setup_pending_sen66_co2_calibration,
               uint16_t *setup_pending_sen66_co2_reference_ppm, int *setup_firmware_update_active)
      : active_(active), saved_(saved), last_activity_ms_(last_activity_ms), pending_wifi_save_(setup_pending_wifi_save),
        pending_wifi_ssid_(setup_pending_wifi_ssid), pending_wifi_password_(setup_pending_wifi_password),
        pending_sen66_co2_calibration_(setup_pending_sen66_co2_calibration),
        pending_sen66_co2_reference_ppm_(setup_pending_sen66_co2_reference_ppm),
        firmware_update_active_(setup_firmware_update_active) {}

  bool canHandle(AsyncWebServerRequest *request) const override {
    if (this->active_ == nullptr || *this->active_ != 1)
      return false;

    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    const auto url = request->url_to(url_buf);
    if (request->method() == HTTP_GET) {
      return url == "/" || url == "/settings" || url == "/i18n" || url == "/activity" ||
             url == "/firmware-update-start" || url == "/firmware-update-cancel" || url == "/favicon.ico";
    }

    return request->method() == HTTP_POST && (url == "/" || url == "/settings");
  }

  void handleRequest(AsyncWebServerRequest *request) override {
    if (this->last_activity_ms_ != nullptr)
      *this->last_activity_ms_ = millis();

    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    const auto url = request->url_to(url_buf);
    if (this->saved_redirect_pending_ && this->saved_redirect_started_ms_ != 0 &&
        static_cast<uint32_t>(millis() - this->saved_redirect_started_ms_) > 120000UL) {
      this->saved_redirect_pending_ = false;
      this->saved_redirect_started_ms_ = 0;
    }

    if (url == "/firmware-update-start") {
      if (this->firmware_update_active_ != nullptr)
        *this->firmware_update_active_ = 1;
      this->send_setup_response_(request, "200 OK", "text/plain; charset=utf-8", "OK");
      return;
    }

    if (url == "/firmware-update-cancel") {
      if (this->firmware_update_active_ != nullptr)
        *this->firmware_update_active_ = -1;
      this->send_setup_response_(request, "200 OK", "text/plain; charset=utf-8", "OK");
      return;
    }

    if (request->method() == HTTP_POST) {
      this->save_(request);
      this->saved_redirect_pending_ = true;
      this->saved_redirect_started_ms_ = millis();
      if (this->saved_redirect_started_ms_ == 0)
        this->saved_redirect_started_ms_ = 1;
      this->send_saved_redirect_(request);
      return;
    }

    if (this->saved_redirect_pending_) {
      if (url != "/") {
        this->send_saved_redirect_(request);
        return;
      }

      this->send_saved_page_(request);
      this->saved_redirect_pending_ = false;
      this->saved_redirect_started_ms_ = 0;
      if (this->saved_ != nullptr)
        *this->saved_ = 1;
      if (this->active_ != nullptr)
        *this->active_ = 0;
      return;
    }

    if (url == "/i18n") {
      this->send_translation_(request);
      return;
    }

    if (url == "/activity") {
      this->send_setup_response_(request, "200 OK", "text/plain; charset=utf-8", "OK");
      return;
    }

    if (url == "/favicon.ico") {
      this->send_setup_response_(request, "204 No Content", "image/x-icon", "");
      return;
    }

    this->prepare_setup_page_();
    SetupPageRenderer::send_page(request, false);
  }

 protected:
  static void prepare_setup_page_() {
    if (setup_network_options_empty_()) {
      refresh_setup_network_scan_cache();
      if (setup_network_options_empty_())
        request_setup_network_scan();
    }
  }

  static void send_setup_response_(AsyncWebServerRequest *request, const char *status, const char *content_type,
                                   const char *content) {
    if (request == nullptr)
      return;

    httpd_resp_set_status(*request, status);
    httpd_resp_set_type(*request, content_type);
    httpd_resp_set_hdr(*request, "Accept-Ranges", "none");
    httpd_resp_set_hdr(*request, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(*request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(*request, "Connection", "close");
    httpd_resp_send(*request, content, content != nullptr ? HTTPD_RESP_USE_STRLEN : 0);
  }

  static void send_saved_page_(AsyncWebServerRequest *request) {
    send_setup_response_(
        request, "200 OK", "text/html; charset=utf-8",
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>html,body{margin:0;min-height:100%;background:#000}</style>"
        "</head><body></body></html>");
  }

  static void send_saved_redirect_(AsyncWebServerRequest *request) {
    if (request == nullptr)
      return;

    httpd_resp_set_status(*request, "303 See Other");
    httpd_resp_set_type(*request, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(*request, "Location", "/");
    httpd_resp_set_hdr(*request, "Accept-Ranges", "none");
    httpd_resp_set_hdr(*request, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(*request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(*request, "Connection", "close");
    httpd_resp_send(*request, "", 0);
  }

  static std::string selected_wifi_ssid_(AsyncWebServerRequest *request) {
    if (request == nullptr)
      return {};
    if (request->hasArg("wifi_ssid_select")) {
      const std::string selected = bounded_arg_(request, "wifi_ssid_select", MAX_WIFI_SSID_LENGTH);
      if (!selected.empty())
        return selected;
    }
    return bounded_arg_(request, "wifi_ssid", MAX_WIFI_SSID_LENGTH);
  }

  void save_(AsyncWebServerRequest *request) {
    const std::string selected_wifi_ssid = selected_wifi_ssid_(request);
    const bool wifi_enabled = !selected_wifi_ssid.empty();
    const bool time_sync_enabled = request->hasArg("time_server_enabled");
    const std::string time_source = bounded_arg_(request, "time_source", 12);
    const bool manual_time_requested = time_sync_enabled && time_source == "manual";
    const bool time_server_enabled = wifi_enabled && time_sync_enabled && !manual_time_requested;
    uint32_t manual_time_epoch = 0;
    const bool manual_time_valid =
        manual_time_requested &&
        parse_manual_time_epoch_(bounded_arg_(request, "manual_time_epoch", 10), manual_time_epoch);
    const bool weather_enabled = wifi_enabled && request->hasArg("weather_enabled");
    const bool ha_discovery_enabled = wifi_enabled && request->hasArg("ha_discovery");
    const bool mqtt_enabled = wifi_enabled && request->hasArg("mqtt_enabled");
    const auto selected_ui_language =
        ui_language_from_value(bounded_arg_(request, "ui_language", MAX_UI_LANGUAGE_VALUE_LENGTH));
    const auto &stored_flight_radar_settings = load_flight_radar_settings();
    const bool flight_radar_enabled = wifi_enabled && request->hasArg("flight_radar_enabled");
    const uint8_t flight_radar_range_km = request->hasArg("flight_radar_range_km")
                                              ? parse_flight_radar_range_km_(
                                                    bounded_arg_(request, "flight_radar_range_km", 3),
                                                    stored_flight_radar_settings.range_km)
                                              : stored_flight_radar_settings.range_km;
    const bool flight_radar_military_only =
        request->hasArg("flight_radar_traffic")
            ? bounded_arg_(request, "flight_radar_traffic", 16) == "military"
            : stored_flight_radar_settings.traffic_mode == FLIGHT_RADAR_TRAFFIC_MILITARY_ONLY;

    save_ui_language(selected_ui_language);
    save_time_server_enabled(time_server_enabled);
    save_manual_time_enabled(manual_time_valid);
    if (wifi_enabled)
      save_weather_enabled(weather_enabled);
    save_flight_radar_settings(flight_radar_enabled, flight_radar_range_km, flight_radar_military_only);
    save_home_assistant_discovery_enabled(ha_discovery_enabled);
    save_unit_system(request->hasArg("units") && request->arg("units") == "imperial" ? UNIT_SYSTEM_IMPERIAL
                                                                                       : UNIT_SYSTEM_METRIC);
    if (request->hasArg("time_format")) {
      save_time_format(request->arg("time_format") == "12h" ? TIME_FORMAT_12H : TIME_FORMAT_24H);
    }
    if (manual_time_valid) {
      AirDot::time_weather::set_manual_time_epoch(manual_time_epoch);
    } else if (!time_server_enabled) {
      AirDot::time_weather::stop_sntp_sync();
    }
    const auto &stored_location = load_location_settings();
    const bool stored_location_valid =
        location_e7_coordinates_are_valid(stored_location.latitude_e7, stored_location.longitude_e7);
    int32_t exact_latitude_e7 = stored_location_valid ? stored_location.latitude_e7 : 0;
    int32_t exact_longitude_e7 = stored_location_valid ? stored_location.longitude_e7 : 0;
    const bool latitude_valid =
        parse_location_coordinate_e7_(bounded_arg_(request, "location_latitude", 18), LATITUDE_MIN_E7,
                                      LATITUDE_MAX_E7, exact_latitude_e7) ||
        stored_location_valid;
    const bool longitude_valid =
        parse_location_coordinate_e7_(bounded_arg_(request, "location_longitude", 18), LONGITUDE_MIN_E7,
                                      LONGITUDE_MAX_E7, exact_longitude_e7) ||
        stored_location_valid;
    save_location_settings_e7(
        request->hasArg("exact_location_enabled") && latitude_valid && longitude_valid,
        exact_latitude_e7, exact_longitude_e7);
    if ((time_server_enabled || manual_time_valid) && request->hasArg("time_zone_offset_schedule")) {
      const std::string time_zone_offset_schedule =
          bounded_arg_(request, "time_zone_offset_schedule", MAX_TIME_ZONE_OFFSET_SCHEDULE_LENGTH);
      if (!time_zone_offset_schedule.empty()) {
        save_time_zone_offset_schedule_(parse_time_zone_offset_schedule_(time_zone_offset_schedule));
      }
    }
    save_display_brightness(parse_display_brightness_(bounded_arg_(request, "brightness", 8)));
    save_dark_mode_enabled(request->hasArg("dark_mode"));
    save_auto_dim_enabled(request->hasArg("auto_dim"));
    const bool auto_page_switch_enabled = bounded_arg_(request, "auto_page_switch", 1) == "1";
    save_auto_page_switch_enabled(auto_page_switch_enabled);
    save_night_screen_off_enabled((time_server_enabled || manual_time_valid) && request->hasArg("night_screen_off"));
    save_screen_off_start_minutes(
        parse_minute_of_day_(bounded_arg_(request, "screen_off_start", 8), load_screen_off_start_minutes()));
    save_screen_off_end_minutes(
        parse_minute_of_day_(bounded_arg_(request, "screen_off_end", 8), load_screen_off_end_minutes()));
    if (ha_discovery_enabled || mqtt_enabled || auto_page_switch_enabled) {
      save_monitoring_update_interval_seconds(
          parse_monitoring_update_interval_(bounded_arg_(request, "update_interval", 2)));
    }
    save_display_alert_wake_screen_enabled(request->hasArg("display_alert_wake_screen"));
    save_audio_alerts_enabled(request->hasArg("audio_alerts"));
    save_hazard_focus_mode_enabled(request->hasArg("hazard_focus_mode"));
    save_air_quality_profile(
        AirDot::air_quality_profile_from_value(bounded_arg_(request, "air_quality_profile", 32)));
    save_sen66_temperature_offset_display_c(parse_sen66_temperature_offset_display_c_(
        bounded_arg_(request, "sen66_temperature_offset_c", 8), sen66_temperature_offset_display_c()));
    if (this->pending_sen66_co2_calibration_ != nullptr && this->pending_sen66_co2_reference_ppm_ != nullptr) {
      *this->pending_sen66_co2_calibration_ = request->hasArg("sen66_force_co2_calibration") ? 1 : 0;
      *this->pending_sen66_co2_reference_ppm_ =
          parse_bounded_uint16_(bounded_arg_(request, "sen66_co2_reference_ppm", 5), 427, 400, 2000);
    }
    if (mqtt_enabled) {
      const auto &existing_mqtt_settings = load_mqtt_settings();
      const std::string mqtt_username = bounded_arg_(request, "mqtt_username", MAX_MQTT_USERNAME_LENGTH);
      std::string mqtt_password = bounded_arg_(request, "mqtt_password", MAX_MQTT_PASSWORD_LENGTH);
      if (mqtt_password.empty() && mqtt_username == fixed_string_(existing_mqtt_settings.username))
        mqtt_password = fixed_string_(existing_mqtt_settings.password);
      save_mqtt_settings(
          true, bounded_arg_(request, "mqtt_broker", MAX_MQTT_BROKER_LENGTH),
          bounded_arg_(request, "mqtt_port", 5),
          mqtt_username,
          mqtt_password,
          bounded_arg_(request, "mqtt_topic_prefix", MAX_MQTT_TOPIC_PREFIX_LENGTH));
    } else {
      const auto &settings = load_mqtt_settings();
      save_mqtt_settings(
          false, fixed_string_(settings.broker), settings.port, fixed_string_(settings.username),
          fixed_string_(settings.password), fixed_string_(settings.topic_prefix));
    }
    this->save_wifi_(request, selected_wifi_ssid);
  }

  static bool parse_signed_int_(const std::string &value, size_t &index, int &parsed) {
    if (index >= value.size())
      return false;

    bool negative = false;
    if (value[index] == '-' || value[index] == '+') {
      negative = value[index] == '-';
      index++;
    }

    if (index >= value.size() || value[index] < '0' || value[index] > '9')
      return false;

    int result = 0;
    while (index < value.size() && value[index] >= '0' && value[index] <= '9') {
      result = result * 10 + (value[index] - '0');
      if (result > 214748000)
        return false;
      index++;
    }

    parsed = negative ? -result : result;
    return true;
  }

  static bool parse_unsigned_int_(const std::string &value, size_t &index, uint32_t &parsed) {
    if (index >= value.size() || value[index] < '0' || value[index] > '9')
      return false;

    uint32_t result = 0;
    while (index < value.size() && value[index] >= '0' && value[index] <= '9') {
      const uint32_t digit = static_cast<uint32_t>(value[index] - '0');
      if (result > 429496729UL || (result == 429496729UL && digit > 5UL))
        return false;
      result = result * 10UL + digit;
      index++;
    }

    parsed = result;
    return true;
  }

  static TimeZoneOffsetSchedule parse_time_zone_offset_schedule_(const std::string &value) {
    TimeZoneOffsetSchedule schedule{};

    if (value.empty())
      return schedule;

    size_t index = 0;
    int base_offset = 0;
    if (!parse_signed_int_(value, index, base_offset))
      return schedule;

    schedule.valid = 1;
    schedule.base_offset_minutes = normalize_time_zone_offset_minutes_(base_offset);
    if (index >= value.size())
      return schedule;
    if (value[index] != '|')
      return schedule;
    index++;

    uint32_t last_timestamp = 0;
    while (index < value.size() && schedule.transition_count < TIME_ZONE_OFFSET_MAX_TRANSITIONS) {
      uint32_t timestamp = 0;
      int offset = 0;
      if (!parse_unsigned_int_(value, index, timestamp))
        break;
      if (index >= value.size() || value[index] != ':')
        break;
      index++;
      if (!parse_signed_int_(value, index, offset))
        break;
      if (timestamp > last_timestamp) {
        auto &transition = schedule.transitions[schedule.transition_count++];
        transition.utc_timestamp = timestamp;
        transition.offset_minutes = normalize_time_zone_offset_minutes_(offset);
        last_timestamp = timestamp;
      }
      if (index >= value.size())
        break;
      if (value[index] != ',')
        break;
      index++;
    }

    return schedule;
  }

  static DisplayBrightness parse_display_brightness_(const std::string &value) {
    if (value == "low")
      return DISPLAY_BRIGHTNESS_LOW;
    if (value == "high")
      return DISPLAY_BRIGHTNESS_HIGH;
    return DISPLAY_BRIGHTNESS_MEDIUM;
  }

  static bool parse_manual_time_epoch_(const std::string &value, uint32_t &epoch) {
    if (value.empty() || value.size() > 10)
      return false;

    uint32_t parsed = 0;
    for (const char character : value) {
      if (character < '0' || character > '9')
        return false;
      const uint32_t digit = static_cast<uint32_t>(character - '0');
      if (parsed > (UINT32_MAX - digit) / 10)
        return false;
      parsed = parsed * 10 + digit;
    }

    if (parsed < 946684800UL)
      return false;
    epoch = parsed;
    return true;
  }

  static uint16_t parse_minute_of_day_(const std::string &value, uint16_t fallback) {
    if (value.size() < 5 || value[2] != ':')
      return fallback;
    if (value[0] < '0' || value[0] > '9' || value[1] < '0' || value[1] > '9' ||
        value[3] < '0' || value[3] > '9' || value[4] < '0' || value[4] > '9') {
      return fallback;
    }

    const uint8_t hour = static_cast<uint8_t>((value[0] - '0') * 10 + (value[1] - '0'));
    const uint8_t minute = static_cast<uint8_t>((value[3] - '0') * 10 + (value[4] - '0'));
    if (hour > 23 || minute > 59)
      return fallback;
    return static_cast<uint16_t>(hour) * 60 + minute;
  }

  static uint8_t parse_monitoring_update_interval_(const std::string &value) {
    if (value == "5")
      return 5;
    if (value == "30")
      return 30;
    return 10;
  }

  static float parse_sen66_temperature_offset_display_c_(const std::string &value, float fallback) {
    std::string normalized = trim_copy_(value);
    if (normalized.empty())
      return fallback;

    for (char &character : normalized) {
      if (character == ',')
        character = '.';
    }

    char *end = nullptr;
    const float parsed = std::strtof(normalized.c_str(), &end);
    if (end == normalized.c_str() || end == nullptr || *end != '\0' || !std::isfinite(parsed))
      return fallback;

    return std::fabs(parsed);
  }

  static bool parse_location_coordinate_e7_(const std::string &value, int32_t minimum_e7, int32_t maximum_e7,
                                            int32_t &coordinate_e7) {
    std::string normalized = trim_copy_(value);
    if (normalized.empty())
      return false;

    size_t index = 0;
    bool negative = false;
    if (normalized[index] == '-') {
      negative = true;
      index++;
    }
    if (index >= normalized.size())
      return false;

    int64_t whole = 0;
    uint8_t whole_digits = 0;
    while (index < normalized.size() && normalized[index] >= '0' && normalized[index] <= '9') {
      whole = whole * 10 + (normalized[index] - '0');
      whole_digits++;
      index++;
    }
    if (whole_digits == 0)
      return false;

    int64_t fraction = 0;
    uint8_t fraction_digits = 0;
    if (index < normalized.size() && (normalized[index] == '.' || normalized[index] == ',')) {
      index++;
      while (index < normalized.size() && normalized[index] >= '0' && normalized[index] <= '9') {
        if (fraction_digits >= 7)
          return false;
        fraction = fraction * 10 + (normalized[index] - '0');
        fraction_digits++;
        index++;
      }
      if (fraction_digits == 0)
        return false;
    }
    if (index != normalized.size())
      return false;

    while (fraction_digits < 7) {
      fraction *= 10;
      fraction_digits++;
    }

    int64_t e7 = whole * 10000000LL + fraction;
    if (negative)
      e7 = -e7;
    if (e7 < minimum_e7 || e7 > maximum_e7)
      return false;

    coordinate_e7 = static_cast<int32_t>(e7);
    return true;
  }

  static uint8_t parse_flight_radar_range_km_(const std::string &value, uint8_t fallback) {
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (end == value.c_str() || end == nullptr || *end != '\0')
      return normalize_flight_radar_range_km(fallback);
    return normalize_flight_radar_range_km(static_cast<int>(parsed));
  }

  static uint16_t parse_bounded_uint16_(const std::string &value, uint16_t fallback, uint16_t minimum,
                                        uint16_t maximum) {
    const std::string normalized = trim_copy_(value);
    if (normalized.empty())
      return fallback;

    uint32_t parsed = 0;
    for (const char character : normalized) {
      if (character < '0' || character > '9')
        return fallback;
      const uint32_t digit = static_cast<uint32_t>(character - '0');
      if (parsed > (UINT32_MAX - digit) / 10)
        return fallback;
      parsed = parsed * 10 + digit;
    }

    if (parsed < minimum)
      return minimum;
    if (parsed > maximum)
      return maximum;
    return static_cast<uint16_t>(parsed);
  }

  static std::string bounded_arg_(AsyncWebServerRequest *request, const char *name, size_t max_length) {
    if (request == nullptr || !request->hasArg(name))
      return {};

    std::string value = request->arg(name);
    if (value.size() > max_length)
      value.resize(max_length);
    return value;
  }

  void save_wifi_(AsyncWebServerRequest *request, const std::string &ssid) {
    if (request == nullptr || this->pending_wifi_save_ == nullptr || this->pending_wifi_ssid_ == nullptr ||
        this->pending_wifi_password_ == nullptr) {
      return;
    }

    std::string password = bounded_arg_(request, "wifi_password", MAX_WIFI_PASSWORD_LENGTH);
    if (!ssid.empty() && password.empty()) {
      esphome::wifi::SavedWifiSettings existing{};
      if (load_setup_page_wifi_settings(existing) && ssid == fixed_string_(existing.ssid))
        password = fixed_string_(existing.password);
    }

    const auto validation = AirDot::connectivity::validate_wifi_config(ssid, password);
    if (!ssid.empty() && !validation.ok) {
      AirDot::connectivity::set_service_status(
          AirDot::connectivity::Service::WIFI, validation.status, validation.error, esphome::millis());
      *this->pending_wifi_ssid_ = ssid;
      this->pending_wifi_password_->clear();
      *this->pending_wifi_save_ = -1;
      return;
    }

    *this->pending_wifi_ssid_ = ssid;
    *this->pending_wifi_password_ = password;
    *this->pending_wifi_save_ = ssid.empty() ? 0 : 1;
    if (!ssid.empty())
      note_runtime_wifi_settings_saved(ssid, password);
  }

  void send_translation_(AsyncWebServerRequest *request) {
    const auto language = ui_language_from_value(bounded_arg_(request, "lang", MAX_UI_LANGUAGE_VALUE_LENGTH));
    std::string json;
    json.reserve(4096);
    append_setup_page_translation_json(json, language);

    this->send_setup_response_(request, "200 OK", "application/json; charset=utf-8", json.c_str());
  }

  int *active_{nullptr};
  int *saved_{nullptr};
  uint32_t *last_activity_ms_{nullptr};
  bool saved_redirect_pending_{false};
  uint32_t saved_redirect_started_ms_{0};
  int *pending_wifi_save_{nullptr};
  std::string *pending_wifi_ssid_{nullptr};
  std::string *pending_wifi_password_{nullptr};
  int *pending_sen66_co2_calibration_{nullptr};
  uint16_t *pending_sen66_co2_reference_ppm_{nullptr};
  int *firmware_update_active_{nullptr};
};

inline void register_setup_handler(int *active, int *saved, uint32_t *last_activity_ms, int *setup_pending_wifi_save,
                                   std::string *setup_pending_wifi_ssid, std::string *setup_pending_wifi_password,
                                   int *setup_pending_sen66_co2_calibration,
                                   uint16_t *setup_pending_sen66_co2_reference_ppm,
                                   int *setup_firmware_update_active) {
  static bool registered = false;
  if (registered)
    return;

  restore_interrupted_setup_wifi_backup();

  if (esphome::web_server_base::global_web_server_base == nullptr)
    return;

  esphome::web_server_base::global_web_server_base->add_handler_without_auth(
      new SetupHandler(active, saved, last_activity_ms, setup_pending_wifi_save, setup_pending_wifi_ssid,
                       setup_pending_wifi_password, setup_pending_sen66_co2_calibration,
                       setup_pending_sen66_co2_reference_ppm, setup_firmware_update_active));
  registered = true;
}

inline void start_setup_web_server(int *started) {
  if (started == nullptr || *started == 1 || esphome::web_server_base::global_web_server_base == nullptr)
    return;

  esphome::web_server_base::global_web_server_base->init();
  *started = 1;
}

inline void stop_setup_web_server(int *started) {
  if (started == nullptr || *started == 0 || esphome::web_server_base::global_web_server_base == nullptr)
    return;

  esphome::web_server_base::global_web_server_base->deinit();
  *started = 0;
}

enum Screen : int {
  SETUP_CONNECT = 0,
  SETUP_APPLYING,
  SETUP_COMPLETE,
  SETUP_LANGUAGE,
};

inline ScreenContent screen_content(int screen) {
  return screen_content(screen, load_ui_language());
}

}  // namespace AirDot::onboarding
