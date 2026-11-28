#pragma once

#include "radar_map_protocol.h"
#include <cstdlib>

#ifdef USE_ESP32
#include <atomic>
#include <esp_heap_caps.h>
#include <nvs.h>
#endif

namespace AirDot::radar_map::persistent_cache {

// A single replaceable value in its own namespace. Never initialize/erase NVS,
// delete the previous value first, or touch the ESPHome preferences namespace.
inline constexpr const char *NVS_NAMESPACE = "airdot_map";
inline constexpr const char *NVS_KEY = "cache_v1";
inline constexpr const char *NVS_MARINE_KEY = "cache_marine_v1";
inline const char *key_for(MapKind kind) { return kind == MapKind::MARINE ? NVS_MARINE_KEY : NVS_KEY; }

enum class Result : uint8_t {
  HIT, MISS, KEY_MISMATCH, CORRUPT, NO_SPACE, NO_MEMORY, UNCHANGED, SAVED,
  STORAGE_ERROR, INVALID_MAP, BUSY
};
namespace detail {

enum class IoResult : uint8_t { OK, NOT_FOUND, NO_SPACE, ERROR };
struct Statistics { size_t free_entries{0}, available_entries{0}; };

// IDF 5.5 NVS pages contain 126 32-byte entries; blob data chunks hold at most
// 4000 bytes. Round every new chunk up to an entire page, allow a partial first
// page and the final index, and retain eight pages for settings plus two extra
// GC/scheduling pages. available_entries already excludes IDF's own spare page.
// The old blob remains live and is therefore already excluded from these stats.
inline constexpr size_t NVS_ENTRIES_PER_PAGE = 126;
inline constexpr size_t NVS_BLOB_CHUNK_BYTES = 4000;
inline constexpr size_t SETTINGS_RESERVE_ENTRIES = 8 * NVS_ENTRIES_PER_PAGE;
inline constexpr size_t SAFETY_RESERVE_ENTRIES = 2 * NVS_ENTRIES_PER_PAGE;
inline size_t write_entry_budget(size_t length) {
  return ((length + NVS_BLOB_CHUNK_BYTES - 1) / NVS_BLOB_CHUNK_BYTES + 1) *
             NVS_ENTRIES_PER_PAGE + 1;
}
inline bool has_write_space(const Statistics &stats, size_t length) {
  const size_t required = write_entry_budget(length) + SETTINGS_RESERVE_ENTRIES + SAFETY_RESERVE_ENTRIES;
  return stats.available_entries >= required && stats.free_entries >= required;
}

class PackedBuffer {
 public:
  explicit PackedBuffer(size_t length) : length_(length) {
    if (!length || length > protocol::MAX_ENCODED_SIZE) return;
#ifdef USE_ESP32
    data_ = static_cast<uint8_t *>(heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    data_ = static_cast<uint8_t *>(std::malloc(length));
#endif
  }
  ~PackedBuffer() { std::free(data_); }
  PackedBuffer(const PackedBuffer &) = delete;
  PackedBuffer &operator=(const PackedBuffer &) = delete;
  uint8_t *data() { return data_; }
  size_t size() const { return length_; }
 private:
  uint8_t *data_{nullptr};
  size_t length_{0};
};

inline Result io_result(IoResult result) {
  if (result == IoResult::NOT_FOUND) return Result::MISS;
  if (result == IoResult::NO_SPACE) return Result::NO_SPACE;
  return Result::STORAGE_ERROR;
}

template<typename Storage>
Result load_from(Storage &storage, float latitude, float longitude, uint8_t range,
                 MapData &map, protocol::Metadata *metadata, bool latest) {
  map.count = 0;
  size_t length = 0;
  IoResult io = storage.blob_length(length);
  if (io != IoResult::OK) return io_result(io);
  if (length < protocol::HEADER_SIZE || length > protocol::MAX_ENCODED_SIZE) return Result::CORRUPT;
  PackedBuffer packed(length);
  if (!packed.data()) return Result::NO_MEMORY;
  size_t actual = length;
  io = storage.read_blob(packed.data(), actual);
  if (io != IoResult::OK) return io_result(io);
  if (actual != length) return Result::CORRUPT;
  protocol::Metadata header;
  if (!protocol::inspect_header(packed.data(), length, header) ||
      protocol::HEADER_SIZE + header.payload_length != length) return Result::CORRUPT;
  if (latest) {
    if (header.range_km != range) return Result::KEY_MISMATCH;
    latitude = static_cast<float>(header.latitude_key / 100000.0);
    longitude = static_cast<float>(header.longitude_key / 100000.0);
    // MapData uses floats. Reject a wire key that cannot round-trip to that API.
    if (coordinate_key(latitude) != header.latitude_key || coordinate_key(longitude) != header.longitude_key)
      return Result::CORRUPT;
  }
  protocol::Decoder decoder(map, latitude, longitude, range);
  if (!decoder.feed(packed.data(), length) || !decoder.finish()) {
    return decoder.error() == protocol::DecodeError::KEY_MISMATCH ? Result::KEY_MISMATCH : Result::CORRUPT;
  }
  if (metadata) *metadata = decoder.metadata();
  return Result::HIT;
}

template<typename Storage>
Result save_to(Storage &storage, const MapData &map, uint64_t created_unix) {
  uint8_t desired_header[protocol::HEADER_SIZE];
  if (!protocol::make_header(map, created_unix, desired_header)) return Result::INVALID_MAP;
  const size_t length = protocol::HEADER_SIZE + static_cast<size_t>(map.count) * protocol::SEGMENT_SIZE +
                       (map.has_water_mask ? MASK_BYTES : 0);
  size_t old_length = 0;
  const IoResult old_result = storage.blob_length(old_length);
  if (old_result != IoResult::OK && old_result != IoResult::NOT_FOUND) return io_result(old_result);
  const bool bounded_old = old_result == IoResult::OK && old_length >= protocol::HEADER_SIZE &&
                           old_length <= protocol::MAX_ENCODED_SIZE;
  PackedBuffer packed(bounded_old ? std::max(old_length, length) : length);
  if (!packed.data()) return Result::NO_MEMORY;

  if (bounded_old) {
    size_t actual = old_length;
    const IoResult io = storage.read_blob(packed.data(), actual);
    if (io != IoResult::OK) return io_result(io);
    protocol::Metadata old;
    if (actual == old_length && old_length == length && protocol::inspect_header(packed.data(), old_length, old)) {
      // Compare the full deterministic encoding with the previous creation time.
      // Equality also proves the old body's CRC and segments are valid. A mere
      // matching checksum would miss header corruption; timestamp changes alone
      // must not cause a fresh flash write on every refresh.
      size_t offset = 0;
      const bool unchanged = protocol::encode(map, old.created_unix, [&](const uint8_t *data, size_t size) {
        if (size > old_length - offset || std::memcmp(packed.data() + offset, data, size)) return false;
        offset += size;
        return true;
      });
      if (unchanged && offset == length) return Result::UNCHANGED;
    }
  }

  Statistics stats;
  IoResult io = storage.statistics(stats);
  if (io != IoResult::OK) return io_result(io);
  if (!has_write_space(stats, length)) return Result::NO_SPACE;
  size_t offset = 0;
  if (!protocol::encode(map, created_unix, [&](const uint8_t *data, size_t size) {
        if (size > length - offset) return false;
        std::memcpy(packed.data() + offset, data, size); offset += size; return true;
      }) || offset != length) return Result::INVALID_MAP;

  // IDF writes a new multi-page blob version and final index before deleting
  // the old version. Its recovery handles interrupted writes. Never erase the
  // existing key to make room. Commit is required by the public NVS contract.
  io = storage.write_blob(packed.data(), length);
  if (io != IoResult::OK) return io_result(io);
  io = storage.commit();
  return io == IoResult::OK ? Result::SAVED : io_result(io);
}

#ifdef USE_ESP32
inline IoResult nvs_result(esp_err_t error) {
  if (error == ESP_OK) return IoResult::OK;
  if (error == ESP_ERR_NVS_NOT_FOUND) return IoResult::NOT_FOUND;
  if (error == ESP_ERR_NVS_NOT_ENOUGH_SPACE || error == ESP_ERR_NVS_VALUE_TOO_LONG) return IoResult::NO_SPACE;
  return IoResult::ERROR;
}
class NvsStorage {
 public:
  NvsStorage(bool writable, const char *key) : key_(key) {
    error_ = nvs_open(NVS_NAMESPACE, writable ? NVS_READWRITE : NVS_READONLY, &handle_);
  }
  ~NvsStorage() { if (error_ == ESP_OK) nvs_close(handle_); }
  NvsStorage(const NvsStorage &) = delete;
  NvsStorage &operator=(const NvsStorage &) = delete;
  IoResult blob_length(size_t &length) {
    return nvs_result(error_ == ESP_OK ? nvs_get_blob(handle_, key_, nullptr, &length) : error_);
  }
  IoResult read_blob(uint8_t *data, size_t &length) {
    return nvs_result(error_ == ESP_OK ? nvs_get_blob(handle_, key_, data, &length) : error_);
  }
  IoResult statistics(Statistics &out) {
    nvs_stats_t stats{};
    const esp_err_t error = nvs_get_stats(nullptr, &stats);
    if (error != ESP_OK) return nvs_result(error);
    out.free_entries = stats.free_entries; out.available_entries = stats.available_entries;
    return IoResult::OK;
  }
  IoResult write_blob(const uint8_t *data, size_t length) {
    return nvs_result(error_ == ESP_OK ? nvs_set_blob(handle_, key_, data, length) : error_);
  }
  IoResult commit() { return nvs_result(error_ == ESP_OK ? nvs_commit(handle_) : error_); }
 private:
  const char *key_;
  nvs_handle_t handle_{0};
  esp_err_t error_{ESP_FAIL};
};
inline std::atomic_flag &operation_flag() { static std::atomic_flag value = ATOMIC_FLAG_INIT; return value; }
class Operation {
 public:
  Operation() : acquired_(!operation_flag().test_and_set(std::memory_order_acquire)) {}
  ~Operation() { if (acquired_) operation_flag().clear(std::memory_order_release); }
  bool acquired() const { return acquired_; }
 private:
  bool acquired_;
};
#endif
}  // namespace detail

#ifdef USE_ESP32
// Call load once per requested location while no map worker is active. Only a
// fully validated matching map is returned, including offline after a reboot.
// map.kind selects the stored slot.
inline Result load(float latitude, float longitude, uint8_t range, MapData &map,
                   protocol::Metadata *metadata = nullptr) {
  detail::Operation operation;
  if (!operation.acquired()) { map.count = 0; return Result::BUSY; }
  detail::NvsStorage storage(false, key_for(map.kind));
  return detail::load_from(storage, latitude, longitude, range, map, metadata, false);
}
inline Result load_latest(uint8_t range, MapData &map, protocol::Metadata *metadata = nullptr) {
  detail::Operation operation;
  if (!operation.acquired()) { map.count = 0; return Result::BUSY; }
  detail::NvsStorage storage(false, key_for(map.kind));
  return detail::load_from(storage, 0, 0, range, map, metadata, true);
}
// Flash writes belong in the existing map worker, before publication, rather
// than the LVGL loop. Failure leaves the fresh RAM map usable.
inline Result save(const MapData &map, uint64_t created_unix = 0) {
  detail::Operation operation;
  if (!operation.acquired()) return Result::BUSY;
  detail::NvsStorage storage(true, key_for(map.kind));
  return detail::save_to(storage, map, created_unix);
}
#endif

}  // namespace AirDot::radar_map::persistent_cache
