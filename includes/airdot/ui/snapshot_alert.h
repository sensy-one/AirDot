#pragma once

#ifdef USE_ESP32
#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_netif.h>
#include <jpeg_decoder.h>
#include <mdns.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

#include "esphome/components/network/util.h"
#include "esphome/components/watchdog/watchdog.h"
#include "esphome/core/hal.h"

#include "connectivity.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <lvgl.h>

namespace AirDot::snapshot_alert {

#ifdef USE_ESP32
inline constexpr int SNAPSHOT_TASK_IDLE = 0;
inline constexpr int SNAPSHOT_TASK_RUNNING = 1;
inline constexpr int SNAPSHOT_TASK_SUCCESS = 2;
inline constexpr int SNAPSHOT_TASK_FAILED = 3;
inline constexpr uint16_t SNAPSHOT_DISPLAY_MAX_WIDTH = 480;
inline constexpr uint16_t SNAPSHOT_DISPLAY_MAX_HEIGHT = 480;
inline constexpr uint16_t SNAPSHOT_DEFAULT_DURATION_SECONDS = 30;
inline constexpr uint16_t SNAPSHOT_MAX_DURATION_SECONDS = 3600;
inline constexpr uint32_t SNAPSHOT_HTTP_TASK_STACK_SIZE = 24576;
inline constexpr int SNAPSHOT_HTTP_TIMEOUT_MS = 8000;
inline constexpr uint32_t SNAPSHOT_MDNS_TIMEOUT_MS = 1500;
inline constexpr uint32_t SNAPSHOT_TASK_TIMEOUT_MS = 45000;
inline constexpr size_t SNAPSHOT_MAX_JPEG_BYTES = 8 * 1024 * 1024;
inline constexpr uint32_t SNAPSHOT_MAX_SOURCE_PIXELS = 1920UL * 1920UL;
inline constexpr uint8_t SNAPSHOT_RGB888_BYTES_PER_PIXEL = 3;
inline constexpr uint8_t SNAPSHOT_RGB565_BYTES_PER_PIXEL = 2;

struct SnapshotImage {
  lv_image_dsc_t dsc{};
  uint8_t *pixels{nullptr};
  uint16_t duration_seconds{SNAPSHOT_DEFAULT_DURATION_SECONDS};
  bool sound_enabled{false};
};

struct SnapshotRequest {
  char *url{nullptr};
  uint16_t duration_seconds{SNAPSHOT_DEFAULT_DURATION_SECONDS};
  bool sound_enabled{false};
  uint32_t generation{0};
};

struct HttpResponse {
  uint8_t *body{nullptr};
  size_t length{0};
  size_t capacity{0};
  size_t max_length{0};
  bool overflow{false};
};

struct ParsedHttpUrl {
  std::string scheme;
  std::string host;
  std::string host_header;
  std::string path_query;
  uint16_t port{0};
  bool https{false};
  bool valid{false};
};

class BufferGuard {
 public:
  explicit BufferGuard(uint8_t *buffer = nullptr) : buffer_(buffer) {}
  ~BufferGuard() { this->reset(); }

  BufferGuard(const BufferGuard &) = delete;
  BufferGuard &operator=(const BufferGuard &) = delete;

  uint8_t *get() const { return this->buffer_; }
  uint8_t *release() {
    uint8_t *buffer = this->buffer_;
    this->buffer_ = nullptr;
    return buffer;
  }
  void reset(uint8_t *buffer = nullptr) {
    if (this->buffer_ != nullptr)
      heap_caps_free(this->buffer_);
    this->buffer_ = buffer;
  }

 private:
  uint8_t *buffer_{nullptr};
};

inline std::atomic<int> &snapshot_task_state_() {
  static std::atomic<int> state{SNAPSHOT_TASK_IDLE};
  return state;
}

inline std::atomic<uint32_t> &snapshot_generation_() {
  static std::atomic<uint32_t> generation{1};
  return generation;
}

inline std::atomic<uint32_t> &snapshot_task_started_ms_() {
  static std::atomic<uint32_t> started_ms{0};
  return started_ms;
}

inline SemaphoreHandle_t snapshot_mutex_() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
  return mutex;
}

class SnapshotLock {
 public:
  explicit SnapshotLock(uint32_t timeout_ms = 10) {
    const auto mutex = snapshot_mutex_();
    if (mutex != nullptr)
      this->locked_ = xSemaphoreTake(mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
  }

  ~SnapshotLock() {
    if (this->locked_)
      xSemaphoreGive(snapshot_mutex_());
  }

  bool locked() const { return this->locked_; }

 private:
  bool locked_{false};
};

inline SnapshotImage &pending_snapshot_() {
  static SnapshotImage snapshot{};
  return snapshot;
}

inline SnapshotImage &active_snapshot_() {
  static SnapshotImage snapshot{};
  return snapshot;
}

inline lv_obj_t *&snapshot_image_obj_() {
  static lv_obj_t *obj = nullptr;
  return obj;
}

inline void free_snapshot_image_(SnapshotImage &snapshot) {
  if (snapshot.pixels != nullptr)
    heap_caps_free(snapshot.pixels);
  snapshot = SnapshotImage{};
}

inline bool snapshot_generation_current_(uint32_t generation) {
  return generation != 0 && generation == snapshot_generation_().load(std::memory_order_acquire);
}

inline void store_snapshot_state_if_current_(uint32_t generation, int state) {
  if (!snapshot_generation_current_(generation))
    return;
  if (state != SNAPSHOT_TASK_RUNNING)
    snapshot_task_started_ms_().store(0, std::memory_order_release);
  snapshot_task_state_().store(state, std::memory_order_release);
}

inline void clear_pending_snapshot_() {
  SnapshotLock lock(10);
  if (lock.locked())
    free_snapshot_image_(pending_snapshot_());
}

inline void recover_snapshot_state_for_new_request_() {
  const int state = snapshot_task_state_().load(std::memory_order_acquire);
  if (state == SNAPSHOT_TASK_FAILED || state == SNAPSHOT_TASK_SUCCESS) {
    clear_pending_snapshot_();
    snapshot_task_started_ms_().store(0, std::memory_order_release);
    snapshot_task_state_().store(SNAPSHOT_TASK_IDLE, std::memory_order_release);
    return;
  }

  if (state != SNAPSHOT_TASK_RUNNING)
    return;

  const uint32_t started_ms = snapshot_task_started_ms_().load(std::memory_order_acquire);
  if (started_ms == 0)
    return;

  const uint32_t now = esphome::millis();
  if (static_cast<uint32_t>(now - started_ms) <= SNAPSHOT_TASK_TIMEOUT_MS)
    return;

  snapshot_generation_().fetch_add(1, std::memory_order_acq_rel);
  snapshot_task_started_ms_().store(0, std::memory_order_release);
  snapshot_task_state_().store(SNAPSHOT_TASK_IDLE, std::memory_order_release);
}

inline uint16_t normalize_duration_seconds_(int duration_seconds) {
  if (duration_seconds < 0)
    return SNAPSHOT_DEFAULT_DURATION_SECONDS;
  if (duration_seconds > SNAPSHOT_MAX_DURATION_SECONDS)
    return SNAPSHOT_MAX_DURATION_SECONDS;
  return static_cast<uint16_t>(duration_seconds);
}

inline bool source_size_supported_(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0)
    return false;
  if (width > 4096 || height > 4096)
    return false;
  return width * height <= SNAPSHOT_MAX_SOURCE_PIXELS;
}

inline esp_jpeg_image_scale_t decode_scale_for_(uint32_t source_width, uint32_t source_height) {
  if (source_width <= SNAPSHOT_DISPLAY_MAX_WIDTH * 2U &&
      source_height <= SNAPSHOT_DISPLAY_MAX_HEIGHT * 2U)
    return JPEG_IMAGE_SCALE_0;
  if (source_width <= SNAPSHOT_DISPLAY_MAX_WIDTH * 4U &&
      source_height <= SNAPSHOT_DISPLAY_MAX_HEIGHT * 4U)
    return JPEG_IMAGE_SCALE_1_2;
  if (source_width <= SNAPSHOT_DISPLAY_MAX_WIDTH * 8U &&
      source_height <= SNAPSHOT_DISPLAY_MAX_HEIGHT * 8U)
    return JPEG_IMAGE_SCALE_1_4;
  return JPEG_IMAGE_SCALE_1_8;
}

inline void target_size_for_(uint32_t source_width, uint32_t source_height,
                             uint16_t &target_width, uint16_t &target_height) {
  if (source_width <= SNAPSHOT_DISPLAY_MAX_WIDTH && source_height <= SNAPSHOT_DISPLAY_MAX_HEIGHT) {
    target_width = static_cast<uint16_t>(source_width);
    target_height = static_cast<uint16_t>(source_height);
    return;
  }

  if (source_width * SNAPSHOT_DISPLAY_MAX_HEIGHT > source_height * SNAPSHOT_DISPLAY_MAX_WIDTH) {
    target_width = SNAPSHOT_DISPLAY_MAX_WIDTH;
    target_height = static_cast<uint16_t>(
        std::max<uint32_t>(1, (source_height * SNAPSHOT_DISPLAY_MAX_WIDTH + source_width / 2) / source_width));
  } else {
    target_height = SNAPSHOT_DISPLAY_MAX_HEIGHT;
    target_width = static_cast<uint16_t>(
        std::max<uint32_t>(1, (source_width * SNAPSHOT_DISPLAY_MAX_HEIGHT + source_height / 2) / source_height));
  }
}

inline uint8_t *alloc_psram_(size_t size) {
  return static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

inline uint16_t rgb565_from_rgb888_(uint8_t red, uint8_t green, uint8_t blue) {
  const uint16_t r = (static_cast<uint16_t>(red) * 31U + 127U) / 255U;
  const uint16_t g = (static_cast<uint16_t>(green) * 63U + 127U) / 255U;
  const uint16_t b = (static_cast<uint16_t>(blue) * 31U + 127U) / 255U;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

inline void store_rgb565_le_(uint8_t *target, uint8_t red, uint8_t green, uint8_t blue) {
  const uint16_t color = rgb565_from_rgb888_(red, green, blue);
  target[0] = static_cast<uint8_t>(color & 0xFF);
  target[1] = static_cast<uint8_t>(color >> 8);
}

inline void sample_area_rgb888_(const uint8_t *source, uint32_t source_width, uint32_t source_height,
                                uint32_t source_stride, uint16_t target_width, uint16_t target_height,
                                uint32_t x, uint32_t y, uint8_t *target_pixel) {
  uint32_t source_x0 = static_cast<uint32_t>((static_cast<uint64_t>(x) * source_width) / target_width);
  uint32_t source_x1 = static_cast<uint32_t>(
      (static_cast<uint64_t>(x + 1U) * source_width + target_width - 1U) / target_width);
  uint32_t source_y0 = static_cast<uint32_t>((static_cast<uint64_t>(y) * source_height) / target_height);
  uint32_t source_y1 = static_cast<uint32_t>(
      (static_cast<uint64_t>(y + 1U) * source_height + target_height - 1U) / target_height);

  source_x1 = std::min<uint32_t>(std::max<uint32_t>(source_x1, source_x0 + 1U), source_width);
  source_y1 = std::min<uint32_t>(std::max<uint32_t>(source_y1, source_y0 + 1U), source_height);

  uint32_t red = 0;
  uint32_t green = 0;
  uint32_t blue = 0;
  uint32_t count = 0;

  for (uint32_t source_y = source_y0; source_y < source_y1; source_y++) {
    const auto *source_row = source + source_y * source_stride;
    for (uint32_t source_x = source_x0; source_x < source_x1; source_x++) {
      const auto *source_pixel = source_row + source_x * SNAPSHOT_RGB888_BYTES_PER_PIXEL;
      red += source_pixel[0];
      green += source_pixel[1];
      blue += source_pixel[2];
      count++;
    }
  }

  target_pixel[0] = static_cast<uint8_t>((red + count / 2U) / count);
  target_pixel[1] = static_cast<uint8_t>((green + count / 2U) / count);
  target_pixel[2] = static_cast<uint8_t>((blue + count / 2U) / count);
}

inline void reset_http_response_(HttpResponse &response) {
  const size_t max_length = response.max_length;
  if (response.body != nullptr)
    heap_caps_free(response.body);
  response = HttpResponse{};
  response.max_length = max_length;
}

inline ParsedHttpUrl parse_http_url_(const std::string &url) {
  ParsedHttpUrl parsed{};
  const std::string trimmed = AirDot::connectivity::trim_copy(url);
  const std::string lowered = AirDot::connectivity::ascii_lower_copy(trimmed);

  size_t offset = std::string::npos;
  if (lowered.rfind("http://", 0) == 0) {
    parsed.scheme = "http";
    parsed.https = false;
    offset = 7;
  } else if (lowered.rfind("https://", 0) == 0) {
    parsed.scheme = "https";
    parsed.https = true;
    offset = 8;
  }
  if (offset == std::string::npos)
    return parsed;

  const uint16_t default_port = parsed.https ? 443 : 80;
  const auto authority_end = trimmed.find_first_of("/?#", offset);
  std::string authority = authority_end == std::string::npos
                              ? trimmed.substr(offset)
                              : trimmed.substr(offset, authority_end - offset);
  authority = AirDot::connectivity::trim_copy(authority);
  if (authority.empty())
    return parsed;

  const auto at_pos = authority.rfind('@');
  parsed.host_header = at_pos == std::string::npos ? authority : authority.substr(at_pos + 1);
  parsed.host_header = AirDot::connectivity::trim_copy(parsed.host_header);

  if (authority_end == std::string::npos) {
    parsed.path_query = "/";
  } else {
    parsed.path_query = trimmed.substr(authority_end);
    const auto fragment = parsed.path_query.find('#');
    if (fragment != std::string::npos)
      parsed.path_query.resize(fragment);
    if (parsed.path_query.empty() || parsed.path_query.front() == '?')
      parsed.path_query.insert(parsed.path_query.begin(), '/');
  }

  const auto host_port = AirDot::connectivity::normalize_host_port_input(authority, default_port, default_port);
  parsed.host = host_port.host;
  parsed.port = host_port.port;
  parsed.valid = host_port.host_valid && host_port.port_valid && !parsed.host_header.empty();
  return parsed;
}

inline bool host_is_mdns_local_(const std::string &host) {
  const std::string lowered = AirDot::connectivity::ascii_lower_copy(host);
  return lowered.size() > 6 && lowered.compare(lowered.size() - 6, 6, ".local") == 0;
}

inline bool build_mdns_ipv4_url_(const ParsedHttpUrl &parsed, std::string &resolved_url, std::string &host_header) {
  if (!parsed.valid || parsed.https || !host_is_mdns_local_(parsed.host))
    return false;

  std::string mdns_host = parsed.host.substr(0, parsed.host.size() - 6);
  while (!mdns_host.empty() && mdns_host.back() == '.')
    mdns_host.pop_back();
  if (mdns_host.empty() || mdns_host.find(':') != std::string::npos)
    return false;

  (void) mdns_init();

  esp_ip4_addr_t address{};
  if (mdns_query_a(mdns_host.c_str(), SNAPSHOT_MDNS_TIMEOUT_MS, &address) != ESP_OK)
    return false;

  char ip_address[16]{};
  if (esp_ip4addr_ntoa(&address, ip_address, sizeof(ip_address)) == nullptr || ip_address[0] == '\0')
    return false;

  resolved_url = parsed.scheme + "://" + ip_address;
  if (parsed.port != 80) {
    char port[8]{};
    std::snprintf(port, sizeof(port), ":%u", static_cast<unsigned>(parsed.port));
    resolved_url += port;
  }
  resolved_url += parsed.path_query.empty() ? "/" : parsed.path_query;
  host_header = parsed.host_header;
  return true;
}

inline bool response_has_jpeg_magic_(const HttpResponse &response) {
  return response.length >= 2 && response.body != nullptr &&
         response.body[0] == 0xFF && response.body[1] == 0xD8;
}

inline bool ensure_http_response_capacity_(HttpResponse *response, size_t required) {
  if (response == nullptr || response->max_length == 0 || required > response->max_length) {
    if (response != nullptr)
      response->overflow = true;
    return false;
  }

  if (required <= response->capacity)
    return true;

  size_t capacity = response->capacity == 0 ? 16 * 1024 : response->capacity;
  while (capacity < required) {
    const size_t next_capacity = capacity * 2;
    if (next_capacity <= capacity || next_capacity > response->max_length) {
      capacity = response->max_length;
      break;
    }
    capacity = next_capacity;
  }

  void *buffer = response->body == nullptr
                     ? heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                     : heap_caps_realloc(response->body, capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buffer == nullptr) {
    response->overflow = true;
    return false;
  }

  response->body = static_cast<uint8_t *>(buffer);
  response->capacity = capacity;
  return true;
}

inline esp_err_t http_response_event_handler_(esp_http_client_event_t *event) {
  if (event == nullptr || event->event_id != HTTP_EVENT_ON_DATA || event->data == nullptr || event->data_len <= 0)
    return ESP_OK;

  auto *response = static_cast<HttpResponse *>(event->user_data);
  const size_t incoming = static_cast<size_t>(event->data_len);
  if (response == nullptr || !ensure_http_response_capacity_(response, response->length + incoming))
    return ESP_FAIL;

  std::memcpy(response->body + response->length, event->data, incoming);
  response->length += incoming;
  return ESP_OK;
}

inline bool download_snapshot_jpeg_once_(const char *url, const char *host_header, HttpResponse &response) {
  esp_http_client_config_t config{};
  config.url = url;
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = SNAPSHOT_HTTP_TIMEOUT_MS;
  config.max_redirection_count = 3;
  config.event_handler = http_response_event_handler_;
  config.user_data = &response;
  config.buffer_size = 4096;
  config.buffer_size_tx = 1024;
  config.user_agent = "AirDot";
  config.addr_type = HTTP_ADDR_TYPE_UNSPEC;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  config.crt_bundle_attach = esp_crt_bundle_attach;
#endif

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr)
    return false;

  if (host_header != nullptr && host_header[0] != '\0')
    esp_http_client_set_header(client, "Host", host_header);
  esp_http_client_set_header(client, "Accept", "image/jpeg,image/*;q=0.9,*/*;q=0.1");
  esp_http_client_set_header(client, "Cache-Control", "no-cache, no-store");
  esp_http_client_set_header(client, "Pragma", "no-cache");
  esp_http_client_set_header(client, "Connection", "close");

  const esp_err_t error = esp_http_client_perform(client);
  const int status_code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (error != ESP_OK || status_code != 200 || response.overflow || !response_has_jpeg_magic_(response))
    return false;

  return true;
}

inline bool download_snapshot_jpeg_(const char *url, HttpResponse &response) {
  reset_http_response_(response);
  if (download_snapshot_jpeg_once_(url, nullptr, response))
    return true;

  const ParsedHttpUrl parsed = parse_http_url_(url);
  std::string resolved_url;
  std::string host_header;
  if (!build_mdns_ipv4_url_(parsed, resolved_url, host_header))
    return false;

  reset_http_response_(response);
  return download_snapshot_jpeg_once_(resolved_url.c_str(), host_header.c_str(), response);
}

inline bool resample_rgb888_to_rgb565_(const uint8_t *source, uint32_t source_width, uint32_t source_height,
                                       uint32_t source_stride, SnapshotImage &snapshot, uint16_t duration_seconds,
                                       bool sound_enabled) {
  uint16_t target_width = 0;
  uint16_t target_height = 0;
  target_size_for_(source_width, source_height, target_width, target_height);
  if (target_width == 0 || target_height == 0)
    return false;

  const size_t rgb_stride = static_cast<size_t>(target_width) * SNAPSHOT_RGB888_BYTES_PER_PIXEL;
  const size_t rgb_size = rgb_stride * target_height;
  BufferGuard rgb(alloc_psram_(rgb_size));
  if (rgb.get() == nullptr)
    return false;

  if (target_width == source_width && target_height == source_height && source_stride == rgb_stride) {
    std::memcpy(rgb.get(), source, rgb_size);
  } else {
    for (uint32_t y = 0; y < target_height; y++) {
      auto *target_row = rgb.get() + y * rgb_stride;
      for (uint32_t x = 0; x < target_width; x++) {
        auto *target_pixel = target_row + x * SNAPSHOT_RGB888_BYTES_PER_PIXEL;
        sample_area_rgb888_(source, source_width, source_height, source_stride,
                            target_width, target_height, x, y, target_pixel);
      }
    }
  }

  const size_t target_stride = static_cast<size_t>(target_width) * SNAPSHOT_RGB565_BYTES_PER_PIXEL;
  const size_t target_size = target_stride * target_height;
  BufferGuard target(alloc_psram_(target_size));
  if (target.get() == nullptr)
    return false;

  for (uint32_t y = 0; y < target_height; y++) {
    auto *target_row = target.get() + y * target_stride;
    for (uint32_t x = 0; x < target_width; x++) {
      const auto *source_pixel = rgb.get() + y * rgb_stride + x * SNAPSHOT_RGB888_BYTES_PER_PIXEL;
      const uint8_t red = source_pixel[0];
      const uint8_t green = source_pixel[1];
      const uint8_t blue = source_pixel[2];
      store_rgb565_le_(target_row + x * SNAPSHOT_RGB565_BYTES_PER_PIXEL, red, green, blue);
    }
  }

  snapshot.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  snapshot.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  snapshot.dsc.header.flags = 0;
  snapshot.dsc.header.w = target_width;
  snapshot.dsc.header.h = target_height;
  snapshot.dsc.header.stride = static_cast<uint16_t>(target_stride);
  snapshot.dsc.data_size = static_cast<uint32_t>(target_size);
  snapshot.dsc.data = target.get();
  snapshot.dsc.reserved = nullptr;
  snapshot.dsc.reserved_2 = nullptr;
  snapshot.pixels = target.release();
  snapshot.duration_seconds = duration_seconds;
  snapshot.sound_enabled = sound_enabled;
  return true;
}

inline bool decode_snapshot_jpeg_(const uint8_t *jpeg_data, size_t jpeg_size,
                                  SnapshotImage &snapshot, uint16_t duration_seconds,
                                  bool sound_enabled) {
  esp_jpeg_image_cfg_t jpeg_cfg{};
  jpeg_cfg.indata = const_cast<uint8_t *>(jpeg_data);
  jpeg_cfg.indata_size = static_cast<uint32_t>(jpeg_size);
  jpeg_cfg.out_format = JPEG_IMAGE_FORMAT_RGB888;
  jpeg_cfg.out_scale = JPEG_IMAGE_SCALE_0;
  jpeg_cfg.flags.swap_color_bytes = 0;

  esp_jpeg_image_output_t source_info{};
  esp_err_t error = esp_jpeg_get_image_info(&jpeg_cfg, &source_info);
  if (error != ESP_OK)
    return false;
  if (!source_size_supported_(source_info.width, source_info.height))
    return false;

  jpeg_cfg.out_scale = decode_scale_for_(source_info.width, source_info.height);
  esp_jpeg_image_output_t scaled_info{};
  error = esp_jpeg_get_image_info(&jpeg_cfg, &scaled_info);
  if (error != ESP_OK)
    return false;

  const size_t decoded_size = scaled_info.output_len;
  if (decoded_size == 0)
    return false;

  BufferGuard decoded(alloc_psram_(decoded_size));
  if (decoded.get() == nullptr)
    return false;

  jpeg_cfg.outbuf = decoded.get();
  jpeg_cfg.outbuf_size = static_cast<uint32_t>(decoded_size);

  esp_jpeg_image_output_t decoded_info{};
  error = esp_jpeg_decode(&jpeg_cfg, &decoded_info);
  if (error != ESP_OK || decoded_info.output_len == 0)
    return false;

  const uint32_t decoded_stride = static_cast<uint32_t>(decoded_info.width) * SNAPSHOT_RGB888_BYTES_PER_PIXEL;
  if (!resample_rgb888_to_rgb565_(
          decoded.get(), decoded_info.width, decoded_info.height, decoded_stride, snapshot, duration_seconds,
          sound_enabled))
    return false;

  return true;
}

inline void publish_snapshot_result_(SnapshotImage &snapshot, uint32_t generation) {
  if (!snapshot_generation_current_(generation)) {
    free_snapshot_image_(snapshot);
    return;
  }

  SnapshotLock lock(50);
  if (!lock.locked()) {
    free_snapshot_image_(snapshot);
    store_snapshot_state_if_current_(generation, SNAPSHOT_TASK_FAILED);
    return;
  }

  if (!snapshot_generation_current_(generation)) {
    free_snapshot_image_(snapshot);
    return;
  }

  free_snapshot_image_(pending_snapshot_());
  pending_snapshot_() = snapshot;
  snapshot = SnapshotImage{};
  store_snapshot_state_if_current_(generation, SNAPSHOT_TASK_SUCCESS);
}

inline void free_snapshot_request_(SnapshotRequest *request) {
  if (request == nullptr)
    return;
  if (request->url != nullptr)
    heap_caps_free(request->url);
  heap_caps_free(request);
}

inline void snapshot_task_(void *arg) {
  esphome::watchdog::WatchdogManager watchdog(30000);
  auto *request = static_cast<SnapshotRequest *>(arg);
  SnapshotImage decoded_snapshot;
  const uint32_t request_generation = request != nullptr ? request->generation : 0;

  HttpResponse response;
  response.max_length = SNAPSHOT_MAX_JPEG_BYTES;

  bool ok = false;
  if (request != nullptr && request->url != nullptr &&
      download_snapshot_jpeg_(request->url, response) &&
      decode_snapshot_jpeg_(response.body, response.length, decoded_snapshot, request->duration_seconds,
                            request->sound_enabled)) {
    if (request->generation == snapshot_generation_().load(std::memory_order_acquire))
      ok = true;
  }

  if (response.body != nullptr)
    heap_caps_free(response.body);
  free_snapshot_request_(request);

  if (ok) {
    publish_snapshot_result_(decoded_snapshot, request_generation);
  } else {
    free_snapshot_image_(decoded_snapshot);
    store_snapshot_state_if_current_(request_generation, SNAPSHOT_TASK_FAILED);
  }

  vTaskDelete(nullptr);
}

inline bool request_running() {
  recover_snapshot_state_for_new_request_();
  return snapshot_task_state_().load(std::memory_order_acquire) == SNAPSHOT_TASK_RUNNING;
}

inline bool start_snapshot_request(const std::string &url, int duration_seconds, bool sound_enabled = false) {
  recover_snapshot_state_for_new_request_();

  if (!esphome::network::is_connected())
    return false;

  const std::string trimmed_url = AirDot::connectivity::trim_copy(url);
  if (trimmed_url.empty() || trimmed_url.size() > 512 || !AirDot::connectivity::is_valid_http_url(trimmed_url))
    return false;

  int expected = SNAPSHOT_TASK_IDLE;
  if (!snapshot_task_state_().compare_exchange_strong(expected, SNAPSHOT_TASK_RUNNING, std::memory_order_acq_rel))
    return false;

  const uint32_t request_generation = snapshot_generation_().fetch_add(1, std::memory_order_acq_rel) + 1;
  snapshot_task_started_ms_().store(esphome::millis(), std::memory_order_release);

  auto *request = static_cast<SnapshotRequest *>(heap_caps_malloc(sizeof(SnapshotRequest), MALLOC_CAP_8BIT));
  char *url_copy = static_cast<char *>(heap_caps_malloc(trimmed_url.size() + 1, MALLOC_CAP_8BIT));
  if (request == nullptr || url_copy == nullptr) {
    if (request != nullptr)
      heap_caps_free(request);
    if (url_copy != nullptr)
      heap_caps_free(url_copy);
    store_snapshot_state_if_current_(request_generation, SNAPSHOT_TASK_FAILED);
    return false;
  }

  std::memset(request, 0, sizeof(SnapshotRequest));
  std::memcpy(url_copy, trimmed_url.c_str(), trimmed_url.size() + 1);
  request->url = url_copy;
  request->duration_seconds = normalize_duration_seconds_(duration_seconds);
  request->sound_enabled = sound_enabled;
  request->generation = request_generation;

  const BaseType_t created = xTaskCreate(
      snapshot_task_, "snapshot", SNAPSHOT_HTTP_TASK_STACK_SIZE, request, 1, nullptr);
  if (created == pdPASS)
    return true;

  free_snapshot_request_(request);
  store_snapshot_state_if_current_(request_generation, SNAPSHOT_TASK_FAILED);
  return false;
}

inline lv_obj_t *snapshot_image_from_container_(lv_obj_t *container) {
  if (container != nullptr) {
    lv_obj_t *child = lv_obj_get_child(container, 0);
    if (child != nullptr)
      snapshot_image_obj_() = child;
  }
  return snapshot_image_obj_();
}

inline lv_obj_t *ensure_snapshot_widget_(lv_obj_t *container) {
  lv_obj_t *image = snapshot_image_from_container_(container);
  if (image == nullptr)
    return nullptr;

  snapshot_image_obj_() = image;
  lv_obj_set_size(image, SNAPSHOT_DISPLAY_MAX_WIDTH, SNAPSHOT_DISPLAY_MAX_HEIGHT);
  lv_obj_center(image);
  ::lv_image_set_antialias(image, true);
  ::lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CENTER);
  return image;
}

inline void clear_active_snapshot(lv_obj_t *container) {
  snapshot_generation_().fetch_add(1, std::memory_order_acq_rel);
  clear_pending_snapshot_();
  snapshot_task_started_ms_().store(0, std::memory_order_release);
  snapshot_task_state_().store(SNAPSHOT_TASK_IDLE, std::memory_order_release);
  lv_obj_t *image = ensure_snapshot_widget_(container);
  if (image != nullptr)
    ::lv_image_set_src(image, static_cast<const void *>(nullptr));
  if (container != nullptr)
    lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
  free_snapshot_image_(active_snapshot_());
}

inline int consume_snapshot_result(lv_obj_t *container, uint16_t &duration_seconds, bool &sound_enabled) {
  const int state = snapshot_task_state_().load(std::memory_order_acquire);
  if (state == SNAPSHOT_TASK_FAILED) {
    SnapshotLock lock(5);
    if (lock.locked())
      free_snapshot_image_(pending_snapshot_());
    snapshot_task_started_ms_().store(0, std::memory_order_release);
    snapshot_task_state_().store(SNAPSHOT_TASK_IDLE, std::memory_order_release);
    return -1;
  }

  if (state != SNAPSHOT_TASK_SUCCESS)
    return 0;

  SnapshotImage snapshot;
  {
    SnapshotLock lock(20);
    if (!lock.locked())
      return 0;
    snapshot = pending_snapshot_();
    pending_snapshot_() = SnapshotImage{};
  }

  if (snapshot.pixels == nullptr) {
    snapshot_task_started_ms_().store(0, std::memory_order_release);
    snapshot_task_state_().store(SNAPSHOT_TASK_IDLE, std::memory_order_release);
    return -1;
  }

  lv_obj_t *image = ensure_snapshot_widget_(container);
  if (image == nullptr) {
    free_snapshot_image_(snapshot);
    snapshot_task_started_ms_().store(0, std::memory_order_release);
    snapshot_task_state_().store(SNAPSHOT_TASK_IDLE, std::memory_order_release);
    return -1;
  }

  clear_active_snapshot(container);
  active_snapshot_() = snapshot;
  duration_seconds = active_snapshot_().duration_seconds;
  sound_enabled = active_snapshot_().sound_enabled;
  ::lv_image_set_src(image, &active_snapshot_().dsc);
  lv_obj_set_size(image, active_snapshot_().dsc.header.w, active_snapshot_().dsc.header.h);
  lv_obj_center(image);
  if (container != nullptr)
    lv_obj_clear_flag(container, LV_OBJ_FLAG_HIDDEN);
  snapshot_task_started_ms_().store(0, std::memory_order_release);
  snapshot_task_state_().store(SNAPSHOT_TASK_IDLE, std::memory_order_release);
  return 1;
}

#else
inline bool request_running() { return false; }
inline bool start_snapshot_request(const std::string &, int, bool = false) { return false; }
inline void clear_active_snapshot(lv_obj_t *) {}
inline int consume_snapshot_result(lv_obj_t *, uint16_t &, bool &) { return 0; }
#endif

}
