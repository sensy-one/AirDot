#pragma once

// Ship radar data: a persistent aisstream.io subscription and a vessel cache.
//
// AIS splits a vessel's data across messages. Positions arrive every few
// seconds to minutes, but Class A static data (name, type) only every 6
// minutes and Class B static data every 6 minutes in two separate parts. The
// stream therefore runs whenever the feature is enabled, not only while the
// page is showing, and merges every message into a per-MMSI cache.
//
// Threading: one long-lived task ("ship_ais") owns the socket and writes the
// cache under a mutex. The main loop configures the stream, copies render
// lists and runs the favourite alert state machine. The task never touches
// NVS (its stack may be in PSRAM); settings are passed in by the main loop.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "connectivity.h"
#include "radar_map_data.h"
#include "radar_map_projection.h"
#include "setup_settings.h"

#ifdef USE_ESP32
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "ais_websocket.h"
#endif

namespace AirDot::ship_radar {

inline constexpr const char *TAG = "ship_radar";
inline constexpr const char *STREAM_HOST = "stream.aisstream.io";
inline constexpr const char *STREAM_PATH = "/v0/stream";

inline constexpr size_t MAX_VESSELS = 384;
inline constexpr size_t MAX_RENDER = 96;
inline constexpr uint8_t MAX_LABELS = 6;
// Label priority after favourites: the larger of length / 200 m and speed / 20 kn.
inline constexpr float LABEL_REFERENCE_LENGTH_M = 200.0f;
inline constexpr float LABEL_REFERENCE_SOG_KN = 20.0f;
inline constexpr uint8_t MAX_FAVORITES = AirDot::onboarding::SHIP_RADAR_MAX_FAVORITES;

// aisstream delivers at most about one position per vessel per minute, and in
// sparse coverage (Boston Harbor) gaps of 3-12 minutes are common, so vessels
// last seen under way are kept longer than stationary ones.
inline constexpr uint32_t DRAW_STALE_MS = 10UL * 60UL * 1000UL;         // Hide positions older than this...
inline constexpr uint32_t DRAW_STALE_MOVING_MS = 20UL * 60UL * 1000UL;  // ...or this, if last reported under way.
// A vessel that was under way stays shown through a brief stop (a ferry at a
// dock, a boat slowing to turn) until it reports slow twice or stays slow this long.
inline constexpr uint32_t UNDERWAY_HOLD_MS = 10UL * 60UL * 1000UL;
inline constexpr uint8_t UNDERWAY_HOLD_SLOW_REPORTS = 2;
inline constexpr uint32_t EVICT_MS = 30UL * 60UL * 1000UL;            // Forget vessels silent for this long.
inline constexpr uint32_t UNKNOWN_TYPE_GRACE_MS = 12UL * 60UL * 1000UL;  // Static data may still be on its way.
inline constexpr uint32_t PING_INTERVAL_MS = 30000UL;
inline constexpr uint32_t RX_TIMEOUT_MS = 75000UL;
inline constexpr uint32_t CONFIRM_TIMEOUT_MS = 15000UL;
inline constexpr uint32_t RESUBSCRIBE_MIN_MS = 1500UL;                // Server allows one update per second.
inline constexpr uint32_t AUTH_RETRY_MS = 15UL * 60UL * 1000UL;
// TLS 1.3 handshakes with certificate-bundle verification peak around 6 KB;
// the stack is in PSRAM, so leave generous headroom.
inline constexpr uint32_t TASK_STACK_BYTES = 16384;
inline constexpr uint32_t STABLE_CONNECTION_MS = 3UL * 60UL * 1000UL;  // Backoff resets only after this.
inline constexpr uint32_t DEFLATE_RETRY_MS = 60UL * 60UL * 1000UL;
inline constexpr uint8_t MESSAGES_PER_YIELD = 16;
inline constexpr float BOUNDING_BOX_MARGIN = 1.1f;

inline constexpr uint16_t SOG_UNKNOWN = 1023;
inline constexpr uint16_t COG_UNKNOWN = 3600;
inline constexpr uint16_t HEADING_UNKNOWN = 511;

// Age of a timestamp written by another task. A timestamp taken after `now`
// was read counts as age 0 rather than wrapping to ~49 days.
inline uint32_t age_ms(uint32_t now, uint32_t then) {
  const int32_t age = static_cast<int32_t>(now - then);
  return age < 0 ? 0 : static_cast<uint32_t>(age);
}

// Favourite motion thresholds (knots) and timing.
inline constexpr float MOVING_SOG_KN = 2.0f;
inline constexpr float MOVING_FAST_SOG_KN = 3.0f;
inline constexpr float STOPPED_SOG_KN = 0.5f;
inline constexpr float DRAW_MOVING_SOG_KN = 1.0f;
inline constexpr float UNDERWAY_STATUS_SOG_KN = 0.5f;  // With Class A status "under way".
inline constexpr uint8_t NAV_STATUS_UNDERWAY_ENGINE = 0;
inline constexpr uint8_t NAV_STATUS_UNDERWAY_SAILING = 8;
inline constexpr uint32_t MOVING_CONFIRM_MS = 60000UL;
inline constexpr uint32_t STOPPED_MIN_MS = 5UL * 60UL * 1000UL;
// Longer than the 3-12 minute gaps between reports seen in sparse coverage.
inline constexpr uint32_t ABSENCE_ESTABLISH_MS = 20UL * 60UL * 1000UL;
inline constexpr uint32_t STOPPED_KEEP_OFFLINE_MS = 15UL * 60UL * 1000UL;
inline constexpr uint32_t REARM_MIN_MS = 30UL * 60UL * 1000UL;
inline constexpr uint32_t REARM_QUIET_MS = 10UL * 60UL * 1000UL;

enum class StreamStatus : uint8_t {
  DISABLED,
  NO_KEY,
  NO_LOCATION,
  OFFLINE,
  CONNECTING,
  CONNECTED,
  KEY_REJECTED,
  RETRY_WAIT,
};

struct Vessel {
  uint32_t mmsi{0};
  int32_t lat_e7{0}, lon_e7{0};
  uint16_t sog_dkn{SOG_UNKNOWN};    // Tenths of a knot.
  uint16_t cog_ddeg{COG_UNKNOWN};   // Tenths of a degree.
  uint16_t heading{HEADING_UNKNOWN};
  uint16_t length_m{0};
  uint8_t nav_status{15};
  uint8_t ship_type{0};
  uint8_t flags{0};
  char name[21]{};
  uint32_t first_seen_ms{0}, position_ms{0}, last_seen_ms{0};
  uint32_t underway_ms{0};    // Last position report that was under way, 0 if none.
  uint8_t slow_reports{0};    // Position reports not under way since then.
};
inline constexpr uint8_t VESSEL_HAS_POSITION = 1U << 0;
inline constexpr uint8_t VESSEL_CLASS_B = 1U << 1;

struct RenderVessel {
  float x{0}, y{0};             // Projected, NORMALIZED_RADIUS = configured range.
  float heading_deg{NAN};       // True heading, else course over ground; NAN if unknown.
  float sog_kn{NAN};
  bool favorite{false}, moving{false};
  bool labelled{false};         // One of the MAX_LABELS vessels whose name is shown.
  uint8_t label_rank{0};        // 0 = highest label priority (valid when labelled).
  char name[21]{};              // Empty until static data arrives. Never the MMSI.
};
struct RenderList {
  RenderVessel vessels[MAX_RENDER]{};
  uint8_t count{0};
};

inline uint8_t category_for_type(uint8_t type) {
  using namespace AirDot::onboarding;
  if (type >= 70 && type <= 79) return SHIP_CATEGORY_CARGO;
  if (type >= 80 && type <= 89) return SHIP_CATEGORY_TANKER;
  if (type >= 60 && type <= 69) return SHIP_CATEGORY_PASSENGER;
  if (type == 30) return SHIP_CATEGORY_FISHING;
  if (type == 36 || type == 37) return SHIP_CATEGORY_PLEASURE;
  if ((type >= 31 && type <= 35) || (type >= 50 && type <= 59)) return SHIP_CATEGORY_SERVICE;
  if (type >= 40 && type <= 49) return SHIP_CATEGORY_HIGH_SPEED;
  return SHIP_CATEGORY_OTHER;
}

// Copies an AIS text field, dropping '@' padding and surrounding spaces.
inline void copy_ais_text(char *out, size_t size, const char *text) {
  if (size == 0) return;
  out[0] = '\0';
  if (text == nullptr) return;
  while (*text == ' ') ++text;
  size_t length = 0;
  while (text[length] != '\0' && text[length] != '@' && length + 1 < size) {
    const char c = text[length];
    out[length] = (c >= 0x20 && c < 0x7F) ? c : ' ';
    ++length;
  }
  while (length > 0 && out[length - 1] == ' ') --length;
  out[length] = '\0';
}

// Builds the subscription JSON. Boxes are [[lat, lon], [lat, lon]]; a box that
// crosses the antimeridian is split in two.
inline int build_subscription(char *out, size_t size, const char *key, float latitude, float longitude,
                              uint8_t range_km) {
  const double radius_km = range_km * BOUNDING_BOX_MARGIN;
  const double dlat = radius_km / 111.32;
  const double cos_lat = std::max(0.01, std::cos(latitude * radar_map::PI / 180.0));
  const double dlon = std::min(180.0, radius_km / (111.32 * cos_lat));
  const double south = std::max(-90.0, latitude - dlat), north = std::min(90.0, latitude + dlat);
  const double west = longitude - dlon, east = longitude + dlon;
  char boxes[160];
  if (west < -180.0) {
    std::snprintf(boxes, sizeof(boxes), "[[%.5f,%.5f],[%.5f,180.0]],[[%.5f,-180.0],[%.5f,%.5f]]",
                  south, west + 360.0, north, south, north, east);
  } else if (east > 180.0) {
    std::snprintf(boxes, sizeof(boxes), "[[%.5f,%.5f],[%.5f,180.0]],[[%.5f,-180.0],[%.5f,%.5f]]",
                  south, west, north, south, north, east - 360.0);
  } else {
    std::snprintf(boxes, sizeof(boxes), "[[%.5f,%.5f],[%.5f,%.5f]]", south, west, north, east);
  }
  return std::snprintf(out, size,
                       "{\"APIKey\":\"%s\",\"BoundingBoxes\":[%s],\"FilterMessageTypes\":["
                       "\"PositionReport\",\"StandardClassBPositionReport\",\"ExtendedClassBPositionReport\","
                       "\"ShipStaticData\",\"StaticDataReport\"]}",
                       key, boxes);
}

#ifdef USE_ESP32

inline bool vessel_underway(const Vessel &vessel) {
  if (vessel.sog_dkn == SOG_UNKNOWN) return false;
  const float sog = vessel.sog_dkn / 10.0f;
  if (sog >= DRAW_MOVING_SOG_KN) return true;
  const bool status_underway = !(vessel.flags & VESSEL_CLASS_B) &&
      (vessel.nav_status == NAV_STATUS_UNDERWAY_ENGINE || vessel.nav_status == NAV_STATUS_UNDERWAY_SAILING);
  return status_underway && sog >= UNDERWAY_STATUS_SOG_KN;
}

// Under way now, or recently enough that a brief stop shouldn't hide it.
inline bool vessel_shown_underway(const Vessel &vessel, uint32_t now) {
  if (vessel_underway(vessel)) return true;
  return vessel.underway_ms != 0 && vessel.slow_reports < UNDERWAY_HOLD_SLOW_REPORTS &&
         age_ms(now, vessel.underway_ms) < UNDERWAY_HOLD_MS;
}

namespace detail {

struct Config {
  bool enabled{false}, runtime_ok{false}, location_valid{false};
  char key[AirDot::onboarding::SHIP_RADAR_API_KEY_MAX_LENGTH + 1]{};
  float latitude{0}, longitude{0};
  uint8_t range_km{AirDot::onboarding::SHIP_RADAR_RANGE_DEFAULT_KM};
  uint32_t favorites[MAX_FAVORITES]{};
  uint8_t favorite_count{0};
  uint32_t generation{0};
};

struct Shared {
  SemaphoreHandle_t mutex{nullptr};
  Config config;                    // Written by the main loop.
  Vessel *vessels{nullptr};         // PSRAM table, MAX_VESSELS entries.
  Vessel *render_scratch{nullptr};  // PSRAM copy used by the main loop outside the lock.
  size_t vessel_count{0};
  std::atomic<uint32_t> revision{0};
  std::atomic<uint8_t> status{static_cast<uint8_t>(StreamStatus::DISABLED)};
  std::atomic<uint32_t> subscribed_generation{0};
  std::atomic<uint32_t> subscribed_since_ms{0};  // 0 while not confirmed.
  std::atomic<bool> task_started{false};
};
inline Shared &shared() {
  static Shared value;
  return value;
}

class Lock {
 public:
  explicit Lock(uint32_t timeout_ms = 20) : handle_(shared().mutex) {
    locked_ = handle_ != nullptr && xSemaphoreTake(handle_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
  }
  ~Lock() { if (locked_) xSemaphoreGive(handle_); }
  bool locked() const { return locked_; }
 private:
  SemaphoreHandle_t handle_;
  bool locked_{false};
};

inline void set_status(StreamStatus status, uint32_t next_retry_ms = 0) {
  shared().status.store(static_cast<uint8_t>(status), std::memory_order_release);
  using AirDot::connectivity::ConnectivityError;
  using AirDot::connectivity::ConnectivityStatus;
  ConnectivityStatus service = ConnectivityStatus::DISABLED;
  ConnectivityError error = ConnectivityError::NONE;
  switch (status) {
    case StreamStatus::DISABLED: break;
    case StreamStatus::NO_KEY: service = ConnectivityStatus::CONFIG_MISSING; error = ConnectivityError::CONFIG_MISSING; break;
    case StreamStatus::NO_LOCATION: service = ConnectivityStatus::CONFIG_MISSING; error = ConnectivityError::CONFIG_MISSING; break;
    case StreamStatus::OFFLINE: service = ConnectivityStatus::OFFLINE; error = ConnectivityError::OFFLINE; break;
    case StreamStatus::CONNECTING: service = ConnectivityStatus::CONNECTING; break;
    case StreamStatus::CONNECTED: service = ConnectivityStatus::CONNECTED; break;
    case StreamStatus::KEY_REJECTED: service = ConnectivityStatus::AUTH_FAILED; error = ConnectivityError::AUTH_FAILED; break;
    case StreamStatus::RETRY_WAIT: service = ConnectivityStatus::RETRY_WAIT; error = ConnectivityError::SERVER_ERROR; break;
  }
  AirDot::connectivity::set_service_status(AirDot::connectivity::Service::SHIP_RADAR, service, error,
                                           esphome::millis(), next_retry_ms);
}

// Cache access; callers hold the lock.
inline bool is_favorite_locked(uint32_t mmsi) {
  const auto &config = shared().config;
  for (uint8_t i = 0; i < config.favorite_count; ++i)
    if (config.favorites[i] == mmsi) return true;
  return false;
}
inline Vessel *find_locked(uint32_t mmsi) {
  auto &state = shared();
  for (size_t i = 0; i < state.vessel_count; ++i)
    if (state.vessels[i].mmsi == mmsi) return &state.vessels[i];
  return nullptr;
}
inline Vessel *upsert_locked(uint32_t mmsi, uint32_t now) {
  auto &state = shared();
  if (Vessel *found = find_locked(mmsi)) {
    found->last_seen_ms = now;
    return found;
  }
  size_t slot = state.vessel_count;
  if (slot >= MAX_VESSELS) {
    // Full: replace the longest-silent vessel that is not a favourite.
    uint32_t oldest_age = 0;
    slot = MAX_VESSELS;
    for (size_t i = 0; i < state.vessel_count; ++i) {
      const uint32_t age = age_ms(now, state.vessels[i].last_seen_ms);
      if (age >= oldest_age && !is_favorite_locked(state.vessels[i].mmsi)) {
        oldest_age = age;
        slot = i;
      }
    }
    if (slot == MAX_VESSELS) return nullptr;
  } else {
    ++state.vessel_count;
  }
  Vessel &vessel = state.vessels[slot];
  vessel = Vessel{};
  vessel.mmsi = mmsi;
  vessel.first_seen_ms = vessel.last_seen_ms = now;
  return &vessel;
}
inline void prune_locked(uint32_t now) {
  auto &state = shared();
  size_t kept = 0;
  for (size_t i = 0; i < state.vessel_count; ++i) {
    if (age_ms(now, state.vessels[i].last_seen_ms) >= EVICT_MS) continue;
    if (kept != i) state.vessels[kept] = state.vessels[i];
    ++kept;
  }
  state.vessel_count = kept;
}

inline bool valid_position(double latitude, double longitude) {
  return std::isfinite(latitude) && std::isfinite(longitude) && std::fabs(latitude) <= 90.0 &&
         std::fabs(longitude) <= 180.0 && !(latitude == 0.0 && longitude == 0.0);
}

inline void merge_position_locked(Vessel &vessel, JsonObjectConst report, bool class_b, uint32_t now) {
  const double latitude = report["Latitude"] | 91.0;
  const double longitude = report["Longitude"] | 181.0;
  if (!valid_position(latitude, longitude)) return;
  vessel.lat_e7 = static_cast<int32_t>(std::lround(latitude * 1e7));
  vessel.lon_e7 = static_cast<int32_t>(std::lround(longitude * 1e7));
  const double sog = report["Sog"] | 102.3;
  vessel.sog_dkn = sog >= 0 && sog < 102.25 ? static_cast<uint16_t>(std::lround(sog * 10.0)) : SOG_UNKNOWN;
  const double cog = report["Cog"] | 360.0;
  vessel.cog_ddeg = cog >= 0 && cog < 360.0 ? static_cast<uint16_t>(std::lround(cog * 10.0)) % 3600 : COG_UNKNOWN;
  const int heading = report["TrueHeading"] | static_cast<int>(HEADING_UNKNOWN);
  vessel.heading = heading >= 0 && heading < 360 ? static_cast<uint16_t>(heading) : HEADING_UNKNOWN;
  if (class_b) {
    vessel.flags |= VESSEL_CLASS_B;
    vessel.nav_status = 15;
  } else {
    vessel.flags &= static_cast<uint8_t>(~VESSEL_CLASS_B);
    vessel.nav_status = static_cast<uint8_t>(std::clamp(report["NavigationalStatus"] | 15, 0, 15));
  }
  vessel.flags |= VESSEL_HAS_POSITION;
  vessel.position_ms = now;
  if (vessel_underway(vessel)) {
    vessel.underway_ms = now;
    vessel.slow_reports = 0;
  } else if (vessel.slow_reports < UINT8_MAX) {
    ++vessel.slow_reports;
  }
}

inline void merge_dimension_locked(Vessel &vessel, JsonObjectConst dimension) {
  const int length = (dimension["A"] | 0) + (dimension["B"] | 0);
  if (length > 0 && length < 1023) vessel.length_m = static_cast<uint16_t>(length);
}

inline void merge_type_locked(Vessel &vessel, int type) {
  if (type > 0 && type < 100) vessel.ship_type = static_cast<uint8_t>(type);
}

inline void merge_name_locked(Vessel &vessel, const char *name) {
  char clean[sizeof(vessel.name)];
  copy_ais_text(clean, sizeof(clean), name);
  if (clean[0] != '\0') std::memcpy(vessel.name, clean, sizeof(clean));
}

enum class MessageResult : uint8_t { IGNORED, VESSEL, CONFIRMED, ERROR };

inline MessageResult handle_message(const char *data, size_t length, uint32_t now) {
  struct PsramAllocator : ArduinoJson::Allocator {
    void *allocate(size_t size) override { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
    void deallocate(void *pointer) override { heap_caps_free(pointer); }
    void *reallocate(void *pointer, size_t size) override {
      return heap_caps_realloc(pointer, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
  };
  static PsramAllocator allocator;
  JsonDocument document(&allocator);
  if (deserializeJson(document, data, length, DeserializationOption::NestingLimit(8)) != DeserializationError::Ok)
    return MessageResult::IGNORED;
  if (!document["error"].isNull()) {
    const char *error = document["error"] | "";
    ESP_LOGW(TAG, "aisstream error: %s", error);
    return MessageResult::ERROR;
  }
  const char *type = document["MessageType"] | "";
  if (std::strcmp(type, "SubscriptionConfirmation") == 0) {
    const bool compressed = document["Message"]["CompressionEnabled"] | false;
    ESP_LOGI(TAG, "Subscription confirmed (compression %s)", compressed ? "on" : "off");
    return MessageResult::CONFIRMED;
  }
  JsonObjectConst meta = document["MetaData"];
  JsonObjectConst body = document["Message"][type];
  const uint32_t mmsi = meta["MMSI"] | 0UL;
  if (body.isNull() || !AirDot::onboarding::ship_radar_mmsi_is_valid(mmsi)) return MessageResult::IGNORED;

  Lock lock;
  if (!lock.locked()) return MessageResult::IGNORED;
  Vessel *vessel = upsert_locked(mmsi, now);
  if (vessel == nullptr) return MessageResult::IGNORED;
  bool moved = false;
  if (std::strcmp(type, "PositionReport") == 0) {
    merge_position_locked(*vessel, body, false, now);
    moved = true;
  } else if (std::strcmp(type, "StandardClassBPositionReport") == 0) {
    merge_position_locked(*vessel, body, true, now);
    moved = true;
  } else if (std::strcmp(type, "ExtendedClassBPositionReport") == 0) {
    merge_position_locked(*vessel, body, true, now);
    merge_name_locked(*vessel, body["Name"] | "");
    merge_type_locked(*vessel, body["Type"] | 0);
    merge_dimension_locked(*vessel, body["Dimension"]);
    moved = true;
  } else if (std::strcmp(type, "ShipStaticData") == 0) {
    merge_name_locked(*vessel, body["Name"] | "");
    merge_type_locked(*vessel, body["Type"] | 0);
    merge_dimension_locked(*vessel, body["Dimension"]);
  } else if (std::strcmp(type, "StaticDataReport") == 0) {
    const bool part_b = body["PartNumber"] | false;
    if (!part_b) {
      JsonObjectConst part = body["ReportA"];
      if (part["Valid"] | false) merge_name_locked(*vessel, part["Name"] | "");
    } else {
      JsonObjectConst part = body["ReportB"];
      if (part["Valid"] | false) {
        merge_type_locked(*vessel, part["ShipType"] | 0);
        merge_dimension_locked(*vessel, part["Dimension"]);
      }
    }
  }
  // MetaData carries the last known name for any message type.
  if (vessel->name[0] == '\0') merge_name_locked(*vessel, meta["ShipName"] | "");
  if (moved) shared().revision.fetch_add(1, std::memory_order_relaxed);
  return MessageResult::VESSEL;
}

inline void stream_task(void *) {
  using AirDot::connectivity::RetryPolicy;
  using AirDot::connectivity::RetryState;
  auto &state = shared();
  ws::Socket socket;
  RetryState retry;
  const RetryPolicy policy{5000, 300000, 20};
  Config active{};
  bool deflate_disabled = false, confirmed = false;
  uint32_t connect_ms = 0, last_rx_ms = 0, last_ping_ms = 0, last_subscribe_ms = 0, last_prune_ms = 0;
  uint32_t auth_blocked_until_ms = 0, confirmed_ms = 0, deflate_disabled_ms = 0;
  bool auth_blocked = false;
  uint8_t unconfirmed_closes = 0, messages_since_yield = 0;
  char auth_blocked_key[sizeof(active.key)]{};
  char connected_key[sizeof(active.key)]{};
  char subscription[512];

  auto disconnect = [&](bool polite) {
    if (socket.connected() && polite) socket.send_close();
    socket.close();
    confirmed = false;
    state.subscribed_since_ms.store(0, std::memory_order_release);
  };
  // server_closed: the server dropped a connection whose subscription it had
  // not confirmed, or sent an error message before confirming it; that is
  // how aisstream answers an invalid key. Timeouts and local errors never
  // count towards a key rejection.
  auto fail = [&](const char *reason, bool server_closed = false) {
    const bool was_confirmed = confirmed;
    disconnect(false);
    const uint32_t now = esphome::millis();
    if (!was_confirmed && server_closed && connect_ms != 0 && ++unconfirmed_closes >= 2) {
      ESP_LOGW(TAG, "Stream closed before confirming the subscription; check the API key");
      std::memcpy(auth_blocked_key, active.key, sizeof(auth_blocked_key));
      auth_blocked = true;
      auth_blocked_until_ms = now + AUTH_RETRY_MS;
      unconfirmed_closes = 0;
      set_status(StreamStatus::KEY_REJECTED, auth_blocked_until_ms);
      return;
    }
    // Only a connection that stayed up for a while resets the backoff, so a
    // server that confirms and then drops us does not cause a reconnect storm.
    if (was_confirmed && age_ms(now, confirmed_ms) >= STABLE_CONNECTION_MS) retry.reset();
    const uint32_t delay = retry.record_failure(now, policy, esp_random());
    ESP_LOGW(TAG, "Stream %s; retrying in %u s", reason, static_cast<unsigned>(delay / 1000));
    set_status(StreamStatus::RETRY_WAIT, now + delay);
  };
  auto send_subscription = [&]() {
    const int length = build_subscription(subscription, sizeof(subscription), active.key, active.latitude,
                                          active.longitude, active.range_km);
    last_subscribe_ms = esphome::millis();
    return length > 0 && static_cast<size_t>(length) < sizeof(subscription) &&
           socket.send_text(subscription, static_cast<size_t>(length));
  };

  for (;;) {
    {
      Lock lock(50);
      if (lock.locked()) active = state.config;
    }
    const uint32_t now = esphome::millis();
    if (now - last_prune_ms >= 60000UL) {
      Lock lock(50);
      if (lock.locked()) {
        prune_locked(now);
        last_prune_ms = now;
      }
    }

    StreamStatus idle = StreamStatus::CONNECTING;
    if (!active.enabled) idle = StreamStatus::DISABLED;
    else if (active.key[0] == '\0') idle = StreamStatus::NO_KEY;
    else if (!active.location_valid) idle = StreamStatus::NO_LOCATION;
    else if (!active.runtime_ok || !esphome::network::is_connected()) idle = StreamStatus::OFFLINE;
    else if (auth_blocked) {
      if (std::strcmp(auth_blocked_key, active.key) != 0 || static_cast<int32_t>(now - auth_blocked_until_ms) >= 0)
        auth_blocked = false;  // New key, or the block expired: try again.
      else
        idle = StreamStatus::KEY_REJECTED;
    }
    if (deflate_disabled && age_ms(now, deflate_disabled_ms) >= DEFLATE_RETRY_MS) deflate_disabled = false;
    if (idle != StreamStatus::CONNECTING) {
      if (socket.connected()) disconnect(true);
      if (idle != StreamStatus::KEY_REJECTED) {
        retry.reset();
        unconfirmed_closes = 0;
      }
      set_status(idle, idle == StreamStatus::KEY_REJECTED ? auth_blocked_until_ms : 0);
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (!socket.connected()) {
      if (!retry.due(now)) {
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
      set_status(StreamStatus::CONNECTING);
      const auto error = socket.connect(STREAM_HOST, STREAM_PATH, !deflate_disabled);
      if (error != ws::ConnectError::NONE) {
        connect_ms = 0;
        fail(error == ws::ConnectError::TLS ? "TLS connection failed" : "handshake failed");
        continue;
      }
      connect_ms = last_rx_ms = last_ping_ms = esphome::millis();
      std::memcpy(connected_key, active.key, sizeof(connected_key));
      if (!send_subscription()) {
        fail("subscription send failed");
        continue;
      }
      state.subscribed_generation.store(active.generation, std::memory_order_release);
      ESP_LOGD(TAG, "Connected (deflate %s)", socket.deflate() ? "on" : "off");
    }

    // Key changes need a new connection; location changes only a new subscription.
    if (std::strcmp(active.key, connected_key) != 0) {
      disconnect(true);
      retry.reset();
      continue;
    }
    if (active.generation != state.subscribed_generation.load(std::memory_order_acquire) &&
        now - last_subscribe_ms >= RESUBSCRIBE_MIN_MS) {
      if (!send_subscription()) {
        fail("resubscribe failed");
        continue;
      }
      state.subscribed_generation.store(active.generation, std::memory_order_release);
      if (confirmed) state.subscribed_since_ms.store(std::max<uint32_t>(1, now), std::memory_order_release);
    }

    const char *message = nullptr;
    size_t length = 0;
    const auto result = socket.poll(message, length, last_rx_ms);
    const uint32_t after = esphome::millis();
    if (result == ws::Result::OK) {
      const auto handled = handle_message(message, length, after);
      if (handled == MessageResult::CONFIRMED) {
        confirmed = true;
        confirmed_ms = after;
        unconfirmed_closes = 0;
        auth_blocked = false;
        state.subscribed_since_ms.store(std::max<uint32_t>(1, after), std::memory_order_release);
        set_status(StreamStatus::CONNECTED);
      } else if (handled == MessageResult::ERROR) {
        fail("reported an error", !confirmed);
      }
      // Busy areas can deliver hundreds of messages a second; let other
      // tasks on this core run.
      if (++messages_since_yield >= MESSAGES_PER_YIELD) {
        messages_since_yield = 0;
        vTaskDelay(1);
      }
      continue;
    }
    if (result == ws::Result::INFLATE) {
      ESP_LOGW(TAG, "Compressed message could not be decoded; reconnecting without compression");
      deflate_disabled = true;
      deflate_disabled_ms = esphome::millis();
      disconnect(false);
      continue;
    }
    if (result != ws::Result::NONE) {
      fail(result == ws::Result::PROTOCOL ? "protocol error" : result == ws::Result::ERROR ? "read error" : "closed",
           result == ws::Result::CLOSED);
      continue;
    }
    if (!confirmed && after - connect_ms >= CONFIRM_TIMEOUT_MS) {
      fail("subscription not confirmed");
      continue;
    }
    if (after - last_rx_ms >= RX_TIMEOUT_MS) {
      fail("timed out");
      continue;
    }
    if (after - last_rx_ms >= PING_INTERVAL_MS && after - last_ping_ms >= PING_INTERVAL_MS) {
      last_ping_ms = after;
      if (!socket.send_ping()) fail("ping failed");
    }
  }
}

inline bool ensure_started() {
  auto &state = shared();
  if (state.task_started.load(std::memory_order_acquire)) return true;
  if (state.mutex == nullptr) state.mutex = xSemaphoreCreateMutex();
  if (state.mutex == nullptr) return false;
  if (state.vessels == nullptr) {
    state.vessels = static_cast<Vessel *>(
        heap_caps_calloc(MAX_VESSELS, sizeof(Vessel), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (state.vessels == nullptr) return false;
  }
  if (state.render_scratch == nullptr) {
    state.render_scratch = static_cast<Vessel *>(
        heap_caps_calloc(MAX_VESSELS, sizeof(Vessel), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (state.render_scratch == nullptr) return false;
  }
  // The stack can live in PSRAM: the task never writes flash.
  // Core 0 keeps message floods away from the ESPHome loop on core 1.
  BaseType_t created = xTaskCreatePinnedToCoreWithCaps(stream_task, "ship_ais", TASK_STACK_BYTES, nullptr, 1,
                                                       nullptr, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (created != pdPASS)
    created = xTaskCreatePinnedToCore(stream_task, "ship_ais", TASK_STACK_BYTES, nullptr, 1, nullptr, 0);
  if (created != pdPASS) return false;
  state.task_started.store(true, std::memory_order_release);
  return true;
}

}  // namespace detail

inline StreamStatus stream_status() {
  return static_cast<StreamStatus>(detail::shared().status.load(std::memory_order_acquire));
}
inline uint32_t vessel_revision() { return detail::shared().revision.load(std::memory_order_relaxed); }

// Called from the main loop about once a second. Reads the key and favourites
// from settings; a change of key, location or range bumps the generation,
// which makes the task resubscribe (or reconnect for a new key).
inline void configure_stream(bool enabled, bool runtime_ok, bool location_valid, float latitude, float longitude,
                             uint8_t range_km) {
  auto &state = detail::shared();
  if (!enabled && !state.task_started.load(std::memory_order_acquire)) {
    detail::set_status(StreamStatus::DISABLED);
    return;
  }
  if (!detail::ensure_started()) return;
  const char *key = AirDot::onboarding::load_ship_radar_api_key();
  const auto &favorites = AirDot::onboarding::load_ship_radar_favorites();
  detail::Lock lock(10);
  if (!lock.locked()) return;
  auto &config = state.config;
  const bool changed = config.enabled != enabled || config.location_valid != location_valid ||
                       std::strcmp(config.key, key) != 0 || config.range_km != range_km ||
                       radar_map::coordinate_key(config.latitude) != radar_map::coordinate_key(latitude) ||
                       radar_map::coordinate_key(config.longitude) != radar_map::coordinate_key(longitude);
  config.enabled = enabled;
  config.runtime_ok = runtime_ok;
  config.location_valid = location_valid;
  std::snprintf(config.key, sizeof(config.key), "%s", key);
  config.latitude = latitude;
  config.longitude = longitude;
  config.range_km = range_km;
  config.favorite_count = std::min<uint8_t>(favorites.count, MAX_FAVORITES);
  std::memcpy(config.favorites, favorites.mmsi, sizeof(config.favorites));
  if (changed) ++config.generation;
}


inline constexpr uint8_t VESSEL_FAVORITE_SNAPSHOT = 1U << 7;  // Only set in the render snapshot.

// Fills the vessels to draw: fresh positions inside the configured range that
// pass the category filter (favourites always pass). When more qualify than
// fit, favourites come first, then named vessels, then unnamed ones, each
// group nearest first. Stationary vessels are hidden unless they are
// favourites.
// Main loop only: the lock is held just to copy fresh entries into a PSRAM
// snapshot; projection and sorting run without it.
inline bool copy_render_list(RenderList &out, float latitude, float longitude, uint8_t range_km, uint8_t category_mask,
                             uint32_t now) {
  out.count = 0;
  auto &state = detail::shared();
  if (state.vessels == nullptr || state.render_scratch == nullptr) return false;
  size_t snapshot_count = 0;
  {
    detail::Lock lock(10);
    if (!lock.locked()) return false;
    for (size_t i = 0; i < state.vessel_count; ++i) {
      const Vessel &vessel = state.vessels[i];
      if (!(vessel.flags & VESSEL_HAS_POSITION)) continue;
      const uint32_t stale_ms = vessel_underway(vessel) ? DRAW_STALE_MOVING_MS : DRAW_STALE_MS;
      if (age_ms(now, vessel.position_ms) >= stale_ms) continue;
      Vessel &copy = state.render_scratch[snapshot_count++];
      copy = vessel;
      if (detail::is_favorite_locked(vessel.mmsi)) copy.flags |= VESSEL_FAVORITE_SNAPSHOT;
    }
  }

  radar_map::Projection projection(latitude, longitude, range_km);
  struct Candidate { uint16_t index; uint8_t group; float distance; float x, y; };
  static Candidate *candidates = static_cast<Candidate *>(
      heap_caps_malloc(MAX_VESSELS * sizeof(Candidate), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (candidates == nullptr) return false;
  size_t count = 0;
  for (size_t i = 0; i < snapshot_count; ++i) {
    const Vessel &vessel = state.render_scratch[i];
    const bool favorite = vessel.flags & VESSEL_FAVORITE_SNAPSHOT;
    if (!favorite) {
      const bool type_pending = vessel.ship_type == 0 && age_ms(now, vessel.first_seen_ms) < UNKNOWN_TYPE_GRACE_MS;
      if (!type_pending && !(category_for_type(vessel.ship_type) & category_mask)) continue;
      if (!vessel_shown_underway(vessel, now)) continue;  // Only favourites are shown when stationary.
    }
    // Drawn at the last reported position: reports can be ~15 min apart and
    // coastal traffic turns often, so extrapolating along COG drifts badly.
    const double vessel_latitude = vessel.lat_e7 / 1e7, vessel_longitude = vessel.lon_e7 / 1e7;
    double x = 0, y = 0;
    if (!projection.project(vessel_latitude, vessel_longitude, x, y)) continue;
    const double distance = std::sqrt(x * x + y * y);
    if (distance > radar_map::NORMALIZED_RADIUS) continue;
    const uint8_t group = favorite ? 0 : vessel.name[0] ? 1 : 2;
    candidates[count++] = Candidate{static_cast<uint16_t>(i), group,
                                    static_cast<float>(distance), static_cast<float>(x), static_cast<float>(y)};
  }
  const size_t keep = std::min(count, MAX_RENDER);
  std::partial_sort(candidates, candidates + keep, candidates + count, [](const Candidate &a, const Candidate &b) {
    return a.group != b.group ? a.group < b.group : a.distance < b.distance;
  });
  // Label priority among named vessels: favourites, then the largest or
  // fastest, nearest first on ties.
  float scores[MAX_RENDER];
  uint8_t label_order[MAX_RENDER];
  uint8_t label_candidates = 0;
  for (size_t i = 0; i < keep; ++i) {
    const Vessel &vessel = state.render_scratch[candidates[i].index];
    RenderVessel &render = out.vessels[out.count++];
    render = RenderVessel{};
    render.x = candidates[i].x;
    render.y = candidates[i].y;
    render.favorite = candidates[i].group == 0;
    render.sog_kn = vessel.sog_dkn != SOG_UNKNOWN ? vessel.sog_dkn / 10.0f : NAN;
    render.moving = std::isfinite(render.sog_kn) && render.sog_kn >= DRAW_MOVING_SOG_KN;
    if (vessel.heading != HEADING_UNKNOWN) render.heading_deg = vessel.heading;
    else if (vessel.cog_ddeg != COG_UNKNOWN && render.moving) render.heading_deg = vessel.cog_ddeg / 10.0f;
    std::memcpy(render.name, vessel.name, sizeof(render.name));
    if (render.name[0] == '\0') continue;
    const float speed = std::isfinite(render.sog_kn) ? render.sog_kn : 0.0f;
    scores[i] = (render.favorite ? 1000.0f : 0.0f) +
                std::max(vessel.length_m / LABEL_REFERENCE_LENGTH_M, speed / LABEL_REFERENCE_SOG_KN);
    label_order[label_candidates++] = static_cast<uint8_t>(i);
  }
  const uint8_t labels = std::min<uint8_t>(label_candidates, MAX_LABELS);
  std::partial_sort(label_order, label_order + labels, label_order + label_candidates, [&](uint8_t a, uint8_t b) {
    return scores[a] != scores[b] ? scores[a] > scores[b] : candidates[a].distance < candidates[b].distance;
  });
  for (uint8_t n = 0; n < labels; ++n) {
    RenderVessel &render = out.vessels[label_order[n]];
    render.labelled = true;
    render.label_rank = n;
  }
  return true;
}

namespace detail {
enum class Motion : uint8_t { UNKNOWN, ABSENT, STOPPED, MOVING };
struct FavoriteState {
  uint32_t mmsi{0};
  Motion motion{Motion::UNKNOWN};
  Motion before_absent{Motion::UNKNOWN};  // State it was in when it went ABSENT.
  uint32_t since_ms{0};           // When the current state began.
  uint32_t candidate_ms{0};       // First report at >= MOVING_SOG_KN, 0 if none.
  uint32_t last_position_ms{0};   // Last report seen for this favourite.
  uint32_t alerted_ms{0};
  bool armed{true};
};
struct FavoriteTracker {
  FavoriteState states[MAX_FAVORITES]{};
  uint8_t count{0};
  uint32_t generation{0};
};
inline FavoriteTracker &tracker() {
  static FavoriteTracker value;
  return value;
}
inline void set_motion(FavoriteState &favorite, Motion motion, uint32_t now) {
  if (favorite.motion == motion) return;
  if (motion == Motion::ABSENT) favorite.before_absent = favorite.motion;
  favorite.motion = motion;
  favorite.since_ms = now;
}
}  // namespace detail

// Runs the favourite state machine; call once a second from the main loop.
// Returns true when a favourite went from stopped (for at least 5 minutes) or
// not detected (with the stream healthy for 20+ minutes) to moving, and
// alerts are allowed. A vessel first seen moving (after a boot, reconnect or
// settings change) never alerts.
inline bool poll_favorite_alert(uint32_t now, bool allowed) {
  using detail::Motion;
  auto &state = detail::shared();
  auto &tracker = detail::tracker();
  if (state.vessels == nullptr || state.mutex == nullptr) return false;
  detail::Lock lock(10);
  if (!lock.locked()) return false;
  const auto &config = state.config;

  // Keep one state per favourite; any configuration change starts over.
  bool same_list = tracker.count == config.favorite_count && tracker.generation == config.generation;
  for (uint8_t i = 0; same_list && i < tracker.count; ++i) same_list = tracker.states[i].mmsi == config.favorites[i];
  if (!same_list) {
    tracker = detail::FavoriteTracker{};
    tracker.count = config.favorite_count;
    tracker.generation = config.generation;
    for (uint8_t i = 0; i < tracker.count; ++i) {
      tracker.states[i].mmsi = config.favorites[i];
      tracker.states[i].since_ms = now;
    }
  }

  const uint32_t subscribed_since = state.subscribed_since_ms.load(std::memory_order_acquire);
  const bool connected = stream_status() == StreamStatus::CONNECTED && subscribed_since != 0 &&
                         state.subscribed_generation.load(std::memory_order_acquire) == config.generation;
  const bool established = connected && age_ms(now, subscribed_since) >= ABSENCE_ESTABLISH_MS;
  bool fired = false;

  for (uint8_t i = 0; i < tracker.count; ++i) {
    auto &favorite = tracker.states[i];
    const Vessel *vessel = detail::find_locked(favorite.mmsi);
    const bool has_position = vessel != nullptr && (vessel->flags & VESSEL_HAS_POSITION);
    const bool fresh = has_position && age_ms(now, vessel->position_ms) < ABSENCE_ESTABLISH_MS;
    const Motion previous = favorite.motion;
    const uint32_t previous_since = favorite.since_ms;

    if (fresh && vessel->position_ms != favorite.last_position_ms) {
      favorite.last_position_ms = vessel->position_ms;
      const float sog = vessel->sog_dkn != SOG_UNKNOWN ? vessel->sog_dkn / 10.0f : NAN;
      if (std::isfinite(sog)) {
        if (sog >= MOVING_FAST_SOG_KN) {
          detail::set_motion(favorite, Motion::MOVING, now);
        } else if (sog >= MOVING_SOG_KN) {
          if (favorite.candidate_ms == 0) favorite.candidate_ms = vessel->position_ms;
          else if (age_ms(vessel->position_ms, favorite.candidate_ms) >= MOVING_CONFIRM_MS)
            detail::set_motion(favorite, Motion::MOVING, now);
        } else {
          favorite.candidate_ms = 0;
          if (sog < STOPPED_SOG_KN) detail::set_motion(favorite, Motion::STOPPED, now);
          else if (favorite.motion == Motion::ABSENT || favorite.motion == Motion::UNKNOWN)
            detail::set_motion(favorite, Motion::UNKNOWN, now);  // Drifting: no basis for either state.
        }
        if (sog < MOVING_SOG_KN) favorite.candidate_ms = 0;
      }
    } else if (!fresh) {
      favorite.candidate_ms = 0;
      if (established) {
        detail::set_motion(favorite, Motion::ABSENT, now);
      } else if (!connected) {
        if (favorite.motion == Motion::ABSENT ||
            (favorite.motion == Motion::STOPPED &&
             (!has_position || age_ms(now, vessel->position_ms) >= STOPPED_KEEP_OFFLINE_MS)))
          detail::set_motion(favorite, Motion::UNKNOWN, now);
      }
    }

    // Re-arm after a quiet period stopped or absent.
    if (!favorite.armed && (favorite.motion == Motion::STOPPED || favorite.motion == Motion::ABSENT) &&
        age_ms(now, favorite.since_ms) >= REARM_QUIET_MS && age_ms(now, favorite.alerted_ms) >= REARM_MIN_MS)
      favorite.armed = true;

    // Back from ABSENT only counts if it wasn't already moving before the gap
    // (an AIS coverage gap on a vessel under way is not a departure).
    const bool departed = favorite.motion == Motion::MOVING && previous != Motion::MOVING &&
                          ((previous == Motion::ABSENT && favorite.before_absent != Motion::MOVING) ||
                           (previous == Motion::STOPPED && age_ms(now, previous_since) >= STOPPED_MIN_MS));
    if (departed && favorite.armed && allowed && !fired) {
      favorite.armed = false;
      favorite.alerted_ms = now;
      fired = true;
    }
  }
  return fired;
}

#else  // !USE_ESP32
inline StreamStatus stream_status() { return StreamStatus::DISABLED; }
inline uint32_t vessel_revision() { return 0; }
inline void configure_stream(bool, bool, bool, float, float, uint8_t) {}
inline bool copy_render_list(RenderList &out, float, float, uint8_t, uint8_t, uint32_t) { out.count = 0; return false; }
inline bool poll_favorite_alert(uint32_t, bool) { return false; }
#endif

}  // namespace AirDot::ship_radar
