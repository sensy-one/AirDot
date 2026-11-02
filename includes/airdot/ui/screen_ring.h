#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <lvgl.h>

namespace AirDot::screen_ring {

constexpr float DEFAULT_THICKNESS = 8.0f;
constexpr float MAX_THICKNESS = 40.0f;

// Angles are in degrees, clockwise from 12 o'clock. A sweep of 360 draws the full ring.
struct RingStyle {
  bool visible{false};
  lv_color_t color{};
  lv_opa_t opa{LV_OPA_COVER};
  int32_t width{static_cast<int32_t>(DEFAULT_THICKNESS)};
  float start_angle{0.0f};
  float sweep{360.0f};
};

inline uint8_t channel(float value) {
  return static_cast<uint8_t>(std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

inline int32_t ring_width(float thickness) {
  if (std::isnan(thickness))
    thickness = DEFAULT_THICKNESS;
  return static_cast<int32_t>(std::clamp(std::round(thickness), 1.0f, MAX_THICKNESS));
}

inline RingStyle light_style(bool on, float brightness, float red, float green, float blue, float thickness) {
  RingStyle style;
  style.visible = on && brightness > 0.0f;
  style.color = lv_color_make(channel(red), channel(green), channel(blue));
  style.opa = std::max<uint8_t>(channel(brightness), 1);
  style.width = ring_width(thickness);
  return style;
}

inline void set_sweep(lv_obj_t *ring, float start_angle, float sweep) {
  sweep = std::clamp(sweep, 0.0f, 360.0f);
  start_angle = std::fmod(start_angle, 360.0f);
  if (start_angle < 0.0f)
    start_angle += 360.0f;
  // The arc widget is rotated by 270 degrees, so angle 0 is at 12 o'clock.
  lv_arc_set_angles(ring, start_angle, start_angle + sweep);
}

// The arc object covers the whole screen, so LVGL would run the ring's radius-mask drawing for every
// redrawn area of the UI, adding ~30 ms to ordinary page refreshes. Skip drawing for areas that lie
// entirely inside the ring's inner circle, where the ring has nothing to draw.
inline void skip_draw_inside_ring_(lv_event_t *e) {
  auto *ring = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
  lv_layer_t *layer = lv_event_get_layer(e);
  if (ring == nullptr || layer == nullptr)
    return;

  lv_area_t coords;
  lv_obj_get_coords(ring, &coords);
  const int32_t cx = (coords.x1 + coords.x2) / 2;
  const int32_t cy = (coords.y1 + coords.y2) / 2;
  // Two pixels of slack for anti-aliasing.
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

inline void install_draw_filter_(lv_obj_t *ring) {
  static lv_obj_t *installed = nullptr;
  if (installed == ring)
    return;
  lv_obj_add_event_cb(ring, skip_draw_inside_ring_,
                      static_cast<lv_event_code_t>(LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS), nullptr);
  installed = ring;
}

// Every style change invalidates the whole 480x480 arc object, which takes ~200 ms to redraw, so only
// properties that actually changed are written. Angle changes invalidate just the affected slice.
inline void apply(lv_obj_t *ring, const RingStyle &style) {
  if (ring == nullptr)
    return;
  install_draw_filter_(ring);

  if (!style.visible || style.sweep <= 0.0f) {
    if (!lv_obj_has_flag(ring, LV_OBJ_FLAG_HIDDEN))
      lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  if (!lv_color_eq(lv_obj_get_style_arc_color(ring, LV_PART_INDICATOR), style.color))
    lv_obj_set_style_arc_color(ring, style.color, LV_PART_INDICATOR);
  if (lv_obj_get_style_arc_opa(ring, LV_PART_INDICATOR) != style.opa)
    lv_obj_set_style_arc_opa(ring, style.opa, LV_PART_INDICATOR);
  if (lv_obj_get_style_arc_width(ring, LV_PART_INDICATOR) != style.width)
    lv_obj_set_style_arc_width(ring, style.width, LV_PART_INDICATOR);
  set_sweep(ring, style.start_angle, style.sweep);
  lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
  if (lv_obj_has_flag(ring, LV_OBJ_FLAG_HIDDEN))
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(ring);
}

}  // namespace AirDot::screen_ring
