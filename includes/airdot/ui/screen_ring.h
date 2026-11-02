#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <lvgl.h>
#include <esp_heap_caps.h>

namespace AirDot::screen_ring {

// The segment matches the ring style of the screen it sits on: the width of the pollutant arcs on the home
// and alert screens, and a hairline everywhere else (calm view, time, history, radar, messages).
constexpr int32_t SEGMENT_WIDTH = 12;
constexpr int32_t HAIRLINE_WIDTH = 4;

inline int32_t &current_width() {
  static int32_t width = SEGMENT_WIDTH;
  return width;
}

// Angles are in degrees, clockwise from 12 o'clock. A sweep of 360 draws the full ring.
struct RingStyle {
  bool visible{false};
  lv_color_t color{};
  lv_opa_t opa{LV_OPA_COVER};
  float start_angle{0.0f};
  float sweep{360.0f};
};

inline uint8_t channel(float value) {
  return static_cast<uint8_t>(std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

inline RingStyle light_style(bool on, float brightness, float red, float green, float blue) {
  RingStyle style;
  style.visible = on && brightness > 0.0f;
  style.color = lv_color_make(channel(red), channel(green), channel(blue));
  style.opa = std::max<uint8_t>(channel(brightness), 1);
  return style;
}

// The screen ring sits in the bottom gap of the home screen's pollutant arcs, at the same radius and with
// the same rounded ends, so it reads as one more segment instead of covering them. Every page keeps that
// gap free. Progress fills it from left to right. Angles are clockwise from 12 o'clock.
constexpr int32_t GAP_START_DEG = 150;
constexpr int32_t GAP_END_DEG = 210;
constexpr int32_t VALUE_RANGE = 1000;

// Skip drawing for areas entirely inside the ring's inner circle, where it has nothing to draw.
inline void skip_draw_inside_ring_(lv_event_t *e) {
  auto *ring = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
  lv_layer_t *layer = lv_event_get_layer(e);
  if (ring == nullptr || layer == nullptr)
    return;

  lv_area_t coords;
  lv_obj_get_coords(ring, &coords);
  const int32_t cx = (coords.x1 + coords.x2) / 2;
  const int32_t cy = (coords.y1 + coords.y2) / 2;
  const int32_t r_in = lv_area_get_width(&coords) / 2 - lv_obj_get_style_arc_width(ring, LV_PART_INDICATOR) - 2;
  if (r_in <= 0)
    return;

  const lv_area_t &clip = layer->_clip_area;
  for (const int32_t x : {clip.x1, clip.x2}) {
    for (const int32_t y : {clip.y1, clip.y2}) {
      const int32_t dx = x - cx;
      const int32_t dy = y - cy;
      if (dx * dx + dy * dy >= r_in * r_in)
        return;
    }
  }
  lv_event_stop_processing(e);
}

inline void configure_(lv_obj_t *ring) {
  static lv_obj_t *configured = nullptr;
  if (configured == ring)
    return;
  lv_obj_add_event_cb(ring, skip_draw_inside_ring_,
                      static_cast<lv_event_code_t>(LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS), nullptr);
  lv_arc_set_rotation(ring, 270);
  lv_arc_set_bg_angles(ring, GAP_START_DEG, GAP_END_DEG);
  lv_arc_set_range(ring, 0, VALUE_RANGE);
  lv_obj_set_style_arc_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);
  configured = ring;
}

inline void apply(lv_obj_t *ring, const RingStyle &style) {
  if (ring == nullptr)
    return;
  configure_(ring);

  if (!style.visible || style.sweep <= 0.0f) {
    if (!lv_obj_has_flag(ring, LV_OBJ_FLAG_HIDDEN))
      lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  const int32_t width = current_width();
  if (!lv_color_eq(lv_obj_get_style_arc_color(ring, LV_PART_INDICATOR), style.color))
    lv_obj_set_style_arc_color(ring, style.color, LV_PART_INDICATOR);
  if (lv_obj_get_style_arc_opa(ring, LV_PART_INDICATOR) != style.opa)
    lv_obj_set_style_arc_opa(ring, style.opa, LV_PART_INDICATOR);
  if (lv_obj_get_style_arc_width(ring, LV_PART_INDICATOR) != width)
    lv_obj_set_style_arc_width(ring, width, LV_PART_INDICATOR);
  const int32_t value =
      static_cast<int32_t>(std::round(std::clamp(style.sweep, 0.0f, 360.0f) / 360.0f * VALUE_RANGE));
  if (lv_arc_get_value(ring) != value)
    lv_arc_set_value(ring, value);
  lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
  if (lv_obj_has_flag(ring, LV_OBJ_FLAG_HIDDEN))
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(ring);
}

}  // namespace AirDot::screen_ring
