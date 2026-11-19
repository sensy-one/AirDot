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
inline Request &wanted() { static Request value; return value; }
inline Request &in_flight() { static Request value; return value; }
inline std::atomic<TaskState> &task_state() { static std::atomic<TaskState> value{TaskState::IDLE}; return value; }
inline std::atomic<Status> &status_storage() { static std::atomic<Status> value{Status::EMPTY}; return value; }
inline std::atomic<MapData *> &pending() { static std::atomic<MapData *> value{nullptr}; return value; }
inline std::atomic<uint8_t> &percentage() { static std::atomic<uint8_t> value{0}; return value; }
inline bool &unavailable() { static bool value{false}; return value; }
inline MapData *&current() { static MapData *value = nullptr; return value; }
inline uint32_t &revision() { static uint32_t value = 0; return value; }
inline uint32_t &received_ms() { static uint32_t value = 0; return value; }
inline uint32_t &next_retry_ms() { static uint32_t value = 0; return value; }
inline uint8_t &failure_count() { static uint8_t value = 0; return value; }
inline uint64_t &current_created_unix() { static uint64_t value = 0; return value; }
inline uint64_t &pending_created_unix() { static uint64_t value = 0; return value; }
inline bool &current_restored() { static bool value = false; return value; }
inline Request &cache_probe_request() { static Request value; return value; }
inline bool &cache_probe_done() { static bool value = false; return value; }
inline uint8_t &cache_latest_probe_range() { static uint8_t value = 0; return value; }
inline SemaphoreHandle_t mutex() { static SemaphoreHandle_t value = xSemaphoreCreateMutex(); return value; }

class Lock {
 public:
  explicit Lock(uint32_t timeout = 2) {
    const auto handle = mutex();
    locked_ = handle && xSemaphoreTake(handle, pdMS_TO_TICKS(timeout)) == pdTRUE;
  }
  ~Lock() { if (locked_) xSemaphoreGive(mutex()); }
  bool acquired() const { return locked_; }
 private:
  bool locked_{false};
};

inline bool same_request(const Request &a, const Request &b) {
  return a.range == b.range && coordinate_key(a.latitude) == coordinate_key(b.latitude) &&
         coordinate_key(a.longitude) == coordinate_key(b.longitude);
}
inline MapData *allocate() {
  void *memory = heap_caps_malloc(sizeof(MapData), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return memory ? new (memory) MapData{} : nullptr;
}
inline void destroy(MapData *map) {
  if (map) { map->~MapData(); heap_caps_free(map); }
}
inline uint64_t unix_now() {
  const std::time_t value = std::time(nullptr);
  return value >= 1577836800 ? static_cast<uint64_t>(value) : 0;
}
inline bool fetch_requested_map() {
  MapData *map = allocate();
  if (!map) return false;
  const auto request = in_flight();
  const auto result = tiles::fetch_map(*map, request.latitude, request.longitude, request.range,
      [](const tiles::Progress &progress) {
        percentage().store(std::max(percentage().load(), static_cast<uint8_t>(std::min<unsigned>(99, progress.percent))));
      });
  if (!result.ok || map->truncated) {
    unavailable() = map->truncated || result.partial || std::strcmp(result.error, "outside_coverage") == 0 ||
                    std::strcmp(result.error, "budget_exhausted") == 0;
    destroy(map);
    return false;
  }
  pending_created_unix() = unix_now();
  persistent_cache::save(*map, pending_created_unix());
  pending().store(map, std::memory_order_release);
  return true;
}
inline void worker(void *) {
  const bool ok = fetch_requested_map();
  AirDot::time_weather::release_weather_http_request_slot_();
  task_state().store(ok ? TaskState::SUCCESS : TaskState::FAILED, std::memory_order_release);
  vTaskDelete(nullptr);
}
inline bool install_restored(MapData *map, const protocol::Metadata &metadata) {
  Lock lock(10);
  if (!lock.acquired()) { destroy(map); return false; }
  MapData *old = current();
  current() = map;
  ++revision();
  destroy(old);
  current_created_unix() = metadata.created_unix;
  current_restored() = true;
  received_ms() = esphome::millis();
  status_storage().store(Status::READY);
  return true;
}
inline bool restore_for_key(float latitude, float longitude, uint8_t range) {
  if (task_state().load(std::memory_order_acquire) != TaskState::IDLE) return false;
  const Request request{latitude, longitude, range};
  if (cache_probe_done() && same_request(cache_probe_request(), request)) return false;
  MapData *map = allocate();
  if (!map) return false;
  protocol::Metadata metadata;
  const auto result = persistent_cache::load(latitude, longitude, range, *map, &metadata);
  if (result != persistent_cache::Result::BUSY) {
    cache_probe_request() = request;
    cache_probe_done() = true;
  }
  if (result != persistent_cache::Result::HIT) { destroy(map); return false; }
  return install_restored(map, metadata);
}
inline bool current_is_fresh(uint32_t now) {
  if (now - received_ms() >= CACHE_LIFETIME_MS) return false;
  const uint64_t utc = unix_now(), created = current_created_unix();
  if (current_restored() && utc && created)
    return created <= utc + 300 && (created > utc || utc - created < CACHE_LIFETIME_MS / 1000UL);
  return true;
}
}  // namespace detail

inline Status status() { return detail::status_storage().load(std::memory_order_acquire); }
inline uint32_t map_revision() { return detail::revision(); }
inline Status status_for(float latitude, float longitude, uint8_t range) {
  if (!esphome::network::is_connected()) return Status::OFFLINE;
  const detail::Request request{latitude, longitude, range};
  if (detail::task_state().load(std::memory_order_acquire) == detail::TaskState::RUNNING &&
      detail::same_request(detail::in_flight(), request)) return Status::LOADING;
  return detail::same_request(detail::wanted(), request) ? status() : Status::EMPTY;
}
inline bool progress_for_key(float latitude, float longitude, uint8_t range, DownloadProgress &out) {
  const auto state = detail::task_state().load(std::memory_order_acquire);
  if (state == detail::TaskState::IDLE || state == detail::TaskState::FAILED ||
      !detail::same_request(detail::in_flight(), detail::Request{latitude, longitude, range})) return false;
  out.percentage = detail::percentage().load();
  return true;
}
inline bool has_map(float latitude, float longitude, uint8_t range) {
  if (!valid_coordinates(latitude, longitude)) return false;
  detail::Lock lock;
  return lock.acquired() && detail::current() && matches(*detail::current(), latitude, longitude, range);
}
template<typename Callback>
bool with_map(float latitude, float longitude, uint8_t range, Callback callback) {
  if (!valid_coordinates(latitude, longitude)) return false;
  detail::Lock lock;
  if (!lock.acquired() || !detail::current() || !matches(*detail::current(), latitude, longitude, range)) return false;
  callback(*detail::current());
  return true;
}
inline bool restore_cached_map(float latitude, float longitude, uint8_t range) {
  return !has_map(latitude, longitude, range) && detail::restore_for_key(latitude, longitude, range);
}
inline bool cached_location_for_range(uint8_t range, float &latitude, float &longitude) {
  {
    detail::Lock lock;
    if (lock.acquired() && detail::current() && detail::current()->range_km == range) {
      latitude = detail::current()->center_latitude;
      longitude = detail::current()->center_longitude;
      return true;
    }
  }
  if (detail::task_state().load(std::memory_order_acquire) != detail::TaskState::IDLE ||
      detail::cache_latest_probe_range() == range) return false;
  MapData *map = detail::allocate();
  if (!map) return false;
  protocol::Metadata metadata;
  const auto result = persistent_cache::load_latest(range, *map, &metadata);
  if (result != persistent_cache::Result::BUSY) detail::cache_latest_probe_range() = range;
  if (result != persistent_cache::Result::HIT) { detail::destroy(map); return false; }
  latitude = map->center_latitude;
  longitude = map->center_longitude;
  return detail::install_restored(map, metadata);
}
inline bool consume_result() {
  const auto state = detail::task_state().load(std::memory_order_acquire);
  if (state != detail::TaskState::SUCCESS && state != detail::TaskState::FAILED) return false;
  bool accepted = false;
  if (state == detail::TaskState::SUCCESS) {
    detail::Lock lock(10);
    if (!lock.acquired()) return false;
    MapData *map = detail::pending().exchange(nullptr, std::memory_order_acq_rel);
    if (map && matches(*map, detail::wanted().latitude, detail::wanted().longitude, detail::wanted().range)) {
      MapData *old = detail::current();
      detail::current() = map;
      ++detail::revision();
      detail::destroy(old);
      detail::received_ms() = esphome::millis();
      detail::failure_count() = 0;
      detail::next_retry_ms() = 0;
      detail::current_created_unix() = detail::pending_created_unix();
      detail::current_restored() = false;
      detail::status_storage().store(Status::READY);
      accepted = true;
    } else detail::destroy(map);
  } else if (detail::same_request(detail::wanted(), detail::in_flight())) {
    const uint32_t delay = std::min<uint32_t>(60000UL << std::min<unsigned>(detail::failure_count(), 4), 900000UL);
    if (detail::failure_count() < 4) ++detail::failure_count();
    detail::next_retry_ms() = esphome::millis() + delay;
    detail::status_storage().store(detail::unavailable() ? Status::UNAVAILABLE : Status::FAILED);
  }
  detail::task_state().store(detail::TaskState::IDLE, std::memory_order_release);
  return accepted || state == detail::TaskState::FAILED;
}
inline bool request_map(float latitude, float longitude, uint8_t range, bool allow_network = true) {
  if (!valid_coordinates(latitude, longitude) || range < 5 || range > 50) return false;
  const detail::Request request{latitude, longitude, range};
  if (!detail::same_request(request, detail::wanted())) {
    detail::wanted() = request;
    detail::failure_count() = 0;
    detail::next_retry_ms() = 0;
    detail::status_storage().store(Status::EMPTY);
  }
  bool restored = false;
  if (!has_map(latitude, longitude, range)) restored = detail::restore_for_key(latitude, longitude, range);
  const uint32_t now = esphome::millis();
  if (has_map(latitude, longitude, range) && detail::current_is_fresh(now)) {
    detail::status_storage().store(Status::READY);
    return restored;
  }
  if (status() == Status::UNAVAILABLE ||
      detail::task_state().load(std::memory_order_acquire) != detail::TaskState::IDLE || !allow_network) return restored;
  if (!esphome::network::is_connected()) {
    detail::status_storage().store(has_map(latitude, longitude, range) ? Status::READY : Status::OFFLINE);
    return restored;
  }
  if (detail::next_retry_ms() && static_cast<int32_t>(now - detail::next_retry_ms()) < 0) return false;
  if (AirDot::flight_radar::request_running() || AirDot::time_weather::http_request_active()) return false;
  if (!AirDot::time_weather::acquire_weather_http_request_slot_(AirDot::connectivity::Service::FLIGHT_RADAR)) return false;
  detail::in_flight() = request;
  detail::unavailable() = false;
  detail::percentage().store(0);
  detail::task_state().store(detail::TaskState::RUNNING, std::memory_order_release);
  detail::status_storage().store(Status::LOADING);
  if (xTaskCreate(detail::worker, "radar_map", 12288, nullptr, 1, nullptr) != pdPASS) {
    AirDot::time_weather::release_weather_http_request_slot_();
    detail::task_state().store(detail::TaskState::FAILED, std::memory_order_release);
    return false;
  }
  return true;
}

}  // namespace AirDot::radar_map
#endif
