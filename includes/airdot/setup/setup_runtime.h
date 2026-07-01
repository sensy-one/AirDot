#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "esphome/components/api/api_server.h"
#include "esphome/components/mdns/mdns_component.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/hal.h"

#include "connectivity.h"
#include "setup_settings.h"
#include "display_runtime.h"

#ifdef USE_ESP32
#include <esp_wifi.h>
#endif

namespace AirDot::onboarding {

inline void note_wifi_connecting() {
  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::WIFI, AirDot::connectivity::ConnectivityStatus::CONNECTING,
      AirDot::connectivity::ConnectivityError::NONE, esphome::millis());
  if (load_home_assistant_discovery_enabled()) {
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::HOME_ASSISTANT,
        AirDot::connectivity::ConnectivityStatus::CONNECTING,
        AirDot::connectivity::ConnectivityError::NONE, esphome::millis());
  }
}

inline void note_wifi_connected() {
  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::WIFI, AirDot::connectivity::ConnectivityStatus::CONNECTED,
      AirDot::connectivity::ConnectivityError::NONE, esphome::millis());
  if (load_home_assistant_discovery_enabled()) {
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::HOME_ASSISTANT,
        AirDot::connectivity::ConnectivityStatus::CONNECTED,
        AirDot::connectivity::ConnectivityError::NONE, esphome::millis());
  }
}

inline void note_wifi_disconnected() {
  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::WIFI, AirDot::connectivity::ConnectivityStatus::OFFLINE,
      AirDot::connectivity::ConnectivityError::OFFLINE, esphome::millis());
  if (load_home_assistant_discovery_enabled()) {
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::HOME_ASSISTANT,
        AirDot::connectivity::ConnectivityStatus::OFFLINE,
        AirDot::connectivity::ConnectivityError::OFFLINE, esphome::millis());
  }
}

inline void note_wifi_config_missing() {
  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::WIFI, AirDot::connectivity::ConnectivityStatus::CONFIG_MISSING,
      AirDot::connectivity::ConnectivityError::CONFIG_MISSING, esphome::millis());
}

inline bool setup_or_onboarding_active(int setup_ap_active, int setup_portal_active, int onboarding_ui_active) {
  return setup_ap_active != 0 || setup_portal_active != 0 || onboarding_ui_active != 0;
}

inline bool online_runtime_available(int setup_ap_active, int setup_portal_active, int onboarding_ui_active) {
  return !setup_or_onboarding_active(setup_ap_active, setup_portal_active, onboarding_ui_active) &&
         esphome::wifi::global_wifi_component != nullptr &&
         esphome::wifi::global_wifi_component->is_connected();
}

inline bool time_server_runtime_allowed(int setup_ap_active, int setup_portal_active, int onboarding_ui_active) {
  return online_runtime_available(setup_ap_active, setup_portal_active, onboarding_ui_active) &&
         load_time_server_enabled() && !load_manual_time_enabled();
}

inline bool weather_runtime_allowed(int setup_ap_active, int setup_portal_active, int onboarding_ui_active) {
  return online_runtime_available(setup_ap_active, setup_portal_active, onboarding_ui_active) && load_weather_enabled();
}

inline void discard_setup_wifi_backup() {
  erase_wifi_settings_(SETUP_WIFI_BACKUP_PREF_KEY);
  clear_setup_wifi_backup_memory_();
}

inline void prepare_runtime_wifi_scan_cache() {
  if (esphome::wifi::global_wifi_component == nullptr)
    return;

  esphome::wifi::global_wifi_component->set_keep_scan_results(true);
}

inline void copy_runtime_wifi_scan_results_to_cache_() {
  std::vector<NetworkOption> cache;

  if (esphome::wifi::global_wifi_component == nullptr) {
    replace_setup_network_options_cache_(cache);
    return;
  }

  for (const auto &scan : esphome::wifi::global_wifi_component->get_scan_result()) {
    if (scan.get_is_hidden())
      continue;
    append_network_option_(cache, scan.get_ssid().str(), scan.get_rssi());
  }
  sort_network_options_(cache);
  replace_setup_network_options_cache_(cache);
}

#ifdef USE_ESP32
inline uint32_t &last_setup_network_scan_request_ms_() {
  static uint32_t last_request_ms = 0;
  return last_request_ms;
}

inline void request_setup_network_scan() {
  uint32_t &last_request_ms = last_setup_network_scan_request_ms_();
  const uint32_t now = esphome::millis();
  if (last_request_ms != 0 && now - last_request_ms < SETUP_NETWORK_SCAN_RETRY_MS)
    return;
  last_request_ms = now == 0 ? 1 : now;

  wifi_mode_t previous_mode = WIFI_MODE_NULL;
  const bool has_previous_mode = esp_wifi_get_mode(&previous_mode) == ESP_OK;
  const wifi_mode_t scan_mode = has_previous_mode && (previous_mode == WIFI_MODE_AP || previous_mode == WIFI_MODE_APSTA)
                                    ? WIFI_MODE_APSTA
                                    : WIFI_MODE_STA;

  if (esp_wifi_set_mode(scan_mode) != ESP_OK)
    return;

  const esp_err_t start_result = esp_wifi_start();
  if (start_result != ESP_OK && start_result != ESP_ERR_WIFI_CONN)
    return;

  wifi_scan_config_t config{};
  config.ssid = nullptr;
  config.bssid = nullptr;
  config.channel = 0;
  config.show_hidden = false;
  config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  config.scan_time.active.min = 80;
  config.scan_time.active.max = 180;

  if (esp_wifi_scan_start(&config, false) != ESP_OK)
    last_request_ms = 0;
}
#else
inline void request_setup_network_scan() {}
#endif

inline void refresh_setup_network_scan_cache() {
  copy_runtime_wifi_scan_results_to_cache_();
  if (!setup_network_options_empty_()) {
    save_setup_network_options_to_preferences_();
    return;
  }

  load_setup_network_options_from_preferences_();
}

inline bool has_setup_network_options() {
  return !setup_network_options_empty_();
}

inline void force_setup_network_ap_mode() {
  if (esphome::global_preferences == nullptr || esphome::wifi::global_wifi_component == nullptr)
    return;

  note_wifi_disconnected();
  discard_setup_wifi_backup();

  auto &backup = setup_wifi_backup_();
  backup.valid = load_wifi_settings_(RUNTIME_WIFI_PREF_KEY, backup.settings);
  if (backup.valid)
    save_wifi_settings_(SETUP_WIFI_BACKUP_PREF_KEY, backup.settings);
  erase_wifi_settings_(RUNTIME_WIFI_PREF_KEY);

  esphome::wifi::global_wifi_component->clear_sta();
}

inline bool restore_setup_wifi_backup() {
  auto &backup = setup_wifi_backup_();
  esphome::wifi::SavedWifiSettings settings{};
  if (backup.valid) {
    settings = backup.settings;
  } else {
    backup.valid = load_wifi_settings_(SETUP_WIFI_BACKUP_PREF_KEY, settings);
  }

  if (backup.valid) {
    note_wifi_connecting();
    save_wifi_settings_(RUNTIME_WIFI_PREF_KEY, settings);
    if (esphome::wifi::global_wifi_component != nullptr) {
      esphome::wifi::WiFiAP sta{};
      sta.set_ssid(settings.ssid);
      sta.set_password(settings.password);
      esphome::wifi::global_wifi_component->set_sta(sta);
    }
  }

  const bool restored = backup.valid;
  discard_setup_wifi_backup();
  return restored;
}

inline void forget_saved_wifi_settings() {
  note_wifi_config_missing();
  erase_wifi_settings_(RUNTIME_WIFI_PREF_KEY);
  discard_setup_wifi_backup();

  if (esphome::wifi::global_wifi_component != nullptr)
    esphome::wifi::global_wifi_component->clear_sta();
}

inline void restore_interrupted_setup_wifi_backup() {
  esphome::wifi::SavedWifiSettings backup_settings{};
  if (load_wifi_settings_(SETUP_WIFI_BACKUP_PREF_KEY, backup_settings)) {
    auto &backup = setup_wifi_backup_();
    backup.valid = true;
    backup.settings = backup_settings;
    restore_setup_wifi_backup();
    return;
  }
}

template<typename Component> inline void apply_monitoring_update_interval_(Component *component, uint32_t update_ms) {
  if (component == nullptr)
    return;

  component->set_update_interval(update_ms);
  if (component->is_ready()) {
    component->stop_poller();
    component->start_poller();
  }
}

template<typename... Components> inline void apply_monitoring_update_interval(Components *...components) {
  const uint32_t update_ms = static_cast<uint32_t>(load_monitoring_update_interval_seconds()) * 1000UL;
  (apply_monitoring_update_interval_(components, update_ms), ...);
}


inline bool &home_assistant_mdns_running_() {
  static bool running = true;
  return running;
}

inline bool &home_assistant_api_running_() {
  static bool running = true;
  return running;
}

inline void prepare_home_assistant_api_before_setup(esphome::api::APIServer *api_component) {
  if (api_component == nullptr)
    return;

  if (load_home_assistant_discovery_enabled()) {
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::HOME_ASSISTANT,
        AirDot::connectivity::ConnectivityStatus::CONNECTING,
        AirDot::connectivity::ConnectivityError::NONE, esphome::millis());
    return;
  }

  api_component->disable_loop();
  home_assistant_api_running_() = false;
  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::HOME_ASSISTANT,
      AirDot::connectivity::ConnectivityStatus::DISABLED,
      AirDot::connectivity::ConnectivityError::NONE, esphome::millis());
}

inline void prepare_home_assistant_mdns_before_setup(esphome::mdns::MDNSComponent *mdns_component) {
  if (mdns_component == nullptr || load_home_assistant_discovery_enabled())
    return;

  mdns_component->disable_loop();
  home_assistant_mdns_running_() = false;
}

inline void apply_home_assistant_api(esphome::api::APIServer *api_component) {
  if (api_component == nullptr)
    return;

  const bool enabled = load_home_assistant_discovery_enabled();
  bool &running = home_assistant_api_running_();

  if (enabled) {
    if (!running) {
      api_component->setup();
      api_component->enable_loop();
      running = true;
    }
    const bool wifi_connected = esphome::wifi::global_wifi_component != nullptr &&
                                esphome::wifi::global_wifi_component->is_connected();
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::HOME_ASSISTANT,
        wifi_connected ? AirDot::connectivity::ConnectivityStatus::CONNECTED
                       : AirDot::connectivity::ConnectivityStatus::OFFLINE,
        wifi_connected ? AirDot::connectivity::ConnectivityError::NONE
                       : AirDot::connectivity::ConnectivityError::OFFLINE,
        esphome::millis());
    return;
  }

  if (running) {
    api_component->on_shutdown();
    api_component->disable_loop();
    running = false;
  }
  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::HOME_ASSISTANT,
      AirDot::connectivity::ConnectivityStatus::DISABLED,
      AirDot::connectivity::ConnectivityError::NONE, esphome::millis());
}

inline void pause_home_assistant_api_for_network_change(esphome::api::APIServer *api_component) {
  if (api_component == nullptr || !home_assistant_api_running_())
    return;

  api_component->on_shutdown();
  home_assistant_api_running_() = false;
  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::HOME_ASSISTANT,
      AirDot::connectivity::ConnectivityStatus::OFFLINE,
      AirDot::connectivity::ConnectivityError::OFFLINE, esphome::millis());
}

inline void finish_home_assistant_api_pause(esphome::api::APIServer *api_component) {
  if (api_component == nullptr || home_assistant_api_running_())
    return;

  api_component->teardown();
  api_component->disable_loop();
}

inline void restore_home_assistant_api(esphome::api::APIServer *api_component) {
  apply_home_assistant_api(api_component);
}

inline void apply_home_assistant_discovery(esphome::mdns::MDNSComponent *mdns_component) {
  if (mdns_component == nullptr)
    return;

  const bool enabled = load_home_assistant_discovery_enabled();
  bool &running = home_assistant_mdns_running_();

  if (enabled) {
    if (!running) {
      mdns_component->setup();
      mdns_component->enable_loop();
      running = true;
    }
    return;
  }

  if (running) {
    mdns_component->on_shutdown();
    mdns_component->disable_loop();
    running = false;
  }
}

inline void start_setup_mdns(esphome::mdns::MDNSComponent *mdns_component) {
  if (mdns_component == nullptr)
    return;

  bool &running = home_assistant_mdns_running_();
  if (!running) {
    mdns_component->setup();
    mdns_component->enable_loop();
    running = true;
  }
}

inline void restore_home_assistant_mdns(esphome::mdns::MDNSComponent *mdns_component) {
  apply_home_assistant_discovery(mdns_component);
}

template<typename Entity> inline void set_home_assistant_entity_exposed_(Entity *entity, bool exposed) {
  if (entity == nullptr)
    return;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  static_cast<esphome::EntityBase *>(entity)->set_internal(!exposed);
#pragma GCC diagnostic pop
}

template<typename... Entities> inline void apply_home_assistant_entity_exposure(Entities *...entities) {
  (set_home_assistant_entity_exposed_(entities, true), ...);
}

template<typename... Entities>
inline void prepare_home_assistant_before_api_setup(esphome::api::APIServer *api_component,
                                                   esphome::mdns::MDNSComponent *mdns_component,
                                                   Entities *...entities) {
  prepare_home_assistant_api_before_setup(api_component);
  prepare_home_assistant_mdns_before_setup(mdns_component);
  apply_home_assistant_entity_exposure(entities...);
}

template<typename... Entities>
inline void apply_home_assistant_settings(esphome::api::APIServer *api_component,
                                          esphome::mdns::MDNSComponent *mdns_component, Entities *...entities) {
  apply_home_assistant_api(api_component);
  apply_home_assistant_discovery(mdns_component);
  apply_home_assistant_entity_exposure(entities...);
}

template<typename... Entities>
inline void apply_home_assistant_runtime_settings(esphome::api::APIServer *api_component,
                                                  esphome::mdns::MDNSComponent *mdns_component,
                                                  Entities *...entities) {
  apply_home_assistant_settings(api_component, mdns_component, entities...);
  apply_monitoring_update_interval(entities...);
}

inline bool &mqtt_settings_have_applied_() {
  static bool applied = false;
  return applied;
}

inline bool &mqtt_settings_applied_enabled_() {
  static bool enabled = false;
  return enabled;
}

inline MqttSettings &mqtt_settings_applied_() {
  static MqttSettings settings = default_mqtt_settings_();
  return settings;
}

inline AirDot::connectivity::ConnectivityError mqtt_disconnect_error_(
    esphome::mqtt::MQTTClientDisconnectReason reason) {
  switch (reason) {
    case esphome::mqtt::MQTTClientDisconnectReason::MQTT_MALFORMED_CREDENTIALS:
    case esphome::mqtt::MQTTClientDisconnectReason::MQTT_NOT_AUTHORIZED:
      return AirDot::connectivity::ConnectivityError::AUTH_FAILED;
    case esphome::mqtt::MQTTClientDisconnectReason::DNS_RESOLVE_ERROR:
      return AirDot::connectivity::ConnectivityError::DNS_FAILED;
    case esphome::mqtt::MQTTClientDisconnectReason::MQTT_SERVER_UNAVAILABLE:
    case esphome::mqtt::MQTTClientDisconnectReason::MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
    case esphome::mqtt::MQTTClientDisconnectReason::MQTT_IDENTIFIER_REJECTED:
      return AirDot::connectivity::ConnectivityError::SERVER_ERROR;
    case esphome::mqtt::MQTTClientDisconnectReason::TCP_DISCONNECTED:
    case esphome::mqtt::MQTTClientDisconnectReason::ESP8266_NOT_ENOUGH_SPACE:
    case esphome::mqtt::MQTTClientDisconnectReason::TLS_BAD_FINGERPRINT:
    default:
      return AirDot::connectivity::ConnectivityError::OFFLINE;
  }
}

template<typename MqttClient> inline void register_mqtt_status_callbacks_(MqttClient *mqtt_client) {
  static bool registered = false;
  if (registered || mqtt_client == nullptr)
    return;

  mqtt_client->set_on_connect([](bool) {
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::MQTT,
        AirDot::connectivity::ConnectivityStatus::CONNECTED,
        AirDot::connectivity::ConnectivityError::NONE, esphome::millis());
  });
  mqtt_client->set_on_disconnect([](esphome::mqtt::MQTTClientDisconnectReason reason) {
    const auto error = mqtt_disconnect_error_(reason);
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::MQTT, AirDot::connectivity::status_for_error(error), error,
        esphome::millis());
  });
  registered = true;
}

template<typename MqttClient> inline void apply_mqtt_settings(MqttClient *mqtt_client) {
  if (mqtt_client == nullptr)
    return;

  register_mqtt_status_callbacks_(mqtt_client);

  const auto &settings = load_mqtt_settings();
  const std::string broker = fixed_string_(settings.broker);
  const std::string username = fixed_string_(settings.username);
  const std::string password = fixed_string_(settings.password);
  const std::string topic_prefix = fixed_string_(settings.topic_prefix);
  const bool enabled = settings.enabled == 1;
  const auto validation =
      AirDot::connectivity::validate_mqtt_config(enabled, broker, settings.port, topic_prefix);
  bool &has_applied_settings = mqtt_settings_have_applied_();
  bool &applied_enabled = mqtt_settings_applied_enabled_();
  MqttSettings &applied_settings = mqtt_settings_applied_();

  if (!validation.ok) {
    if (!has_applied_settings || applied_enabled)
      mqtt_client->disable();
    applied_settings = settings;
    has_applied_settings = true;
    applied_enabled = false;
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::MQTT, validation.status, validation.error, esphome::millis());
    return;
  }

  if (!enabled) {
    if (!has_applied_settings || applied_enabled)
      mqtt_client->disable();
    applied_settings = settings;
    has_applied_settings = true;
    applied_enabled = false;
    AirDot::connectivity::set_service_status(
        AirDot::connectivity::Service::MQTT,
        AirDot::connectivity::ConnectivityStatus::DISABLED,
        AirDot::connectivity::ConnectivityError::NONE, esphome::millis());
    return;
  }

  if (has_applied_settings && applied_enabled && mqtt_settings_equal_(applied_settings, settings))
    return;

  mqtt_client->disable();
  mqtt_client->set_broker_address(broker);
  mqtt_client->set_broker_port(settings.port);
  mqtt_client->set_username(username);
  mqtt_client->set_password(password);
  mqtt_client->set_clean_session(true);
  mqtt_client->set_topic_prefix(topic_prefix, AIRDOT_DEFAULT_MQTT_TOPIC_PREFIX);
#ifdef USE_MQTT
  const std::string availability_topic = topic_prefix + "/availability";
  mqtt_client->set_birth_message(esphome::mqtt::MQTTMessage{availability_topic, "online", 1, true});
  mqtt_client->set_last_will(esphome::mqtt::MQTTMessage{availability_topic, "offline", 1, true});
  mqtt_client->set_shutdown_message(esphome::mqtt::MQTTMessage{availability_topic, "offline", 1, true});
#endif
  applied_settings = settings;
  has_applied_settings = true;
  applied_enabled = true;
  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::MQTT,
      AirDot::connectivity::ConnectivityStatus::CONNECTING,
      AirDot::connectivity::ConnectivityError::NONE, esphome::millis());
  mqtt_client->enable();
}

template<typename MqttClient> inline void pause_mqtt_for_network_change(MqttClient *mqtt_client) {
  if (mqtt_client == nullptr)
    return;

  mqtt_client->disable();
  mqtt_settings_have_applied_() = false;
  mqtt_settings_applied_enabled_() = false;
  mqtt_settings_applied_() = default_mqtt_settings_();
  AirDot::connectivity::set_service_status(
      AirDot::connectivity::Service::MQTT,
      AirDot::connectivity::ConnectivityStatus::OFFLINE,
      AirDot::connectivity::ConnectivityError::OFFLINE, esphome::millis());
}

inline float normalized_backlight_brightness_(float brightness) {
  if (!std::isfinite(brightness))
    return 0.0f;
  return std::min<float>(1.0f, std::max<float>(0.0f, brightness));
}

template<typename DisplayBacklight>
inline void turn_display_backlight_off(DisplayBacklight *display_backlight) {
  if (display_backlight == nullptr)
    return;

  auto call = display_backlight->turn_off();
  call.set_transition_length(0);
  call.perform();
}

template<typename DisplayBacklight>
inline void set_display_backlight_brightness_(DisplayBacklight *display_backlight, float brightness) {
  if (display_backlight == nullptr)
    return;

  auto call = display_backlight->turn_on();
  call.set_transition_length(0);
  call.set_brightness(normalized_backlight_brightness_(brightness));
  call.perform();
}

template<typename DisplayBacklight>
inline uint8_t current_display_brightness_percent(DisplayBacklight *display_backlight) {
  if (display_backlight == nullptr)
    return load_display_brightness_percent();

  float brightness = 0.0f;
  display_backlight->current_values_as_brightness(&brightness);
  const float normalized = normalized_backlight_brightness_(brightness);
  return display_brightness_percent_for_value_(normalized);
}

template<typename DisplayBacklight, typename Number>
inline void publish_current_display_brightness_percent(DisplayBacklight *display_backlight, Number *number) {
  if (number == nullptr)
    return;

  const uint8_t percent = current_display_brightness_percent(display_backlight);
  if (number->has_state()) {
    const uint8_t published = normalize_display_brightness_percent_(static_cast<int>(std::round(number->state)));
    if (published == percent)
      return;
  }
  number->publish_state(percent);
}

template<typename DisplayBacklight>
inline void apply_display_brightness(DisplayBacklight *display_backlight, bool screen_off) {
  if (display_backlight == nullptr)
    return;

  if (screen_off) {
    turn_display_backlight_off(display_backlight);
    return;
  }

  set_display_backlight_brightness_(display_backlight, display_brightness_value());
}

template<typename DisplayBacklight> inline void apply_night_screen_brightness(DisplayBacklight *display_backlight) {
  if (display_backlight == nullptr)
    return;

  if (!load_night_screen_dim_enabled()) {
    turn_display_backlight_off(display_backlight);
    return;
  }

  set_display_backlight_brightness_(display_backlight, display_brightness_value_for_percent_(0));
}

inline uint8_t effective_display_brightness_percent(float ambient_lux, float min_lux, float max_lux,
                                                    float min_brightness) {
  const uint8_t max_percent = load_display_brightness_percent();
  if (!load_auto_dim_enabled())
    return max_percent;

  const uint8_t min_percent = std::min<uint8_t>(display_brightness_percent_for_value_(min_brightness), max_percent);
  if (!std::isfinite(ambient_lux))
    return min_percent;

  const float corrected_min_lux = std::max(0.1f, min_lux);
  const float corrected_max_lux = std::max(corrected_min_lux + 1.0f, max_lux);
  const float corrected_lux = std::clamp(ambient_lux, corrected_min_lux, corrected_max_lux);
  const float ratio = (corrected_lux - corrected_min_lux) / (corrected_max_lux - corrected_min_lux);
  const float percent = static_cast<float>(min_percent) + ratio * static_cast<float>(max_percent - min_percent);
  return normalize_display_brightness_percent_(static_cast<int>(std::round(percent)));
}

inline float effective_display_brightness(float ambient_lux, float min_lux, float max_lux, float min_brightness) {
  return display_brightness_value_for_percent_(
      effective_display_brightness_percent(ambient_lux, min_lux, max_lux, min_brightness));
}

template<typename DisplayBacklight>
inline void apply_display_brightness(DisplayBacklight *display_backlight, float ambient_lux, float min_lux,
                                     float max_lux, float min_brightness, bool screen_off = false) {
  if (display_backlight == nullptr)
    return;

  if (screen_off) {
    turn_display_backlight_off(display_backlight);
    return;
  }

  set_display_backlight_brightness_(
      display_backlight, effective_display_brightness(ambient_lux, min_lux, max_lux, min_brightness));
}

template<typename DisplayBacklight> inline void apply_display_brightness(DisplayBacklight *display_backlight) {
  apply_display_brightness(display_backlight, false);
}

template<typename DisplayPanel, typename DisplayBacklight>
inline void wake_display_with_brightness(DisplayPanel *display_panel, DisplayBacklight *display_backlight,
                                         float ambient_lux, float min_lux, float max_lux, float min_brightness) {
  apply_display_brightness(display_backlight, ambient_lux, min_lux, max_lux, min_brightness);
  if (display_panel != nullptr)
    display_panel->set_screen_powered(true);
}

template<typename DisplayPanel, typename DisplayBacklight>
inline void apply_night_screen_state(DisplayPanel *display_panel, DisplayBacklight *display_backlight) {
  apply_night_screen_brightness(display_backlight);
  if (display_panel != nullptr)
    display_panel->set_screen_powered(load_night_screen_dim_enabled());
}

template<typename DisplayBacklight, typename... Entities>
inline void apply_runtime_settings(esphome::api::APIServer *api_component,
                                   esphome::mdns::MDNSComponent *mdns_component,
                                   DisplayBacklight *display_backlight, float ambient_lux, float min_lux,
                                   float max_lux, float min_brightness, Entities *...entities) {
  apply_home_assistant_runtime_settings(api_component, mdns_component, entities...);
  apply_display_brightness(display_backlight, ambient_lux, min_lux, max_lux, min_brightness);
}

}  // namespace AirDot::onboarding
