#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <lvgl.h>

#include "flight_radar_display.h"
#include "weather_radar_client.h"

namespace AirDot::weather_radar {

inline constexpr uint32_t FRAME_DURATION_MS = 650;
inline constexpr uint32_t LAST_FRAME_PAUSE_MS = 1500;

struct WeatherScopeState {
  lv_obj_t *scope{nullptr};
  const lv_font_t *title_font{nullptr}, *percent_font{nullptr}, *detail_font{nullptr};
  float map_latitude{0}, map_longitude{0};
  uint8_t map_range_km{25};
  bool map_location_valid{false}, animation_valid{false}, animation_active{false};
  uint32_t animation_started_ms{0}, dataset_generated_time{0}, dataset_fetched_ms{0};
};
inline WeatherScopeState &weather_scope_state_() { static WeatherScopeState state; return state; }

inline void configure_weather_radar_location(float latitude, float longitude, bool valid) {
  // Resolve the same stored exact/IP/cached location and range as Flight Radar.
  AirDot::flight_radar::configure_radar_map_location(latitude, longitude, valid);
  const auto &location = AirDot::flight_radar::radar_scope_state_();
  auto &state = weather_scope_state_();
  const bool changed = state.map_location_valid != location.map_location_valid ||
      state.map_latitude != location.map_latitude || state.map_longitude != location.map_longitude ||
      state.map_range_km != location.map_range_km;
  state.map_latitude = location.map_latitude; state.map_longitude = location.map_longitude;
  state.map_range_km = location.map_range_km; state.map_location_valid = location.map_location_valid;
  if (changed) {
    state.animation_valid = false;
    state.animation_active = false;
    if (state.scope) lv_obj_invalidate(state.scope);
  }
}

inline uint8_t playback_frame_index(uint32_t elapsed_ms, uint8_t frame_count) {
  if (frame_count < 2) return 0;
  const uint32_t cycle = frame_count * FRAME_DURATION_MS + LAST_FRAME_PAUSE_MS;
  return static_cast<uint8_t>(std::min<uint32_t>((elapsed_ms % cycle) / FRAME_DURATION_MS, frame_count - 1));
}

using View = AirDot::flight_radar::RadarMapLoadingPhase;
inline bool weather_frame_ready_(const WeatherScopeState &state);
inline bool weather_data_available_(const WeatherScopeState &state) {
  bool available = false;
  if (state.map_location_valid) with_data(state.map_latitude, state.map_longitude, state.map_range_km,
      [&](const RadarData &data) { available = data.frame_count > 0; });
  return available;
}
inline View weather_view_(const WeatherScopeState &state) {
  if (!state.map_location_valid) return View::LOADING;
  if (!valid_location(state.map_latitude, state.map_longitude, state.map_range_km)) return View::HIDDEN;
  const bool map = AirDot::radar_map::has_map(state.map_latitude, state.map_longitude, state.map_range_km);
  const bool radar = weather_data_available_(state);
  if (map && weather_frame_ready_(state)) return View::HIDDEN;
  const auto status = status_for(state.map_latitude, state.map_longitude, state.map_range_km);
  const auto map_status = AirDot::radar_map::status_for(state.map_latitude, state.map_longitude, state.map_range_km);
  if ((!radar && status == Status::OFFLINE) || (!map && map_status == AirDot::radar_map::Status::OFFLINE))
    return View::HIDDEN;
  if ((!radar && status == Status::UNAVAILABLE) ||
      (!map && map_status == AirDot::radar_map::Status::UNAVAILABLE)) return View::HIDDEN;
  if ((!radar && status == Status::LOADING) ||
      (!map && map_status == AirDot::radar_map::Status::LOADING)) return View::DOWNLOADING;
  if ((!radar && status == Status::FAILED) ||
      (!map && map_status == AirDot::radar_map::Status::FAILED)) return View::RETRYING;
  return View::LOADING;
}
inline bool weather_radar_loading_visible_() {
  return weather_view_(weather_scope_state_()) != View::HIDDEN;
}
inline uint8_t weather_loading_percent_(const WeatherScopeState &state) {
  if (weather_view_(state) == View::HIDDEN) return 100;
  uint32_t map_percent = AirDot::radar_map::has_map(state.map_latitude, state.map_longitude, state.map_range_km)
      ? 100 : AirDot::flight_radar::radar_map_loading_progress_(AirDot::flight_radar::radar_scope_state_()).percent;
  const uint32_t radar_percent = weather_data_available_(state) ? 100 :
      percent_for(state.map_latitude, state.map_longitude, state.map_range_km);
  return static_cast<uint8_t>(std::min<uint32_t>(99, (map_percent + radar_percent) / 2));
}

// Preserve roads while compositing positive source echoes in grayscale.
inline uint8_t composite_gray(uint8_t map_gray, uint8_t intensity, bool dark) {
  if (!intensity) return map_gray;
  const uint32_t shade = 24 + static_cast<uint32_t>(intensity) * 108 / 255;
  return static_cast<uint8_t>(dark ? 255 - (255 - map_gray) * (255 - shade) / 255
                                   : map_gray * (255 - shade) / 255);
}
struct WeatherCompositeCache {
  uint16_t *pixels{nullptr};
  size_t capacity{0};
  lv_image_dsc_t image{};
  RadarData source{};
  uint32_t base_revision{0}, valid_time{0}, published_ms{0};
  uint8_t frame{0};
  bool dark{true}, valid{false};
};
struct WeatherCompositeJob {
  WeatherCompositeCache target{};
  int row{0};
  bool running{false};
};
inline WeatherCompositeCache &weather_composite_cache_() { static WeatherCompositeCache cache; return cache; }
inline WeatherCompositeJob &weather_composite_job_() { static WeatherCompositeJob job; return job; }
inline uint64_t weather_wall_time_() {
  const auto epoch = std::time(nullptr);
  return epoch >= VALID_CLOCK ? static_cast<uint64_t>(epoch) : 0;
}
inline bool weather_base_matches_(const AirDot::radar_map::RenderCache &base, const WeatherScopeState &state) {
  return state.map_location_valid && base.valid && base.width == WIDTH && base.height == HEIGHT &&
      base.range_km == state.map_range_km && base.dark == AirDot::onboarding::load_dark_mode_enabled() &&
      coordinate_key(base.latitude) == coordinate_key(state.map_latitude) &&
      coordinate_key(base.longitude) == coordinate_key(state.map_longitude);
}
inline bool weather_frame_ready_(const WeatherScopeState &state) {
  const auto &base = AirDot::radar_map::render_cache();
  const auto &cache = weather_composite_cache_();
  return weather_base_matches_(base, state) && cache.valid && cache.base_revision == base.revision &&
      cache.dark == base.dark && matches(cache.source, state.map_latitude, state.map_longitude, state.map_range_km) &&
      cache.frame < cache.source.frame_count && frame_current(cache.source.frames[cache.frame],
          reference_time(cache.source, weather_wall_time_(), esphome::millis()));
}
inline void publish_weather_frame_() {
  auto &cache = weather_composite_cache_();
  auto &job = weather_composite_job_();
  lv_image_cache_drop(&cache.image);
  std::swap(cache, job.target);
  cache.image = {};
  cache.image.header.magic = LV_IMAGE_HEADER_MAGIC; cache.image.header.cf = LV_COLOR_FORMAT_RGB565;
  cache.image.header.w = WIDTH; cache.image.header.h = HEIGHT;
  cache.image.header.stride = WIDTH * sizeof(uint16_t);
  cache.image.data_size = PIXEL_COUNT * sizeof(uint16_t);
  cache.image.data = reinterpret_cast<const uint8_t *>(cache.pixels);
  cache.valid = true;
  cache.published_ms = esphome::millis();
  job.running = false;
  if (weather_scope_state_().scope) lv_obj_invalidate(weather_scope_state_().scope);
}

// Main-loop preparation keeps the published image immutable between frames.
inline void service_weather_render(bool active) {
  auto &state = weather_scope_state_();
  auto &job = weather_composite_job_();
  if (!state.map_location_valid || !valid_location(state.map_latitude, state.map_longitude, state.map_range_km)) {
    job.running = false;
    return;
  }
  const lv_area_t area{0, 0, WIDTH - 1, HEIGHT - 1};
  const bool dark = AirDot::onboarding::load_dark_mode_enabled();
  if (!AirDot::radar_map::prepare_map(area, state.map_latitude, state.map_longitude,
          state.map_range_km, PIXELS_PER_RANGE, CLIP_RADIUS, dark)) {
    job.running = false;
    return;
  }
  const auto &base = AirDot::radar_map::render_cache();
  with_data(state.map_latitude, state.map_longitude, state.map_range_km, [&](const RadarData &data) {
    const uint32_t now = esphome::millis();
    const uint64_t reference = reference_time(data, weather_wall_time_(), now);
    if (!state.animation_valid || state.dataset_generated_time != data.generated_time ||
        state.dataset_fetched_ms != data.fetched_ms) {
      state.animation_started_ms = now; state.dataset_generated_time = data.generated_time;
      state.dataset_fetched_ms = data.fetched_ms; state.animation_valid = true;
      state.animation_active = active;
    } else if (active != state.animation_active) {
      if (active) state.animation_started_ms = now;
      state.animation_active = active;
    }
    auto &target = job.target;
    if (job.running && (target.base_revision != base.revision || target.dark != dark ||
        !matches(target.source, state.map_latitude, state.map_longitude, state.map_range_km) ||
        target.source.generated_time != data.generated_time || target.source.fetched_ms != data.fetched_ms ||
        target.frame >= data.frame_count || target.valid_time != data.frames[target.frame].valid_time ||
        !frame_current(data.frames[target.frame], reference))) job.running = false;
    if (!job.running) {
      uint8_t timeline[MAX_FRAMES]{}, count = 0;
      for (uint8_t i = 0; i < std::min<unsigned>(MAX_FRAMES, data.frame_count); ++i)
        if (frame_current(data.frames[i], reference)) timeline[count++] = i;
      if (!count) return;
      const auto &front = weather_composite_cache_();
      uint8_t index = timeline[active ? playback_frame_index(now - state.animation_started_ms, count) : 0];
      if (!active && weather_frame_ready_(state) && front.source.generated_time == data.generated_time &&
          front.source.fetched_ms == data.fetched_ms && front.frame < data.frame_count &&
          front.valid_time == data.frames[front.frame].valid_time && frame_current(data.frames[front.frame], reference))
        index = front.frame;
      if (weather_frame_ready_(state) && front.source.generated_time == data.generated_time &&
          front.source.fetched_ms == data.fetched_ms && front.frame == index && front.valid_time == data.frames[index].valid_time)
        return;
      if (!data.frames[index].intensity) return;
      if (target.capacity < PIXEL_COUNT) {
#ifdef USE_ESP32
        auto *pixels = static_cast<uint16_t *>(heap_caps_malloc(PIXEL_COUNT * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
        auto *pixels = static_cast<uint16_t *>(std::malloc(PIXEL_COUNT * sizeof(uint16_t)));
#endif
        if (!pixels) return;
        std::free(target.pixels); target.pixels = pixels; target.capacity = PIXEL_COUNT;
      }
      target.source = data;
      target.source.coverage = nullptr;
      for (auto &frame : target.source.frames) frame.intensity = nullptr;
      target.base_revision = base.revision; target.frame = index; target.valid_time = data.frames[index].valid_time;
      target.dark = dark; target.valid = false; job.row = 0; job.running = true;
    }
    const auto *rain = data.frames[target.frame].intensity;
    if (!rain) { job.running = false; return; }
    const uint32_t started = esphome::micros();
    const int end_row = std::min(HEIGHT, job.row + 8);
    const int radius_squared = CLIP_RADIUS * CLIP_RADIUS;
    const int fade_radius = CLIP_RADIUS * AirDot::radar_map::FADE_START_PERCENT / 100;
    const int fade_start_squared = fade_radius * fade_radius;
    const int fade_span = std::max(1, radius_squared - fade_start_squared);
    while (job.row < end_row) {
      const int y = job.row++;
      for (int x = 0; x < WIDTH; ++x) {
        const size_t offset = static_cast<size_t>(y) * WIDTH + x;
        const auto pixel = base.pixels[offset];
        const int dx = x - WIDTH / 2, dy = y - HEIGHT / 2;
        const int distance_squared = dx * dx + dy * dy;
        const bool sampled = data.coverage && data.coverage[offset] != 0;
        if (!rain[offset] || !sampled || distance_squared > radius_squared) target.pixels[offset] = pixel;
        else {
          const uint8_t map_gray = pixel == 0xFFFF ? 255 : static_cast<uint8_t>(((pixel >> 11) & 31) << 3);
          int gray = composite_gray(map_gray, rain[offset], dark);
          if (distance_squared > fade_start_squared)
            gray = map_gray + (gray - map_gray) * (radius_squared - distance_squared) / fade_span;
          target.pixels[offset] = AirDot::radar_map::map_gray_rgb565(static_cast<uint8_t>(gray), x, y);
        }
      }
      if (static_cast<uint32_t>(esphome::micros() - started) >= 2000) break;
    }
    if (job.row == HEIGHT) {
      const auto &front = weather_composite_cache_();
      if (active && weather_frame_ready_(state) && front.source.generated_time == target.source.generated_time &&
          front.source.fetched_ms == target.source.fetched_ms) {
        uint32_t hold = FRAME_DURATION_MS;
        bool last = true;
        for (uint8_t i = front.frame + 1; i < std::min<unsigned>(MAX_FRAMES, front.source.frame_count); ++i)
          if (frame_current(front.source.frames[i], reference)) last = false;
        if (last) hold += LAST_FRAME_PAUSE_MS;
        if (static_cast<uint32_t>(now - front.published_ms) < hold) return;
      }
      publish_weather_frame_();
    }
  });
}

inline void weather_label_(lv_layer_t *layer, const lv_area_t &area, const char *text,
                           const lv_font_t *font, int center_y, lv_color_t color) {
  lv_point_t size{};
  lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  AirDot::flight_radar::draw_label_(layer, area, font, color,
      area.x1 + (area.x2 - area.x1 + 1 - size.x) / 2, center_y - size.y / 2, text, LV_TEXT_ALIGN_CENTER);
}
inline void weather_loading_(lv_layer_t *layer, const lv_area_t &area, const WeatherScopeState &state, View view) {
  const char *title = AirDot::flight_radar::radar_map_loading_text_(view);
  char percent[8]; std::snprintf(percent, sizeof(percent), "%u%%", static_cast<unsigned>(weather_loading_percent_(state)));
  lv_point_t title_size{}, percent_size{};
  lv_text_get_size(&title_size, title, state.title_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_text_get_size(&percent_size, percent, state.percent_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  const int center_y = (area.y1 + area.y2 + 1) / 2;
  const int top = center_y - (title_size.y + 10 + percent_size.y) / 2;
  weather_label_(layer, area, title, state.title_font, top + title_size.y / 2, AirDot::display_text_color());
  weather_label_(layer, area, percent, state.percent_font,
      top + title_size.y + 10 + percent_size.y / 2, AirDot::display_text_color());
}
inline void weather_image_(lv_layer_t *layer, const lv_area_t &area, const lv_image_dsc_t &source) {
  lv_draw_image_dsc_t image;
  lv_draw_image_dsc_init(&image); image.src = &source; image.opa = LV_OPA_COVER;
  lv_draw_image(layer, &image, &area);
}
inline void draw_weather_radar_(lv_event_t *event) {
  auto *state = static_cast<WeatherScopeState *>(lv_event_get_user_data(event));
  auto *scope = static_cast<lv_obj_t *>(lv_event_get_target(event));
  auto *layer = lv_event_get_layer(event);
  if (!state || !layer || scope != state->scope) return;
  lv_area_t area; lv_obj_get_coords(scope, &area);
  AirDot::flight_radar::draw_rect_(layer, area, AirDot::display_background_color());
  const auto view = weather_view_(*state);
  if (view != View::HIDDEN) { weather_loading_(layer, area, *state, view); return; }
  const auto &base = AirDot::radar_map::render_cache();
  const bool map = weather_base_matches_(base, *state);
  bool displayed = false;
  uint32_t timestamp = 0;
  if (weather_frame_ready_(*state)) {
    const auto &cache = weather_composite_cache_();
    weather_image_(layer, area, cache.image);
    displayed = true; timestamp = cache.valid_time;
  }
  if (!displayed && map) weather_image_(layer, area, base.image);
  const auto foreground = AirDot::display_text_color();
  const int cx = (area.x1 + area.x2 + 1) / 2, cy = (area.y1 + area.y2 + 1) / 2;
  AirDot::flight_radar::draw_compass_(layer, area, state->title_font, cx, cy, CLIP_RADIUS);
  if (state->map_location_valid && AirDot::onboarding::load_radar_location_dot_enabled()) {
    lv_draw_rect_dsc_t dot;
    lv_draw_rect_dsc_init(&dot);
    dot.bg_color = AirDot::color(0xFF, 0xFF, 0xFF); dot.bg_opa = LV_OPA_COVER; dot.radius = LV_RADIUS_CIRCLE;
    lv_area_t location{cx - 3, cy - 3, cx + 3, cy + 3};
    lv_draw_rect(layer, &dot, &location);
  }
  if (displayed) {
    char time_text[20] = "--:--";
    if (timestamp && AirDot::onboarding::has_time_zone_offset_minutes()) {
      const std::time_t local_epoch = static_cast<std::time_t>(timestamp) +
          static_cast<std::time_t>(AirDot::onboarding::time_zone_offset_minutes_for_utc(timestamp)) * 60;
      std::tm local{};
      const char *format = AirDot::onboarding::load_time_format() == AirDot::onboarding::TIME_FORMAT_12H
          ? "%I:%M %p" : "%H:%M";
      if (gmtime_r(&local_epoch, &local)) std::strftime(time_text, sizeof(time_text), format, &local);
    }
    weather_label_(layer, area, time_text, state->detail_font, area.y1 + 425, foreground);
  }
}
inline void prepare_weather_scope_(lv_obj_t *scope, const lv_font_t *title, const lv_font_t *percent, const lv_font_t *detail) {
  if (!scope) return;
  auto &state = weather_scope_state_();
  state.title_font = title ? title : lv_font_get_default();
  state.percent_font = percent ? percent : state.title_font;
  state.detail_font = detail ? detail : state.title_font;
  if (state.scope != scope) { state.scope = scope; lv_obj_add_event_cb(scope, draw_weather_radar_, LV_EVENT_DRAW_MAIN, &state); }
  AirDot::flight_radar::disable_radar_antialiasing_(scope);
  lv_obj_set_style_bg_color(scope, AirDot::display_background_color(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scope, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(scope, 0, LV_PART_MAIN); lv_obj_set_style_pad_all(scope, 0, LV_PART_MAIN);
  lv_obj_remove_flag(scope, LV_OBJ_FLAG_SCROLLABLE); lv_obj_invalidate(scope);
}
template<typename Scope> inline void refresh_weather_radar_scope(Scope *scope, const lv_font_t *title,
    const lv_font_t *percent, const lv_font_t *detail) {
  prepare_weather_scope_(AirDot::lv_obj(scope), title, percent, detail);
}
template<typename Scope> inline void refresh_weather_radar_scope(Scope *scope, const esphome::font::Font *title,
    const esphome::font::Font *percent, const esphome::font::Font *detail) {
  prepare_weather_scope_(AirDot::lv_obj(scope), title ? title->get_lv_font() : nullptr,
      percent ? percent->get_lv_font() : nullptr, detail ? detail->get_lv_font() : nullptr);
}
} // namespace AirDot::weather_radar
