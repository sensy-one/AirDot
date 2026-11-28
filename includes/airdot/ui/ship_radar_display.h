#pragma once

// Ship radar page: AIS vessels over the marine (coastline) map. Reuses the
// flight radar's scope drawing helpers so both radars look alike.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <new>

#include <lvgl.h>

#include "esphome/components/font/font.h"
#include "esphome/core/hal.h"

#include "display_runtime.h"
#include "flight_radar_display.h"
#include "radar_map_display.h"
#include "setup_settings.h"
#include "ship_radar_client.h"

namespace AirDot::ship_radar {

using namespace AirDot::flight_radar;

inline constexpr int SHIP_BOW_PX = 9;
inline constexpr int SHIP_SHOULDER_PX = 2;
inline constexpr int SHIP_STERN_PX = 6;
inline constexpr int SHIP_BEAM_PX = 4;
inline constexpr int SHIP_DIAMOND_PX = 4;
inline constexpr int SHIP_MAX_LABELS = MAX_LABELS;
inline constexpr int SHIP_LABEL_GAP_PX = 3;  // Minimum clear space between two labels.
inline constexpr size_t SHIP_LABEL_CHARS = 14;
inline constexpr radar_map::MapKind MAP_KIND = radar_map::MapKind::MARINE;

struct ShipScopeState {
  lv_obj_t *scope{nullptr};
  const lv_font_t *label_font{nullptr};
  const lv_font_t *bearing_font{nullptr};
  const lv_font_t *progress_font{nullptr};
  float map_latitude{0.0f};
  float map_longitude{0.0f};
  uint8_t map_range_km{AirDot::onboarding::SHIP_RADAR_RANGE_DEFAULT_KM};
  bool map_location_valid{false};
};

inline ShipScopeState &ship_scope_state_() {
  static ShipScopeState state{};
  return state;
}

inline int ship_traffic_radius_(int radius) { return std::max(20, radius - RADAR_TRAFFIC_EDGE_RESERVE_PX); }

inline RadarMapLoadingPhase ship_map_loading_phase_(const ShipScopeState &state) {
  if (state.map_location_valid &&
      radar_map::has_map(state.map_latitude, state.map_longitude, state.map_range_km, MAP_KIND)) {
    const lv_area_t area{0, 0, 479, 479};
    const int radius = 240 - RADAR_SCREEN_MARGIN;
    return radar_map::prepare_map(area, state.map_latitude, state.map_longitude, state.map_range_km,
                                  ship_traffic_radius_(radius), radius,
                                  AirDot::onboarding::load_dark_mode_enabled(), MAP_KIND)
               ? RadarMapLoadingPhase::HIDDEN
               : RadarMapLoadingPhase::LOADING;
  }
  // No location: show the scope with its "Location missing" status instead of a loading screen.
  if (!state.map_location_valid) return RadarMapLoadingPhase::HIDDEN;
  const auto status = radar_map::status_for(state.map_latitude, state.map_longitude, state.map_range_km, MAP_KIND);
  if (status == radar_map::Status::OFFLINE || status == radar_map::Status::UNAVAILABLE)
    return RadarMapLoadingPhase::HIDDEN;
  if (status == radar_map::Status::LOADING) return RadarMapLoadingPhase::DOWNLOADING;
  if (status == radar_map::Status::FAILED) return RadarMapLoadingPhase::RETRYING;
  return RadarMapLoadingPhase::LOADING;
}
inline bool ship_map_loading_visible_() {
  return ship_map_loading_phase_(ship_scope_state_()) != RadarMapLoadingPhase::HIDDEN;
}
inline RadarMapProgress ship_map_loading_progress_(const ShipScopeState &state) {
  const auto phase = ship_map_loading_phase_(state);
  if (phase == RadarMapLoadingPhase::HIDDEN) return {100};
  if (phase == RadarMapLoadingPhase::LOADING && state.map_location_valid &&
      radar_map::has_map(state.map_latitude, state.map_longitude, state.map_range_km, MAP_KIND))
    return {99};
  if (phase != RadarMapLoadingPhase::DOWNLOADING) return {};
  radar_map::DownloadProgress progress;
  if (!radar_map::progress_for_key(state.map_latitude, state.map_longitude, state.map_range_km, progress, MAP_KIND))
    return {};
  return {static_cast<uint8_t>(std::min<unsigned>(99, progress.percentage))};
}

// The radar's own location if set, else the device location, else the
// location of the cached marine map.
inline void configure_ship_radar_location(float outdoor_latitude, float outdoor_longitude, bool outdoor_valid) {
  auto &state = ship_scope_state_();
  const uint8_t range = AirDot::onboarding::load_ship_radar_range_km();
  float latitude = 0.0f, longitude = 0.0f;
  bool valid = AirDot::onboarding::load_ship_radar_custom_location(latitude, longitude);
  if (!valid) valid = AirDot::onboarding::load_exact_location(latitude, longitude);
  if (!valid && outdoor_valid) {
    latitude = outdoor_latitude;
    longitude = outdoor_longitude;
    valid = radar_map::valid_coordinates(latitude, longitude);
  }
  if (!valid) valid = radar_map::cached_location_for_range(range, latitude, longitude, MAP_KIND);
  if (valid) radar_map::restore_cached_map(latitude, longitude, range, MAP_KIND);
  const bool changed = state.map_location_valid != valid || state.map_latitude != latitude ||
                       state.map_longitude != longitude || state.map_range_km != range;
  state.map_location_valid = valid;
  state.map_latitude = latitude;
  state.map_longitude = longitude;
  state.map_range_km = range;
  if (changed && state.scope != nullptr) lv_obj_invalidate(state.scope);
}

inline lv_color_t ship_favorite_color_() { return AirDot::color(0xFF, 0xC8, 0x00); }
inline lv_color_t ship_stationary_color_() {
  return lv_color_mix(radar_target_color_(), radar_background_color_(), 150);
}
inline lv_color_t ship_color_(const RenderVessel &vessel) {
  if (vessel.favorite) return ship_favorite_color_();
  return vessel.moving ? radar_target_color_() : ship_stationary_color_();
}

inline lv_point_t ship_point_(int cx, int cy, float sin_h, float cos_h, float forward, float right) {
  return lv_point_t{
      static_cast<lv_coord_t>(cx + static_cast<int>(std::round(sin_h * forward + cos_h * right))),
      static_cast<lv_coord_t>(cy + static_cast<int>(std::round(-cos_h * forward + sin_h * right))),
  };
}

inline void draw_ship_marker_(lv_layer_t *layer, const RenderVessel &vessel, int x, int y, lv_color_t color) {
  if (!vessel.moving || !std::isfinite(vessel.heading_deg)) {
    const lv_point_t top{static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y - SHIP_DIAMOND_PX)};
    const lv_point_t bottom{static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y + SHIP_DIAMOND_PX)};
    const lv_point_t left{static_cast<lv_coord_t>(x - SHIP_DIAMOND_PX), static_cast<lv_coord_t>(y)};
    const lv_point_t right{static_cast<lv_coord_t>(x + SHIP_DIAMOND_PX), static_cast<lv_coord_t>(y)};
    draw_filled_triangle_(layer, color, RadarTriangle{{top, left, right}});
    draw_filled_triangle_(layer, color, RadarTriangle{{bottom, left, right}});
    return;
  }
  const float heading = vessel.heading_deg * DEG_TO_RAD;
  const float sin_h = std::sin(heading), cos_h = std::cos(heading);
  const lv_point_t bow = ship_point_(x, y, sin_h, cos_h, SHIP_BOW_PX, 0);
  const lv_point_t port = ship_point_(x, y, sin_h, cos_h, SHIP_SHOULDER_PX, -SHIP_BEAM_PX);
  const lv_point_t starboard = ship_point_(x, y, sin_h, cos_h, SHIP_SHOULDER_PX, SHIP_BEAM_PX);
  const lv_point_t stern_port = ship_point_(x, y, sin_h, cos_h, -SHIP_STERN_PX, -SHIP_BEAM_PX);
  const lv_point_t stern_starboard = ship_point_(x, y, sin_h, cos_h, -SHIP_STERN_PX, SHIP_BEAM_PX);
  draw_filled_triangle_(layer, color, RadarTriangle{{bow, port, starboard}});
  draw_filled_triangle_(layer, color, RadarTriangle{{port, starboard, stern_starboard}});
  draw_filled_triangle_(layer, color, RadarTriangle{{port, stern_starboard, stern_port}});
}

// Draw scratch, all in PSRAM: the LVGL draw callback runs deep in the main
// loop's stack, and LVGL draws the scope in several strips per refresh.
struct ShipDrawBuffers {
  RenderList list;
  RadarAircraftRender markers[MAX_RENDER];
  int pixel_x[MAX_RENDER], pixel_y[MAX_RENDER], order[MAX_RENDER];
  RadarLabelPlacement placed[SHIP_MAX_LABELS];
  char texts[SHIP_MAX_LABELS][SHIP_LABEL_CHARS + 1];
};
inline ShipDrawBuffers *ship_draw_buffers_() {
  static ShipDrawBuffers *buffers = [] {
    void *memory = heap_caps_calloc(1, sizeof(ShipDrawBuffers), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return memory != nullptr ? new (memory) ShipDrawBuffers{} : nullptr;
  }();
  return buffers;
}

// Rebuilds the vessel list from the cache. Called from the 1 s interval (and
// on page refresh), never from the draw callback, so every strip of a frame
// shows the same list and the cache lock is not taken while drawing.
inline void refresh_ship_render_list() {
  auto *buffers = ship_draw_buffers_();
  const auto &state = ship_scope_state_();
  if (buffers == nullptr) return;
  if (!state.map_location_valid) {
    buffers->list.count = 0;
    return;
  }
  RenderList &list = buffers->list;
  if (!copy_render_list(list, state.map_latitude, state.map_longitude, state.map_range_km,
                        AirDot::onboarding::load_ship_radar_category_mask(), esphome::millis()))
    list.count = 0;
}

inline void draw_ships_(lv_layer_t *layer, const lv_area_t &scope_area, const lv_font_t *font, int center_x,
                        int center_y, int traffic_radius) {
  auto *buffers = ship_draw_buffers_();
  if (buffers == nullptr || buffers->list.count == 0) return;
  const RenderList &list = buffers->list;
  RadarAircraftRender *markers = buffers->markers;
  int *pixel_x = buffers->pixel_x, *pixel_y = buffers->pixel_y;
  int marker_count = 0;
  for (int i = 0; i < list.count; ++i) {
    const auto &vessel = list.vessels[i];
    pixel_x[i] = center_x + static_cast<int>(std::lround(vessel.x * traffic_radius / radar_map::NORMALIZED_RADIUS));
    pixel_y[i] = center_y + static_cast<int>(std::lround(vessel.y * traffic_radius / radar_map::NORMALIZED_RADIUS));
    markers[marker_count++] = RadarAircraftRender{pixel_x[i], pixel_y[i], 0.0f, vessel.heading_deg, ship_color_(vessel),
                                                  nullptr};
  }

  // Favourites last so they stay on top.
  for (int pass = 0; pass < 2; ++pass)
    for (int i = 0; i < list.count; ++i)
      if (list.vessels[i].favorite == (pass == 1))
        draw_ship_marker_(layer, list.vessels[i], pixel_x[i], pixel_y[i], ship_color_(list.vessels[i]));

  // Names for the labelled vessels, in priority order (favourites, then the
  // largest or fastest). The MMSI is never shown. A label that would still
  // touch an already placed one after placement is dropped.
  int *order = buffers->order;
  int labelled = 0;
  for (int i = 0; i < list.count; ++i)
    if (list.vessels[i].labelled) order[labelled++] = i;
  std::sort(order, order + labelled,
            [&](int a, int b) { return list.vessels[a].label_rank < list.vessels[b].label_rank; });
  labelled = std::min(labelled, SHIP_MAX_LABELS);

  RadarLabelPlacement *placed = buffers->placed;
  auto &texts = buffers->texts;
  int placed_count = 0;
  for (int n = 0; n < labelled; ++n) {
    const int i = order[n];
    const auto &vessel = list.vessels[i];
    std::snprintf(texts[placed_count], sizeof(texts[placed_count]), "%.*s", static_cast<int>(SHIP_LABEL_CHARS),
                  vessel.name);
    const float bearing = std::atan2(vessel.x, -vessel.y);
    const lv_color_t color = ship_color_(vessel);
    RadarLabelPlacement label = place_aircraft_label_(scope_area, font, color, center_x, pixel_x[i], center_y,
                                                      pixel_y[i], bearing, vessel.heading_deg, texts[placed_count],
                                                      placed, placed_count, markers, marker_count);
    const lv_area_t padded = expand_area_(label.area, SHIP_LABEL_GAP_PX);
    bool collides = false;
    for (int p = 0; p < placed_count && !collides; ++p) collides = areas_intersect_(padded, placed[p].area);
    if (collides) continue;
    draw_pixel_line_(layer, color, label.connector_kink.x, label.connector_kink.y, label.connector_end.x,
                     label.connector_end.y, 1);
    draw_pixel_line_(layer, color, label.connector_kink.x, label.connector_kink.y, label.connector_start.x,
                     label.connector_start.y, 1);
    draw_label_in_area_(layer, label);
    placed[placed_count++] = label;
  }
}

inline const char *stream_status_text_(StreamStatus status, UiLanguage language) {
  switch (status) {
    case StreamStatus::NO_KEY: return AirDot::ship_radar_key_missing_text(language);
    case StreamStatus::NO_LOCATION: return AirDot::ship_radar_location_missing_text(language);
    case StreamStatus::OFFLINE: return AirDot::ship_radar_offline_text(language);
    case StreamStatus::CONNECTING: return AirDot::ship_radar_connecting_text(language);
    case StreamStatus::RETRY_WAIT: return AirDot::ship_radar_reconnecting_text(language);
    case StreamStatus::KEY_REJECTED: return AirDot::ship_radar_key_rejected_text(language);
    default: return nullptr;
  }
}

inline void draw_ship_scope_(lv_event_t *event) {
  auto *state = static_cast<ShipScopeState *>(lv_event_get_user_data(event));
  lv_obj_t *scope = static_cast<lv_obj_t *>(lv_event_get_target(event));
  lv_layer_t *layer = lv_event_get_layer(event);
  if (state == nullptr || scope == nullptr || layer == nullptr || scope != state->scope) return;

  lv_area_t area;
  lv_obj_get_coords(scope, &area);
  draw_rect_(layer, area, radar_background_color_());

  const int width = radar_area_width_(area), height = radar_area_height_(area);
  const int size = std::min(width, height);
  const int center_x = area.x1 + width / 2, center_y = area.y1 + height / 2;
  const int radius = std::max(20, size / 2 - RADAR_SCREEN_MARGIN);
  const int traffic_radius = ship_traffic_radius_(radius);
  const lv_color_t text_color = radar_target_color_();

  const auto loading = ship_map_loading_phase_(*state);
  if (loading != RadarMapLoadingPhase::HIDDEN) {
    const lv_font_t *title_font = state->bearing_font != nullptr ? state->bearing_font : lv_font_get_default();
    const lv_font_t *percent_font = state->progress_font != nullptr ? state->progress_font : title_font;
    const auto progress = ship_map_loading_progress_(*state);
    char percent[12];
    std::snprintf(percent, sizeof(percent), "%u%%", static_cast<unsigned>(progress.percent));
    const char *title = radar_map_loading_text_(loading);
    lv_point_t title_size{}, percent_size{};
    lv_text_get_size(&title_size, title, title_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_text_get_size(&percent_size, percent, percent_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    constexpr int gap = 10;
    const int top = center_y - (title_size.y + gap + percent_size.y) / 2;
    draw_label_(layer, area, title_font, text_color, center_x - title_size.x / 2, top, title, LV_TEXT_ALIGN_CENTER);
    draw_label_(layer, area, percent_font, text_color, center_x - percent_size.x / 2, top + title_size.y + gap,
                percent, LV_TEXT_ALIGN_CENTER);
    return;
  }

  if (state->map_location_valid)
    radar_map::draw_map(layer, area, state->map_latitude, state->map_longitude, state->map_range_km, traffic_radius,
                        radius, AirDot::onboarding::load_dark_mode_enabled(), MAP_KIND);
  draw_compass_(layer, area, state->bearing_font, center_x, center_y, radius);
  const auto *font = state->label_font != nullptr ? state->label_font : lv_font_get_default();
  if (state->map_location_valid) draw_ships_(layer, area, font, center_x, center_y, traffic_radius);

  const bool imperial = AirDot::onboarding::load_unit_system() == AirDot::onboarding::UNIT_SYSTEM_IMPERIAL;
  const unsigned range =
      imperial ? static_cast<unsigned>(std::lround(state->map_range_km * 0.621371192)) : state->map_range_km;
  char range_text[16];
  std::snprintf(range_text, sizeof(range_text), "%u %s", range, imperial ? "MI" : "KM");
  lv_point_t text_size{};
  lv_text_get_size(&text_size, range_text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  draw_label_(layer, area, font, text_color, area.x1 + (width - text_size.x) / 2, area.y1 + 425 - text_size.y / 2,
              range_text, LV_TEXT_ALIGN_CENTER);

  const auto language = AirDot::onboarding::load_ui_language();
  const char *status = state->map_location_valid ? stream_status_text_(stream_status(), language)
                                                 : AirDot::ship_radar_location_missing_text(language);
  if (status != nullptr) {
    const lv_font_t *status_font = state->bearing_font != nullptr ? state->bearing_font : font;
    lv_text_get_size(&text_size, status, status_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    draw_label_(layer, area, status_font, text_color, area.x1 + (width - text_size.x) / 2,
                area.y1 + 398 - text_size.y / 2, status, LV_TEXT_ALIGN_CENTER);
  }
}

inline void prepare_ship_scope_(lv_obj_t *scope, const lv_font_t *label_font, const lv_font_t *bearing_font,
                                const lv_font_t *progress_font = nullptr) {
  if (scope == nullptr) return;
  auto &state = ship_scope_state_();
  state.label_font = label_font;
  state.bearing_font = bearing_font != nullptr ? bearing_font : label_font;
  state.progress_font = progress_font;
  disable_radar_antialiasing_(scope);
  if (state.scope != scope) {
    state.scope = scope;
    lv_obj_add_event_cb(scope, draw_ship_scope_, LV_EVENT_DRAW_MAIN, &state);
  }
  lv_obj_set_style_bg_color(scope, radar_background_color_(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scope, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(scope, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(scope, 0, LV_PART_MAIN);
  lv_obj_remove_flag(scope, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_invalidate(scope);
}

template<typename ScopeWidget>
inline void refresh_ship_scope(ScopeWidget *scope, const esphome::font::Font *label_font,
                               const esphome::font::Font *bearing_font,
                               const esphome::font::Font *progress_font = nullptr) {
  prepare_ship_scope_(AirDot::lv_obj(scope), label_font == nullptr ? nullptr : label_font->get_lv_font(),
                      bearing_font == nullptr ? nullptr : bearing_font->get_lv_font(),
                      progress_font == nullptr ? nullptr : progress_font->get_lv_font());
}

inline void invalidate_ship_scope() {
  auto *scope = ship_scope_state_().scope;
  if (scope != nullptr) lv_obj_invalidate(scope);
}

}  // namespace AirDot::ship_radar
