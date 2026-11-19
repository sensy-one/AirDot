#pragma once

#include "radar_tile_data.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#ifdef USE_ESP32
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esphome/core/hal.h"
#endif

namespace AirDot::radar_map::tiles {

inline constexpr size_t MAX_METADATA_BYTES = 32UL * 1024UL;
inline constexpr size_t MAX_WIRE_TILE_BYTES = 1024UL * 1024UL;
inline constexpr size_t MAX_TIER_BYTES = 1536UL * 1024UL;
inline constexpr size_t MAX_FETCH_BYTES = 3UL * 1024UL * 1024UL;
inline constexpr uint32_t FETCH_TIMEOUT_MS = 120000;
inline constexpr uint32_t TILE_HEADER_TIMEOUT_MS = 30000;
inline constexpr uint32_t TILE_IDLE_TIMEOUT_MS = 15000;
inline constexpr uint8_t MAX_ZOOM_ATTEMPTS = 3;
inline constexpr const char *TILEJSON_URL = "https://tiles.openfreemap.org/planet";

struct Progress {
  uint8_t done{0}, total{0}, percent{0};
};

struct FetchResult {
  bool ok{false};
  const char *error{"none"};
  uint32_t bytes{0};
  bool partial{false};
};

namespace tile_client_detail {

struct Source {
  char url_template[256]{};
  uint8_t min_zoom{0}, max_zoom{14};
};

// The provider publishes a versioned path. Only that fixed origin, one version
// component, and the three expected XYZ placeholders are accepted.
inline bool valid_template(const char *url) {
  constexpr const char *prefix = "https://tiles.openfreemap.org/planet/";
  constexpr const char *suffix = "/{z}/{x}/{y}.pbf";
  const size_t prefix_size = std::strlen(prefix);
  if (std::strncmp(url, prefix, prefix_size) != 0) return false;
  const char *cursor = url + prefix_size;
  const char *version = cursor;
  while ((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') ||
         (*cursor >= '0' && *cursor <= '9') || *cursor == '_' || *cursor == '-') ++cursor;
  return cursor != version && cursor - version <= 96 && std::strcmp(cursor, suffix) == 0;
}

// A bounded JSON reader avoids allocating the large vector_layers description.
// Unknown values are still parsed completely, so a truncated document cannot
// supply an apparently valid URL from its first few fields.
class JsonReader {
 public:
  JsonReader(const uint8_t *data, size_t length) : cursor_(data), end_(data + length) {}

  bool source(Source &result) {
    if (!take('{')) return false;
    bool saw_tiles = false, saw_min = false, saw_max = false;
    unsigned minimum = 0, maximum = 0;
    if (take('}')) return false;
    do {
      char key[64];
      if (!string(key, sizeof(key)) || !take(':')) return false;
      if (std::strcmp(key, "tiles") == 0) {
        if (saw_tiles || !take('[') || !string(result.url_template, sizeof(result.url_template))) return false;
        saw_tiles = true;
        while (take(',')) if (!string(nullptr, 0)) return false;
        if (!take(']')) return false;
      } else if (std::strcmp(key, "minzoom") == 0) {
        if (saw_min || !integer(minimum)) return false;
        saw_min = true;
      } else if (std::strcmp(key, "maxzoom") == 0) {
        if (saw_max || !integer(maximum)) return false;
        saw_max = true;
      } else if (!value(0)) return false;
    } while (take(','));
    whitespace();
    if (!take('}')) return false;
    whitespace();
    if (cursor_ != end_ || !saw_tiles || !saw_min || !saw_max ||
        minimum > maximum || minimum > 14 || maximum > 22 || !valid_template(result.url_template)) return false;
    result.min_zoom = static_cast<uint8_t>(minimum);
    result.max_zoom = static_cast<uint8_t>(std::min(maximum, 14U));
    return true;
  }

 private:
  const uint8_t *cursor_, *end_;

  void whitespace() { while (cursor_ != end_ && (*cursor_ == ' ' || *cursor_ == '\n' || *cursor_ == '\r' || *cursor_ == '\t')) ++cursor_; }
  bool take(char c) {
    whitespace();
    if (cursor_ == end_ || *cursor_ != static_cast<uint8_t>(c)) return false;
    ++cursor_; return true;
  }
  static int hex(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }
  bool string(char *output, size_t capacity) {
    if (!take('"')) return false;
    size_t size = 0;
    while (cursor_ != end_) {
      unsigned c = *cursor_++;
      if (c == '"') {
        if (output) output[size] = 0;
        return true;
      }
      if (c < 0x20) return false;
      if (c == '\\') {
        if (cursor_ == end_) return false;
        c = *cursor_++;
        if (c == 'u') {
          c = 0;
          for (unsigned i = 0; i < 4; ++i) {
            if (cursor_ == end_) return false;
            const int digit = hex(*cursor_++);
            if (digit < 0) return false;
            c = c * 16 + static_cast<unsigned>(digit);
          }
          if (output && (c == 0 || c >= 128)) return false;
        } else {
          const char *escapes = "\"\\/bfnrt";
          const char *found = std::strchr(escapes, static_cast<int>(c));
          if (!found || c == 0) return false;
          const unsigned char decoded[] = {'"', '\\', '/', '\b', '\f', '\n', '\r', '\t'};
          c = decoded[found - escapes];
        }
      }
      if (output) {
        if (size + 1 >= capacity) return false;
        output[size++] = static_cast<char>(c);
      }
    }
    return false;
  }
  bool integer(unsigned &result) {
    whitespace();
    if (cursor_ == end_ || *cursor_ < '0' || *cursor_ > '9') return false;
    const bool zero = *cursor_ == '0';
    result = 0;
    do {
      if (result > 1000000) return false;
      result = result * 10 + (*cursor_++ - '0');
    } while (!zero && cursor_ != end_ && *cursor_ >= '0' && *cursor_ <= '9');
    return true;
  }
  bool number() {
    whitespace();
    if (cursor_ != end_ && *cursor_ == '-') ++cursor_;
    if (cursor_ == end_) return false;
    if (*cursor_ == '0') ++cursor_;
    else {
      if (*cursor_ < '1' || *cursor_ > '9') return false;
      do { ++cursor_; } while (cursor_ != end_ && *cursor_ >= '0' && *cursor_ <= '9');
    }
    if (cursor_ != end_ && *cursor_ == '.') {
      ++cursor_;
      if (cursor_ == end_ || *cursor_ < '0' || *cursor_ > '9') return false;
      do { ++cursor_; } while (cursor_ != end_ && *cursor_ >= '0' && *cursor_ <= '9');
    }
    if (cursor_ != end_ && (*cursor_ == 'e' || *cursor_ == 'E')) {
      ++cursor_;
      if (cursor_ != end_ && (*cursor_ == '+' || *cursor_ == '-')) ++cursor_;
      if (cursor_ == end_ || *cursor_ < '0' || *cursor_ > '9') return false;
      do { ++cursor_; } while (cursor_ != end_ && *cursor_ >= '0' && *cursor_ <= '9');
    }
    return true;
  }
  bool value(unsigned depth) {
    if (depth >= 16) return false;
    whitespace();
    if (cursor_ == end_) return false;
    if (*cursor_ == '"') return string(nullptr, 0);
    if (take('{')) {
      if (take('}')) return true;
      do { if (!string(nullptr, 0) || !take(':') || !value(depth + 1)) return false; } while (take(','));
      return take('}');
    }
    if (take('[')) {
      if (take(']')) return true;
      do { if (!value(depth + 1)) return false; } while (take(','));
      return take(']');
    }
    for (const char *literal : {"true", "false", "null"}) {
      const size_t size = std::strlen(literal);
      if (static_cast<size_t>(end_ - cursor_) >= size && std::memcmp(cursor_, literal, size) == 0) { cursor_ += size; return true; }
    }
    return number();
  }
};

inline bool parse_source(const uint8_t *data, size_t length, Source &source) {
  return data && length && length <= MAX_METADATA_BYTES && JsonReader(data, length).source(source);
}

inline bool tile_url(const Source &source, const Tile &tile, char *output, size_t capacity) {
  if (!valid_template(source.url_template) || tile.z < source.min_zoom || tile.z > source.max_zoom) return false;
  const char *suffix = std::strstr(source.url_template, "/{z}");
  if (!suffix) return false;
  const int length = std::snprintf(output, capacity, "%.*s/%u/%lu/%lu.pbf",
      static_cast<int>(suffix - source.url_template), source.url_template, static_cast<unsigned>(tile.z),
      static_cast<unsigned long>(tile.x), static_cast<unsigned long>(tile.y));
  return length > 0 && static_cast<size_t>(length) < capacity;
}

#ifdef USE_ESP32

class Buffer {
 public:
  explicit Buffer(size_t size) : data_(static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))) {}
  ~Buffer() { heap_caps_free(data_); }
  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;
  uint8_t *data() const { return data_; }
 private:
  uint8_t *data_;
};

enum class Download { OK, BUDGET, FAILED };

class Http {
 public:
  explicit Http(FetchResult &result) : result_(result), started_(esphome::millis()) {
    esp_http_client_config_t config{};
    config.url = TILEJSON_URL;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 10000;
    config.buffer_size = 1024;
    config.buffer_size_tx = 512;
    config.addr_type = HTTP_ADDR_TYPE_INET;
    config.disable_auto_redirect = true;
    config.keep_alive_enable = true;
    config.user_agent = "AirDot (+https://github.com/sensy-one/AirDot)";
    config.event_handler = header_event;
    config.user_data = this;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    config.crt_bundle_attach = esp_crt_bundle_attach;
#endif
    client_ = esp_http_client_init(&config);
    if (client_) esp_http_client_set_header(client_, "Accept-Encoding", "identity");
  }
  ~Http() { if (client_) esp_http_client_cleanup(client_); }
  Http(const Http &) = delete;
  Http &operator=(const Http &) = delete;
  bool valid() const { return client_ != nullptr; }
  bool timed_out() const { return esphome::millis() - started_ >= FETCH_TIMEOUT_MS; }

  template<class Callback>
  Download get(const char *url, uint8_t *output, size_t capacity, size_t &length,
               size_t &tier_bytes, Progress &progress, Callback &callback, bool metadata = false) {
    length = 0;
    content_encoding_[0] = 0;
    if (timed_out()) return fail("http_timeout");
    if (result_.bytes >= MAX_FETCH_BYTES || (!metadata && tier_bytes >= MAX_TIER_BYTES)) return budget();
    const esp_err_t url_error = esp_http_client_set_url(client_, url);
    if (url_error != ESP_OK) return fail("http_url");
    esp_http_client_set_header(client_, "Accept", metadata ? "application/json" : "application/vnd.mapbox-vector-tile, application/x-protobuf");
    callback(progress);
    esp_http_client_set_timeout_ms(client_, remaining_timeout(10000));
    const esp_err_t open_error = esp_http_client_open(client_, 0);
    if (open_error != ESP_OK) return fail("http_connect");
    callback(progress);
    const uint32_t header_started = esphome::millis();
    int64_t expected;
    do {
      if (timed_out()) return fail("http_timeout");
      esp_http_client_set_timeout_ms(client_, remaining_timeout(1500));
      expected = esp_http_client_fetch_headers(client_);
      if (expected != -ESP_ERR_HTTP_EAGAIN) break;
      vTaskDelay(1);
    } while (esphome::millis() - header_started < TILE_HEADER_TIMEOUT_MS);
    if (expected < 0) return fail("http_headers");
    if (esp_http_client_get_status_code(client_) != 200) return fail("http_status");
    if (content_encoding_[0] && std::strcmp(content_encoding_, "identity") != 0) return fail("http_content_encoding");
    if (static_cast<uint64_t>(expected) > capacity ||
        static_cast<uint64_t>(expected) > MAX_FETCH_BYTES - result_.bytes ||
        (!metadata && static_cast<uint64_t>(expected) > MAX_TIER_BYTES - tier_bytes)) return budget();
    uint32_t last_data = esphome::millis();
    size_t since_yield = 0;
    while (true) {
      if (timed_out()) return fail("http_timeout");
      if (esphome::millis() - last_data >= TILE_IDLE_TIMEOUT_MS) return fail("http_stalled");
      size_t available = std::min<size_t>(capacity - length, MAX_FETCH_BYTES - result_.bytes);
      if (!metadata) available = std::min(available, MAX_TIER_BYTES - tier_bytes);
      if (!available) {
        if (esp_http_client_is_complete_data_received(client_)) break;
        return budget();
      }
      esp_http_client_set_timeout_ms(client_, remaining_timeout(1500));
      const int count = esp_http_client_read(client_, reinterpret_cast<char *>(output + length), static_cast<int>(std::min(available, size_t(4096))));
      if (count == -ESP_ERR_HTTP_EAGAIN) { vTaskDelay(1); continue; }
      if (count < 0) return fail("http_read");
      if (count == 0) break;
      last_data = esphome::millis();
      length += static_cast<size_t>(count);
      result_.bytes += static_cast<uint32_t>(count);
      if (!metadata) tier_bytes += static_cast<size_t>(count);
      since_yield += static_cast<size_t>(count);
      const uint32_t part = expected > 0 ? static_cast<uint32_t>(std::min<uint64_t>(100, uint64_t(length) * 100 / expected)) :
          static_cast<uint32_t>(uint64_t(length) * 100 / (length + 65536));
      progress.percent = metadata ? 2 : static_cast<uint8_t>(5 + (uint32_t(progress.done) * 100 + part) * 90 / (uint32_t(progress.total) * 100));
      callback(progress);
      if (since_yield >= 16384) { since_yield = 0; vTaskDelay(1); }
    }
    if (!esp_http_client_is_complete_data_received(client_)) return fail("http_incomplete");
    if (metadata && !length) return fail("metadata_content");
    return Download::OK;
  }

 private:
  FetchResult &result_;
  esp_http_client_handle_t client_{nullptr};
  uint32_t started_;
  char content_encoding_[24]{};

  uint32_t remaining_timeout(uint32_t maximum) const {
    const uint32_t elapsed = esphome::millis() - started_;
    return std::max<uint32_t>(1, std::min(maximum, elapsed < FETCH_TIMEOUT_MS ? FETCH_TIMEOUT_MS - elapsed : 1));
  }
  Download fail(const char *error) {
    result_.error = error;
    if (client_) esp_http_client_close(client_);
    return Download::FAILED;
  }
  Download budget() {
    result_.error = "budget_exhausted";
    if (client_) esp_http_client_close(client_);  // Discard unread bytes before a lower zoom.
    return Download::BUDGET;
  }
  static esp_err_t header_event(esp_http_client_event_t *event) {
    if (event->event_id != HTTP_EVENT_ON_HEADER || !event->user_data || !event->header_key || !event->header_value) return ESP_OK;
    auto &self = *static_cast<Http *>(event->user_data);
    const char *key = event->header_key;
    constexpr const char *encoding = "content-encoding";
    size_t i = 0;
    while (key[i] && encoding[i]) {
      const char lower = key[i] >= 'A' && key[i] <= 'Z' ? key[i] + ('a' - 'A') : key[i];
      if (lower != encoding[i]) return ESP_OK;
      ++i;
    }
    if (!key[i] && !encoding[i]) {
      std::snprintf(self.content_encoding_, sizeof(self.content_encoding_), "%s", event->header_value);
      for (char &c : self.content_encoding_) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    return ESP_OK;
  }
};

#endif
}  // namespace tile_client_detail

#ifdef USE_ESP32

// Called inside the existing map worker after its shared HTTP slot is acquired.
// All buffers and the TLS client are destroyed on return, before that caller can
// publish geometry or release its slot. No task, timer, or global state is owned.
template<class Callback>
inline FetchResult fetch_map(MapData &map, float latitude, float longitude, uint8_t range, Callback callback) {
  using namespace tile_client_detail;
  FetchResult result;
  Progress progress;
  map.center_latitude = latitude; map.center_longitude = longitude; map.range_km = range;
  map.count = 0; map.truncated = false; map.coarse = false;
  auto failure = [&](const char *error) {
    result.ok = false; result.error = error;
    map.count = 0; map.truncated = false;
    callback(progress);
    return result;
  };
  if (!valid_coordinates(latitude, longitude) || range < 5 || range > 50) return failure("tile_context");
  Buffer metadata(MAX_METADATA_BYTES), buffer(MAX_WIRE_TILE_BYTES);
  if (!metadata.data() || !buffer.data()) return failure("tile_alloc");
  Http http(result);
  if (!http.valid()) return failure("http_client_init");
  size_t metadata_length = 0, unused_tier_bytes = 0;
  const Download metadata_result = http.get(TILEJSON_URL, metadata.data(), MAX_METADATA_BYTES, metadata_length, unused_tier_bytes, progress, callback, true);
  if (metadata_result != Download::OK)
    return failure(metadata_result == Download::BUDGET ? "metadata_content" : result.error);
  Source source;
  if (!parse_source(metadata.data(), metadata_length, source)) return failure("metadata_content");
  const uint8_t first_zoom = std::max(source.min_zoom, std::min(source.max_zoom, initial_zoom(latitude, range)));
  for (unsigned attempt = 0; attempt < MAX_ZOOM_ATTEMPTS && attempt <= unsigned(first_zoom - source.min_zoom); ++attempt) {
    const uint8_t zoom = static_cast<uint8_t>(first_zoom - attempt);
    result.error = "none";
    progress.done = progress.total = progress.percent = 0;
    TilePlan plan{};
    const Result planned = plan_tiles(latitude, longitude, range, zoom, plan);
    result.partial = plan.partial_coverage;
    if (planned == Result::OUTSIDE_COVERAGE || plan.partial_coverage) return failure("outside_coverage");
    if (planned == Result::LOWER_ZOOM || planned == Result::BUDGET) continue;
    if (planned != Result::OK || !plan.count || plan.count > 12) return failure("tile_plan");
    progress.total = plan.count;
    map.count = 0; map.truncated = false; map.coarse = attempt != 0;
    TileDecoder decoder(map, []() { vTaskDelay(1); });
    size_t tier_bytes = 0;
    bool lower_zoom = false;
    callback(progress);
    for (uint8_t i = 0; i < plan.count; ++i) {
      char url[256];
      if (!tile_url(source, plan.tiles[i], url, sizeof(url))) return failure("metadata_content");
      size_t length = 0;
      const Download downloaded = http.get(url, buffer.data(), MAX_WIRE_TILE_BYTES, length, tier_bytes, progress, callback);
      if (downloaded == Download::FAILED) return failure(result.error);
      if (downloaded == Download::BUDGET) { lower_zoom = true; break; }
      callback(progress);
      const Result decoded = decoder.decode_tile(buffer.data(), length, plan.tiles[i]);
      if (decoded == Result::BUDGET || decoded == Result::LOWER_ZOOM) { lower_zoom = true; break; }
      if (decoded != Result::OK) return failure("mvt_invalid");
      progress.done = i + 1;
      progress.percent = static_cast<uint8_t>(5 + uint32_t(progress.done) * 90 / progress.total);
      callback(progress);
      if (http.timed_out()) return failure("http_timeout");
      vTaskDelay(1);
    }
    if (lower_zoom) { map.count = 0; map.truncated = false; continue; }
    if (map.truncated) return failure("budget_exhausted");
    result.ok = true; result.error = "none";
    progress.percent = 99; callback(progress);
    return result;
  }
  return failure("budget_exhausted");
}

#endif
}  // namespace AirDot::radar_map::tiles
