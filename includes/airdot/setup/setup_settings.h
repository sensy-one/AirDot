#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/application.h"
#include "esphome/core/preferences.h"

#include "classification.h"
#include "connectivity.h"
#include "i18n_texts.h"

#ifdef USE_ESP32
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <nvs.h>
#endif

#ifndef AIRDOT_DEFAULT_MQTT_TOPIC_PREFIX
#define AIRDOT_DEFAULT_MQTT_TOPIC_PREFIX "airdot"
#endif

#ifndef AIRDOT_DEFAULT_SEN66_TEMPERATURE_OFFSET_C
#define AIRDOT_DEFAULT_SEN66_TEMPERATURE_OFFSET_C -4.3f
#endif

namespace AirDot::onboarding {

static constexpr uint32_t RUNTIME_WIFI_PREF_KEY = 88491487UL;
static constexpr uint32_t SETUP_WIFI_BACKUP_PREF_KEY = 2918394821UL;
static constexpr uint32_t HOME_ASSISTANT_DISCOVERY_PREF_KEY = 2918394822UL;
static constexpr uint32_t UNIT_SYSTEM_PREF_KEY = 2918394823UL;
static constexpr uint32_t DISPLAY_BRIGHTNESS_PREF_KEY = 2918394824UL;
static constexpr uint32_t AUTO_DIM_PREF_KEY = 2918394825UL;
static constexpr uint32_t MONITORING_UPDATE_INTERVAL_PREF_KEY = 2918394826UL;
static constexpr uint32_t AUDIO_ALERTS_PREF_KEY = 2918394827UL;
static constexpr uint32_t AIR_QUALITY_PROFILE_PREF_KEY = 2918394828UL;
static constexpr uint32_t HAZARD_FOCUS_MODE_PREF_KEY = 2918394829UL;
static constexpr uint32_t UI_LANGUAGE_PREF_KEY = 2918394830UL;
static constexpr uint32_t TIME_FORMAT_PREF_KEY = 2918394831UL;
static constexpr uint32_t TIME_ZONE_OFFSET_SCHEDULE_PREF_KEY = 2918394833UL;
static constexpr uint32_t NIGHT_SCREEN_OFF_PREF_KEY = 2918394834UL;
static constexpr uint32_t SCREEN_OFF_START_MINUTES_PREF_KEY = 2918394835UL;
static constexpr uint32_t SCREEN_OFF_END_MINUTES_PREF_KEY = 2918394836UL;
static constexpr uint32_t MQTT_SETTINGS_PREF_KEY = 2918394837UL;
static constexpr uint32_t TIME_SERVER_PREF_KEY = 2918394838UL;
static constexpr uint32_t SETUP_NETWORK_OPTIONS_PREF_KEY = 2918394839UL;
static constexpr uint32_t DARK_MODE_PREF_KEY = 2918394840UL;
static constexpr uint32_t FIRST_RUN_ONBOARDING_COMPLETE_PREF_KEY = 2918394841UL;
static constexpr uint32_t LOCATION_PREF_KEY = 2918394847UL;
static constexpr uint32_t MANUAL_TIME_PREF_KEY = 2918394843UL;
static constexpr uint32_t WEATHER_SYNC_PREF_KEY = 2918394844UL;
static constexpr uint32_t SEN66_TEMPERATURE_OFFSET_CENTI_C_PREF_KEY = 2918394845UL;
static constexpr uint32_t DISPLAY_ALERT_WAKE_SCREEN_PREF_KEY = 2918394846UL;
static constexpr int32_t LATITUDE_MIN_E7 = -900000000;
static constexpr int32_t LATITUDE_MAX_E7 = 900000000;
static constexpr int32_t LONGITUDE_MIN_E7 = -1800000000;
static constexpr int32_t LONGITUDE_MAX_E7 = 1800000000;
static constexpr uint32_t FLIGHT_RADAR_SETTINGS_PREF_KEY = 2918394848UL;
static constexpr uint8_t FLIGHT_RADAR_RANGE_MIN_KM = 5;
static constexpr uint8_t FLIGHT_RADAR_RANGE_DEFAULT_KM = 25;
static constexpr uint8_t FLIGHT_RADAR_RANGE_MAX_KM = 50;
static constexpr uint8_t FLIGHT_RADAR_RANGE_STEP_KM = 5;
static constexpr int16_t TIME_ZONE_OFFSET_MINUTES_UNSET = 32767;
static constexpr uint8_t TIME_ZONE_OFFSET_MAX_TRANSITIONS = 48;
static constexpr int16_t SEN66_TEMPERATURE_OFFSET_MIN_CENTI_C = -2000;
static constexpr int16_t SEN66_TEMPERATURE_OFFSET_MAX_CENTI_C = 0;
static constexpr uint16_t SCREEN_OFF_DEFAULT_START_MINUTES = 22 * 60;
static constexpr uint16_t SCREEN_OFF_DEFAULT_END_MINUTES = 7 * 60;
static constexpr uint16_t MINUTES_PER_DAY = 24 * 60;
static constexpr uint16_t MQTT_DEFAULT_PORT = 1883;
static constexpr uint32_t SETUP_NETWORK_SCAN_RETRY_MS = 5000;
static constexpr size_t MAX_TIME_ZONE_OFFSET_SCHEDULE_LENGTH = 900;
static constexpr size_t MAX_UI_LANGUAGE_VALUE_LENGTH = 16;
static constexpr size_t MAX_WIFI_SSID_LENGTH = 32;
static constexpr size_t MAX_WIFI_PASSWORD_LENGTH = 64;
static constexpr size_t MAX_MQTT_BROKER_LENGTH = 64;
static constexpr size_t MAX_MQTT_USERNAME_LENGTH = 64;
static constexpr size_t MAX_MQTT_PASSWORD_LENGTH = 64;
static constexpr size_t MAX_MQTT_TOPIC_PREFIX_LENGTH = 64;
static constexpr size_t MAX_SETUP_NETWORK_OPTIONS = 32;

struct NetworkOption {
  std::string ssid;
  int8_t rssi;
};

enum UnitSystem : uint8_t {
  UNIT_SYSTEM_METRIC = 0,
  UNIT_SYSTEM_IMPERIAL = 1,
};

enum DisplayBrightness : uint8_t {
  DISPLAY_BRIGHTNESS_LOW = 0,
  DISPLAY_BRIGHTNESS_MEDIUM = 1,
  DISPLAY_BRIGHTNESS_HIGH = 2,
};

enum TimeFormat : uint8_t {
  TIME_FORMAT_24H = 0,
  TIME_FORMAT_12H = 1,
};

struct SetupWifiBackup {
  bool valid;
  esphome::wifi::SavedWifiSettings settings;
};

inline SetupWifiBackup &setup_wifi_backup_() {
  static SetupWifiBackup backup{};
  return backup;
}

inline std::vector<NetworkOption> &setup_network_options_cache_() {
  static std::vector<NetworkOption> networks;
  return networks;
}

#ifdef USE_ESP32
inline SemaphoreHandle_t setup_network_options_mutex_() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
  return mutex;
}

class SetupNetworkOptionsLock {
 public:
  SetupNetworkOptionsLock() {
    const auto mutex = setup_network_options_mutex_();
    if (mutex != nullptr)
      this->locked_ = xSemaphoreTake(mutex, pdMS_TO_TICKS(250)) == pdTRUE;
  }

  ~SetupNetworkOptionsLock() {
    if (this->locked_)
      xSemaphoreGive(setup_network_options_mutex_());
  }

 private:
  bool locked_{false};
};
#else
class SetupNetworkOptionsLock {
 public:
  SetupNetworkOptionsLock() = default;
};
#endif

inline void append_network_option_(std::vector<NetworkOption> &networks, const std::string &ssid, int8_t rssi) {
  if (ssid.empty())
    return;

  auto existing = std::find_if(networks.begin(), networks.end(), [&ssid](const NetworkOption &network) {
    return network.ssid == ssid;
  });
  if (existing != networks.end()) {
    if (rssi > existing->rssi)
      existing->rssi = rssi;
    return;
  }

  if (networks.size() < MAX_SETUP_NETWORK_OPTIONS) {
    networks.push_back({ssid, rssi});
    return;
  }

  auto weakest = std::min_element(networks.begin(), networks.end(), [](const NetworkOption &a,
                                                                        const NetworkOption &b) {
    return a.rssi < b.rssi;
  });
  if (weakest != networks.end() && rssi > weakest->rssi)
    *weakest = {ssid, rssi};
}

inline void sort_network_options_(std::vector<NetworkOption> &networks) {
  std::sort(networks.begin(), networks.end(), [](const NetworkOption &a, const NetworkOption &b) {
    return a.rssi > b.rssi;
  });
}

inline std::vector<NetworkOption> setup_network_options_snapshot_() {
  SetupNetworkOptionsLock lock;
  return setup_network_options_cache_();
}

inline bool setup_network_options_empty_() {
  SetupNetworkOptionsLock lock;
  return setup_network_options_cache_().empty();
}

inline void replace_setup_network_options_cache_(const std::vector<NetworkOption> &networks) {
  SetupNetworkOptionsLock lock;
  setup_network_options_cache_() = networks;
}

template<size_t N> inline std::string fixed_string_(const char (&text)[N]) {
  size_t length = 0;
  while (length < N && text[length] != '\0')
    length++;
  return std::string(text, length);
}

inline std::string setup_ap_ssid() {
  const auto &name = esphome::App.get_name();
  return name.empty() ? std::string("airdot") : std::string(name.c_str(), name.size());
}

inline std::string setup_connect_instruction_text(AirDot::UiLanguage language, bool wifi_failed) {
  (void) wifi_failed;
  const std::string ssid = setup_ap_ssid();

  switch (language) {
    case AirDot::UiLanguage::NL:
      return std::string("Open wifi op je telefoon\nen kies ") + ssid +
             ".\n\nOpen daarna je browser\nen ga naar 192.168.4.1";
    case AirDot::UiLanguage::DE:
      return std::string("Öffne WLAN auf deinem Smartphone\nund wähle ") + ssid +
             ".\n\nÖffne danach deinen Browser\nund gehe zu 192.168.4.1";
    case AirDot::UiLanguage::FR:
      return std::string("Ouvrez le Wi-Fi sur votre téléphone\net choisissez ") + ssid +
             ".\n\nOuvrez ensuite votre navigateur\net allez sur 192.168.4.1";
    case AirDot::UiLanguage::EN:
    default:
      return std::string("Open Wi-Fi on your phone\nand choose ") + ssid +
             ".\n\nThen open your browser\nand go to 192.168.4.1";
  }
}

inline void clear_setup_wifi_backup_memory_() {
  auto &backup = setup_wifi_backup_();
  backup.valid = false;
  backup.settings = {};
}

struct CachedUint8Preference {
  uint32_t key;
  uint8_t default_value;
  uint8_t value;
  bool loaded;
  bool preference_ready;
  esphome::ESPPreferenceObject preference;
};

struct CachedUint16Preference {
  uint32_t key;
  uint16_t default_value;
  uint16_t value;
  bool loaded;
  bool preference_ready;
  esphome::ESPPreferenceObject preference;
};

struct CachedInt16Preference {
  uint32_t key;
  int16_t default_value;
  int16_t value;
  bool loaded;
  bool preference_ready;
  esphome::ESPPreferenceObject preference;
};

struct TimeZoneOffsetTransition {
  uint32_t utc_timestamp{0};
  int16_t offset_minutes{0};
};

struct TimeZoneOffsetSchedule {
  uint8_t valid{0};
  int16_t base_offset_minutes{TIME_ZONE_OFFSET_MINUTES_UNSET};
  uint8_t transition_count{0};
  TimeZoneOffsetTransition transitions[TIME_ZONE_OFFSET_MAX_TRANSITIONS]{};
};

struct MqttSettings {
  uint8_t enabled{0};
  uint16_t port{MQTT_DEFAULT_PORT};
  char broker[MAX_MQTT_BROKER_LENGTH + 1]{};
  char username[MAX_MQTT_USERNAME_LENGTH + 1]{};
  char password[MAX_MQTT_PASSWORD_LENGTH + 1]{};
  char topic_prefix[MAX_MQTT_TOPIC_PREFIX_LENGTH + 1]{};
};

struct LocationSettings {
  uint8_t exact_enabled{0};
  int32_t latitude_e7{0};
  int32_t longitude_e7{0};
  uint8_t reserved[7]{};
};

enum FlightRadarTrafficMode : uint8_t {
  FLIGHT_RADAR_TRAFFIC_ALL = 0,
  FLIGHT_RADAR_TRAFFIC_MILITARY_ONLY = 1,
};

struct FlightRadarSettings {
  uint8_t enabled{1};
  uint8_t range_km{FLIGHT_RADAR_RANGE_DEFAULT_KM};
  uint8_t traffic_mode{FLIGHT_RADAR_TRAFFIC_ALL};
  uint8_t reserved[5]{};
};

struct StoredNetworkOption {
  char ssid[MAX_WIFI_SSID_LENGTH + 1]{};
  int8_t rssi{-127};
};

struct StoredNetworkOptions {
  uint8_t count{0};
  StoredNetworkOption networks[MAX_SETUP_NETWORK_OPTIONS]{};
};

struct MqttBrokerInput {
  std::string broker;
  uint16_t port{MQTT_DEFAULT_PORT};
  bool port_valid{true};
};

inline const MqttSettings &load_mqtt_settings();
inline const LocationSettings &load_location_settings();
inline const FlightRadarSettings &load_flight_radar_settings();

struct CachedTimeZoneOffsetSchedulePreference {
  TimeZoneOffsetSchedule value{};
  bool loaded{false};
  bool preference_ready{false};
  esphome::ESPPreferenceObject preference{};
};

struct CachedMqttSettingsPreference {
  MqttSettings value{};
  bool loaded{false};
  bool preference_ready{false};
  esphome::ESPPreferenceObject preference{};
};

struct CachedLocationPreference {
  LocationSettings value{};
  bool loaded{false};
  bool preference_ready{false};
  esphome::ESPPreferenceObject preference{};
};

struct CachedFlightRadarPreference {
  FlightRadarSettings value{1, FLIGHT_RADAR_RANGE_DEFAULT_KM, FLIGHT_RADAR_TRAFFIC_ALL, {}};
  bool loaded{false};
  bool preference_ready{false};
  esphome::ESPPreferenceObject preference{};
};

struct CachedStoredNetworkOptionsPreference {
  StoredNetworkOptions value{};
  bool loaded{false};
  bool preference_ready{false};
  esphome::ESPPreferenceObject preference{};
};

struct CachedWifiSettingsPreference {
  uint32_t key{0};
  esphome::wifi::SavedWifiSettings value{};
  bool loaded{false};
  bool valid{false};
  bool preference_ready{false};
  esphome::ESPPreferenceObject preference{};
};

inline void prepare_cached_uint8_preference_(CachedUint8Preference &cache) {
  if (cache.preference_ready || esphome::global_preferences == nullptr)
    return;

  cache.preference = esphome::global_preferences->make_preference<uint8_t>(cache.key, true);
  cache.preference_ready = true;
}

inline CachedWifiSettingsPreference &runtime_wifi_settings_pref_() {
  static CachedWifiSettingsPreference cache{RUNTIME_WIFI_PREF_KEY, {}, false, false, false, {}};
  return cache;
}

inline CachedWifiSettingsPreference &setup_wifi_backup_pref_() {
  static CachedWifiSettingsPreference cache{SETUP_WIFI_BACKUP_PREF_KEY, {}, false, false, false, {}};
  return cache;
}

inline CachedWifiSettingsPreference *wifi_settings_pref_for_key_(uint32_t key) {
  if (key == RUNTIME_WIFI_PREF_KEY)
    return &runtime_wifi_settings_pref_();
  if (key == SETUP_WIFI_BACKUP_PREF_KEY)
    return &setup_wifi_backup_pref_();
  return nullptr;
}

inline void prepare_wifi_settings_preference_(CachedWifiSettingsPreference &cache) {
  if (cache.preference_ready || esphome::global_preferences == nullptr)
    return;

  cache.preference = esphome::global_preferences->make_preference<esphome::wifi::SavedWifiSettings>(cache.key, true);
  cache.preference_ready = true;
}

inline void cache_wifi_settings_(CachedWifiSettingsPreference &cache,
                                 const esphome::wifi::SavedWifiSettings &settings) {
  cache.value = settings;
  cache.value.ssid[sizeof(cache.value.ssid) - 1] = '\0';
  cache.value.password[sizeof(cache.value.password) - 1] = '\0';
  cache.valid = cache.value.ssid[0] != '\0';
  cache.loaded = true;
}

inline void clear_cached_wifi_settings_(uint32_t key) {
  if (auto *cache = wifi_settings_pref_for_key_(key); cache != nullptr) {
    cache->value = {};
    cache->valid = false;
    cache->loaded = true;
  }
}

inline uint8_t load_cached_uint8_preference_(CachedUint8Preference &cache) {
  if (cache.loaded)
    return cache.value;
  if (esphome::global_preferences == nullptr)
    return cache.default_value;

  prepare_cached_uint8_preference_(cache);

  uint8_t value = cache.default_value;
  if (cache.preference.load(&value))
    cache.value = value;
  else
    cache.value = cache.default_value;
  cache.loaded = true;
  return cache.value;
}

inline bool load_optional_cached_uint8_preference_(CachedUint8Preference &cache, uint8_t &value) {
  if (cache.loaded) {
    value = cache.value;
    return true;
  }
  if (esphome::global_preferences == nullptr)
    return false;

  prepare_cached_uint8_preference_(cache);
  uint8_t loaded_value = cache.default_value;
  if (!cache.preference.load(&loaded_value))
    return false;

  cache.value = loaded_value;
  cache.loaded = true;
  value = loaded_value;
  return true;
}

inline void save_cached_uint8_preference_(CachedUint8Preference &cache, uint8_t value) {
  cache.value = value;
  cache.loaded = true;
  if (esphome::global_preferences == nullptr)
    return;

  prepare_cached_uint8_preference_(cache);
  cache.preference.save(&cache.value);
  esphome::global_preferences->sync();
}

inline void prepare_cached_uint16_preference_(CachedUint16Preference &cache) {
  if (cache.preference_ready || esphome::global_preferences == nullptr)
    return;

  cache.preference = esphome::global_preferences->make_preference<uint16_t>(cache.key, true);
  cache.preference_ready = true;
}

inline uint16_t load_cached_uint16_preference_(CachedUint16Preference &cache) {
  if (cache.loaded)
    return cache.value;
  if (esphome::global_preferences == nullptr)
    return cache.default_value;

  prepare_cached_uint16_preference_(cache);

  uint16_t value = cache.default_value;
  if (cache.preference.load(&value))
    cache.value = value;
  else
    cache.value = cache.default_value;
  cache.loaded = true;
  return cache.value;
}

inline void save_cached_uint16_preference_(CachedUint16Preference &cache, uint16_t value) {
  cache.value = value;
  cache.loaded = true;
  if (esphome::global_preferences == nullptr)
    return;

  prepare_cached_uint16_preference_(cache);
  cache.preference.save(&cache.value);
  esphome::global_preferences->sync();
}

inline void prepare_cached_int16_preference_(CachedInt16Preference &cache) {
  if (cache.preference_ready || esphome::global_preferences == nullptr)
    return;

  cache.preference = esphome::global_preferences->make_preference<int16_t>(cache.key, true);
  cache.preference_ready = true;
}

inline int16_t load_cached_int16_preference_(CachedInt16Preference &cache) {
  if (cache.loaded)
    return cache.value;
  if (esphome::global_preferences == nullptr)
    return cache.default_value;

  prepare_cached_int16_preference_(cache);

  int16_t value = cache.default_value;
  if (cache.preference.load(&value))
    cache.value = value;
  else
    cache.value = cache.default_value;
  cache.loaded = true;
  return cache.value;
}

inline void save_cached_int16_preference_(CachedInt16Preference &cache, int16_t value) {
  cache.value = value;
  cache.loaded = true;
  if (esphome::global_preferences == nullptr)
    return;

  prepare_cached_int16_preference_(cache);
  cache.preference.save(&cache.value);
  esphome::global_preferences->sync();
}

inline void copy_limited_c_string_(char *destination, size_t destination_size, const std::string &value) {
  if (destination == nullptr || destination_size == 0)
    return;

  const size_t length = std::min(value.size(), destination_size - 1);
  std::memcpy(destination, value.data(), length);
  destination[length] = '\0';
  if (length + 1 < destination_size)
    std::memset(destination + length + 1, 0, destination_size - length - 1);
}

inline CachedStoredNetworkOptionsPreference &stored_network_options_pref_() {
  static CachedStoredNetworkOptionsPreference cache{};
  return cache;
}

inline void prepare_stored_network_options_preference_(CachedStoredNetworkOptionsPreference &cache) {
  if (cache.preference_ready || esphome::global_preferences == nullptr)
    return;

  cache.preference =
      esphome::global_preferences->make_preference<StoredNetworkOptions>(SETUP_NETWORK_OPTIONS_PREF_KEY, true);
  cache.preference_ready = true;
}

inline StoredNetworkOptions load_stored_network_options_() {
  auto &cache = stored_network_options_pref_();
  if (cache.loaded)
    return cache.value;
  if (esphome::global_preferences == nullptr)
    return cache.value;

  prepare_stored_network_options_preference_(cache);
  StoredNetworkOptions value{};
  if (cache.preference.load(&value))
    cache.value = value;
  cache.value.count = std::min<uint8_t>(cache.value.count, MAX_SETUP_NETWORK_OPTIONS);
  cache.loaded = true;
  return cache.value;
}

inline bool load_setup_network_options_from_preferences_() {
  const StoredNetworkOptions stored = load_stored_network_options_();
  std::vector<NetworkOption> networks;
  networks.reserve(stored.count);

  for (uint8_t index = 0; index < stored.count; index++)
    append_network_option_(networks, fixed_string_(stored.networks[index].ssid), stored.networks[index].rssi);

  sort_network_options_(networks);
  replace_setup_network_options_cache_(networks);
  return !networks.empty();
}

inline void save_setup_network_options_to_preferences_() {
  auto &cache = stored_network_options_pref_();
  auto networks = setup_network_options_snapshot_();
  sort_network_options_(networks);

  StoredNetworkOptions stored{};
  stored.count = static_cast<uint8_t>(std::min(networks.size(), MAX_SETUP_NETWORK_OPTIONS));
  for (uint8_t index = 0; index < stored.count; index++) {
    copy_limited_c_string_(stored.networks[index].ssid, sizeof(stored.networks[index].ssid), networks[index].ssid);
    stored.networks[index].rssi = networks[index].rssi;
  }

  if (cache.loaded && cache.value.count == stored.count) {
    bool unchanged = true;
    for (uint8_t index = 0; index < stored.count; index++) {
      if (fixed_string_(cache.value.networks[index].ssid) != fixed_string_(stored.networks[index].ssid) ||
          cache.value.networks[index].rssi != stored.networks[index].rssi) {
        unchanged = false;
        break;
      }
    }
    if (unchanged)
      return;
  }

  cache.value = stored;
  cache.loaded = true;
  if (esphome::global_preferences == nullptr)
    return;

  prepare_stored_network_options_preference_(cache);
  cache.preference.save(&cache.value);
  esphome::global_preferences->sync();
}

inline std::string trim_copy_(const std::string &value) {
  return AirDot::connectivity::trim_copy(value);
}

inline std::string ascii_lower_copy_(const std::string &value) {
  return AirDot::connectivity::ascii_lower_copy(value);
}

inline bool parse_mqtt_port_value_(const std::string &value, uint16_t &port) {
  return AirDot::connectivity::parse_port(value, port);
}

inline bool is_valid_ipv4_address_(const std::string &value) {
  return AirDot::connectivity::is_valid_ipv4_address(value);
}

inline MqttBrokerInput normalize_mqtt_broker_input_(const std::string &broker, uint16_t fallback_port,
                                                    bool fallback_port_valid = true) {
  MqttBrokerInput normalized{};
  normalized.port = fallback_port_valid && fallback_port != 0 ? fallback_port : 0;
  normalized.port_valid = fallback_port_valid && normalized.port != 0;

  const auto host_port =
      AirDot::connectivity::normalize_host_port_input(
          broker, normalized.port, MQTT_DEFAULT_PORT, normalized.port_valid);
  normalized.broker = host_port.host;
  normalized.port_valid = host_port.port_valid;
  normalized.port = host_port.port_valid ? host_port.port : 0;
  return normalized;
}

inline std::string sanitize_mqtt_topic_prefix_(const std::string &value) {
  std::string trimmed = trim_copy_(value);
  if (trimmed.size() > MAX_MQTT_TOPIC_PREFIX_LENGTH)
    trimmed.resize(MAX_MQTT_TOPIC_PREFIX_LENGTH);
  return trimmed.empty() ? std::string(AIRDOT_DEFAULT_MQTT_TOPIC_PREFIX) : trimmed;
}

inline MqttSettings default_mqtt_settings_() {
  MqttSettings settings{};
  settings.enabled = 0;
  settings.port = MQTT_DEFAULT_PORT;
  copy_limited_c_string_(settings.topic_prefix, sizeof(settings.topic_prefix), AIRDOT_DEFAULT_MQTT_TOPIC_PREFIX);
  return settings;
}

inline void normalize_mqtt_settings_(MqttSettings &settings) {
  settings.broker[MAX_MQTT_BROKER_LENGTH] = '\0';
  settings.username[MAX_MQTT_USERNAME_LENGTH] = '\0';
  settings.password[MAX_MQTT_PASSWORD_LENGTH] = '\0';
  settings.topic_prefix[MAX_MQTT_TOPIC_PREFIX_LENGTH] = '\0';
  settings.enabled = settings.enabled == 1 ? 1 : 0;
  if (settings.port == 0 && settings.enabled != 1)
    settings.port = MQTT_DEFAULT_PORT;
  const auto normalized_broker =
      normalize_mqtt_broker_input_(fixed_string_(settings.broker), settings.port, settings.port != 0);
  settings.port = normalized_broker.port;
  copy_limited_c_string_(
      settings.broker, sizeof(settings.broker), normalized_broker.broker);
  copy_limited_c_string_(
      settings.topic_prefix, sizeof(settings.topic_prefix), sanitize_mqtt_topic_prefix_(settings.topic_prefix));
}

inline bool mqtt_settings_equal_(const MqttSettings &a, const MqttSettings &b) {
  return a.enabled == b.enabled && a.port == b.port && fixed_string_(a.broker) == fixed_string_(b.broker) &&
         fixed_string_(a.username) == fixed_string_(b.username) && fixed_string_(a.password) == fixed_string_(b.password) &&
         fixed_string_(a.topic_prefix) == fixed_string_(b.topic_prefix);
}

inline bool location_coordinates_are_valid(float latitude, float longitude) {
  return std::isfinite(latitude) && std::isfinite(longitude) &&
         latitude >= -90.0f && latitude <= 90.0f &&
         longitude >= -180.0f && longitude <= 180.0f;
}

inline bool location_e7_coordinates_are_valid(int32_t latitude_e7, int32_t longitude_e7) {
  return latitude_e7 >= LATITUDE_MIN_E7 && latitude_e7 <= LATITUDE_MAX_E7 &&
         longitude_e7 >= LONGITUDE_MIN_E7 && longitude_e7 <= LONGITUDE_MAX_E7;
}

inline int32_t coordinate_to_e7_(double value) {
  return static_cast<int32_t>(std::round(value * 10000000.0));
}

inline float coordinate_from_e7_(int32_t value) {
  return static_cast<float>(value) / 10000000.0f;
}

inline void normalize_location_settings_(LocationSettings &settings) {
  settings.exact_enabled =
      settings.exact_enabled == 1 && location_e7_coordinates_are_valid(settings.latitude_e7, settings.longitude_e7)
          ? 1
          : 0;
}

inline uint8_t normalize_flight_radar_range_km(int value) {
  if (value < FLIGHT_RADAR_RANGE_MIN_KM || value > FLIGHT_RADAR_RANGE_MAX_KM)
    return FLIGHT_RADAR_RANGE_DEFAULT_KM;

  const int offset = value - FLIGHT_RADAR_RANGE_MIN_KM;
  const int steps = (offset + FLIGHT_RADAR_RANGE_STEP_KM / 2) / FLIGHT_RADAR_RANGE_STEP_KM;
  return static_cast<uint8_t>(FLIGHT_RADAR_RANGE_MIN_KM + steps * FLIGHT_RADAR_RANGE_STEP_KM);
}

inline FlightRadarSettings default_flight_radar_settings_() {
  FlightRadarSettings settings{};
  settings.enabled = 1;
  settings.range_km = FLIGHT_RADAR_RANGE_DEFAULT_KM;
  settings.traffic_mode = FLIGHT_RADAR_TRAFFIC_ALL;
  return settings;
}

inline void normalize_flight_radar_settings_(FlightRadarSettings &settings) {
  settings.enabled = settings.enabled == 1 ? 1 : 0;
  settings.range_km = normalize_flight_radar_range_km(settings.range_km);
  settings.traffic_mode =
      settings.traffic_mode == FLIGHT_RADAR_TRAFFIC_MILITARY_ONLY
          ? FLIGHT_RADAR_TRAFFIC_MILITARY_ONLY
          : FLIGHT_RADAR_TRAFFIC_ALL;
}

inline int16_t normalize_sen66_temperature_offset_centi_c_(int value) {
  return static_cast<int16_t>(
      std::max<int>(SEN66_TEMPERATURE_OFFSET_MIN_CENTI_C,
                    std::min<int>(SEN66_TEMPERATURE_OFFSET_MAX_CENTI_C, value)));
}

inline int16_t default_sen66_temperature_offset_centi_c_() {
  const float configured_offset = static_cast<float>(AIRDOT_DEFAULT_SEN66_TEMPERATURE_OFFSET_C);
  if (!std::isfinite(configured_offset))
    return -430;

  return normalize_sen66_temperature_offset_centi_c_(
      static_cast<int>(std::round(configured_offset * 100.0f)));
}

inline CachedUint8Preference &home_assistant_discovery_pref_() {
  static CachedUint8Preference cache{HOME_ASSISTANT_DISCOVERY_PREF_KEY, 0, 0, false, false, {}};
  return cache;
}

inline CachedUint8Preference &unit_system_pref_() {
  static CachedUint8Preference cache{UNIT_SYSTEM_PREF_KEY, UNIT_SYSTEM_METRIC, UNIT_SYSTEM_METRIC, false, false, {}};
  return cache;
}

inline CachedUint8Preference &display_brightness_pref_() {
  static CachedUint8Preference cache{
      DISPLAY_BRIGHTNESS_PREF_KEY, DISPLAY_BRIGHTNESS_MEDIUM, DISPLAY_BRIGHTNESS_MEDIUM, false, false, {}};
  return cache;
}

inline CachedUint8Preference &dark_mode_pref_() {
  static CachedUint8Preference cache{DARK_MODE_PREF_KEY, 1, 1, false, false, {}};
  return cache;
}

inline CachedUint8Preference &first_run_onboarding_complete_pref_() {
  static CachedUint8Preference cache{FIRST_RUN_ONBOARDING_COMPLETE_PREF_KEY, 0, 0, false, false, {}};
  return cache;
}

inline CachedUint8Preference &auto_dim_pref_() {
  static CachedUint8Preference cache{AUTO_DIM_PREF_KEY, 0, 0, false, false, {}};
  return cache;
}

inline CachedUint8Preference &monitoring_update_interval_pref_() {
  static CachedUint8Preference cache{MONITORING_UPDATE_INTERVAL_PREF_KEY, 10, 10, false, false, {}};
  return cache;
}

inline CachedUint8Preference &audio_alerts_pref_() {
  static CachedUint8Preference cache{AUDIO_ALERTS_PREF_KEY, 0, 0, false, false, {}};
  return cache;
}

inline CachedUint8Preference &air_quality_profile_pref_() {
  static CachedUint8Preference cache{
      AIR_QUALITY_PROFILE_PREF_KEY,
      static_cast<uint8_t>(AirDot::AirQualityProfile::GLOBAL_WHO_EEA_STRICT),
      static_cast<uint8_t>(AirDot::AirQualityProfile::GLOBAL_WHO_EEA_STRICT),
      false,
      false,
      {}};
  return cache;
}

inline CachedUint8Preference &hazard_focus_mode_pref_() {
  static CachedUint8Preference cache{HAZARD_FOCUS_MODE_PREF_KEY, 0, 0, false, false, {}};
  return cache;
}

inline CachedUint8Preference &display_alert_wake_screen_pref_() {
  static CachedUint8Preference cache{DISPLAY_ALERT_WAKE_SCREEN_PREF_KEY, 0, 0, false, false, {}};
  return cache;
}

inline CachedUint8Preference &ui_language_pref_() {
  static CachedUint8Preference cache{
      UI_LANGUAGE_PREF_KEY,
      static_cast<uint8_t>(AirDot::UiLanguage::EN),
      static_cast<uint8_t>(AirDot::UiLanguage::EN),
      false,
      false,
      {}};
  return cache;
}

inline CachedUint8Preference &time_format_pref_() {
  static CachedUint8Preference cache{TIME_FORMAT_PREF_KEY, TIME_FORMAT_24H, TIME_FORMAT_24H, false, false, {}};
  return cache;
}

inline CachedUint8Preference &time_server_pref_() {
  static CachedUint8Preference cache{TIME_SERVER_PREF_KEY, 1, 1, false, false, {}};
  return cache;
}

inline CachedUint8Preference &manual_time_pref_() {
  static CachedUint8Preference cache{MANUAL_TIME_PREF_KEY, 0, 0, false, false, {}};
  return cache;
}

inline CachedUint8Preference &weather_sync_pref_() {
  static CachedUint8Preference cache{WEATHER_SYNC_PREF_KEY, 1, 1, false, false, {}};
  return cache;
}

inline CachedInt16Preference &sen66_temperature_offset_centi_c_pref_() {
  static CachedInt16Preference cache{
      SEN66_TEMPERATURE_OFFSET_CENTI_C_PREF_KEY,
      default_sen66_temperature_offset_centi_c_(),
      default_sen66_temperature_offset_centi_c_(),
      false,
      false,
      {}};
  return cache;
}

inline CachedUint8Preference &night_screen_off_pref_() {
  static CachedUint8Preference cache{NIGHT_SCREEN_OFF_PREF_KEY, 0, 0, false, false, {}};
  return cache;
}

inline CachedUint16Preference &screen_off_start_minutes_pref_() {
  static CachedUint16Preference cache{
      SCREEN_OFF_START_MINUTES_PREF_KEY,
      SCREEN_OFF_DEFAULT_START_MINUTES,
      SCREEN_OFF_DEFAULT_START_MINUTES,
      false,
      false,
      {}};
  return cache;
}

inline CachedUint16Preference &screen_off_end_minutes_pref_() {
  static CachedUint16Preference cache{
      SCREEN_OFF_END_MINUTES_PREF_KEY,
      SCREEN_OFF_DEFAULT_END_MINUTES,
      SCREEN_OFF_DEFAULT_END_MINUTES,
      false,
      false,
      {}};
  return cache;
}

inline CachedTimeZoneOffsetSchedulePreference &time_zone_offset_schedule_pref_() {
  static CachedTimeZoneOffsetSchedulePreference cache{};
  return cache;
}

inline CachedMqttSettingsPreference &mqtt_settings_pref_() {
  static CachedMqttSettingsPreference cache{default_mqtt_settings_(), false, false, {}};
  return cache;
}

inline CachedLocationPreference &location_pref_() {
  static CachedLocationPreference cache{};
  return cache;
}

inline CachedFlightRadarPreference &flight_radar_pref_() {
  static CachedFlightRadarPreference cache{default_flight_radar_settings_(), false, false, {}};
  return cache;
}

inline void prepare_time_zone_offset_schedule_preference_(CachedTimeZoneOffsetSchedulePreference &cache) {
  if (cache.preference_ready || esphome::global_preferences == nullptr)
    return;

  cache.preference =
      esphome::global_preferences->make_preference<TimeZoneOffsetSchedule>(TIME_ZONE_OFFSET_SCHEDULE_PREF_KEY, true);
  cache.preference_ready = true;
}

inline const TimeZoneOffsetSchedule &load_time_zone_offset_schedule_() {
  auto &cache = time_zone_offset_schedule_pref_();
  if (cache.loaded)
    return cache.value;
  if (esphome::global_preferences == nullptr)
    return cache.value;

  prepare_time_zone_offset_schedule_preference_(cache);
  TimeZoneOffsetSchedule value{};
  if (cache.preference.load(&value)) {
    cache.value = value;
  }
  cache.loaded = true;
  return cache.value;
}

inline void save_time_zone_offset_schedule_(const TimeZoneOffsetSchedule &schedule) {
  auto &cache = time_zone_offset_schedule_pref_();
  cache.value = schedule;
  cache.loaded = true;
  if (esphome::global_preferences == nullptr)
    return;

  prepare_time_zone_offset_schedule_preference_(cache);
  cache.preference.save(&cache.value);
  esphome::global_preferences->sync();
}

inline void prepare_mqtt_settings_preference_(CachedMqttSettingsPreference &cache) {
  if (cache.preference_ready || esphome::global_preferences == nullptr)
    return;

  cache.preference = esphome::global_preferences->make_preference<MqttSettings>(MQTT_SETTINGS_PREF_KEY, true);
  cache.preference_ready = true;
}

inline void prepare_location_preference_(CachedLocationPreference &cache) {
  if (cache.preference_ready || esphome::global_preferences == nullptr)
    return;

  cache.preference = esphome::global_preferences->make_preference<LocationSettings>(LOCATION_PREF_KEY, true);
  cache.preference_ready = true;
}

inline void prepare_flight_radar_preference_(CachedFlightRadarPreference &cache) {
  if (cache.preference_ready || esphome::global_preferences == nullptr)
    return;

  cache.preference = esphome::global_preferences->make_preference<FlightRadarSettings>(
      FLIGHT_RADAR_SETTINGS_PREF_KEY, true);
  cache.preference_ready = true;
}

inline const MqttSettings &load_mqtt_settings() {
  auto &cache = mqtt_settings_pref_();
  if (cache.loaded)
    return cache.value;
  if (esphome::global_preferences == nullptr)
    return cache.value;

  prepare_mqtt_settings_preference_(cache);
  MqttSettings value = default_mqtt_settings_();
  if (cache.preference.load(&value))
    cache.value = value;
  else
    cache.value = default_mqtt_settings_();
  normalize_mqtt_settings_(cache.value);
  cache.loaded = true;
  return cache.value;
}

inline void save_mqtt_settings(bool enabled, const std::string &broker, uint16_t port, const std::string &username,
                               const std::string &password, const std::string &topic_prefix) {
  auto &cache = mqtt_settings_pref_();
  MqttSettings value = default_mqtt_settings_();
  const auto normalized_broker = normalize_mqtt_broker_input_(broker, port, port != 0);
  value.enabled = enabled ? 1 : 0;
  value.port = normalized_broker.port;
  copy_limited_c_string_(value.broker, sizeof(value.broker), normalized_broker.broker);
  copy_limited_c_string_(value.username, sizeof(value.username), trim_copy_(username));
  copy_limited_c_string_(value.password, sizeof(value.password), password);
  copy_limited_c_string_(value.topic_prefix, sizeof(value.topic_prefix), sanitize_mqtt_topic_prefix_(topic_prefix));
  normalize_mqtt_settings_(value);

  cache.value = value;
  cache.loaded = true;
  if (esphome::global_preferences == nullptr)
    return;

  prepare_mqtt_settings_preference_(cache);
  cache.preference.save(&cache.value);
  esphome::global_preferences->sync();
}

inline void save_mqtt_settings(bool enabled, const std::string &broker, const std::string &port_value,
                               const std::string &username, const std::string &password,
                               const std::string &topic_prefix) {
  uint16_t port = 0;
  const bool port_valid = parse_mqtt_port_value_(port_value, port);
  save_mqtt_settings(enabled, broker, port_valid ? port : 0, username, password, topic_prefix);
}

inline const LocationSettings &load_location_settings() {
  auto &cache = location_pref_();
  if (cache.loaded)
    return cache.value;
  if (esphome::global_preferences == nullptr)
    return cache.value;

  prepare_location_preference_(cache);
  LocationSettings value{};
  if (cache.preference.load(&value))
    cache.value = value;
  else
    cache.value = {};
  normalize_location_settings_(cache.value);
  cache.loaded = true;
  return cache.value;
}

inline void save_location_settings_e7(bool exact_enabled, int32_t latitude_e7, int32_t longitude_e7) {
  auto &cache = location_pref_();
  LocationSettings value{};
  const bool coordinates_valid = location_e7_coordinates_are_valid(latitude_e7, longitude_e7);
  if (coordinates_valid) {
    value.latitude_e7 = latitude_e7;
    value.longitude_e7 = longitude_e7;
  }
  if (exact_enabled && coordinates_valid)
    value.exact_enabled = 1;
  normalize_location_settings_(value);

  cache.value = value;
  cache.loaded = true;
  if (esphome::global_preferences == nullptr)
    return;

  prepare_location_preference_(cache);
  cache.preference.save(&cache.value);
  esphome::global_preferences->sync();
}

inline void save_location_settings(bool exact_enabled, float latitude, float longitude) {
  save_location_settings_e7(exact_enabled, coordinate_to_e7_(latitude), coordinate_to_e7_(longitude));
}

inline bool load_exact_location(float &latitude, float &longitude) {
  const auto &settings = load_location_settings();
  if (settings.exact_enabled != 1)
    return false;

  const float stored_latitude = coordinate_from_e7_(settings.latitude_e7);
  const float stored_longitude = coordinate_from_e7_(settings.longitude_e7);
  if (!location_coordinates_are_valid(stored_latitude, stored_longitude))
    return false;

  latitude = stored_latitude;
  longitude = stored_longitude;
  return true;
}

inline bool exact_location_enabled() {
  float latitude = 0.0f;
  float longitude = 0.0f;
  return load_exact_location(latitude, longitude);
}

inline const FlightRadarSettings &load_flight_radar_settings() {
  auto &cache = flight_radar_pref_();
  if (cache.loaded)
    return cache.value;
  if (esphome::global_preferences == nullptr)
    return cache.value;

  prepare_flight_radar_preference_(cache);
  FlightRadarSettings value = default_flight_radar_settings_();
  if (cache.preference.load(&value))
    cache.value = value;
  else
    cache.value = default_flight_radar_settings_();
  normalize_flight_radar_settings_(cache.value);
  cache.loaded = true;
  return cache.value;
}

inline bool load_flight_radar_enabled() {
  return load_flight_radar_settings().enabled == 1;
}

inline uint8_t load_flight_radar_range_km() {
  return load_flight_radar_settings().range_km;
}

inline bool load_flight_radar_military_only() {
  return load_flight_radar_settings().traffic_mode == FLIGHT_RADAR_TRAFFIC_MILITARY_ONLY;
}

inline void save_flight_radar_settings(bool enabled, uint8_t range_km, bool military_only) {
  auto &cache = flight_radar_pref_();
  FlightRadarSettings value{};
  value.enabled = enabled ? 1 : 0;
  value.range_km = range_km;
  value.traffic_mode = military_only ? FLIGHT_RADAR_TRAFFIC_MILITARY_ONLY : FLIGHT_RADAR_TRAFFIC_ALL;
  normalize_flight_radar_settings_(value);

  cache.value = value;
  cache.loaded = true;
  if (esphome::global_preferences == nullptr)
    return;

  prepare_flight_radar_preference_(cache);
  cache.preference.save(&cache.value);
  esphome::global_preferences->sync();
}

inline void erase_wifi_settings_(uint32_t key) {
#ifdef USE_ESP32
  nvs_handle_t handle;
  if (nvs_open("esphome", NVS_READWRITE, &handle) != ESP_OK) {
    clear_cached_wifi_settings_(key);
    return;
  }

  char key_string[12];
  std::snprintf(key_string, sizeof(key_string), "%u", static_cast<unsigned>(key));
  const esp_err_t erase_result = nvs_erase_key(handle, key_string);
  if (erase_result == ESP_OK || erase_result == ESP_ERR_NVS_NOT_FOUND)
    nvs_commit(handle);
  nvs_close(handle);
#endif
  clear_cached_wifi_settings_(key);
}

inline bool load_wifi_settings_(uint32_t key, esphome::wifi::SavedWifiSettings &settings) {
  auto *cache = wifi_settings_pref_for_key_(key);
  if (cache == nullptr) {
    settings = {};
    return false;
  }

  if (!cache->loaded) {
    if (esphome::global_preferences == nullptr) {
      settings = {};
      return false;
    }

    cache->value = {};
    cache->valid = false;
    prepare_wifi_settings_preference_(*cache);
    esphome::wifi::SavedWifiSettings loaded{};
    cache->valid = cache->preference.load(&loaded) && loaded.ssid[0] != '\0';
    if (cache->valid) {
      loaded.ssid[sizeof(loaded.ssid) - 1] = '\0';
      loaded.password[sizeof(loaded.password) - 1] = '\0';
      cache->value = loaded;
    }
    cache->loaded = true;
  }

  settings = cache->valid ? cache->value : esphome::wifi::SavedWifiSettings{};
  return cache->valid;
}

inline void save_wifi_settings_(uint32_t key, const esphome::wifi::SavedWifiSettings &settings) {
  auto *cache = wifi_settings_pref_for_key_(key);
  if (cache == nullptr)
    return;

  cache_wifi_settings_(*cache, settings);
  if (esphome::global_preferences == nullptr)
    return;

  prepare_wifi_settings_preference_(*cache);
  cache->preference.save(&cache->value);
  esphome::global_preferences->sync();
}

inline void note_runtime_wifi_settings_saved(const std::string &ssid, const std::string &password) {
  esphome::wifi::SavedWifiSettings settings{};
  std::strncpy(settings.ssid, ssid.c_str(), sizeof(settings.ssid) - 1);
  std::strncpy(settings.password, password.c_str(), sizeof(settings.password) - 1);
  cache_wifi_settings_(runtime_wifi_settings_pref_(), settings);
}


inline bool has_saved_wifi_settings() {
  esphome::wifi::SavedWifiSettings settings{};
  return load_wifi_settings_(RUNTIME_WIFI_PREF_KEY, settings);
}

inline bool load_first_run_onboarding_complete() {
  return load_cached_uint8_preference_(first_run_onboarding_complete_pref_()) == 1 || has_saved_wifi_settings();
}

inline void save_first_run_onboarding_complete(bool complete) {
  const uint8_t value = complete ? 1 : 0;
  save_cached_uint8_preference_(first_run_onboarding_complete_pref_(), value);
}

inline void factory_reset_preferences_and_reboot() {
  if (esphome::global_preferences != nullptr)
    esphome::global_preferences->reset();
  delay(100);
  esphome::App.safe_reboot();
}

inline bool load_setup_page_wifi_settings(esphome::wifi::SavedWifiSettings &settings) {
  if (load_wifi_settings_(RUNTIME_WIFI_PREF_KEY, settings))
    return true;

  auto &backup = setup_wifi_backup_();
  if (backup.valid && backup.settings.ssid[0] != '\0') {
    settings = backup.settings;
    return true;
  }

  if (load_wifi_settings_(SETUP_WIFI_BACKUP_PREF_KEY, settings))
    return true;
  return false;
}

inline void clear_pending_wifi_setup(int &setup_pending_wifi_save, std::string &setup_pending_wifi_ssid,
                                     std::string &setup_pending_wifi_password) {
  setup_pending_wifi_save = 0;
  setup_pending_wifi_ssid.clear();
  setup_pending_wifi_password.clear();
}


inline bool load_home_assistant_discovery_enabled() {
  return load_cached_uint8_preference_(home_assistant_discovery_pref_()) == 1;
}

inline UnitSystem load_unit_system() {
  const uint8_t value = load_cached_uint8_preference_(unit_system_pref_());
  return value == UNIT_SYSTEM_IMPERIAL ? UNIT_SYSTEM_IMPERIAL : UNIT_SYSTEM_METRIC;
}

inline void save_unit_system(UnitSystem unit_system) {
  uint8_t value = unit_system == UNIT_SYSTEM_IMPERIAL ? UNIT_SYSTEM_IMPERIAL : UNIT_SYSTEM_METRIC;
  save_cached_uint8_preference_(unit_system_pref_(), value);
}

inline float display_temperature_c(float temperature_c, UnitSystem unit_system) {
  return unit_system == UNIT_SYSTEM_IMPERIAL ? temperature_c * 9.0f / 5.0f + 32.0f : temperature_c;
}

inline float display_temperature_c(float temperature_c) {
  return display_temperature_c(temperature_c, load_unit_system());
}

inline float display_pressure_pa(float pressure_pa, UnitSystem unit_system) {
  return unit_system == UNIT_SYSTEM_IMPERIAL ? pressure_pa * 0.000295299830714f : pressure_pa / 100.0f;
}

inline float display_pressure_pa(float pressure_pa) {
  return display_pressure_pa(pressure_pa, load_unit_system());
}

inline const char *temperature_unit_text(UnitSystem unit_system) {
  return unit_system == UNIT_SYSTEM_IMPERIAL ? "°F" : "°C";
}

inline const char *temperature_unit_text() {
  return temperature_unit_text(load_unit_system());
}

inline const char *pressure_unit_text(UnitSystem unit_system) {
  return unit_system == UNIT_SYSTEM_IMPERIAL ? "inHg" : "hPa";
}

inline const char *pressure_unit_text() {
  return pressure_unit_text(load_unit_system());
}

inline const char *pressure_format(UnitSystem unit_system) {
  return unit_system == UNIT_SYSTEM_IMPERIAL ? "%.2f" : "%.0f";
}

inline const char *pressure_format() {
  return pressure_format(load_unit_system());
}

inline void save_home_assistant_discovery_enabled(bool enabled) {
  uint8_t value = enabled ? 1 : 0;
  save_cached_uint8_preference_(home_assistant_discovery_pref_(), value);
}

inline TimeFormat normalize_time_format_(uint8_t value) {
  return value == TIME_FORMAT_12H ? TIME_FORMAT_12H : TIME_FORMAT_24H;
}

inline TimeFormat load_time_format() {
  return normalize_time_format_(load_cached_uint8_preference_(time_format_pref_()));
}

inline void save_time_format(TimeFormat time_format) {
  uint8_t value = time_format == TIME_FORMAT_12H ? TIME_FORMAT_12H : TIME_FORMAT_24H;
  save_cached_uint8_preference_(time_format_pref_(), value);
}

inline bool load_time_server_enabled() {
  return load_cached_uint8_preference_(time_server_pref_()) == 1;
}

inline void save_time_server_enabled(bool enabled) {
  save_cached_uint8_preference_(time_server_pref_(), enabled ? 1 : 0);
}

inline bool load_manual_time_enabled() {
  return load_cached_uint8_preference_(manual_time_pref_()) == 1;
}

inline void save_manual_time_enabled(bool enabled) {
  save_cached_uint8_preference_(manual_time_pref_(), enabled ? 1 : 0);
}

inline bool load_weather_enabled() {
  uint8_t value = 0;
  if (load_optional_cached_uint8_preference_(weather_sync_pref_(), value))
    return value == 1;

  return true;
}

inline void save_weather_enabled(bool enabled) {
  save_cached_uint8_preference_(weather_sync_pref_(), enabled ? 1 : 0);
}

inline int16_t load_sen66_temperature_offset_centi_c() {
  return normalize_sen66_temperature_offset_centi_c_(
      load_cached_int16_preference_(sen66_temperature_offset_centi_c_pref_()));
}

inline float load_sen66_temperature_offset_c() {
  return static_cast<float>(load_sen66_temperature_offset_centi_c()) / 100.0f;
}

inline float sen66_temperature_offset_display_c() {
  return std::fabs(load_sen66_temperature_offset_c());
}

inline void save_sen66_temperature_offset_centi_c(int value) {
  save_cached_int16_preference_(
      sen66_temperature_offset_centi_c_pref_(), normalize_sen66_temperature_offset_centi_c_(value));
}

inline void save_sen66_temperature_offset_display_c(float correction_celsius) {
  if (!std::isfinite(correction_celsius))
    correction_celsius = sen66_temperature_offset_display_c();

  const float positive_correction =
      std::max(0.0f, std::min(20.0f, std::fabs(correction_celsius)));
  save_sen66_temperature_offset_centi_c(
      -static_cast<int>(std::round(positive_correction * 100.0f)));
}

inline bool load_local_time_enabled() {
  return load_time_server_enabled() || load_manual_time_enabled();
}

inline int16_t normalize_time_zone_offset_minutes_(int value) {
  return static_cast<int16_t>(std::max(-720, std::min(840, value)));
}

inline bool has_time_zone_offset_minutes() {
  const auto &schedule = load_time_zone_offset_schedule_();
  return schedule.valid == 1 && schedule.base_offset_minutes != TIME_ZONE_OFFSET_MINUTES_UNSET;
}

inline int16_t time_zone_offset_minutes_for_utc(uint32_t utc_timestamp) {
  const auto &schedule = load_time_zone_offset_schedule_();
  if (schedule.valid == 1 && schedule.base_offset_minutes != TIME_ZONE_OFFSET_MINUTES_UNSET) {
    int16_t offset_minutes = normalize_time_zone_offset_minutes_(schedule.base_offset_minutes);
    const uint8_t transition_count = std::min(schedule.transition_count, TIME_ZONE_OFFSET_MAX_TRANSITIONS);
    for (uint8_t index = 0; index < transition_count; index++) {
      const auto &transition = schedule.transitions[index];
      if (transition.utc_timestamp == 0 || utc_timestamp < transition.utc_timestamp)
        break;
      offset_minutes = normalize_time_zone_offset_minutes_(transition.offset_minutes);
    }
    return offset_minutes;
  }

  return 0;
}

inline uint16_t normalize_minute_of_day_(uint16_t minute_of_day) {
  return static_cast<uint16_t>(minute_of_day % MINUTES_PER_DAY);
}

inline bool load_night_screen_off_enabled() {
  return load_cached_uint8_preference_(night_screen_off_pref_()) == 1;
}

inline void save_night_screen_off_enabled(bool enabled) {
  uint8_t value = enabled ? 1 : 0;
  save_cached_uint8_preference_(night_screen_off_pref_(), value);
}

inline uint16_t load_screen_off_start_minutes() {
  return normalize_minute_of_day_(load_cached_uint16_preference_(screen_off_start_minutes_pref_()));
}

inline uint16_t load_screen_off_end_minutes() {
  return normalize_minute_of_day_(load_cached_uint16_preference_(screen_off_end_minutes_pref_()));
}

inline void save_screen_off_start_minutes(uint16_t minute_of_day) {
  save_cached_uint16_preference_(screen_off_start_minutes_pref_(), normalize_minute_of_day_(minute_of_day));
}

inline void save_screen_off_end_minutes(uint16_t minute_of_day) {
  save_cached_uint16_preference_(screen_off_end_minutes_pref_(), normalize_minute_of_day_(minute_of_day));
}

inline uint16_t local_minute_of_day_from_utc(uint32_t utc_timestamp) {
  int32_t local_second_of_day = static_cast<int32_t>(utc_timestamp % 86400UL) +
                                static_cast<int32_t>(time_zone_offset_minutes_for_utc(utc_timestamp)) * 60;
  local_second_of_day %= 86400;
  if (local_second_of_day < 0)
    local_second_of_day += 86400;
  return static_cast<uint16_t>(local_second_of_day / 60);
}

inline bool screen_off_window_active_for_minute(uint16_t minute_of_day) {
  if (!load_night_screen_off_enabled())
    return false;

  const uint16_t start = load_screen_off_start_minutes();
  const uint16_t end = load_screen_off_end_minutes();
  const uint16_t current = normalize_minute_of_day_(minute_of_day);
  if (start == end)
    return false;
  if (start < end)
    return current >= start && current < end;
  return current >= start || current < end;
}

inline bool screen_off_window_active_at_utc(uint32_t utc_timestamp) {
  return load_local_time_enabled() && has_time_zone_offset_minutes() &&
         screen_off_window_active_for_minute(local_minute_of_day_from_utc(utc_timestamp));
}

inline DisplayBrightness normalize_display_brightness_(uint8_t value) {
  switch (value) {
    case DISPLAY_BRIGHTNESS_LOW:
      return DISPLAY_BRIGHTNESS_LOW;
    case DISPLAY_BRIGHTNESS_HIGH:
      return DISPLAY_BRIGHTNESS_HIGH;
    case DISPLAY_BRIGHTNESS_MEDIUM:
    default:
      return DISPLAY_BRIGHTNESS_MEDIUM;
  }
}

inline DisplayBrightness load_display_brightness() {
  return normalize_display_brightness_(load_cached_uint8_preference_(display_brightness_pref_()));
}

inline void save_display_brightness(DisplayBrightness brightness) {
  uint8_t value = normalize_display_brightness_(brightness);
  save_cached_uint8_preference_(display_brightness_pref_(), value);
}

inline bool load_dark_mode_enabled() {
  return load_cached_uint8_preference_(dark_mode_pref_()) == 1;
}

inline void save_dark_mode_enabled(bool enabled) {
  save_cached_uint8_preference_(dark_mode_pref_(), enabled ? 1 : 0);
}

inline float display_brightness_value(DisplayBrightness brightness) {
  switch (brightness) {
    case DISPLAY_BRIGHTNESS_LOW:
      return 0.35f;
    case DISPLAY_BRIGHTNESS_HIGH:
      return 1.0f;
    case DISPLAY_BRIGHTNESS_MEDIUM:
    default:
      return 0.7f;
  }
}

inline bool load_auto_dim_enabled() {
  return load_cached_uint8_preference_(auto_dim_pref_()) == 1;
}

inline void save_auto_dim_enabled(bool enabled) {
  uint8_t value = enabled ? 1 : 0;
  save_cached_uint8_preference_(auto_dim_pref_(), value);
}

inline uint8_t normalize_monitoring_update_interval_seconds_(uint8_t seconds) {
  return seconds == 5 || seconds == 30 ? seconds : 10;
}

inline uint8_t load_monitoring_update_interval_seconds() {
  uint8_t seconds = load_cached_uint8_preference_(monitoring_update_interval_pref_());
  return normalize_monitoring_update_interval_seconds_(seconds);
}

inline void save_monitoring_update_interval_seconds(uint8_t seconds) {
  uint8_t value = normalize_monitoring_update_interval_seconds_(seconds);
  save_cached_uint8_preference_(monitoring_update_interval_pref_(), value);
}

inline bool load_audio_alerts_enabled() {
  return load_cached_uint8_preference_(audio_alerts_pref_()) == 1;
}

inline void save_audio_alerts_enabled(bool enabled) {
  uint8_t value = enabled ? 1 : 0;
  save_cached_uint8_preference_(audio_alerts_pref_(), value);
}

inline bool load_display_alert_wake_screen_enabled() {
  return load_cached_uint8_preference_(display_alert_wake_screen_pref_()) == 1;
}

inline void save_display_alert_wake_screen_enabled(bool enabled) {
  uint8_t value = enabled ? 1 : 0;
  save_cached_uint8_preference_(display_alert_wake_screen_pref_(), value);
}

inline bool load_hazard_focus_mode_enabled() {
  return load_cached_uint8_preference_(hazard_focus_mode_pref_()) == 1;
}

inline void save_hazard_focus_mode_enabled(bool enabled) {
  uint8_t value = enabled ? 1 : 0;
  save_cached_uint8_preference_(hazard_focus_mode_pref_(), value);
}

inline AirDot::UiLanguage load_ui_language() {
  return AirDot::normalize_ui_language(load_cached_uint8_preference_(ui_language_pref_()));
}

inline bool has_saved_ui_language() {
  auto &cache = ui_language_pref_();
  if (esphome::global_preferences == nullptr)
    return false;

  prepare_cached_uint8_preference_(cache);
  uint8_t value = cache.default_value;
  return cache.preference.load(&value);
}

inline void save_ui_language(AirDot::UiLanguage language) {
  const uint8_t value = static_cast<uint8_t>(AirDot::normalize_ui_language(static_cast<uint8_t>(language)));
  save_cached_uint8_preference_(ui_language_pref_(), value);
}

inline AirDot::AirQualityProfile load_air_quality_profile() {
  return AirDot::normalize_air_quality_profile(load_cached_uint8_preference_(air_quality_profile_pref_()));
}

inline void save_air_quality_profile(AirDot::AirQualityProfile profile) {
  const uint8_t value = static_cast<uint8_t>(AirDot::normalize_air_quality_profile(static_cast<uint8_t>(profile)));
  save_cached_uint8_preference_(air_quality_profile_pref_(), value);
}

}  // namespace AirDot::onboarding
