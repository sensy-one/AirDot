#pragma once

#include "radar_map_cache.h"
#include "radar_tile_client.h"

#ifdef USE_ESP32
#include <algorithm>
#include <atomic>
#include <cstring>
#include <ctime>
#include <new>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "flight_radar_client.h"
#include "weather_client.h"

namespace AirDot::radar_map {

struct DownloadProgress { uint8_t percentage{0}; };

namespace detail {
inline constexpr uint32_t CACHE_LIFETIME_MS = 7UL * 24UL * 60UL * 60UL * 1000UL;
enum class TaskState : uint8_t { IDLE, RUNNING, SUCCESS, FAILED };
struct Request { float latitude{0}, longitude{0}; uint8_t range{25}; };

// One independent map pipeline per MapKind: the road map (flight and rain
// radar) and the marine map (ship radar) never replace each other.
struct KindState {
  Request wanted, in_flight, cache_probe_request;
  std::atomic<TaskState> task_state{TaskState::IDLE};
  std::atomic<Status> status{Status::EMPTY};
  std::atomic<MapData *> pending{nullptr};
  std::atomic<uint8_t> percentage{0};
  bool unavailable{false}, current_restored{false}, cache_probe_done{false};
  MapData *current{nullptr};
  uint32_t revision{0}, received_ms{0}, next_retry_ms{0};
  uint8_t failure_count{0}, cache_latest_probe_range{0};
  uint64_t current_created_unix{0}, pending_created_unix{0};
};
inline KindState &st(MapKind kind) {
  static KindState states[MAP_KIND_COUNT];
  return states[static_cast<size_t>(kind) < MAP_KIND_COUNT ? static_cast<size_t>(kind) : 0];
}
inline SemaphoreHandle_t mutex(MapKind kind) {
  static SemaphoreHandle_t handles[MAP_KIND_COUNT] = {xSemaphoreCreateMutex(), xSemaphoreCreateMutex()};
  return handles[static_cast<size_t>(kind) < MAP_KIND_COUNT ? static_cast<size_t>(kind) : 0];
}

class Lock {
 public:
  explicit Lock(MapKind kind, uint32_t timeout = 2) : handle_(mutex(kind)) {
    locked_ = handle_ && xSemaphoreTake(handle_, pdMS_TO_TICKS(timeout)) == pdTRUE;
  }
  ~Lock() { if (locked_) xSemaphoreGive(handle_); }
  bool acquired() const { return locked_; }
 private:
  SemaphoreHandle_t handle_;
  bool locked_{false};
};

inline bool same_request(const Request &a, const Request &b) {
  return a.range == b.range && coordinate_key(a.latitude) == coordinate_key(b.latitude) &&
         coordinate_key(a.longitude) == coordinate_key(b.longitude);
}
inline MapData *allocate(MapKind kind) {
  void *memory = heap_caps_malloc(sizeof(MapData), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) return nullptr;
  auto *map = new (memory) MapData{};
  map->kind = kind;
  return map;
}
inline void destroy(MapData *map) {
  if (map) { map->~MapData(); heap_caps_free(map); }
}
inline uint64_t unix_now() {
  const std::time_t value = std::time(nullptr);
  return value >= 1577836800 ? static_cast<uint64_t>(value) : 0;
}
inline bool fetch_requested_map(MapKind kind) {
  auto &state = st(kind);
  MapData *map = allocate(kind);
  if (!map) return false;
  const auto request = state.in_flight;
  const auto result = tiles::fetch_map(*map, request.latitude, request.longitude, request.range,
      [&state](const tiles::Progress &progress) {
        state.percentage.store(std::max(state.percentage.load(), static_cast<uint8_t>(std::min<unsigned>(99, progress.percent))));
      });
  if (!result.ok || map->truncated) {
    state.unavailable = map->truncated || result.partial || std::strcmp(result.error, "outside_coverage") == 0 ||
                        std::strcmp(result.error, "budget_exhausted") == 0;
    destroy(map);
    return false;
  }
  state.pending_created_unix = unix_now();
  persistent_cache::save(*map, state.pending_created_unix);
  state.pending.store(map, std::memory_order_release);
  return true;
}
inline void worker(void *argument) {
  const auto kind = static_cast<MapKind>(reinterpret_cast<uintptr_t>(argument));
  const bool ok = fetch_requested_map(kind);
  AirDot::time_weather::release_weather_http_request_slot_();
  st(kind).task_state.store(ok ? TaskState::SUCCESS : TaskState::FAILED, std::memory_order_release);
  vTaskDelete(nullptr);
}
inline bool any_task_running() {
  for (size_t i = 0; i < MAP_KIND_COUNT; ++i)
    if (st(static_cast<MapKind>(i)).task_state.load(std::memory_order_acquire) == TaskState::RUNNING) return true;
  return false;
}
inline bool install_restored(MapKind kind, MapData *map, const protocol::Metadata &metadata) {
  auto &state = st(kind);
  Lock lock(kind, 10);
  if (!lock.acquired()) { destroy(map); return false; }
  MapData *old = state.current;
  state.current = map;
  ++state.revision;
  destroy(old);
  state.current_created_unix = metadata.created_unix;
  state.current_restored = true;
  state.received_ms = esphome::millis();
  state.status.store(Status::READY);
  return true;
}
inline bool restore_for_key(MapKind kind, float latitude, float longitude, uint8_t range) {
  auto &state = st(kind);
  if (state.task_state.load(std::memory_order_acquire) != TaskState::IDLE) return false;
  const Request request{latitude, longitude, range};
  if (state.cache_probe_done && same_request(state.cache_probe_request, request)) return false;
  MapData *map = allocate(kind);
  if (!map) return false;
  protocol::Metadata metadata;
  const auto result = persistent_cache::load(latitude, longitude, range, *map, &metadata);
  if (result != persistent_cache::Result::BUSY) {
    state.cache_probe_request = request;
    state.cache_probe_done = true;
  }
  if (result != persistent_cache::Result::HIT) { destroy(map); return false; }
  return install_restored(kind, map, metadata);
}
inline bool current_is_fresh(MapKind kind, uint32_t now) {
  auto &state = st(kind);
  if (now - state.received_ms >= CACHE_LIFETIME_MS) return false;
  const uint64_t utc = unix_now(), created = state.current_created_unix;
  if (state.current_restored && utc && created)
    return created <= utc + 300 && (created > utc || utc - created < CACHE_LIFETIME_MS / 1000UL);
  return true;
}
}  // namespace detail

inline Status status(MapKind kind = MapKind::ROADS) { return detail::st(kind).status.load(std::memory_order_acquire); }
inline uint32_t map_revision(MapKind kind = MapKind::ROADS) { return detail::st(kind).revision; }
inline Status status_for(float latitude, float longitude, uint8_t range, MapKind kind = MapKind::ROADS) {
  if (!esphome::network::is_connected()) return Status::OFFLINE;
  const auto &state = detail::st(kind);
  const detail::Request request{latitude, longitude, range};
  if (state.task_state.load(std::memory_order_acquire) == detail::TaskState::RUNNING &&
      detail::same_request(state.in_flight, request)) return Status::LOADING;
  return detail::same_request(state.wanted, request) ? status(kind) : Status::EMPTY;
}
inline bool progress_for_key(float latitude, float longitude, uint8_t range, DownloadProgress &out,
                             MapKind kind = MapKind::ROADS) {
  const auto &state = detail::st(kind);
  const auto task = state.task_state.load(std::memory_order_acquire);
  if (task == detail::TaskState::IDLE || task == detail::TaskState::FAILED ||
      !detail::same_request(state.in_flight, detail::Request{latitude, longitude, range})) return false;
  out.percentage = state.percentage.load();
  return true;
}
inline bool has_map(float latitude, float longitude, uint8_t range, MapKind kind = MapKind::ROADS) {
  if (!valid_coordinates(latitude, longitude)) return false;
  detail::Lock lock(kind);
  const auto *current = detail::st(kind).current;
  return lock.acquired() && current && matches(*current, latitude, longitude, range);
}
template<typename Callback>
bool with_map(float latitude, float longitude, uint8_t range, Callback callback, MapKind kind = MapKind::ROADS) {
  if (!valid_coordinates(latitude, longitude)) return false;
  detail::Lock lock(kind);
  const auto *current = detail::st(kind).current;
  if (!lock.acquired() || !current || !matches(*current, latitude, longitude, range)) return false;
  callback(*current);
  return true;
}
inline bool restore_cached_map(float latitude, float longitude, uint8_t range, MapKind kind = MapKind::ROADS) {
  return !has_map(latitude, longitude, range, kind) && detail::restore_for_key(kind, latitude, longitude, range);
}
inline bool cached_location_for_range(uint8_t range, float &latitude, float &longitude, MapKind kind = MapKind::ROADS) {
  auto &state = detail::st(kind);
  {
    detail::Lock lock(kind);
    if (lock.acquired() && state.current && state.current->range_km == range) {
      latitude = state.current->center_latitude;
      longitude = state.current->center_longitude;
      return true;
    }
  }
  if (state.task_state.load(std::memory_order_acquire) != detail::TaskState::IDLE ||
      state.cache_latest_probe_range == range) return false;
  MapData *map = detail::allocate(kind);
  if (!map) return false;
  protocol::Metadata metadata;
  const auto result = persistent_cache::load_latest(range, *map, &metadata);
  if (result != persistent_cache::Result::BUSY) state.cache_latest_probe_range = range;
  if (result != persistent_cache::Result::HIT) { detail::destroy(map); return false; }
  latitude = map->center_latitude;
  longitude = map->center_longitude;
  return detail::install_restored(kind, map, metadata);
}
inline bool consume_result(MapKind kind = MapKind::ROADS) {
  auto &state = detail::st(kind);
  const auto task = state.task_state.load(std::memory_order_acquire);
  if (task != detail::TaskState::SUCCESS && task != detail::TaskState::FAILED) return false;
  bool accepted = false;
  if (task == detail::TaskState::SUCCESS) {
    detail::Lock lock(kind, 10);
    if (!lock.acquired()) return false;
    MapData *map = state.pending.exchange(nullptr, std::memory_order_acq_rel);
    if (map && matches(*map, state.wanted.latitude, state.wanted.longitude, state.wanted.range)) {
      MapData *old = state.current;
      state.current = map;
      ++state.revision;
      detail::destroy(old);
      state.received_ms = esphome::millis();
      state.failure_count = 0;
      state.next_retry_ms = 0;
      state.current_created_unix = state.pending_created_unix;
      state.current_restored = false;
      state.status.store(Status::READY);
      accepted = true;
    } else detail::destroy(map);
  } else if (detail::same_request(state.wanted, state.in_flight)) {
    const uint32_t delay = std::min<uint32_t>(60000UL << std::min<unsigned>(state.failure_count, 4), 900000UL);
    if (state.failure_count < 4) ++state.failure_count;
    state.next_retry_ms = esphome::millis() + delay;
    state.status.store(state.unavailable ? Status::UNAVAILABLE : Status::FAILED);
  }
  state.task_state.store(detail::TaskState::IDLE, std::memory_order_release);
  return accepted || task == detail::TaskState::FAILED;
}
inline bool request_map(float latitude, float longitude, uint8_t range, bool allow_network = true,
                        MapKind kind = MapKind::ROADS) {
  if (!valid_coordinates(latitude, longitude) || range < 5 || range > 50) return false;
  auto &state = detail::st(kind);
  const detail::Request request{latitude, longitude, range};
  if (!detail::same_request(request, state.wanted)) {
    state.wanted = request;
    state.failure_count = 0;
    state.next_retry_ms = 0;
    state.status.store(Status::EMPTY);
  }
  bool restored = false;
  if (!has_map(latitude, longitude, range, kind)) restored = detail::restore_for_key(kind, latitude, longitude, range);
  const uint32_t now = esphome::millis();
  if (has_map(latitude, longitude, range, kind) && detail::current_is_fresh(kind, now)) {
    state.status.store(Status::READY);
    return restored;
  }
  if (status(kind) == Status::UNAVAILABLE ||
      state.task_state.load(std::memory_order_acquire) != detail::TaskState::IDLE || !allow_network) return restored;
  if (!esphome::network::is_connected()) {
    state.status.store(has_map(latitude, longitude, range, kind) ? Status::READY : Status::OFFLINE);
    return restored;
  }
  if (state.next_retry_ms && static_cast<int32_t>(now - state.next_retry_ms) < 0) return false;
  // Map downloads of both kinds are serialised: each holds ~1.2 MB of PSRAM.
  if (detail::any_task_running()) return restored;
  if (AirDot::flight_radar::request_running() || AirDot::time_weather::http_request_active()) return false;
  if (!AirDot::time_weather::acquire_weather_http_request_slot_(AirDot::connectivity::Service::FLIGHT_RADAR)) return false;
  state.in_flight = request;
  state.unavailable = false;
  state.percentage.store(0);
  state.task_state.store(detail::TaskState::RUNNING, std::memory_order_release);
  state.status.store(Status::LOADING);
  if (xTaskCreate(detail::worker, kind == MapKind::MARINE ? "radar_marine" : "radar_map", 12288,
                  reinterpret_cast<void *>(static_cast<uintptr_t>(kind)), 1, nullptr) != pdPASS) {
    AirDot::time_weather::release_weather_http_request_slot_();
    state.task_state.store(detail::TaskState::FAILED, std::memory_order_release);
    return false;
  }
  return true;
}

}  // namespace AirDot::radar_map
#endif
