#pragma once

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "esphome/components/web_server_base/web_server_base.h"

#ifndef AIRDOT_FIRMWARE_VERSION
#define AIRDOT_FIRMWARE_VERSION "unknown"
#endif

namespace AirDot::onboarding {

class ChunkedResponse {
 public:
  ChunkedResponse(AsyncWebServerRequest *request, const char *content_type) : request_(request) {
    if (this->request_ == nullptr) {
      this->ok_ = false;
      return;
    }

    httpd_resp_set_status(*this->request_, "200 OK");
    httpd_resp_set_type(*this->request_, content_type);
    httpd_resp_set_hdr(*this->request_, "Accept-Ranges", "none");
    httpd_resp_set_hdr(*this->request_, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(*this->request_, "Cache-Control", "no-store");
    httpd_resp_set_hdr(*this->request_, "Connection", "close");
    this->buffer_.reserve(CHUNK_BUFFER_SIZE);
  }

  ~ChunkedResponse() { this->finish(); }

  void reserve(size_t) {}

  ChunkedResponse &operator+=(const char *text) {
    if (text != nullptr)
      this->append_(text, std::strlen(text));
    return *this;
  }

  ChunkedResponse &operator+=(const std::string &text) {
    this->append_(text.c_str(), text.size());
    return *this;
  }

  void finish() {
    if (this->finished_)
      return;
    this->flush_();
    if (this->ok_)
      httpd_resp_send_chunk(*this->request_, nullptr, 0);
    this->finished_ = true;
  }

 protected:
  static constexpr size_t CHUNK_BUFFER_SIZE = 1024;

  void append_(const char *data, size_t length) {
    if (!this->ok_ || data == nullptr || length == 0)
      return;

    if (length >= CHUNK_BUFFER_SIZE) {
      this->flush_();
      this->send_chunk_(data, length);
      return;
    }

    if (this->buffer_.size() + length > CHUNK_BUFFER_SIZE)
      this->flush_();
    this->buffer_.append(data, length);
  }

  void flush_() {
    if (!this->ok_ || this->buffer_.empty())
      return;

    this->send_chunk_(this->buffer_.c_str(), this->buffer_.size());
    this->buffer_.clear();
  }

  void send_chunk_(const char *data, size_t length) {
    if (httpd_resp_send_chunk(*this->request_, data, length) != ESP_OK)
      this->ok_ = false;
  }

  AsyncWebServerRequest *request_{nullptr};
  std::string buffer_;
  bool ok_{true};
  bool finished_{false};
};

class SetupPageRenderer {
 public:
  static void send_page(AsyncWebServerRequest *request, bool saved) {
    const bool stored_ha_discovery_enabled = load_home_assistant_discovery_enabled();
    const UnitSystem unit_system = load_unit_system();
    const bool imperial_units = unit_system == UNIT_SYSTEM_IMPERIAL;
    const TimeFormat time_format = load_time_format();
    const bool time_format_12h = time_format == TIME_FORMAT_12H;
    const DisplayBrightness display_brightness = load_display_brightness();
    const bool dark_mode_enabled = load_dark_mode_enabled();
    const bool auto_dim_enabled = load_auto_dim_enabled();
    const bool auto_page_switch_enabled = load_auto_page_switch_enabled();
    const bool stored_night_screen_off_enabled = load_night_screen_off_enabled();
    const std::string screen_off_start_value = time_input_value_(load_screen_off_start_minutes());
    const std::string screen_off_end_value = time_input_value_(load_screen_off_end_minutes());
    const uint8_t monitoring_update_interval_seconds = load_monitoring_update_interval_seconds();
    const bool audio_alerts_enabled = load_audio_alerts_enabled();
    const bool display_alert_wake_screen_enabled = load_display_alert_wake_screen_enabled();
    const bool hazard_focus_mode_enabled = load_hazard_focus_mode_enabled();
    uint8_t stored_time_server_value = 0;
    const bool time_server_setting_stored =
        load_optional_cached_uint8_preference_(time_server_pref_(), stored_time_server_value);
    const bool stored_time_server_enabled = time_server_setting_stored ? stored_time_server_value == 1 : true;
    const bool manual_time_enabled = load_manual_time_enabled();
    uint8_t stored_weather_value = 0;
    const bool weather_setting_stored =
        load_optional_cached_uint8_preference_(weather_sync_pref_(), stored_weather_value);
    const bool stored_weather_enabled = weather_setting_stored ? stored_weather_value == 1 : true;
    const auto ui_language = load_ui_language();
    const auto text = setup_page_text(ui_language);
    const auto air_quality_profile = load_air_quality_profile();
    const std::string sen66_temperature_offset_value =
        temperature_offset_input_value_(sen66_temperature_offset_display_c());
    const auto &mqtt_settings = load_mqtt_settings();
    const bool stored_mqtt_enabled = mqtt_settings.enabled == 1;
    const std::string mqtt_broker = fixed_string_(mqtt_settings.broker);
    const std::string mqtt_username = fixed_string_(mqtt_settings.username);
    const std::string mqtt_password = fixed_string_(mqtt_settings.password);
    const std::string mqtt_topic_prefix = fixed_string_(mqtt_settings.topic_prefix);
    const auto &location_settings = load_location_settings();
    const bool stored_location_coordinates_valid =
        location_e7_coordinates_are_valid(location_settings.latitude_e7, location_settings.longitude_e7);
    const bool exact_location_enabled =
        location_settings.exact_enabled == 1 && stored_location_coordinates_valid;
    const bool stored_location_has_value =
        stored_location_coordinates_valid &&
        (exact_location_enabled || location_settings.latitude_e7 != 0 || location_settings.longitude_e7 != 0);
    const bool weather_manual_location_selected = exact_location_enabled && stored_location_has_value;
    const std::string exact_location_latitude_value =
        stored_location_has_value ? coordinate_input_value_(location_settings.latitude_e7) : "";
    const std::string exact_location_longitude_value =
        stored_location_has_value ? coordinate_input_value_(location_settings.longitude_e7) : "";
    const auto &flight_radar_settings = load_flight_radar_settings();
    const bool flight_radar_configured_enabled = flight_radar_settings.enabled == 1;
    const bool flight_radar_military_only =
        flight_radar_settings.traffic_mode == FLIGHT_RADAR_TRAFFIC_MILITARY_ONLY;
    const uint8_t flight_radar_range_km = normalize_flight_radar_range_km(flight_radar_settings.range_km);
    const std::string flight_radar_range_value = std::to_string(static_cast<unsigned>(flight_radar_range_km));
    char mqtt_port_value[6];
    std::snprintf(
        mqtt_port_value, sizeof(mqtt_port_value), "%u", static_cast<unsigned>(mqtt_settings.port));
    esphome::wifi::SavedWifiSettings wifi_settings{};
    const bool has_wifi_settings = load_setup_page_wifi_settings(wifi_settings);
    const std::string saved_wifi_ssid = has_wifi_settings ? fixed_string_(wifi_settings.ssid) : "";
    const std::string saved_wifi_password = has_wifi_settings ? fixed_string_(wifi_settings.password) : "";
    const bool wifi_configured = !saved_wifi_ssid.empty();
    const bool flight_radar_active = wifi_configured && flight_radar_configured_enabled;
    const bool time_sync_enabled = stored_time_server_enabled || manual_time_enabled;
    const bool time_server_enabled = wifi_configured && stored_time_server_enabled && !manual_time_enabled;
    const bool manual_time_mode = time_sync_enabled && (manual_time_enabled || !wifi_configured);
    const bool weather_enabled = wifi_configured && stored_weather_enabled;
    const bool default_online_weather_enabled = !weather_setting_stored || !wifi_configured;
    const bool local_time_enabled = time_server_enabled || manual_time_mode;
    const bool ha_discovery_enabled = wifi_configured && stored_ha_discovery_enabled;
    const bool mqtt_enabled = wifi_configured && stored_mqtt_enabled;
    const bool night_screen_off_enabled = local_time_enabled && stored_night_screen_off_enabled;
    ChunkedResponse html(request, "text/html; charset=utf-8");
    html.reserve(40000);
    html += R"html(<!doctype html>
<html lang=")html";
    html += ui_language_value(ui_language);
    html += R"html(">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
)html";
    html += "  <title>";
    html += text.html_title;
    html += R"html(</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #0b0c0f;
      --bg-radial: rgba(255, 255, 255, 0.08);
      --card: rgba(27, 29, 34, 0.78);
      --line: rgba(255, 255, 255, 0.11);
      --line-strong: rgba(255, 255, 255, 0.17);
      --text: #f5f7fb;
      --muted: #aeb6c2;
      --muted-2: #7f8895;
      --accent: #ffffff;
      --accent-text: #0f1115;
      --success-bg: rgba(52, 108, 78, 0.26);
      --success-border: rgba(112, 183, 141, 0.42);
      --success-text: #d8f2e1;
      --shadow: 0 32px 90px rgba(0, 0, 0, 0.42);
      --radius-xl: 32px;
      --radius-lg: 22px;
      --radius-md: 16px;
    }
    * { box-sizing: border-box; }
    html, body { min-height: 100%; }
    body {
      margin: 0;
      font-family: Roboto, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background:
        radial-gradient(circle at 50% -10%, var(--bg-radial), transparent 34rem),
        linear-gradient(180deg, #111111 0%, var(--bg) 56%, #050505 100%);
      color: var(--text);
      letter-spacing: 0;
    }
    button, input, select { font: inherit; }
    .page {
      width: 100%;
      min-height: 100vh;
      display: grid;
      place-items: center;
      padding: 28px 16px;
    }
    .shell {
      width: min(calc(100vw - 32px), 520px);
      max-width: 100%;
    }
    .card {
      position: relative;
      overflow: hidden;
      background: linear-gradient(180deg, rgba(34, 34, 36, 0.92), rgba(18, 18, 20, 0.9));
      border: 1px solid var(--line);
      border-radius: var(--radius-xl);
      box-shadow: var(--shadow);
      backdrop-filter: blur(30px) saturate(130%);
      -webkit-backdrop-filter: blur(30px) saturate(130%);
    }
    .card::before {
      content: "";
      position: absolute;
      inset: 0;
      pointer-events: none;
      background:
        linear-gradient(180deg, rgba(255, 255, 255, 0.11), transparent 25%),
        radial-gradient(circle at 50% 0%, rgba(255, 255, 255, 0.12), transparent 19rem);
      opacity: 0.85;
    }
    .content {
      position: relative;
      padding: 34px 24px 24px;
    }
    .hero {
      text-align: center;
      padding: 8px 10px 26px;
    }
    h1 {
      margin: 0;
      font-size: 40px;
      line-height: 1;
      letter-spacing: 0;
      font-weight: 400;
    }
    .lede {
      margin: 14px auto 0;
      max-width: 360px;
      color: var(--muted);
      font-size: 17px;
      line-height: 1.45;
      letter-spacing: 0;
    }
    .saved {
      display: flex;
      gap: 12px;
      align-items: flex-start;
      margin: 4px 0 20px;
      padding: 14px 15px;
      border-radius: var(--radius-md);
      color: var(--success-text);
      background: var(--success-bg);
      border: 1px solid var(--success-border);
      font-size: 14px;
      line-height: 1.35;
    }
    .saved svg {
      flex: 0 0 auto;
      margin-top: 1px;
      width: 18px;
      height: 18px;
    }
    form {
      display: grid;
      gap: 16px;
    }
    .section {
      padding: 20px;
      background: rgba(255, 255, 255, 0.052);
      border: 1px solid var(--line);
      border-radius: var(--radius-lg);
    }
    .section-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      margin-bottom: 18px;
    }
    h2 {
      margin: 0;
      font-size: 18px;
      line-height: 1.2;
      letter-spacing: 0;
      font-weight: 400;
    }
    .section-note {
      margin: 4px 0 0;
      color: var(--muted-2);
      font-size: 13px;
      line-height: 1.35;
    }
    .field {
      display: grid;
      gap: 8px;
      margin-top: 16px;
    }
    .field.is-first { margin-top: 0; }
    .field > .segmented { margin-top: 0; }
    .field-row {
      display: grid;
      grid-template-columns: minmax(0, 1fr) minmax(112px, 0.34fr);
      gap: 10px;
    }
    .field-row.even {
      grid-template-columns: 1fr 1fr;
    }
    .field-row .field { margin-top: 0; }
    .section > .field-row { margin-top: 16px; }
    .section > .field-row.is-first { margin-top: 0; }
    .mqtt-fields {
      display: grid;
      gap: 16px;
      margin-top: 18px;
    }
    .mqtt-fields .field-row,
    .mqtt-fields .field-row.even {
      grid-template-columns: 1fr;
      gap: 16px;
    }
    .mqtt-fields > .field { margin-top: 0; }
    .mqtt-fields[hidden] { display: none; }
    .time-range {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
      margin-top: 18px;
    }
    .coordinate-range {
      display: grid;
      grid-template-columns: minmax(0, 1fr) minmax(0, 1fr);
      gap: 10px;
      margin-top: 18px;
    }
    .coordinate-range .field { margin-top: 0; }
    .time-range[hidden] { display: none; }
    .coordinate-range[hidden] { display: none; }
    .time-control { min-width: 0; }
    .flight-radar-fields.is-disabled {
      opacity: 0.56;
    }
    .field[hidden],
    .toggle-row[hidden],
    .time-picker-control[hidden],
    .time-source-mode[hidden],
    .manual-time-fields[hidden],
    .exact-location-fields[hidden],
    .flight-radar-fields[hidden],
    .weather-location-mode[hidden] {
      display: none;
    }
    .manual-time-fields {
      display: grid;
      margin-top: 16px;
    }
    .manual-time-control,
    .time-picker-control {
      position: relative;
      display: block;
      width: 100%;
      min-height: 52px;
      border: 1px solid var(--line-strong);
      border-radius: 15px;
      color: var(--text);
      background: rgba(7, 8, 10, 0.42);
      overflow: hidden;
      cursor: pointer;
    }
    .manual-time-value,
    .time-picker-value {
      display: flex;
      align-items: center;
      width: 100%;
      min-height: 52px;
      padding: 0 14px;
      color: var(--text);
      font-size: 14px;
      text-align: left;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .manual-time-control input,
    .time-picker-control input {
      position: absolute;
      inset: 0;
      width: 100%;
      height: 100%;
      min-height: 52px;
      border: 0;
      color: transparent;
      background: transparent;
      appearance: none;
      -webkit-appearance: none;
      opacity: 0;
      cursor: pointer;
    }
    .manual-time-control input::-webkit-calendar-picker-indicator,
    .time-picker-control input::-webkit-calendar-picker-indicator,
    .manual-time-control input::-webkit-clear-button,
    .time-picker-control input::-webkit-clear-button,
    .manual-time-control input::-webkit-inner-spin-button,
    .time-picker-control input::-webkit-inner-spin-button {
      display: none;
      opacity: 0;
      -webkit-appearance: none;
    }
    .manual-time-control.is-disabled,
    .time-picker-control.is-disabled {
      opacity: 0.52;
      cursor: not-allowed;
    }
    .manual-time-control.is-disabled input,
    .time-picker-control.is-disabled input {
      cursor: not-allowed;
    }
    body.desktop-text-entry .manual-time-value,
    body.desktop-text-entry .time-picker-value {
      display: none;
    }
    body.desktop-text-entry .manual-time-control,
    body.desktop-text-entry .time-picker-control {
      cursor: text;
    }
    body.desktop-text-entry .manual-time-control input,
    body.desktop-text-entry .time-picker-control input {
      position: static;
      opacity: 1;
      min-height: 52px;
      color: var(--text);
      background: transparent;
      padding: 0 14px;
      cursor: text;
      appearance: none;
      -webkit-appearance: none;
    }
    label {
      display: block;
      color: rgba(245, 247, 251, 0.9);
      font-size: 14px;
      font-weight: 400;
      letter-spacing: 0;
    }
    .control-wrap { position: relative; }
    .password-control {
      position: relative;
    }
    .password-control input {
      padding-right: 54px;
    }
    .password-toggle {
      position: absolute;
      right: 6px;
      top: 50%;
      display: grid;
      place-items: center;
      width: 40px;
      height: 40px;
      padding: 0;
      border: 0;
      border-radius: 12px;
      color: rgba(245, 247, 251, 0.58);
      background: transparent;
      cursor: pointer;
      transform: translateY(-50%);
    }
    .password-toggle svg {
      width: 20px;
      height: 20px;
    }
    .password-toggle.is-active {
      color: var(--text);
    }
    .password-toggle:disabled {
      opacity: 0.36;
      cursor: not-allowed;
    }
    .unit-input {
      position: relative;
    }
    .unit-input input {
      padding-right: 52px;
    }
    .unit-suffix {
      position: absolute;
      right: 15px;
      top: 50%;
      transform: translateY(-50%);
      color: rgba(245, 247, 251, 0.62);
      pointer-events: none;
    }
    select, input {
      width: 100%;
      min-height: 52px;
      border: 1px solid var(--line-strong);
      border-radius: 15px;
      color: var(--text);
      background: rgba(7, 8, 10, 0.42);
      outline: none;
      padding: 0 14px;
    }
    select {
      appearance: none;
      -webkit-appearance: none;
      padding-right: 44px;
      cursor: pointer;
    }
    .chevron {
      position: absolute;
      right: 15px;
      top: 50%;
      width: 18px;
      height: 18px;
      transform: translateY(-50%);
      color: rgba(245, 247, 251, 0.55);
      pointer-events: none;
    }
    input::placeholder { color: rgba(174, 182, 194, 0.55); }
    select:disabled,
    input:disabled {
      opacity: 0.52;
      cursor: not-allowed;
    }
    .hint {
      margin: 12px 0 0;
      color: var(--muted-2);
      font-size: 13px;
      line-height: 1.45;
    }
    .section.is-disabled .section-header,
    .toggle-row.is-disabled,
    .field.is-disabled {
      opacity: 0.56;
    }
    #weather_section.is-disabled {
      background: rgba(255, 255, 255, 0.035);
      border-color: rgba(255, 255, 255, 0.08);
    }
    #weather_section.is-disabled .section-header,
    #weather_section.is-disabled .toggle-row,
    #weather_section.is-disabled .field,
    #weather_section.is-disabled .field-row {
      opacity: 0.56;
    }
    .integration-dependent[hidden] { display: none; }
    .toggle-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      margin-top: 18px;
    }
    .toggle-copy { min-width: 0; }
    .toggle-title {
      margin: 0;
      color: rgba(245, 247, 251, 0.94);
      font-size: 15px;
      font-weight: 400;
    }
    .toggle-description {
      margin: 5px 0 0;
      color: var(--muted-2);
      font-size: 13px;
      line-height: 1.4;
    }
    .switch {
      position: relative;
      flex: 0 0 auto;
      display: inline-block;
      width: 56px;
      height: 34px;
      padding: 0;
      border: 0;
      background: transparent;
      appearance: none;
      -webkit-appearance: none;
      cursor: pointer;
    }
    .switch input {
      position: absolute;
      inset: 0;
      opacity: 0;
      width: 100%;
      height: 100%;
      cursor: pointer;
    }
    .switch:disabled {
      cursor: not-allowed;
    }
    .slider {
      position: absolute;
      inset: 0;
      border-radius: 999px;
      background: rgba(255, 255, 255, 0.16);
      border: 1px solid rgba(255, 255, 255, 0.12);
      transition: background 180ms ease, border-color 180ms ease;
      pointer-events: none;
    }
    .slider::after {
      content: "";
      position: absolute;
      top: 50%;
      left: 3px;
      width: 28px;
      height: 28px;
      border-radius: 50%;
      background: #fff;
      box-shadow: 0 3px 12px rgba(0, 0, 0, 0.32);
      transform: translateY(-50%);
      transition: transform 180ms ease;
    }
    .switch input:checked + .slider {
      background: #f5f5f5;
      border-color: rgba(255, 255, 255, 0.42);
    }
    .switch[role="switch"][aria-checked="true"] .slider {
      background: #f5f5f5;
      border-color: rgba(255, 255, 255, 0.42);
    }
    .switch input:checked + .slider::after {
      transform: translate(22px, -50%);
      background: #111111;
    }
    .switch[role="switch"][aria-checked="true"] .slider::after {
      transform: translate(22px, -50%);
      background: #111111;
    }
    .switch input:disabled {
      cursor: not-allowed;
    }
    .switch input:disabled + .slider,
    .switch:disabled .slider {
      opacity: 0.48;
    }
    .segmented {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 0;
      padding: 4px;
      margin-top: 8px;
      border-radius: 16px;
      background: rgba(7, 8, 10, 0.42);
      border: 1px solid var(--line-strong);
      overflow: hidden;
    }
    .segmented.three {
      grid-template-columns: 1fr 1fr 1fr;
    }
    .segment {
      position: relative;
      min-height: 44px;
      border-radius: 12px;
      overflow: hidden;
    }
    .segment input {
      position: absolute;
      inset: 0;
      opacity: 0;
      width: 100%;
      height: 100%;
      cursor: pointer;
    }
    .segment span {
      display: grid;
      place-items: center;
      width: 100%;
      height: 100%;
      color: var(--muted-2);
      font-size: 14px;
      letter-spacing: 0;
      border-radius: 12px;
      transition: background 160ms ease, color 160ms ease;
    }
    .segment input:checked + span {
      background: #f5f5f5;
      color: #111111;
    }
    .segment input:disabled {
      cursor: not-allowed;
    }
    .segment input:disabled + span {
      opacity: 0.45;
    }
    .actions {
      display: grid;
      gap: 10px;
      padding-top: 4px;
    }
    .button {
      width: 100%;
      min-height: 54px;
      border: 0;
      border-radius: 17px;
      cursor: pointer;
      color: var(--accent-text);
      background: var(--accent);
      font-weight: 500;
      letter-spacing: 0;
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.8),
        0 14px 36px rgba(0, 0, 0, 0.28);
      transition: transform 150ms ease, filter 150ms ease;
    }
    .button:hover { filter: brightness(0.96); }
    .button:active { transform: translateY(1px) scale(0.995); }
    .button:disabled {
      cursor: not-allowed;
      opacity: 0.58;
      filter: none;
      transform: none;
    }
    .button.secondary {
      color: rgba(245, 247, 251, 0.94);
      background: rgba(255, 255, 255, 0.12);
      border: 1px solid rgba(255, 255, 255, 0.16);
      box-shadow: none;
    }
    .button.progress-button {
      position: relative;
      overflow: hidden;
      isolation: isolate;
    }
    .button.progress-button.is-uploading {
      cursor: wait;
      opacity: 1;
      filter: none;
      transform: none;
    }
    .button.progress-button.is-success {
      color: var(--success-text);
      background: var(--success-bg);
      border-color: var(--success-border);
    }
    .button.progress-button.is-error {
      color: #ffe0da;
      background: rgba(156, 47, 36, 0.26);
      border-color: rgba(255, 180, 168, 0.42);
    }
    .button-progress-fill {
      position: absolute;
      inset: 0 auto 0 0;
      z-index: 0;
      width: 0%;
      height: 100%;
      pointer-events: none;
      opacity: 0;
      background: rgba(245, 247, 251, 0.22);
      transition: width 140ms ease, opacity 140ms ease;
    }
    .button.progress-button.is-uploading .button-progress-fill {
      opacity: 1;
    }
    .button-label {
      position: relative;
      z-index: 1;
    }
    .firmware-section {
      margin-top: 16px;
    }
    .firmware-header {
      display: flex;
      align-items: flex-start;
      justify-content: space-between;
      gap: 12px;
    }
    .firmware-header > div { min-width: 0; }
    .firmware-header .section-note {
      margin-top: 4px;
    }
    .firmware-version-badge {
      flex: 0 0 auto;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      min-height: 28px;
      padding: 0 10px;
      border: 1px solid rgba(255, 255, 255, 0.16);
      border-radius: 999px;
      color: rgba(245, 247, 251, 0.86);
      background: rgba(255, 255, 255, 0.08);
      font-size: 12px;
      line-height: 1;
      white-space: nowrap;
    }
    .firmware-upload {
      display: grid;
      gap: 0;
    }
    .firmware-file-input {
      display: none;
    }
    .footer-note {
      margin: 14px 4px 0;
      text-align: center;
      color: rgba(174, 182, 194, 0.62);
      font-size: 12px;
      line-height: 1.4;
    }
    @media (max-width: 420px) {
      .page {
        align-items: start;
        padding: 14px 10px;
      }
      .shell { width: min(calc(100vw - 20px), 520px); }
      .content { padding: 26px 16px 16px; }
      .hero { padding: 4px 6px 20px; }
      h1 { font-size: 36px; }
      .section {
        padding: 16px;
        border-radius: 20px;
      }
      .lede { font-size: 16px; }
      .field-row,
      .field-row.even {
        grid-template-columns: 1fr;
      }
    }
  </style>
</head>
<body>
  <main class="page">
    <div class="shell">
      <section class="card" aria-labelledby="setup-title">
        <div class="content">
          <header class="hero">
)html";
    html += "            <h1 id=\"setup-title\" data-i18n=\"hero_title\">";
    html += text.hero_title;
    html += "</h1>\n            <p class=\"lede\" data-i18n=\"hero_body\">";
    html += text.hero_body;
    html += R"html(</p>
          </header>
)html";
    if (saved) {
      html += R"html(
          <div class="saved">
            <svg viewBox="0 0 24 24" fill="none" aria-hidden="true">
              <path d="m5 12 4 4L19 6" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"></path>
            </svg>
)html";
      html += "            <span data-i18n=\"saved\">";
      html += text.saved;
      html += R"html(</span>
          </div>
)html";
    }
    html += R"html(
          <form method="post" action="/settings">
            <input id="time_zone_offset_schedule" type="hidden" name="time_zone_offset_schedule" value="">
            <section class="section" aria-labelledby="connection-title">
              <div class="section-header">
                <div>
)html";
    html += "                  <h2 id=\"connection-title\" data-i18n=\"network_title\">";
    html += text.network_title;
    html += "</h2>\n                  <p class=\"section-note\" data-i18n=\"network_note\">";
    html += text.network_note;
    html += R"html(</p>
                </div>
              </div>

              <div class="field is-first">
)html";
    html += "                <label for=\"wifi_ssid_select\" data-i18n=\"wifi_ssid_label\">";
    html += text.wifi_ssid_label;
    html += R"html(</label>
                <div class="control-wrap">
                  <select id="wifi_ssid_select" name="wifi_ssid_select" data-current-ssid=")html";
    html += html_escape_(saved_wifi_ssid);
    html += R"html(">
)html";
    html += network_options_(saved_wifi_ssid, text);
    html += R"html(
                  </select>
                  <svg class="chevron" viewBox="0 0 20 20" fill="none" aria-hidden="true">
                    <path d="m5 7.5 5 5 5-5" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"></path>
                  </svg>
                </div>
                <input id="wifi_ssid" name="wifi_ssid" type="hidden" value=")html";
    html += html_escape_(saved_wifi_ssid);
    html += R"html(">
              </div>

              <div class="field">
)html";
    html += "                <label for=\"wifi_password\" data-i18n=\"wifi_password_label\">";
    html += text.wifi_password_label;
    html += "</label>\n                <div class=\"password-control\">\n                  <input id=\"wifi_password\" name=\"wifi_password\" type=\"password\" data-i18n-placeholder=\"wifi_password_placeholder\" placeholder=\"";
    html += html_escape_(text.wifi_password_placeholder);
    html += R"html(" autocomplete="new-password" autocorrect="off" autocapitalize="none" spellcheck="false" maxlength="64" value=")html";
    html += html_escape_(saved_wifi_password);
    html += R"html(">
                  <button class="password-toggle" type="button" data-password-toggle="wifi_password" data-i18n-aria-label="password_visibility_toggle_label" aria-label=")html";
    html += html_escape_(text.password_visibility_toggle_label);
    html += R"html(" aria-pressed="false">
                    <svg viewBox="0 0 24 24" fill="none" aria-hidden="true">
                      <path d="M2.5 12s3.5-6 9.5-6 9.5 6 9.5 6-3.5 6-9.5 6-9.5-6-9.5-6Z" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"></path>
                      <path d="M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6Z" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"></path>
                    </svg>
                  </button>
                </div>
              </div>

            </section>

)html";
    html += "            <section id=\"integrations_section\" class=\"section";
    if (!wifi_configured)
      html += " is-disabled";
    html += R"html(" aria-labelledby="integrations-title">
              <div class="section-header">
                <div>
)html";
    html += "                  <h2 id=\"integrations-title\" data-i18n=\"integrations_title\">";
    html += text.integrations_title;
    html += "</h2>\n                  <p class=\"section-note\" data-i18n=\"integrations_note\">";
    html += text.integrations_note;
    html += R"html(</p>
                </div>
              </div>

              <div id="ha_row" class="toggle-row">
                <div class="toggle-copy">
)html";
    html += "                  <p class=\"toggle-title\" data-i18n=\"ha_discovery_title\">";
    html += text.ha_discovery_title;
    html += "</p>\n                  <p class=\"toggle-description\" data-i18n=\"ha_discovery_description\">";
    html += text.ha_discovery_description;
    html += R"html(</p>
                </div>

)html";
    html += "                <label class=\"switch\" data-i18n-aria-label=\"ha_discovery_title\" aria-label=\"";
    html += text.ha_discovery_title;
    html += R"html(">
)html";
    html += "                  <input id=\"ha_discovery\" type=\"checkbox\" name=\"ha_discovery\" value=\"1\"";
    if (ha_discovery_enabled)
      html += " checked";
    if (!wifi_configured)
      html += " disabled";
    html += R"html(>
                  <span class="slider" aria-hidden="true"></span>
                </label>
              </div>

              <div id="mqtt_row" class="toggle-row">
                <div class="toggle-copy">
)html";
    html += "                  <p class=\"toggle-title\" data-i18n=\"mqtt_title\">";
    html += text.mqtt_title;
    html += "</p>\n                  <p class=\"toggle-description\" data-i18n=\"mqtt_description\">";
    html += text.mqtt_description;
    html += R"html(</p>
                </div>

)html";
    html += "                <label class=\"switch\" data-i18n-aria-label=\"mqtt_title\" aria-label=\"";
    html += text.mqtt_title;
    html += R"html(">
)html";
    html += "                  <input id=\"mqtt_enabled\" type=\"checkbox\" name=\"mqtt_enabled\" value=\"1\" aria-controls=\"mqtt_fields\"";
    if (mqtt_enabled)
      html += " checked";
    if (!wifi_configured)
      html += " disabled";
    html += R"html(>
                  <span class="slider" aria-hidden="true"></span>
                </label>
              </div>

              <div id="mqtt_fields" class="mqtt-fields")html";
    if (!mqtt_enabled)
      html += " hidden";
    html += R"html(>
                <div class="field-row">
                  <div class="field">
)html";
    html += "                    <label for=\"mqtt_broker\" data-i18n=\"mqtt_broker_label\">";
    html += text.mqtt_broker_label;
    html += "</label>\n                    <input id=\"mqtt_broker\" name=\"mqtt_broker\" type=\"text\" inputmode=\"url\" data-i18n-placeholder=\"mqtt_broker_placeholder\" placeholder=\"";
    html += html_escape_(text.mqtt_broker_placeholder);
    html += R"html(" autocomplete="off" autocorrect="off" autocapitalize="none" spellcheck="false" enterkeyhint="next" maxlength="64" value=")html";
    html += html_escape_(mqtt_broker);
    html += R"html(">
                  </div>
                  <div class="field">
)html";
    html += "                    <label for=\"mqtt_port\" data-i18n=\"mqtt_port_label\">";
    html += text.mqtt_port_label;
    html += R"html(</label>
                    <input id="mqtt_port" name="mqtt_port" type="text" autocomplete="off" autocorrect="off" autocapitalize="none" spellcheck="false" enterkeyhint="next" maxlength="5" pattern="[0-9]{1,5}" value=")html";
    html += mqtt_port_value;
    html += R"html(">
                  </div>
                </div>

                <div class="field-row even">
                  <div class="field">
)html";
    html += "                    <label for=\"mqtt_username\" data-i18n=\"mqtt_username_label\">";
    html += text.mqtt_username_label;
    html += R"html(</label>
                    <input id="mqtt_username" name="mqtt_username" type="text" autocomplete="off" autocorrect="off" autocapitalize="none" spellcheck="false" maxlength="64" data-i18n-placeholder="optional_placeholder" placeholder=")html";
    html += html_escape_(text.optional_placeholder);
    html += R"html(" value=")html";
    html += html_escape_(mqtt_username);
    html += R"html(">
                  </div>
                  <div class="field">
)html";
    html += "                    <label for=\"mqtt_password\" data-i18n=\"mqtt_password_label\">";
    html += text.mqtt_password_label;
    html += R"html(</label>
                    <div class="password-control">
                      <input id="mqtt_password" name="mqtt_password" type="password" autocomplete="new-password" autocorrect="off" autocapitalize="none" spellcheck="false" maxlength="64" data-i18n-placeholder="optional_placeholder" placeholder=")html";
    html += html_escape_(text.optional_placeholder);
    html += R"html(" value=")html";
    html += html_escape_(mqtt_password);
    html += R"html(">
                      <button class="password-toggle" type="button" data-password-toggle="mqtt_password" data-i18n-aria-label="password_visibility_toggle_label" aria-label=")html";
    html += html_escape_(text.password_visibility_toggle_label);
    html += R"html(" aria-pressed="false">
                        <svg viewBox="0 0 24 24" fill="none" aria-hidden="true">
                          <path d="M2.5 12s3.5-6 9.5-6 9.5 6 9.5 6-3.5 6-9.5 6-9.5-6-9.5-6Z" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"></path>
                          <path d="M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6Z" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"></path>
                        </svg>
                      </button>
                    </div>
                  </div>
                </div>

                <div class="field">
)html";
    html += "                  <label for=\"mqtt_topic_prefix\" data-i18n=\"mqtt_topic_prefix_label\">";
    html += text.mqtt_topic_prefix_label;
    html += R"html(</label>
                  <input id="mqtt_topic_prefix" name="mqtt_topic_prefix" type="text" autocomplete="off" autocapitalize="none" spellcheck="false" maxlength="64" value=")html";
    html += html_escape_(mqtt_topic_prefix);
    html += R"html(">
                </div>
              </div>

)html";
    html += "              <input id=\"update_interval_value\" type=\"hidden\" name=\"update_interval\" value=\"";
    html += std::to_string(static_cast<unsigned>(monitoring_update_interval_seconds));
    html += "\">\n              <div id=\"integration_interval\" class=\"field\"";
    if (!(ha_discovery_enabled || mqtt_enabled))
      html += " hidden";
    html += ">\n                <label for=\"integration_interval_10\" data-i18n=\"update_interval_label\">";
    html += text.update_interval_label;
    html += "</label>\n                <div class=\"segmented three\" role=\"radiogroup\" data-i18n-aria-label=\"update_interval_label\" aria-label=\"";
    html += text.update_interval_label;
    html += R"html(">
                <label class="segment" for="integration_interval_5">
)html";
    html += "                  <input id=\"integration_interval_5\" type=\"radio\" name=\"integration_update_interval\" value=\"5\" data-update-interval-value";
    if (monitoring_update_interval_seconds == 5)
      html += " checked";
    html += R"html(>
                  <span>5s</span>
                </label>
                <label class="segment" for="integration_interval_10">
)html";
    html += "                  <input id=\"integration_interval_10\" type=\"radio\" name=\"integration_update_interval\" value=\"10\" data-update-interval-value";
    if (monitoring_update_interval_seconds == 10)
      html += " checked";
    html += R"html(>
                  <span>10s</span>
                </label>
                <label class="segment" for="integration_interval_30">
)html";
    html += "                  <input id=\"integration_interval_30\" type=\"radio\" name=\"integration_update_interval\" value=\"30\" data-update-interval-value";
    if (monitoring_update_interval_seconds == 30)
      html += " checked";
    html += R"html(>
                  <span>30s</span>
                </label>
                </div>
              </div>

            </section>

            <section class="section" aria-labelledby="air-quality-title">
              <div class="section-header">
                <div>
)html";
    html += "                  <h2 id=\"air-quality-title\" data-i18n=\"air_quality_title\">";
    html += text.air_quality_title;
    html += "</h2>\n                  <p class=\"section-note\" data-i18n=\"air_quality_note\">";
    html += text.air_quality_note;
    html += R"html(</p>
                </div>
              </div>

              <div class="field is-first">
)html";
    html += "                <label for=\"air_quality_profile\" data-i18n=\"air_quality_profile_label\">";
    html += text.air_quality_profile_label;
    html += R"html(</label>
                <div class="control-wrap">
                  <select id="air_quality_profile" name="air_quality_profile">
)html";
    html += air_quality_profile_options_(air_quality_profile);
    html += R"html(
                  </select>
                  <svg class="chevron" viewBox="0 0 20 20" fill="none" aria-hidden="true">
                    <path d="m5 7.5 5 5 5-5" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"></path>
                  </svg>
                </div>
              </div>

            </section>

            <section class="section" aria-labelledby="sensor-calibration-title">
              <div class="section-header">
                <div>
)html";
    html += "                  <h2 id=\"sensor-calibration-title\" data-i18n=\"sensor_calibration_title\">";
    html += text.sensor_calibration_title;
    html += "</h2>\n                  <p class=\"section-note\" data-i18n=\"sensor_calibration_note\">";
    html += text.sensor_calibration_note;
    html += R"html(</p>
                </div>
              </div>

              <div class="field is-first">
)html";
    html += "                <label for=\"sen66_temperature_offset_c\" data-i18n=\"sen66_temperature_offset_label\">";
    html += text.sen66_temperature_offset_label;
    html += R"html(</label>
                <div class="unit-input">
                  <input id="sen66_temperature_offset_c" name="sen66_temperature_offset_c" type="text" autocomplete="off" autocorrect="off" autocapitalize="none" spellcheck="false" maxlength="5" pattern="[0-9]{1,2}([.,][0-9]{1,2})?" value=")html";
    html += html_escape_(sen66_temperature_offset_value);
    html += R"html(">
                  <span class="unit-suffix" aria-hidden="true">°C</span>
                </div>
              </div>

              <div class="field">
)html";
    html += "                <label for=\"sen66_co2_reference_ppm\" data-i18n=\"sen66_co2_reference_label\">";
    html += text.sen66_co2_reference_label;
    html += R"html(</label>
                <div class="unit-input">
                  <input id="sen66_co2_reference_ppm" name="sen66_co2_reference_ppm" type="text" autocomplete="off" autocorrect="off" autocapitalize="none" spellcheck="false" maxlength="4" pattern="[0-9]{3,4}" value="427">
                  <span class="unit-suffix" aria-hidden="true">ppm</span>
                </div>
              </div>

              <div class="toggle-row">
                <div class="toggle-copy">
)html";
    html += "                  <p class=\"toggle-title\" data-i18n=\"sen66_co2_calibration_title\">";
    html += text.sen66_co2_calibration_title;
    html += "</p>\n                  <p class=\"toggle-description\" data-i18n=\"sen66_co2_calibration_description\">";
    html += text.sen66_co2_calibration_description;
    html += R"html(</p>
                </div>

)html";
    html += "                <label class=\"switch\" data-i18n-aria-label=\"sen66_co2_calibration_title\" aria-label=\"";
    html += text.sen66_co2_calibration_title;
    html += R"html(">
                  <input id="sen66_force_co2_calibration" type="checkbox" name="sen66_force_co2_calibration" value="1">
                  <span class="slider" aria-hidden="true"></span>
                </label>
              </div>

            </section>

            <section class="section" aria-labelledby="alerts-title">
              <div class="section-header">
                <div>
)html";
    html += "                  <h2 id=\"alerts-title\" data-i18n=\"alerts_title\">";
    html += text.alerts_title;
    html += "</h2>\n                  <p class=\"section-note\" data-i18n=\"alerts_note\">";
    html += text.alerts_note;
    html += R"html(</p>
                </div>
              </div>

              <div class="toggle-row">
                <div class="toggle-copy">
)html";
    html += "                  <p class=\"toggle-title\" data-i18n=\"display_alert_wake_screen_title\">";
    html += text.display_alert_wake_screen_title;
    html += "</p>\n                  <p class=\"toggle-description\" data-i18n=\"display_alert_wake_screen_description\">";
    html += text.display_alert_wake_screen_description;
    html += R"html(</p>
                </div>

)html";
    html += "                <label class=\"switch\" data-i18n-aria-label=\"display_alert_wake_screen_title\" aria-label=\"";
    html += text.display_alert_wake_screen_title;
    html += R"html(">
)html";
    html += "                  <input type=\"checkbox\" name=\"display_alert_wake_screen\" value=\"1\"";
    if (display_alert_wake_screen_enabled)
      html += " checked";
    html += R"html(>
                  <span class="slider" aria-hidden="true"></span>
                </label>
              </div>

              <div class="toggle-row">
                <div class="toggle-copy">
)html";
    html += "                  <p class=\"toggle-title\" data-i18n=\"audio_alerts_title\">";
    html += text.audio_alerts_title;
    html += "</p>\n                  <p class=\"toggle-description\" data-i18n=\"audio_alerts_description\">";
    html += text.audio_alerts_description;
    html += R"html(</p>
                </div>

)html";
    html += "                <label class=\"switch\" data-i18n-aria-label=\"audio_alerts_title\" aria-label=\"";
    html += text.audio_alerts_title;
    html += R"html(">
)html";
    html += "                  <input type=\"checkbox\" name=\"audio_alerts\" value=\"1\"";
    if (audio_alerts_enabled)
      html += " checked";
    html += R"html(>
                  <span class="slider" aria-hidden="true"></span>
                </label>
              </div>

              <div class="toggle-row">
                <div class="toggle-copy">
)html";
    html += "                  <p class=\"toggle-title\" data-i18n=\"hazard_focus_title\">";
    html += text.hazard_focus_title;
    html += "</p>\n                  <p class=\"toggle-description\" data-i18n=\"hazard_focus_description\">";
    html += text.hazard_focus_description;
    html += R"html(</p>
                </div>

)html";
    html += "                <label class=\"switch\" data-i18n-aria-label=\"hazard_focus_title\" aria-label=\"";
    html += text.hazard_focus_title;
    html += R"html(">
)html";
    html += "                  <input type=\"checkbox\" name=\"hazard_focus_mode\" value=\"1\"";
    if (hazard_focus_mode_enabled)
      html += " checked";
    html += R"html(>
                  <span class="slider" aria-hidden="true"></span>
                </label>
              </div>

            </section>

)html";
    html += R"html(            <section id="time_section" class="section" aria-labelledby="time-title">
              <div class="section-header">
                <div>
)html";
    html += "                  <h2 id=\"time-title\" data-i18n=\"time_title\">";
    html += text.time_title;
    html += "</h2>\n                  <p class=\"section-note\" data-i18n=\"time_note\">";
    html += text.time_note;
    html += R"html(</p>
                </div>
              </div>

)html";
    html += R"html(              <div id="time_server_row" class="toggle-row">
                <div class="toggle-copy">
)html";
    html += "                  <p class=\"toggle-title\" data-i18n=\"time_server_title\">";
    html += text.time_server_title;
    html += "</p>\n                  <p class=\"toggle-description\" data-i18n=\"time_server_description\">";
    html += text.time_server_description;
    html += R"html(</p>
                </div>

)html";
    html += "                <label class=\"switch\" data-i18n-aria-label=\"time_server_title\" aria-label=\"";
    html += text.time_server_title;
    html += R"html(">
)html";
    html += "                  <input id=\"time_server_enabled\" type=\"checkbox\" name=\"time_server_enabled\" value=\"1\" aria-controls=\"time_source_mode manual_time_fields night_screen_off_row screen_off_fields\"";
    if (time_sync_enabled)
      html += " checked";
    html += R"html(>
                  <span class="slider" aria-hidden="true"></span>
                 </label>
               </div>

              <input id="manual_time_epoch" name="manual_time_epoch" type="hidden" value="">

              <div id="time_source_mode" class="field time-source-mode")html";
    if (!time_sync_enabled)
      html += " hidden";
    html += R"html(>
)html";
    html += "                <label for=\"time_source_network\" data-i18n=\"time_source_label\">";
    html += text.time_source_label;
    html += "</label>\n                <div class=\"segmented\" role=\"radiogroup\" data-i18n-aria-label=\"time_source_label\" aria-label=\"";
    html += text.time_source_label;
    html += R"html(">
                  <label class="segment" for="time_source_network">
                    <input id="time_source_network" type="radio" name="time_source" value="network")html";
    if (!manual_time_mode)
      html += " checked";
    if (!time_sync_enabled || !wifi_configured)
      html += " disabled";
    html += R"html(>
)html";
    html += "                    <span data-i18n=\"time_source_network_label\">";
    html += text.time_source_network_label;
    html += R"html(</span>
                  </label>
                  <label class="segment" for="time_source_manual">
                    <input id="time_source_manual" type="radio" name="time_source" value="manual")html";
    if (manual_time_mode)
      html += " checked";
    if (!time_sync_enabled)
      html += " disabled";
    html += R"html(>
)html";
    html += "                    <span data-i18n=\"time_source_manual_label\">";
    html += text.time_source_manual_label;
    html += R"html(</span>
                  </label>
                </div>
              </div>

              <div id="manual_time_fields" class="manual-time-fields")html";
    if (!manual_time_mode)
      html += " hidden";
    html += R"html(>
)html";
    html += "                <div class=\"field is-first\">\n                  <label for=\"manual_time_local\" data-i18n=\"manual_time_label\">";
    html += text.manual_time_label;
    html += R"html(</label>
                  <label id="manual_time_control" class="manual-time-control" for="manual_time_local">
                    <span id="manual_time_display" class="manual-time-value"></span>
                    <input id="manual_time_local" type="datetime-local" step="60")html";
    if (!manual_time_mode)
      html += " disabled";
    html += R"html(>
                  </label>
                </div>
              </div>
)html";

    html += "              <div id=\"time_format_field\" class=\"field time-format-field";
    if (!local_time_enabled)
      html += " is-disabled";
    html += "\"";
    if (!local_time_enabled)
      html += " hidden";
    html += ">\n                <label for=\"time_format_24h\" data-i18n=\"time_format_label\">";
    html += text.time_format_label;
    html += "</label>\n                <div class=\"segmented\" role=\"radiogroup\" data-i18n-aria-label=\"time_format_label\" aria-label=\"";
    html += text.time_format_label;
    html += R"html(">
                <label class="segment" for="time_format_24h">
)html";
    html += "                  <input id=\"time_format_24h\" type=\"radio\" name=\"time_format\" value=\"24h\"";
    if (!time_format_12h)
      html += " checked";
    if (!local_time_enabled)
      html += " disabled";
    html += R"html(>
)html";
    html += "                  <span data-i18n=\"time_24h_label\">";
    html += text.time_24h_label;
    html += R"html(</span>
                </label>
                <label class="segment" for="time_format_12h">
)html";
    html += "                  <input id=\"time_format_12h\" type=\"radio\" name=\"time_format\" value=\"12h\"";
    if (time_format_12h)
      html += " checked";
    if (!local_time_enabled)
      html += " disabled";
    html += R"html(>
)html";
    html += "                  <span data-i18n=\"time_12h_label\">";
    html += text.time_12h_label;
    html += R"html(</span>
                </label>
                </div>
              </div>

              <div id="night_screen_off_row" class="toggle-row night-screen-off-row")html";
    if (!local_time_enabled)
      html += " hidden";
    html += R"html(>
                <div class="toggle-copy">
)html";
    html += "                  <p class=\"toggle-title\" data-i18n=\"night_screen_off_title\">";
    html += text.night_screen_off_title;
    html += "</p>\n                  <p class=\"toggle-description\" data-i18n=\"night_screen_off_description\">";
    html += text.night_screen_off_description;
    html += R"html(</p>
                </div>

)html";
    html += "                <label class=\"switch\" data-i18n-aria-label=\"night_screen_off_title\" aria-label=\"";
    html += text.night_screen_off_title;
    html += R"html(">
)html";
    html += "                  <input id=\"night_screen_off\" type=\"checkbox\" name=\"night_screen_off\" value=\"1\" aria-controls=\"screen_off_fields\"";
    if (night_screen_off_enabled)
      html += " checked";
    if (!local_time_enabled)
      html += " disabled";
    html += R"html(>
                  <span class="slider" aria-hidden="true"></span>
                </label>
              </div>

              <input id="screen_off_start" name="screen_off_start" type="hidden" value=")html";
    html += screen_off_start_value;
    html += R"html(">
                <input id="screen_off_end" name="screen_off_end" type="hidden" value=")html";
    html += screen_off_end_value;
    html += R"html(">
              <div id="screen_off_fields" class="time-range")html";
    if (!night_screen_off_enabled || !local_time_enabled)
      html += " hidden";
    html += R"html(>
)html";
    html += R"html(                <label id="screen_off_start_control" class="time-control time-picker-control" for="screen_off_start_display">
                  <span id="screen_off_start_text" class="time-picker-value"></span>
                  <input id="screen_off_start_display" type="time" step="60" value=")html";
    html += screen_off_start_value;
    html += R"html(">
                </label>
                <label id="screen_off_end_control" class="time-control time-picker-control" for="screen_off_end_display">
                  <span id="screen_off_end_text" class="time-picker-value"></span>
                  <input id="screen_off_end_display" type="time" step="60" value=")html";
    html += screen_off_end_value;
    html += R"html(">
                </label>
              </div>
            </section>

            <section id="location_section" class="section)html";
    if (!wifi_configured)
      html += " is-disabled";
    html += R"html(" aria-labelledby="location-title">
              <div class="section-header">
                <div>
)html";
    html += "                  <h2 id=\"location-title\" data-i18n=\"location_title\">";
    html += text.location_title;
    html += "</h2>\n                  <p class=\"section-note\" data-i18n=\"location_note\">";
    html += text.location_note;
    html += R"html(</p>
                </div>
              </div>

              <div id="exact_location_row" class="toggle-row)html";
    if (!wifi_configured)
      html += " is-disabled";
    html += R"html(">
                <div class="toggle-copy">
)html";
    html += "                  <p class=\"toggle-title\" data-i18n=\"exact_location_title\">";
    html += text.exact_location_title;
    html += "</p>\n                  <p class=\"toggle-description\" data-i18n=\"exact_location_description\">";
    html += text.exact_location_description;
    html += R"html(</p>
                </div>

)html";
    html += "                <label class=\"switch\" data-i18n-aria-label=\"exact_location_title\" aria-label=\"";
    html += text.exact_location_title;
    html += R"html(">
)html";
    html += "                  <input id=\"exact_location_enabled\" type=\"checkbox\" name=\"exact_location_enabled\" value=\"1\" aria-controls=\"exact_location_fields\"";
    if (wifi_configured && exact_location_enabled)
      html += " checked";
    if (!wifi_configured)
      html += " disabled";
    html += R"html(>
                  <span class="slider" aria-hidden="true"></span>
                </label>
              </div>

              <div id="exact_location_fields" class="coordinate-range exact-location-fields")html";
    if (!wifi_configured || !exact_location_enabled)
      html += " hidden";
    html += R"html(>
                <div class="field">
)html";
    html += "                  <label for=\"location_latitude\" data-i18n=\"location_latitude_label\">";
    html += text.location_latitude_label;
    html += R"html(</label>
                  <input id="location_latitude" name="location_latitude" type="text" autocomplete="off" autocorrect="off" autocapitalize="none" spellcheck="false" enterkeyhint="next" maxlength="18" pattern="-?[0-9]{1,2}([.,][0-9]{1,7})?" value=")html";
    html += html_escape_(exact_location_latitude_value);
    html += "\"";
    if (wifi_configured && exact_location_enabled)
      html += " required";
    if (!wifi_configured || !exact_location_enabled)
      html += " disabled";
    html += R"html(>
                </div>
                <div class="field">
)html";
    html += "                  <label for=\"location_longitude\" data-i18n=\"location_longitude_label\">";
    html += text.location_longitude_label;
    html += R"html(</label>
                  <input id="location_longitude" name="location_longitude" type="text" autocomplete="off" autocorrect="off" autocapitalize="none" spellcheck="false" enterkeyhint="done" maxlength="18" pattern="-?[0-9]{1,3}([.,][0-9]{1,7})?" value=")html";
    html += html_escape_(exact_location_longitude_value);
    html += "\"";
    if (wifi_configured && exact_location_enabled)
      html += " required";
    if (!wifi_configured || !exact_location_enabled)
      html += " disabled";
    html += R"html(>
                </div>
              </div>
            </section>

            <section id="weather_section" class="section)html";
    if (!wifi_configured)
      html += " is-disabled";
    html += R"html(" aria-labelledby="weather-title">
              <div class="section-header">
                <div>
)html";
    html += "                  <h2 id=\"weather-title\" data-i18n=\"weather_title\">";
    html += text.weather_title;
    html += "</h2>\n                  <p class=\"section-note\" data-i18n=\"weather_note\">";
    html += text.weather_note;
    html += R"html(</p>
                </div>
              </div>

)html";
    html += "              <div id=\"weather_enabled_row\" class=\"toggle-row";
    if (!wifi_configured)
      html += " is-disabled";
    html += R"html(">
                <div class="toggle-copy">
)html";
    html += "                  <p class=\"toggle-title\" data-i18n=\"weather_sync_title\">";
    html += text.weather_sync_title;
    html += "</p>\n                  <p class=\"toggle-description\" data-i18n=\"weather_sync_description\">";
    html += text.weather_sync_description;
    html += R"html(</p>
                </div>

)html";
    html += "                <label class=\"switch\" data-i18n-aria-label=\"weather_sync_title\" aria-label=\"";
    html += text.weather_sync_title;
    html += R"html(">
)html";
    html += "                  <input id=\"weather_enabled\" type=\"checkbox\" name=\"weather_enabled\" value=\"1\" aria-controls=\"weather_location_mode\" data-default-online=\"";
    html += default_online_weather_enabled ? "1" : "0";
    html += "\"";
    if (weather_enabled)
      html += " checked";
    if (!wifi_configured)
      html += " disabled";
    html += R"html(>
                  <span class="slider" aria-hidden="true"></span>
                </label>
              </div>

              <div id="weather_location_mode" class="field weather-location-mode")html";
    if (!weather_enabled)
      html += " hidden";
    html += R"html(>
)html";
    html += "                <label for=\"weather_location_ip\" data-i18n=\"weather_location_mode_label\">";
    html += text.weather_location_mode_label;
    html += "</label>\n                <div class=\"segmented\" role=\"radiogroup\" data-i18n-aria-label=\"weather_location_mode_label\" aria-label=\"";
    html += text.weather_location_mode_label;
    html += R"html(">
                  <label class="segment" for="weather_location_ip">
                    <input id="weather_location_ip" type="radio" name="weather_location_mode" value="ip")html";
    if (!weather_manual_location_selected)
      html += " checked";
    if (!weather_enabled)
      html += " disabled";
    html += R"html(>
)html";
    html += "                    <span data-i18n=\"weather_location_ip_label\">";
    html += text.weather_location_ip_label;
    html += R"html(</span>
                  </label>
                  <label class="segment" for="weather_location_manual">
                    <input id="weather_location_manual" type="radio" name="weather_location_mode" value="manual")html";
    if (weather_manual_location_selected)
      html += " checked";
    if (!weather_enabled || !stored_location_has_value)
      html += " disabled";
    html += R"html(>
)html";
    html += "                    <span data-i18n=\"weather_location_manual_label\">";
    html += text.weather_location_manual_label;
    html += R"html(</span>
                  </label>
                </div>
              </div>
            </section>

            <section id="flight_radar_section" class="section)html";
    if (!wifi_configured)
      html += " is-disabled";
    html += R"html(" aria-labelledby="flight-radar-title">
              <div class="section-header">
                <div>
)html";
    html += "                  <h2 id=\"flight-radar-title\" data-i18n=\"flight_radar_title\">";
    html += text.flight_radar_title;
    html += "</h2>\n                  <p class=\"section-note\" data-i18n=\"flight_radar_note\">";
    html += text.flight_radar_note;
    html += R"html(</p>
                </div>
              </div>

)html";
    html += "              <div id=\"flight_radar_enabled_row\" class=\"toggle-row";
    if (!wifi_configured)
      html += " is-disabled";
    html += R"html(">
                <div class="toggle-copy">
)html";
    html += "                  <p class=\"toggle-title\" data-i18n=\"flight_radar_enabled_title\">";
    html += text.flight_radar_enabled_title;
    html += "</p>\n                  <p class=\"toggle-description\" data-i18n=\"flight_radar_enabled_description\">";
    html += text.flight_radar_enabled_description;
    html += R"html(</p>
                </div>

)html";
    html += "                <label class=\"switch\" data-i18n-aria-label=\"flight_radar_enabled_title\" aria-label=\"";
    html += text.flight_radar_enabled_title;
    html += R"html(">
)html";
    html += "                  <input id=\"flight_radar_enabled\" type=\"checkbox\" name=\"flight_radar_enabled\" value=\"1\" aria-controls=\"flight_radar_fields\"";
    if (flight_radar_active)
      html += " checked";
    if (!wifi_configured)
      html += " disabled";
    html += R"html(>
                  <span class="slider" aria-hidden="true"></span>
                </label>
              </div>

              <div id="flight_radar_fields" class="flight-radar-fields)html";
    if (!flight_radar_active)
      html += " is-disabled";
    html += "\"";
    if (!flight_radar_active)
      html += " hidden";
    html += R"html(">
                <div class="field">
)html";
    html += "                  <label for=\"flight_radar_range_km\" data-i18n=\"flight_radar_range_label\">";
    html += text.flight_radar_range_label;
    html += R"html(</label>
                  <div class="control-wrap">
                    <select id="flight_radar_range_km" name="flight_radar_range_km")html";
    if (!flight_radar_active)
      html += " disabled";
    html += R"html(>
)html";
    html += flight_radar_range_options_(flight_radar_range_km, imperial_units);
    html += R"html(
                    </select>
                    <svg class="chevron" viewBox="0 0 20 20" fill="none" aria-hidden="true">
                      <path d="m5 7.5 5 5 5-5" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"></path>
                    </svg>
                  </div>
                </div>

                <div class="field">
)html";
    html += "                  <label for=\"flight_radar_traffic_all\" data-i18n=\"flight_radar_traffic_label\">";
    html += text.flight_radar_traffic_label;
    html += "</label>\n                  <div class=\"segmented\" role=\"radiogroup\" data-i18n-aria-label=\"flight_radar_traffic_label\" aria-label=\"";
    html += text.flight_radar_traffic_label;
    html += R"html(">
                    <label class="segment" for="flight_radar_traffic_all">
                      <input id="flight_radar_traffic_all" type="radio" name="flight_radar_traffic" value="all")html";
    if (!flight_radar_military_only)
      html += " checked";
    if (!flight_radar_active)
      html += " disabled";
    html += R"html(>
)html";
    html += "                      <span data-i18n=\"flight_radar_traffic_all_label\">";
    html += text.flight_radar_traffic_all_label;
    html += R"html(</span>
                    </label>
                    <label class="segment" for="flight_radar_traffic_military">
                      <input id="flight_radar_traffic_military" type="radio" name="flight_radar_traffic" value="military")html";
    if (flight_radar_military_only)
      html += " checked";
    if (!flight_radar_active)
      html += " disabled";
    html += R"html(>
)html";
    html += "                      <span data-i18n=\"flight_radar_traffic_military_label\">";
    html += text.flight_radar_traffic_military_label;
    html += R"html(</span>
                    </label>
                  </div>
                </div>
              </div>
            </section>

            <section class="section" aria-labelledby="display-title">
              <div class="section-header">
                <div>
)html";
    html += "                  <h2 id=\"display-title\" data-i18n=\"display_title\">";
    html += text.display_title;
    html += "</h2>\n                  <p class=\"section-note\" data-i18n=\"display_note\">";
    html += text.display_note;
    html += R"html(</p>
                </div>
              </div>

              <div class="field is-first">
)html";
    html += "                <label for=\"ui_language\" data-i18n=\"language_label\">";
    html += text.language_label;
    html += R"html(</label>
                <div class="control-wrap">
                  <select id="ui_language" name="ui_language">
)html";
    html += language_options_(ui_language);
    html += R"html(
                  </select>
                  <svg class="chevron" viewBox="0 0 20 20" fill="none" aria-hidden="true">
                    <path d="m5 7.5 5 5 5-5" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"></path>
                  </svg>
                </div>
              </div>

)html";
    html += "              <div class=\"field\">\n                <label for=\"units_metric\" data-i18n=\"units_label\">";
    html += text.units_label;
    html += "</label>\n                <div class=\"segmented\" role=\"radiogroup\" data-i18n-aria-label=\"units_label\" aria-label=\"";
    html += text.units_label;
    html += R"html(">
                <label class="segment" for="units_metric">
)html";
    html += "                  <input id=\"units_metric\" type=\"radio\" name=\"units\" value=\"metric\"";
    if (!imperial_units)
      html += " checked";
    html += R"html(>
)html";
    html += "                  <span data-i18n=\"metric_label\">";
    html += text.metric_label;
    html += R"html(</span>
                </label>
                <label class="segment" for="units_imperial">
)html";
    html += "                  <input id=\"units_imperial\" type=\"radio\" name=\"units\" value=\"imperial\"";
    if (imperial_units)
      html += " checked";
    html += R"html(>
)html";
    html += "                  <span data-i18n=\"imperial_label\">";
    html += text.imperial_label;
    html += R"html(</span>
                </label>
                </div>
              </div>
)html";
    html += "              <div class=\"field\">\n                <label for=\"brightness_medium\" data-i18n=\"brightness_label\">";
    html += text.brightness_label;
    html += "</label>\n                <div class=\"segmented three\" role=\"radiogroup\" data-i18n-aria-label=\"brightness_label\" aria-label=\"";
    html += text.brightness_label;
    html += R"html(">
                <label class="segment" for="brightness_low">
)html";
    html += "                  <input id=\"brightness_low\" type=\"radio\" name=\"brightness\" value=\"low\"";
    if (display_brightness == DISPLAY_BRIGHTNESS_LOW)
      html += " checked";
    html += R"html(>
)html";
    html += "                  <span data-i18n=\"brightness_low\">";
    html += text.brightness_low;
    html += R"html(</span>
                </label>
                <label class="segment" for="brightness_medium">
)html";
    html += "                  <input id=\"brightness_medium\" type=\"radio\" name=\"brightness\" value=\"medium\"";
    if (display_brightness == DISPLAY_BRIGHTNESS_MEDIUM)
      html += " checked";
    html += R"html(>
)html";
    html += "                  <span data-i18n=\"brightness_medium\">";
    html += text.brightness_medium;
    html += R"html(</span>
                </label>
                <label class="segment" for="brightness_high">
)html";
    html += "                  <input id=\"brightness_high\" type=\"radio\" name=\"brightness\" value=\"high\"";
    if (display_brightness == DISPLAY_BRIGHTNESS_HIGH)
      html += " checked";
    html += R"html(>
)html";
    html += "                  <span data-i18n=\"brightness_high\">";
    html += text.brightness_high;
    html += R"html(</span>
                </label>
                </div>
              </div>

              <div class="toggle-row">
                <div class="toggle-copy">
)html";
    html += "                  <p class=\"toggle-title\" data-i18n=\"dark_mode_title\">";
    html += text.dark_mode_title;
    html += "</p>\n                  <p class=\"toggle-description\" data-i18n=\"dark_mode_description\">";
    html += text.dark_mode_description;
    html += R"html(</p>
                </div>

)html";
    html += "                <label class=\"switch\" data-i18n-aria-label=\"dark_mode_title\" aria-label=\"";
    html += text.dark_mode_title;
    html += R"html(">
)html";
    html += "                  <input type=\"checkbox\" name=\"dark_mode\" value=\"1\"";
    if (dark_mode_enabled)
      html += " checked";
    html += R"html(>
                  <span class="slider" aria-hidden="true"></span>
                </label>
              </div>

              <div class="toggle-row">
                <div class="toggle-copy">
)html";
    html += "                  <p class=\"toggle-title\" data-i18n=\"adaptive_brightness_title\">";
    html += text.adaptive_brightness_title;
    html += "</p>\n                  <p class=\"toggle-description\" data-i18n=\"adaptive_brightness_description\">";
    html += text.adaptive_brightness_description;
    html += R"html(</p>
                </div>

)html";
    html += "                <label class=\"switch\" data-i18n-aria-label=\"adaptive_brightness_title\" aria-label=\"";
    html += text.adaptive_brightness_title;
    html += R"html(">
)html";
    html += "                  <input type=\"checkbox\" name=\"auto_dim\" value=\"1\"";
    if (auto_dim_enabled)
      html += " checked";
    html += R"html(>
                  <span class="slider" aria-hidden="true"></span>
                </label>
              </div>

              <div class="toggle-row">
                <div class="toggle-copy">
)html";
    html += "                  <p class=\"toggle-title\" data-i18n=\"auto_page_switch_title\">";
    html += text.auto_page_switch_title;
    html += "</p>\n                  <p class=\"toggle-description\" data-i18n=\"auto_page_switch_description\">";
    html += text.auto_page_switch_description;
    html += R"html(</p>
                </div>

)html";
    html += "                <input id=\"auto_page_switch_value\" type=\"hidden\" name=\"auto_page_switch\" value=\"";
    html += (auto_page_switch_enabled ? "1" : "0");
    html += "\">\n                <button id=\"auto_page_switch\" class=\"switch\" type=\"button\" role=\"switch\" aria-checked=\"";
    html += (auto_page_switch_enabled ? "true" : "false");
    html += "\" data-i18n-aria-label=\"auto_page_switch_title\" aria-label=\"";
    html += text.auto_page_switch_title;
    html += R"html(">
)html";
    html += R"html(                  <span class="slider" aria-hidden="true"></span>
                </button>
              </div>

)html";
    html += "              <div id=\"auto_page_switch_interval\" class=\"field\"";
    if (!auto_page_switch_enabled)
      html += " hidden";
    html += ">\n                <label for=\"auto_page_switch_interval_10\" data-i18n=\"update_interval_label\">";
    html += text.update_interval_label;
    html += "</label>\n                <div class=\"segmented three\" role=\"radiogroup\" data-i18n-aria-label=\"update_interval_label\" aria-label=\"";
    html += text.update_interval_label;
    html += R"html(">
                <label class="segment" for="auto_page_switch_interval_5">
)html";
    html += "                  <input id=\"auto_page_switch_interval_5\" type=\"radio\" name=\"display_update_interval\" value=\"5\" data-update-interval-value";
    if (monitoring_update_interval_seconds == 5)
      html += " checked";
    html += R"html(>
                  <span>5s</span>
                </label>
                <label class="segment" for="auto_page_switch_interval_10">
)html";
    html += "                  <input id=\"auto_page_switch_interval_10\" type=\"radio\" name=\"display_update_interval\" value=\"10\" data-update-interval-value";
    if (monitoring_update_interval_seconds == 10)
      html += " checked";
    html += R"html(>
                  <span>10s</span>
                </label>
                <label class="segment" for="auto_page_switch_interval_30">
)html";
    html += "                  <input id=\"auto_page_switch_interval_30\" type=\"radio\" name=\"display_update_interval\" value=\"30\" data-update-interval-value";
    if (monitoring_update_interval_seconds == 30)
      html += " checked";
    html += R"html(>
                  <span>30s</span>
                </label>
                </div>
              </div>

            </section>

            <div class="actions">
)html";
    html += "              <button class=\"button\" type=\"submit\" data-i18n=\"save_button\">";
    html += text.save_button;
    html += R"html(</button>
            </div>
          </form>

)html";
    html += R"html(
          <section class="section firmware-section" aria-labelledby="firmware-title">
            <div class="section-header firmware-header">
              <div>
)html";
    html += "                <h2 id=\"firmware-title\" data-i18n=\"firmware_title\">";
    html += text.firmware_title;
    html += "</h2>\n";
    html += "              <p class=\"section-note\" data-i18n=\"firmware_note\">";
    html += text.firmware_note;
    html += R"html(</p>
              </div>
)html";
    html += "              <span class=\"firmware-version-badge\" title=\"Current firmware version\">";
    html += html_escape_(AIRDOT_FIRMWARE_VERSION);
    html += R"html(</span>
            </div>

            <div class="firmware-upload">
)html";
    html += "              <input id=\"firmware_file\" class=\"firmware-file-input\" type=\"file\" accept=\".bin,application/octet-stream\" aria-label=\"";
    html += html_escape_(text.firmware_file_label);
    html += R"html(">
)html";
    html += "              <button id=\"firmware_upload_button\" class=\"button secondary progress-button\" type=\"button\"";
    html += " data-i18n-select-label=\"firmware_select_button\" data-select-label=\"";
    html += html_escape_(text.firmware_select_button);
    html += "\" data-i18n-upload-label=\"firmware_upload_button\" data-upload-label=\"";
    html += html_escape_(text.firmware_upload_button);
    html += "\" data-i18n-success-label=\"firmware_success\" data-success-label=\"";
    html += html_escape_(text.firmware_success);
    html += "\" data-i18n-no-file-label=\"firmware_no_file\" data-no-file-label=\"";
    html += html_escape_(text.firmware_no_file);
    html += "\" data-i18n-failed-label=\"firmware_failed\" data-failed-label=\"";
    html += html_escape_(text.firmware_failed);
    html += "\"><span id=\"firmware_upload_progress_fill\" class=\"button-progress-fill\" aria-hidden=\"true\"></span><span id=\"firmware_upload_button_label\" class=\"button-label\">";
    html += text.firmware_select_button;
    html += R"html(</span></button>
)html";
    html += R"html(            </div>
          </section>

)html";
    html += "          <p class=\"footer-note\" data-i18n=\"footer_note\">";
    html += text.footer_note;
    html += R"html(</p>
          <script>
            const languageSelect = document.getElementById("ui_language");
            const wifiSelect = document.getElementById("wifi_ssid_select");
            const wifiHidden = document.getElementById("wifi_ssid");
            const wifiPassword = document.getElementById("wifi_password");
            const passwordToggleButtons = Array.from(document.querySelectorAll("[data-password-toggle]"));
            const timeServerRow = document.getElementById("time_server_row");
            const timeServerInput = document.getElementById("time_server_enabled");
            const timeSourceMode = document.getElementById("time_source_mode");
            const timeSourceInputs = Array.from(document.querySelectorAll("input[name='time_source']"));
            const timeSourceNetworkInput = document.getElementById("time_source_network");
            const timeSourceManualInput = document.getElementById("time_source_manual");
            const manualTimeFields = document.getElementById("manual_time_fields");
            const manualTimeControl = document.getElementById("manual_time_control");
            const manualTimeDisplay = document.getElementById("manual_time_display");
            const manualTimeLocalInput = document.getElementById("manual_time_local");
            const manualTimeEpochInput = document.getElementById("manual_time_epoch");
            const locationSection = document.getElementById("location_section");
            const exactLocationRow = document.getElementById("exact_location_row");
            const exactLocationInput = document.getElementById("exact_location_enabled");
            const exactLocationFields = document.getElementById("exact_location_fields");
            const locationLatitudeInput = document.getElementById("location_latitude");
            const locationLongitudeInput = document.getElementById("location_longitude");
            const weatherSection = document.getElementById("weather_section");
            const weatherEnabledRow = document.getElementById("weather_enabled_row");
            const weatherEnabledInput = document.getElementById("weather_enabled");
            const weatherLocationMode = document.getElementById("weather_location_mode");
            const weatherLocationInputs = Array.from(document.querySelectorAll("input[name='weather_location_mode']"));
            const weatherAutomaticLocationInput = document.getElementById("weather_location_ip");
            const weatherManualLocationInput = document.getElementById("weather_location_manual");
            const flightRadarSection = document.getElementById("flight_radar_section");
            const flightRadarEnabledRow = document.getElementById("flight_radar_enabled_row");
            const flightRadarEnabledInput = document.getElementById("flight_radar_enabled");
            const flightRadarFields = document.getElementById("flight_radar_fields");
            const flightRadarRangeInput = document.getElementById("flight_radar_range_km");
            const flightRadarTrafficInputs = Array.from(document.querySelectorAll("input[name='flight_radar_traffic']"));
            const integrationsSection = document.getElementById("integrations_section");
            const integrationInterval = document.getElementById("integration_interval");
            const autoPageSwitchInterval = document.getElementById("auto_page_switch_interval");
            const updateIntervalValue = document.getElementById("update_interval_value");
            const haDiscoveryInput = document.getElementById("ha_discovery");
            const haRow = document.getElementById("ha_row");
            const mqttRow = document.getElementById("mqtt_row");
            const mqttEnabledInput = document.getElementById("mqtt_enabled");
            const mqttFields = document.getElementById("mqtt_fields");
            const mqttBrokerInput = document.getElementById("mqtt_broker");
            const mqttFieldInputs = mqttFields ? Array.from(mqttFields.querySelectorAll("input")) : [];
            const integrationIntervalInputs = Array.from(document.querySelectorAll("input[name='integration_update_interval']"));
            const autoPageSwitchIntervalInputs = Array.from(document.querySelectorAll("input[name='display_update_interval']"));
            const updateIntervalInputs = Array.from(document.querySelectorAll("[data-update-interval-value]"));
            const autoPageSwitchInput = document.getElementById("auto_page_switch");
            const autoPageSwitchValue = document.getElementById("auto_page_switch_value");
            const nightScreenOffRow = document.getElementById("night_screen_off_row");
            const timeZoneScheduleInput = document.getElementById("time_zone_offset_schedule");
            const nightScreenOffInput = document.getElementById("night_screen_off");
            const screenOffFields = document.getElementById("screen_off_fields");
            const screenOffStartInput = document.getElementById("screen_off_start");
            const screenOffEndInput = document.getElementById("screen_off_end");
            const screenOffStartControl = document.getElementById("screen_off_start_control");
            const screenOffEndControl = document.getElementById("screen_off_end_control");
            const screenOffStartDisplay = document.getElementById("screen_off_start_display");
            const screenOffEndDisplay = document.getElementById("screen_off_end_display");
            const screenOffStartText = document.getElementById("screen_off_start_text");
            const screenOffEndText = document.getElementById("screen_off_end_text");
            const unitInputs = Array.from(document.querySelectorAll("input[name='units']"));
            const timeFormatField = document.getElementById("time_format_field");
            const timeFormatInputs = Array.from(document.querySelectorAll("input[name='time_format']"));
            const timeFormat24Input = document.getElementById("time_format_24h");
            const firmwareFileInput = document.getElementById("firmware_file");
            const firmwareUploadButton = document.getElementById("firmware_upload_button");
            const firmwareUploadButtonLabel = document.getElementById("firmware_upload_button_label");
            const firmwareUploadProgressFill = document.getElementById("firmware_upload_progress_fill");
            const translationCache = {};
            let timeZoneScheduleReady = false;
            const timeZoneScheduleYears = 10;
            const timeZoneScheduleMaxTransitions = 48;
            const savedWifiSsid = wifiSelect ? wifiSelect.dataset.currentSsid || "" : "";
            const savedWifiPassword = wifiPassword ? wifiPassword.value : "";
            let lastWifiSsid = wifiSelect ? wifiSelect.value : "";
            let timeSettingsTouched = false;
            let timeFormatTouched = false;
            let weatherSettingsTouched = false;
            let flightRadarSettingsTouched = false;
            let firmwareUploadInProgress = false;
            function syncWifiFields(networkChanged) {
              if (!wifiSelect || !wifiHidden) return;
              const selectedOption = wifiSelect.options[wifiSelect.selectedIndex];
              const selectedValue = wifiSelect.value;
              const selectedOffline = selectedOption && selectedOption.dataset.i18n === "offline_option";
              wifiHidden.value = selectedValue || (selectedOffline ? "" : savedWifiSsid);
              if (networkChanged && wifiPassword && wifiHidden.value !== lastWifiSsid) {
                if (wifiHidden.value === savedWifiSsid && savedWifiSsid !== "") {
                  wifiPassword.value = savedWifiPassword;
                } else {
                  wifiPassword.value = "";
                }
              }
              lastWifiSsid = wifiHidden.value;
            }
            function wifiOnlineSelected() {
              return wifiHidden ? wifiHidden.value !== "" : false;
            }
            function timeSyncSelected() {
              return timeServerInput ? timeServerInput.checked : false;
            }
            function selectedTimeSource() {
              return document.querySelector("input[name='time_source']:checked");
            }
            function timeServerSelected() {
              const selected = selectedTimeSource();
              return timeSyncSelected() && wifiOnlineSelected() && selected && selected.value === "network";
            }
            function manualTimeSelected() {
              const selected = selectedTimeSource();
              return timeSyncSelected() && selected && selected.value === "manual";
            }
            function localTimeSelected() {
              return timeServerSelected() || manualTimeSelected();
            }
            function weatherSelected() {
              return wifiOnlineSelected() && weatherEnabledInput ? weatherEnabledInput.checked : false;
            }
            function flightRadarSelected() {
              return wifiOnlineSelected() && flightRadarEnabledInput ? flightRadarEnabledInput.checked : false;
            }
            function selectedUnitSystem() {
              const selected = document.querySelector("input[name='units']:checked");
              return selected ? selected.value : "metric";
            }
            function formatFlightRadarRange(km) {
              if (selectedUnitSystem() === "imperial") {
                return Math.round(km * 0.621371192) + " mi";
              }
              return km + " km";
            }
            function updateFlightRadarRangeLabels() {
              if (!flightRadarRangeInput) return;
              Array.from(flightRadarRangeInput.options).forEach((option) => {
                const km = Number(option.dataset.km || option.value);
                if (Number.isFinite(km)) option.textContent = formatFlightRadarRange(km);
              });
            }
            function parseCoordinateInput(input, minimum, maximum) {
              if (!input) return null;
              const normalized = input.value.trim().replace(",", ".");
              if (!/^-?\d+(\.\d+)?$/.test(normalized)) return null;
              const value = Number(normalized);
              return Number.isFinite(value) && value >= minimum && value <= maximum ? value : null;
            }
            function exactLocationFieldsSelected() {
              return Boolean(wifiOnlineSelected() && exactLocationInput && exactLocationInput.checked);
            }
            function exactLocationCoordinatesReady() {
              return parseCoordinateInput(locationLatitudeInput, -90, 90) !== null &&
                parseCoordinateInput(locationLongitudeInput, -180, 180) !== null;
            }
            function exactLocationSelected() {
              return exactLocationFieldsSelected() && exactLocationCoordinatesReady();
            }
            function syncWeatherLocationMode() {
              const weatherActive = weatherSelected();
              const coordinatesReady = exactLocationCoordinatesReady();
              if (weatherAutomaticLocationInput) {
                weatherAutomaticLocationInput.disabled = !weatherActive;
              }
              if (weatherManualLocationInput) {
                weatherManualLocationInput.disabled = !weatherActive || !coordinatesReady;
              }

              if (weatherActive && exactLocationSelected() && weatherManualLocationInput) {
                weatherManualLocationInput.checked = true;
              } else if (weatherAutomaticLocationInput) {
                weatherAutomaticLocationInput.checked = true;
              }
            }
            function applyConnectionDefaults() {
              if (!timeSettingsTouched && timeServerInput) {
                timeServerInput.checked = true;
                if (wifiOnlineSelected()) {
                  if (timeSourceNetworkInput) timeSourceNetworkInput.checked = true;
                } else if (timeSourceManualInput) {
                  timeSourceManualInput.checked = true;
                }
              }
              if (!timeFormatTouched && timeFormat24Input) timeFormat24Input.checked = true;
              if (!wifiOnlineSelected()) {
                if (weatherEnabledInput) weatherEnabledInput.checked = false;
                if (flightRadarEnabledInput) flightRadarEnabledInput.checked = false;
              } else if (!weatherSettingsTouched && weatherEnabledInput &&
                         weatherEnabledInput.dataset.defaultOnline === "1") {
                weatherEnabledInput.checked = true;
                syncWeatherLocationMode();
              }
              if (wifiOnlineSelected() && !flightRadarSettingsTouched && flightRadarEnabledInput) {
                flightRadarEnabledInput.checked = true;
              }
            }
            function setDisabled(elements, disabled) {
              elements.forEach((element) => {
                if (element) element.disabled = disabled;
              });
            }
            function setDisabledClass(element, disabled) {
              if (element) element.classList.toggle("is-disabled", disabled);
            }
            function switchEnabled(control) {
              return control ? control.getAttribute("aria-checked") === "true" : false;
            }
            function setSwitchEnabled(control, hiddenInput, enabled) {
              if (!control) return;
              control.setAttribute("aria-checked", enabled ? "true" : "false");
              if (hiddenInput) hiddenInput.value = enabled ? "1" : "0";
            }
            function toggleSwitch(control, hiddenInput) {
              if (!control || control.disabled) return;
              setSwitchEnabled(control, hiddenInput, !switchEnabled(control));
            }
            function syncUpdateIntervalControls() {
              if (!updateIntervalValue) return;
              updateIntervalInputs.forEach((input) => {
                input.checked = input.value === updateIntervalValue.value;
              });
            }
            function setUpdateInterval(value) {
              if (updateIntervalValue) updateIntervalValue.value = value;
              syncUpdateIntervalControls();
            }
            function passwordToggleTarget(button) {
              return button && button.dataset.passwordToggle ?
                document.getElementById(button.dataset.passwordToggle) : null;
            }
            function updatePasswordToggleButton(button) {
              const input = passwordToggleTarget(button);
              const disabled = !input || input.disabled;
              if (input && disabled) input.type = "password";
              const visible = Boolean(input && input.type === "text");
              button.disabled = disabled;
              button.classList.toggle("is-active", visible);
              button.setAttribute("aria-pressed", visible ? "true" : "false");
            }
            function updatePasswordToggleButtons() {
              passwordToggleButtons.forEach(updatePasswordToggleButton);
            }
            function togglePasswordVisibility(button) {
              const input = passwordToggleTarget(button);
              if (!input || input.disabled) return;
              input.type = input.type === "password" ? "text" : "password";
              updatePasswordToggleButton(button);
              input.focus();
            }
            function updateFlightRadarControls() {
              const online = wifiOnlineSelected();
              const active = flightRadarSelected();
              setDisabledClass(flightRadarSection, !online);
              setDisabledClass(flightRadarEnabledRow, !online);
              if (flightRadarEnabledInput) flightRadarEnabledInput.disabled = !online;
              if (flightRadarFields) {
                flightRadarFields.hidden = !active;
                flightRadarFields.classList.toggle("is-disabled", !active);
              }
              if (flightRadarRangeInput) flightRadarRangeInput.disabled = !active;
              setDisabled(flightRadarTrafficInputs, !active);
              updateFlightRadarRangeLabels();
            }
            const desktopTextQuery = window.matchMedia ?
              window.matchMedia("(hover: hover) and (pointer: fine)") : null;
            function desktopTextEntryEnabled() {
              return desktopTextQuery ? desktopTextQuery.matches : false;
            }
            function openNativePicker(input) {
              if (!input || input.disabled) return;
              input.focus();
              if (desktopTextEntryEnabled()) return;
              if (typeof input.showPicker === "function") {
                try {
                  input.showPicker();
                  return;
                } catch (error) {}
              }
            }
            function updateMqttFields() {
              const online = wifiOnlineSelected();
              if (!online && mqttEnabledInput) mqttEnabledInput.checked = false;
              const enabled = online && mqttEnabledInput ? mqttEnabledInput.checked : false;
              if (mqttFields) mqttFields.hidden = !enabled;
              if (mqttBrokerInput) mqttBrokerInput.required = enabled;
              setDisabled(mqttFieldInputs, !enabled);
            }
            function updateDependentControls() {
              const online = wifiOnlineSelected();
              if (wifiPassword) wifiPassword.disabled = !online;
              if (!online && wifiPassword) wifiPassword.value = "";
              if (timeSyncSelected() && !online && timeSourceNetworkInput && timeSourceNetworkInput.checked &&
                  timeSourceManualInput) {
                timeSourceManualInput.checked = true;
              }
              if (!online && weatherEnabledInput) weatherEnabledInput.checked = false;
              if (!online && flightRadarEnabledInput) flightRadarEnabledInput.checked = false;
              if (!online && exactLocationInput) exactLocationInput.checked = false;
              const timeSyncActive = timeSyncSelected();
              const timeAvailable = localTimeSelected();
              const weatherActive = weatherSelected();

              setDisabledClass(locationSection, !online);
              setDisabledClass(exactLocationRow, !online);
              if (exactLocationInput) exactLocationInput.disabled = !online;
              setDisabledClass(weatherSection, !online);
              setDisabledClass(weatherEnabledRow, !online);
              updateFlightRadarControls();
              setDisabledClass(integrationsSection, !online);
              setDisabledClass(haRow, !online);
              setDisabledClass(mqttRow, !online);
              if (nightScreenOffRow) nightScreenOffRow.hidden = !timeAvailable;
              setDisabledClass(nightScreenOffRow, !timeAvailable);

              setDisabledClass(timeServerRow, false);
              if (timeSourceMode) timeSourceMode.hidden = !timeSyncActive;
              if (timeSourceNetworkInput) timeSourceNetworkInput.disabled = !timeSyncActive || !online;
              if (timeSourceManualInput) timeSourceManualInput.disabled = !timeSyncActive;
              if (manualTimeFields) manualTimeFields.hidden = !manualTimeSelected();
              if (manualTimeControl) manualTimeControl.classList.toggle("is-disabled", !manualTimeSelected());
              if (manualTimeLocalInput) manualTimeLocalInput.disabled = !manualTimeSelected();
              if (timeFormatField) timeFormatField.hidden = !timeAvailable;
              setDisabledClass(timeFormatField, !timeAvailable);
              setDisabled(timeFormatInputs, !timeAvailable);
              if (weatherEnabledInput) weatherEnabledInput.disabled = !online;
              if (weatherLocationMode) weatherLocationMode.hidden = !weatherActive;
              syncWeatherLocationMode();
              const exactFieldsActive = exactLocationFieldsSelected();
              if (exactLocationFields) exactLocationFields.hidden = !exactFieldsActive;
              [locationLatitudeInput, locationLongitudeInput].forEach((input) => {
                if (!input) return;
                input.required = exactFieldsActive;
                input.disabled = !exactFieldsActive;
              });
              if (!online && haDiscoveryInput) haDiscoveryInput.checked = false;
              if (haDiscoveryInput) haDiscoveryInput.disabled = !online;
              if (mqttEnabledInput) mqttEnabledInput.disabled = !online;
              if (!timeAvailable && nightScreenOffInput) nightScreenOffInput.checked = false;
              if (nightScreenOffInput) nightScreenOffInput.disabled = !timeAvailable;

              updateMqttFields();
              const integrationActive = online &&
                ((haDiscoveryInput && haDiscoveryInput.checked) || (mqttEnabledInput && mqttEnabledInput.checked));
              const autoPageSwitchActive = switchEnabled(autoPageSwitchInput);
              if (integrationInterval) integrationInterval.hidden = !integrationActive;
              if (autoPageSwitchInterval) autoPageSwitchInterval.hidden = !autoPageSwitchActive;
              setDisabled(integrationIntervalInputs, !integrationActive);
              setDisabled(autoPageSwitchIntervalInputs, !autoPageSwitchActive);
              syncUpdateIntervalControls();
              updateNightScreenOffControls();
              updatePasswordToggleButtons();
            }
            function pad2(value) {
              return value < 10 ? "0" + value : String(value);
            }
            function selectedTimeFormat12h() {
              const selected = document.querySelector("input[name='time_format']:checked");
              return selected ? selected.value === "12h" : false;
            }
            function minuteFromClockValue(value) {
              const match = /^(\d{2}):(\d{2})$/.exec(value || "");
              if (!match) return null;
              const hour = Number(match[1]);
              const minute = Number(match[2]);
              if (hour > 23 || minute > 59) return null;
              return hour * 60 + minute;
            }
            function clockValueFromText(value) {
              const match = /^\s*(\d{1,2}):(\d{2})\s*([AaPp][Mm])?\s*$/.exec(value || "");
              if (!match) return "";
              let hour = Number(match[1]);
              const minute = Number(match[2]);
              const suffix = match[3] ? match[3].toUpperCase() : "";
              if (minute > 59) return "";
              if (suffix) {
                if (hour < 1 || hour > 12) return "";
                if (suffix === "PM" && hour < 12) hour += 12;
                if (suffix === "AM" && hour === 12) hour = 0;
              } else if (hour > 23) {
                return "";
              }
              return pad2(hour) + ":" + pad2(minute);
            }
            function displayTimeValue(clockValue) {
              const minuteOfDay = minuteFromClockValue(clockValueFromText(clockValue));
              if (minuteOfDay === null) return "";
              const hour = Math.floor(minuteOfDay / 60);
              const minute = minuteOfDay % 60;
              if (!selectedTimeFormat12h()) return pad2(hour) + ":" + pad2(minute);

              const suffix = hour >= 12 ? "PM" : "AM";
              const displayHour = hour % 12 || 12;
              return displayHour + ":" + pad2(minute) + " " + suffix;
            }
            function updateTimeHiddenFromDisplay(hiddenInput, displayInput) {
              if (!hiddenInput || !displayInput) return;
              const parsed = clockValueFromText(displayInput.value);
              if (parsed) hiddenInput.value = parsed;
            }
            function syncTimeDisplay(hiddenInput, displayInput) {
              if (!hiddenInput || !displayInput) return;
              const parsed = clockValueFromText(displayInput.value) || clockValueFromText(hiddenInput.value);
              if (parsed) hiddenInput.value = parsed;
              displayInput.value = desktopTextEntryEnabled() ? displayTimeValue(hiddenInput.value) : hiddenInput.value;
            }
            function syncTimePicker(hiddenInput, displayInput, textElement) {
              syncTimeDisplay(hiddenInput, displayInput);
              if (textElement && hiddenInput) textElement.textContent = displayTimeValue(hiddenInput.value);
            }
            function refreshTimeDisplays() {
              syncTimePicker(screenOffStartInput, screenOffStartDisplay, screenOffStartText);
              syncTimePicker(screenOffEndInput, screenOffEndDisplay, screenOffEndText);
            }
            function localDateTimeValue(date) {
              return date.getFullYear() + "-" + pad2(date.getMonth() + 1) + "-" + pad2(date.getDate()) +
                "T" + pad2(date.getHours()) + ":" + pad2(date.getMinutes());
            }
            function dateOnlyValue(date) {
              return date.getFullYear() + "-" + pad2(date.getMonth() + 1) + "-" + pad2(date.getDate());
            }
            function normalizeManualDateTimeValue(value) {
              const match = /^\s*(\d{4})-(\d{1,2})-(\d{1,2})[ T]+(\d{1,2}):(\d{2})\s*([AaPp][Mm])?\s*$/
                .exec(value || "");
              if (!match) return "";
              const year = Number(match[1]);
              const month = Number(match[2]);
              const day = Number(match[3]);
              const clock = clockValueFromText(match[4] + ":" + match[5] + (match[6] ? " " + match[6] : ""));
              if (!clock || month < 1 || month > 12 || day < 1 || day > 31) return "";
              const candidate = new Date(year, month - 1, day, Number(clock.slice(0, 2)), Number(clock.slice(3, 5)));
              if (Number.isNaN(candidate.getTime()) || candidate.getFullYear() !== year ||
                  candidate.getMonth() !== month - 1 || candidate.getDate() !== day) {
                return "";
              }
              return dateOnlyValue(candidate) + "T" + clock;
            }
            function dateTimeTextValue(value) {
              const normalized = normalizeManualDateTimeValue(value);
              const date = normalized ? new Date(normalized) : new Date();
              return dateOnlyValue(date) + " " + displayTimeValue(pad2(date.getHours()) + ":" + pad2(date.getMinutes()));
            }
            function setInputType(input, type) {
              if (!input) return;
              try {
                input.type = type;
              } catch (error) {}
            }
            function formatManualTimeInput() {
              if (!manualTimeLocalInput) return;
              const normalized = normalizeManualDateTimeValue(manualTimeLocalInput.value);
              if (!normalized) return;
              manualTimeLocalInput.value = desktopTextEntryEnabled() ? dateTimeTextValue(normalized) : normalized;
              refreshManualTimeDisplay();
            }
            function applyInputEntryMode() {
              const desktop = desktopTextEntryEnabled();
              document.body.classList.toggle("desktop-text-entry", desktop);
              if (manualTimeLocalInput) {
                const normalized = normalizeManualDateTimeValue(manualTimeLocalInput.value) || localDateTimeValue(new Date());
                setInputType(manualTimeLocalInput, desktop ? "text" : "datetime-local");
                manualTimeLocalInput.value = desktop ? dateTimeTextValue(normalized) : normalized;
              }
              [
                [screenOffStartInput, screenOffStartDisplay],
                [screenOffEndInput, screenOffEndDisplay],
              ].forEach(([hiddenInput, displayInput]) => {
                if (!hiddenInput || !displayInput) return;
                const value = clockValueFromText(displayInput.value) || clockValueFromText(hiddenInput.value) || "00:00";
                hiddenInput.value = value;
                setInputType(displayInput, desktop ? "text" : "time");
                displayInput.value = desktop ? displayTimeValue(value) : value;
              });
              refreshManualTimeDisplay();
              refreshTimeDisplays();
            }
            function manualTimeDisplayValue(value) {
              const normalized = normalizeManualDateTimeValue(value);
              const date = normalized ? new Date(normalized) : new Date();
              const validDate = Number.isNaN(date.getTime()) ? new Date() : date;
              try {
                return validDate.toLocaleString(document.documentElement.lang || undefined, {
                  year: "numeric",
                  month: "short",
                  day: "numeric",
                  hour: "2-digit",
                  minute: "2-digit",
                  hour12: selectedTimeFormat12h(),
                });
              } catch (error) {
                return validDate.getFullYear() + "-" + pad2(validDate.getMonth() + 1) + "-" +
                  pad2(validDate.getDate()) + " " + pad2(validDate.getHours()) + ":" +
                  pad2(validDate.getMinutes());
              }
            }
            function refreshManualTimeDisplay() {
              if (manualTimeDisplay) manualTimeDisplay.textContent = manualTimeDisplayValue(
                manualTimeLocalInput ? manualTimeLocalInput.value : "");
            }
            function syncManualTimeEpoch() {
              if (!manualTimeLocalInput || !manualTimeEpochInput) return;
              let normalized = normalizeManualDateTimeValue(manualTimeLocalInput.value);
              if (manualTimeSelected() && !normalized && !manualTimeLocalInput.value) {
                normalized = localDateTimeValue(new Date());
                manualTimeLocalInput.value = desktopTextEntryEnabled() ? dateTimeTextValue(normalized) : normalized;
              }
              const parsed = normalized ? new Date(normalized) : null;
              if (parsed && !Number.isNaN(parsed.getTime())) {
                manualTimeEpochInput.value = String(Math.floor(parsed.getTime() / 1000));
              } else {
                manualTimeEpochInput.value = "";
              }
              refreshManualTimeDisplay();
            }
            function updateNightScreenOffControls() {
              const timeAvailable = localTimeSelected();
              if (!timeAvailable && nightScreenOffInput) nightScreenOffInput.checked = false;
              const enabled = timeAvailable && (!nightScreenOffInput || nightScreenOffInput.checked);
              if (screenOffFields) screenOffFields.hidden = !enabled;
              [screenOffStartControl, screenOffEndControl].forEach((control) => {
                if (control) control.classList.toggle("is-disabled", !enabled);
              });
              [screenOffStartDisplay, screenOffEndDisplay].forEach((input) => {
                if (input) input.disabled = !enabled;
              });
            }
            function localUtcOffsetMinutes(date) {
              return -date.getTimezoneOffset();
            }
            function findOffsetTransition(startMs, endMs, startOffset) {
              let low = startMs;
              let high = endMs;
              while (high - low > 60000) {
                const middle = Math.floor((low + high) / 2);
                if (localUtcOffsetMinutes(new Date(middle)) === startOffset) {
                  low = middle;
                } else {
                  high = middle;
                }
              }
              return Math.floor(high / 60000) * 60000;
            }
            function buildTimeZoneOffsetSchedule() {
              const startMs = Date.now();
              const end = new Date(startMs);
              end.setUTCFullYear(end.getUTCFullYear() + timeZoneScheduleYears);

              const transitions = [];
              let windowStartMs = startMs;
              let windowStartOffset = localUtcOffsetMinutes(new Date(windowStartMs));
              const baseOffset = windowStartOffset;
              const scanStepMs = 7 * 24 * 60 * 60 * 1000;

              while (windowStartMs < end.getTime() && transitions.length < timeZoneScheduleMaxTransitions) {
                const windowEndMs = Math.min(windowStartMs + scanStepMs, end.getTime());
                const windowEndOffset = localUtcOffsetMinutes(new Date(windowEndMs));
                if (windowEndOffset !== windowStartOffset) {
                  const transitionMs = findOffsetTransition(windowStartMs, windowEndMs, windowStartOffset);
                  const transitionOffset = localUtcOffsetMinutes(new Date(transitionMs));
                  transitions.push(Math.floor(transitionMs / 1000) + ":" + transitionOffset);
                  windowStartMs = transitionMs + 60000;
                  windowStartOffset = transitionOffset;
                } else {
                  windowStartMs = windowEndMs;
                  windowStartOffset = windowEndOffset;
                }
              }

              return String(baseOffset) + "|" + transitions.join(",");
            }
            function updateTimeZoneOffset(includeSchedule) {
              if (!timeZoneScheduleInput) return;
              if (!timeZoneScheduleReady && includeSchedule && localTimeSelected()) {
                timeZoneScheduleInput.value = buildTimeZoneOffsetSchedule();
                timeZoneScheduleReady = true;
              }
            }
            function prepareTimeZoneOffsetSchedule() {
              if (!timeZoneScheduleInput || timeZoneScheduleReady || !localTimeSelected()) return;
              timeZoneScheduleInput.value = buildTimeZoneOffsetSchedule();
              timeZoneScheduleReady = true;
            }
            async function loadTranslation(language) {
              if (translationCache[language]) return translationCache[language];
              const response = await fetch("/i18n?lang=" + encodeURIComponent(language), { cache: "no-store" });
              if (!response.ok) return null;
              const text = await response.json();
              translationCache[language] = text;
              return text;
            }
            function applyTranslation(language, text) {
              if (!text) return;
              document.documentElement.lang = language;
              document.title = text.html_title || document.title;
              document.querySelectorAll("[data-i18n]").forEach((element) => {
                const value = text[element.dataset.i18n];
                if (value !== undefined) element.textContent = value;
              });
              document.querySelectorAll("[data-i18n-placeholder]").forEach((element) => {
                const value = text[element.dataset.i18nPlaceholder];
                if (value !== undefined) element.setAttribute("placeholder", value);
              });
              document.querySelectorAll("[data-i18n-aria-label]").forEach((element) => {
                const value = text[element.dataset.i18nAriaLabel];
                if (value !== undefined) element.setAttribute("aria-label", value);
              });
              document.querySelectorAll("[data-i18n-select-label]").forEach((element) => {
                const selectLabel = text[element.dataset.i18nSelectLabel];
                const uploadLabel = text[element.dataset.i18nUploadLabel];
                const successLabel = text[element.dataset.i18nSuccessLabel];
                const noFileLabel = text[element.dataset.i18nNoFileLabel];
                const failedLabel = text[element.dataset.i18nFailedLabel];
                if (selectLabel !== undefined) element.dataset.selectLabel = selectLabel;
                if (uploadLabel !== undefined) element.dataset.uploadLabel = uploadLabel;
                if (successLabel !== undefined) element.dataset.successLabel = successLabel;
                if (noFileLabel !== undefined) element.dataset.noFileLabel = noFileLabel;
                if (failedLabel !== undefined) element.dataset.failedLabel = failedLabel;
              });
              updateFirmwareUploadButtonLabel();
            }
            async function translatePage(language) {
              applyTranslation(language, await loadTranslation(language));
              refreshManualTimeDisplay();
            }
            function sendActivityHeartbeat() {
              fetch("/activity", { cache: "no-store" }).catch(() => {});
            }
            function sleep(ms) {
              return new Promise((resolve) => setTimeout(resolve, ms));
            }
            function firmwareButtonMessage(name) {
              return firmwareUploadButton ? firmwareUploadButton.dataset[name] || "" : "";
            }
            function setFirmwareUploadResult(state, message) {
              if (!firmwareUploadButton || !firmwareUploadButtonLabel) return;
              firmwareUploadButton.classList.toggle("is-success", state === "success");
              firmwareUploadButton.classList.toggle("is-error", state === "error");
              firmwareUploadButtonLabel.textContent = message;
            }
            function clearFirmwareUploadResult() {
              if (!firmwareUploadButton) return;
              firmwareUploadButton.classList.remove("is-success", "is-error");
            }
            function setFirmwareUploadBusy(busy) {
              firmwareUploadInProgress = busy;
              if (firmwareUploadButton) {
                firmwareUploadButton.classList.toggle("is-uploading", busy);
                firmwareUploadButton.setAttribute("aria-busy", busy ? "true" : "false");
              }
              if (firmwareFileInput) firmwareFileInput.disabled = busy;
            }
            function firmwareFileSelected() {
              return firmwareFileInput && firmwareFileInput.files && firmwareFileInput.files.length > 0;
            }
            function updateFirmwareUploadButtonLabel() {
              if (!firmwareUploadButton || !firmwareUploadButtonLabel || firmwareUploadInProgress) return;
              clearFirmwareUploadResult();
              firmwareUploadButtonLabel.textContent = firmwareFileSelected()
                ? firmwareUploadButton.dataset.uploadLabel || ""
                : firmwareUploadButton.dataset.selectLabel || "";
            }
            function setFirmwareUploadProgress(percent) {
              const safePercent = Math.max(0, Math.min(100, percent));
              if (firmwareUploadProgressFill) {
                firmwareUploadProgressFill.style.width = safePercent.toFixed(0) + "%";
              }
              if (firmwareUploadInProgress && firmwareUploadButton && firmwareUploadButtonLabel) {
                const label = firmwareUploadButton.dataset.uploadLabel || "";
                firmwareUploadButtonLabel.textContent = label + " " + safePercent.toFixed(0) + "%";
              }
            }
            function resetFirmwareUploadProgress() {
              if (firmwareUploadProgressFill) firmwareUploadProgressFill.style.width = "0%";
            }
            function clearFirmwareFileSelection() {
              if (!firmwareFileInput) return;
              try {
                firmwareFileInput.value = "";
              } catch (error) {}
            }
            function finishFirmwareUploadFailed() {
              setFirmwareUploadBusy(false);
              resetFirmwareUploadProgress();
              clearFirmwareFileSelection();
              setFirmwareUploadResult("error", firmwareButtonMessage("failedLabel"));
              fetch("/firmware-update-cancel", { cache: "no-store" }).catch(() => {});
            }
            async function notifyFirmwareUploadStart() {
              try {
                await fetch("/firmware-update-start", { cache: "no-store" });
              } catch (error) {}
            }
            async function uploadFirmware() {
              const file = firmwareFileInput && firmwareFileInput.files ? firmwareFileInput.files[0] : null;
              if (!file) {
                setFirmwareUploadResult("error", firmwareButtonMessage("noFileLabel"));
                return;
              }

              const payload = new FormData();
              payload.append("firmware", file, file.name || "firmware.bin");
              const request = new XMLHttpRequest();
              request.open("POST", "/update", true);
              clearFirmwareUploadResult();
              setFirmwareUploadBusy(true);
              setFirmwareUploadProgress(0);
              sendActivityHeartbeat();
              await notifyFirmwareUploadStart();
              await sleep(250);

              request.upload.onprogress = (event) => {
                if (event.lengthComputable && event.total > 0) {
                  setFirmwareUploadProgress((event.loaded / event.total) * 100);
                }
              };
              request.onload = () => {
                const response = request.responseText || "";
                const success = request.status >= 200 && request.status < 300 && /successful/i.test(response);
                setFirmwareUploadProgress(100);
                if (success) {
                  setFirmwareUploadBusy(false);
                  setFirmwareUploadResult("success", firmwareButtonMessage("successLabel"));
                } else {
                  finishFirmwareUploadFailed();
                }
              };
              request.onerror = finishFirmwareUploadFailed;
              request.onabort = finishFirmwareUploadFailed;
              request.ontimeout = finishFirmwareUploadFailed;
              try {
                request.send(payload);
              } catch (error) {
                finishFirmwareUploadFailed();
              }
            }
            if (languageSelect) {
              languageSelect.addEventListener("change", () => translatePage(languageSelect.value));
            }
            timeFormatInputs.forEach((input) => {
              input.addEventListener("change", () => {
                timeFormatTouched = true;
                applyInputEntryMode();
              });
            });
            [
              [screenOffStartInput, screenOffStartDisplay],
              [screenOffEndInput, screenOffEndDisplay],
            ].forEach(([hiddenInput, displayInput]) => {
              if (!displayInput) return;
              displayInput.addEventListener("input", () => updateTimeHiddenFromDisplay(hiddenInput, displayInput));
              displayInput.addEventListener("change", refreshTimeDisplays);
              displayInput.addEventListener("blur", refreshTimeDisplays);
            });
            [
              [manualTimeControl, manualTimeLocalInput],
              [screenOffStartControl, screenOffStartDisplay],
              [screenOffEndControl, screenOffEndDisplay],
            ].forEach(([control, input]) => {
              if (!control || !input) return;
              control.addEventListener("click", (event) => {
                if (desktopTextEntryEnabled()) return;
                event.preventDefault();
                openNativePicker(input);
              });
            });
            if (desktopTextQuery) {
              const onDesktopTextModeChanged = () => applyInputEntryMode();
              if (typeof desktopTextQuery.addEventListener === "function") {
                desktopTextQuery.addEventListener("change", onDesktopTextModeChanged);
              } else if (typeof desktopTextQuery.addListener === "function") {
                desktopTextQuery.addListener(onDesktopTextModeChanged);
              }
            }
            if (nightScreenOffInput) {
              nightScreenOffInput.addEventListener("change", updateDependentControls);
            }
            if (timeServerInput) {
              timeServerInput.addEventListener("change", () => {
                timeSettingsTouched = true;
                syncManualTimeEpoch();
                updateDependentControls();
              });
            }
            timeSourceInputs.forEach((input) => {
              input.addEventListener("change", () => {
                timeSettingsTouched = true;
                syncManualTimeEpoch();
                updateDependentControls();
              });
            });
            if (manualTimeLocalInput) {
              manualTimeLocalInput.addEventListener("input", syncManualTimeEpoch);
              manualTimeLocalInput.addEventListener("change", () => {
                syncManualTimeEpoch();
                formatManualTimeInput();
              });
              manualTimeLocalInput.addEventListener("blur", formatManualTimeInput);
              manualTimeLocalInput.addEventListener("focus", syncManualTimeEpoch);
            }
            if (weatherEnabledInput) {
              weatherEnabledInput.addEventListener("change", () => {
                weatherSettingsTouched = true;
                updateDependentControls();
              });
            }
            if (flightRadarEnabledInput) {
              flightRadarEnabledInput.addEventListener("change", () => {
                flightRadarSettingsTouched = true;
                updateDependentControls();
              });
            }
            if (flightRadarRangeInput) {
              flightRadarRangeInput.addEventListener("change", updateFlightRadarRangeLabels);
            }
            unitInputs.forEach((input) => {
              input.addEventListener("change", updateFlightRadarRangeLabels);
            });
            updateIntervalInputs.forEach((input) => {
              input.addEventListener("change", () => {
                if (input.checked) setUpdateInterval(input.value);
              });
            });
            if (exactLocationInput) {
              exactLocationInput.addEventListener("change", updateDependentControls);
            }
            [locationLatitudeInput, locationLongitudeInput].forEach((input) => {
              if (!input) return;
              input.addEventListener("input", () => {
                if (exactLocationCoordinatesReady() && exactLocationInput)
                  exactLocationInput.checked = true;
                updateDependentControls();
              });
              input.addEventListener("change", updateDependentControls);
            });
            weatherLocationInputs.forEach((input) => input.addEventListener("change", () => {
              weatherSettingsTouched = true;
              if (exactLocationInput && input.checked) {
                exactLocationInput.checked = input.value === "manual";
              }
              updateDependentControls();
            }));
            if (haDiscoveryInput) {
              haDiscoveryInput.addEventListener("change", updateDependentControls);
            }
            if (mqttEnabledInput) {
              mqttEnabledInput.addEventListener("change", updateDependentControls);
            }
            if (autoPageSwitchInput) {
              autoPageSwitchInput.addEventListener("click", () => {
                toggleSwitch(autoPageSwitchInput, autoPageSwitchValue);
                updateDependentControls();
              });
            }
            passwordToggleButtons.forEach((button) => {
              button.addEventListener("click", () => togglePasswordVisibility(button));
            });
            if (wifiSelect) {
              wifiSelect.addEventListener("change", () => {
                syncWifiFields(true);
                applyConnectionDefaults();
                updateDependentControls();
              });
            }
            if (firmwareUploadButton) {
              firmwareUploadButton.addEventListener("click", () => {
                if (firmwareUploadInProgress) return;
                if (!firmwareFileSelected()) {
                  if (firmwareFileInput) firmwareFileInput.click();
                  return;
                }
                uploadFirmware();
              });
            }
            if (firmwareFileInput) {
              firmwareFileInput.addEventListener("change", () => {
                resetFirmwareUploadProgress();
                updateFirmwareUploadButtonLabel();
              });
            }
            updateFirmwareUploadButtonLabel();
            syncWifiFields(false);
            applyInputEntryMode();
            updateDependentControls();
            syncManualTimeEpoch();
            refreshTimeDisplays();
            if (wifiSelect && wifiSelect.querySelector("option[data-i18n='no_networks']")) {
              try {
                if (window.sessionStorage && !sessionStorage.getItem("airdot_network_scan_reload")) {
                  sessionStorage.setItem("airdot_network_scan_reload", "1");
                  setTimeout(() => window.location.reload(), 3500);
                }
              } catch (error) {}
            } else {
              try {
                if (window.sessionStorage) sessionStorage.removeItem("airdot_network_scan_reload");
              } catch (error) {}
            }
            if ("requestIdleCallback" in window) {
              requestIdleCallback(prepareTimeZoneOffsetSchedule, { timeout: 2500 });
            } else {
              setTimeout(prepareTimeZoneOffsetSchedule, 800);
            }
            sendActivityHeartbeat();
            setInterval(sendActivityHeartbeat, 30000);
            document.addEventListener("visibilitychange", () => {
              if (!document.hidden) sendActivityHeartbeat();
            });
            const setupForm = document.querySelector("form");
            if (setupForm) {
              setupForm.addEventListener("submit", () => {
                syncWifiFields(false);
                updateDependentControls();
                syncManualTimeEpoch();
                formatManualTimeInput();
                refreshTimeDisplays();
                updateTimeZoneOffset(localTimeSelected());
              });
            }
          </script>
        </div>
      </section>
    </div>
  </main>
</body>
</html>)html";

    html.finish();
  }


 protected:
  static std::string html_escape_(const std::string &text) {
    std::string escaped;
    escaped.reserve(text.size());

    for (const char c : text) {
      switch (c) {
        case '&':
          escaped += "&amp;";
          break;
        case '<':
          escaped += "&lt;";
          break;
        case '>':
          escaped += "&gt;";
          break;
        case '"':
          escaped += "&quot;";
          break;
        case '\'':
          escaped += "&#39;";
          break;
        default:
          escaped += c;
          break;
      }
    }

    return escaped;
  }

  static std::string time_input_value_(uint16_t minute_of_day) {
    const uint16_t normalized = normalize_minute_of_day_(minute_of_day);
    char text[6];
    std::snprintf(
        text, sizeof(text), "%02u:%02u", static_cast<unsigned>(normalized / 60),
        static_cast<unsigned>(normalized % 60));
    return std::string(text);
  }

  static std::string temperature_offset_input_value_(float offset_celsius) {
    char text[8];
    std::snprintf(text, sizeof(text), "%.2f", static_cast<double>(std::fabs(offset_celsius)));
    return std::string(text);
  }

  static std::string flight_radar_range_display_value_(uint8_t range_km, bool imperial_units) {
    char text[16];
    if (imperial_units) {
      const int miles = static_cast<int>(std::round(static_cast<double>(range_km) * 0.621371192));
      std::snprintf(text, sizeof(text), "%d mi", miles);
    } else {
      std::snprintf(text, sizeof(text), "%u km", static_cast<unsigned>(range_km));
    }
    return std::string(text);
  }

  static std::string coordinate_input_value_(int32_t coordinate_e7) {
    const bool negative = coordinate_e7 < 0;
    int64_t absolute = coordinate_e7;
    if (absolute < 0)
      absolute = -absolute;
    const int64_t whole = absolute / 10000000LL;
    const int64_t fraction = absolute % 10000000LL;
    char text[24];
    std::snprintf(text, sizeof(text), "%s%lld.%07lld", negative ? "-" : "", static_cast<long long>(whole),
                  static_cast<long long>(fraction));
    std::string value(text);
    while (value.size() > 1 && value.back() == '0')
      value.pop_back();
    if (!value.empty() && value.back() == '.')
      value.push_back('0');
    return value;
  }

  static void append_air_quality_profile_option_(std::string &options, AirDot::AirQualityProfile option,
                                                 AirDot::AirQualityProfile selected) {
    options += "<option value='";
    options += AirDot::air_quality_profile_value(option);
    options += "'";
    if (option == selected)
      options += " selected";
    options += ">";
    options += AirDot::air_quality_profile_label(option);
    options += "</option>";
  }

  static std::string flight_radar_range_options_(uint8_t selected_range_km, bool imperial_units) {
    std::string options;
    options.reserve(420);
    for (uint8_t range_km = FLIGHT_RADAR_RANGE_MIN_KM; range_km <= FLIGHT_RADAR_RANGE_MAX_KM;
         range_km = static_cast<uint8_t>(range_km + FLIGHT_RADAR_RANGE_STEP_KM)) {
      const std::string value = std::to_string(static_cast<unsigned>(range_km));
      options += "<option value='";
      options += value;
      options += "' data-km='";
      options += value;
      options += "'";
      if (range_km == selected_range_km)
        options += " selected";
      options += ">";
      options += flight_radar_range_display_value_(range_km, imperial_units);
      options += "</option>";
    }
    return options;
  }

  static std::string network_options_(const std::string &selected_ssid, const SetupPageText &text) {
    std::vector<NetworkOption> networks = setup_network_options_snapshot_();
    sort_network_options_(networks);

    std::string options;
    options.reserve(1600 + networks.size() * 96);
    options += "<option value=''";
    if (selected_ssid.empty())
      options += " selected";
    options += " data-i18n='offline_option'>";
    options += html_escape_(text.offline_option);
    options += "</option>";

    bool selected_in_scan = selected_ssid.empty();
    for (const auto &network : networks) {
      if (network.ssid == selected_ssid) {
        selected_in_scan = true;
        break;
      }
    }

    if (!selected_ssid.empty() && !selected_in_scan) {
      const std::string escaped = html_escape_(selected_ssid);
      options += "<option value='";
      options += escaped;
      options += "' selected>";
      options += escaped;
      options += "</option>";
    }

    for (const auto &network : networks) {
      const std::string escaped = html_escape_(network.ssid);
      options += "<option value='";
      options += escaped;
      options += "'";
      if (network.ssid == selected_ssid)
        options += " selected";
      options += ">";
      options += escaped;
      options += "</option>";
    }

    if (networks.empty()) {
      options += "<option value='' disabled data-i18n='no_networks'>";
      options += html_escape_(text.no_networks);
      options += "</option>";
    }

    return options;
  }

  static std::string air_quality_profile_options_(AirDot::AirQualityProfile selected) {
    std::string options;
    options.reserve(520);
    append_air_quality_profile_option_(options, AirDot::AirQualityProfile::GLOBAL_WHO_EEA_STRICT, selected);
    append_air_quality_profile_option_(options, AirDot::AirQualityProfile::EUROPE_EEA, selected);
    append_air_quality_profile_option_(options, AirDot::AirQualityProfile::NORTH_AMERICA_US_EPA_2024, selected);
    append_air_quality_profile_option_(options, AirDot::AirQualityProfile::UK_DAQI_5, selected);
    append_air_quality_profile_option_(options, AirDot::AirQualityProfile::INDIA_NAQI_5, selected);
    append_air_quality_profile_option_(options, AirDot::AirQualityProfile::CHINA_CN_AQI_5, selected);
    append_air_quality_profile_option_(options, AirDot::AirQualityProfile::AUSTRALIA_NSW_1H, selected);
    return options;
  }

  static void append_language_option_(std::string &options, AirDot::UiLanguage option, AirDot::UiLanguage selected) {
    options += "<option value='";
    options += ui_language_value(option);
    options += "'";
    if (option == selected)
      options += " selected";
    options += ">";
    options += ui_language_label(option);
    options += "</option>";
  }

  static std::string language_options_(AirDot::UiLanguage selected) {
    std::string options;
    options.reserve(static_cast<size_t>(ui_language_count()) * 48);
    for (uint8_t index = 0; index < ui_language_count(); index++)
      append_language_option_(options, ui_language_at(index), selected);
    return options;
  }

};

}  // namespace AirDot::onboarding
