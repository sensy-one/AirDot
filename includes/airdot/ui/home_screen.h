#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <lvgl.h>
#include <esp_heap_caps.h>

#include "esphome/core/time.h"

// Helpers for the glow home screen and the calm view on the air-quality summary page.
//
// Redraws on this panel are expensive (a full-screen invalidation takes ~200 ms), and the glow image sits
// behind every widget on the page, so every helper here only writes a property or text when it actually
// changed. The summary refresh runs every second and must not invalidate anything when nothing moved.
namespace AirDot::home_screen {

// Order of the five pollutant arcs, clockwise from the lower left.
enum ArcIndex : int { ARC_PM25 = 0, ARC_CO2, ARC_VOC, ARC_NOX, ARC_PM10, ARC_COUNT };

// Glow image opacity for Good, Moderate and Poor-or-worse. The image itself peaks at 75% alpha, so these
// give roughly 38%, 51% and 75% peak strength.
constexpr lv_opa_t GLOW_OPA_GOOD = 130;
constexpr lv_opa_t GLOW_OPA_MODERATE = 175;
constexpr lv_opa_t GLOW_OPA_ALERT = LV_OPA_COVER;

// Opacity of secondary text (time, temperature, humidity, ring labels).
constexpr lv_opa_t SECONDARY_TEXT_OPA = 150;

// Names and units for air_quality_status_worst_metric, which indexes
// {PM2.5, PM10, CO2, VOC, NOx, PM1, PM4}.
inline const char *worst_metric_name(int index) {
  static const char *const NAMES[] = {"PM₂.₅", "PM₁₀", "CO₂", "VOC", "NOx", "PM₁", "PM₄"};
  if (index < 0 || index >= static_cast<int>(sizeof(NAMES) / sizeof(NAMES[0])))
    return "";
  return NAMES[index];
}

inline const char *worst_metric_unit(int index) {
  switch (index) {
    case 0:
    case 1:
    case 5:
    case 6:
      return " µg/m³";
    case 2:
      return " ppm";
    default:
      return "";
  }
}

inline void set_hidden(lv_obj_t *obj, bool hidden) {
  if (obj == nullptr)
    return;
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hidden)
    return;
  if (hidden) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

inline void set_text(lv_obj_t *label, const char *text) {
  if (label == nullptr || text == nullptr)
    return;
  const char *current = lv_label_get_text(label);
  if (current != nullptr && std::strcmp(current, text) == 0)
    return;
  lv_label_set_text(label, text);
}

inline void set_text_style(lv_obj_t *label, lv_color_t color, lv_opa_t opa) {
  if (label == nullptr)
    return;
  bool changed = false;
  if (!lv_color_eq(lv_obj_get_style_text_color(label, LV_PART_MAIN), color)) {
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    changed = true;
  }
  if (lv_obj_get_style_text_opa(label, LV_PART_MAIN) != opa) {
    lv_obj_set_style_text_opa(label, opa, LV_PART_MAIN);
    changed = true;
  }
  if (changed)
    lv_obj_invalidate(label);
}

inline void set_arc_color(lv_obj_t *arc, lv_color_t color, lv_opa_t opa = LV_OPA_COVER) {
  if (arc == nullptr)
    return;
  if (!lv_color_eq(lv_obj_get_style_arc_color(arc, LV_PART_MAIN), color))
    lv_obj_set_style_arc_color(arc, color, LV_PART_MAIN);
  if (lv_obj_get_style_arc_opa(arc, LV_PART_MAIN) != opa)
    lv_obj_set_style_arc_opa(arc, opa, LV_PART_MAIN);
}

// Track color for arcs without a reading: a faint mix of the text color into the background.
inline lv_color_t arc_off_color(lv_color_t text, lv_color_t background) { return lv_color_mix(text, background, 40); }

inline lv_opa_t glow_opa_for_level(int level) {
  if (level >= 2)
    return GLOW_OPA_ALERT;
  if (level == 1)
    return GLOW_OPA_MODERATE;
  return GLOW_OPA_GOOD;
}

// The arcs are square objects covering most of the screen. Without this filter LVGL runs the arc's
// radius-mask drawing for every redrawn area on the page (including ones well inside the ring, such as the
// status text), which makes ordinary label updates much slower. Skip areas entirely inside the inner circle.
inline void skip_draw_inside_arc_(lv_event_t *e) {
  auto *arc = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
  lv_layer_t *layer = lv_event_get_layer(e);
  if (arc == nullptr || layer == nullptr)
    return;

  lv_area_t coords;
  lv_obj_get_coords(arc, &coords);
  const int32_t cx = (coords.x1 + coords.x2) / 2;
  const int32_t cy = (coords.y1 + coords.y2) / 2;
  const int32_t r_in = lv_area_get_width(&coords) / 2 - lv_obj_get_style_arc_width(arc, LV_PART_MAIN) - 2;
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

inline void install_arc_draw_filter(lv_obj_t *arc) {
  if (arc == nullptr)
    return;
  lv_obj_add_event_cb(arc, skip_draw_inside_arc_,
                      static_cast<lv_event_code_t>(LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS), nullptr);
}

// Formats local time as "21:47" or "9:47 PM" (suffix optional for the large calm clock).
inline void format_time(char *out, size_t size, const esphome::ESPTime &local, bool time_format_12h, bool suffix) {
  if (!time_format_12h) {
    std::snprintf(out, size, "%02d:%02d", local.hour, local.minute);
    return;
  }
  int hour = local.hour % 12;
  if (hour == 0)
    hour = 12;
  if (suffix) {
    std::snprintf(out, size, "%d:%02d %s", hour, local.minute, local.hour >= 12 ? "PM" : "AM");
  } else {
    std::snprintf(out, size, "%d:%02d", hour, local.minute);
  }
}


// Unit and name shown under the alert value, indexed like worst_metric_name().
inline const char *alert_unit_text(int index) {
  static const char *const UNITS[] = {"µg/m³ PM₂.₅", "µg/m³ PM₁₀", "ppm CO₂", "VOC index", "NOx index",
                                      "µg/m³ PM₁", "µg/m³ PM₄"};
  if (index < 0 || index >= static_cast<int>(sizeof(UNITS) / sizeof(UNITS[0])))
    return "";
  return UNITS[index];
}

// Arc that represents a worst-metric index. PM1 and PM4 map to the nearest particle arc.
inline int arc_for_worst_metric(int index) {
  switch (index) {
    case 0:
    case 5:
      return ARC_PM25;
    case 1:
    case 6:
      return ARC_PM10;
    case 2:
      return ARC_CO2;
    case 3:
      return ARC_VOC;
    case 4:
      return ARC_NOX;
    default:
      return -1;
  }
}

inline void replace_token(std::string &text, const char *token, const char *value) {
  const size_t length = std::strlen(token);
  for (size_t position = text.find(token); position != std::string::npos;
       position = text.find(token, position + std::strlen(value)))
    text.replace(position, length, value);
}

inline void format_metric_value(char *out, size_t size, float value) {
  if (std::fabs(value) < 10.0f && std::fabs(value - std::round(value)) > 0.05f) {
    std::snprintf(out, size, "%.1f", value);
  } else {
    std::snprintf(out, size, "%.0f", value);
  }
}

// Keeps one sample per minute for the last hour of each worst-metric index, for the alert trend line.
class TrendTracker {
 public:
  static constexpr int METRICS = 7;
  static constexpr int SLOTS = 61;

  void record(uint32_t minute, const float *values) {
    if (this->filled_ > 0 && minute == this->last_minute_)
      return;
    this->last_minute_ = minute;
    this->head_ = (this->head_ + 1) % SLOTS;
    for (int i = 0; i < METRICS; i++)
      this->values_[i][this->head_] = values[i];
    if (this->filled_ < SLOTS)
      this->filled_++;
  }

  // Change since the oldest sample (up to an hour ago). Needs at least 10 minutes of samples.
  bool change(int metric, float current, float &delta, int &minutes) const {
    if (metric < 0 || metric >= METRICS || this->filled_ < 11 || !std::isfinite(current))
      return false;
    const int oldest = (this->head_ - (this->filled_ - 1) + SLOTS) % SLOTS;
    const float old = this->values_[metric][oldest];
    if (!std::isfinite(old))
      return false;
    delta = current - old;
    minutes = this->filled_ - 1;
    return true;
  }

 private:
  float values_[METRICS][SLOTS]{};
  uint32_t last_minute_{0};
  int head_{0};
  int filled_{0};
};

inline void set_arc_width(lv_obj_t *arc, int32_t width) {
  if (arc == nullptr)
    return;
  if (lv_obj_get_style_arc_width(arc, LV_PART_MAIN) != width)
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
}


// Renders a soft radial glow straight into an opaque RGB565 image, already blended onto the background and
// dithered with triangular noise. Tinting an alpha mask instead leaves visible steps on this 16-bit panel,
// because the red and blue channels only have 32 levels. Re-renders only when color, background or
// strength change.
class GlowRenderer {
 public:
  GlowRenderer(float radius, float peak) : radius_(radius), peak_(peak) {
    this->size_ = static_cast<int>(std::ceil(radius)) * 2;
  }

  // Returns the image source, or nullptr if PSRAM could not be allocated. Sets `changed` when the pixels
  // were re-rendered.
  const void *get(lv_color_t color, lv_color_t background, float strength, bool &changed) {
    changed = false;
    if (!this->ensure_buffer_())
      return nullptr;
    strength = std::clamp(strength, 0.0f, 1.0f);
    if (this->rendered_ && lv_color_eq(color, this->color_) && lv_color_eq(background, this->background_) &&
        std::fabs(strength - this->strength_) < 0.001f)
      return &this->dsc_;
    this->color_ = color;
    this->background_ = background;
    this->strength_ = strength;
    this->render_();
    this->rendered_ = true;
    changed = true;
    return &this->dsc_;
  }

  // Shared falloff: Gaussian-like core that reaches zero exactly at the edge. t2 is (distance / radius)^2.
  static float falloff(float t2) {
    static float lut[1025];
    static bool ready = false;
    if (!ready) {
      for (int i = 0; i <= 1024; i++) {
        const float t = i / 1024.0f;
        lut[i] = std::exp(-t / (0.55f * 0.55f)) * (1.0f - t * t);
      }
      ready = true;
    }
    if (t2 >= 1.0f)
      return 0.0f;
    const float pos = t2 * 1024.0f;
    const int index = static_cast<int>(pos);
    const float frac = pos - index;
    return lut[index] + (lut[index + 1] - lut[index]) * frac;
  }

  // Triangular dither noise in -1..1 for pixel (x, y).
  static float tpdf(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    const float a = (h & 0xFFFF) / 65535.0f;
    const float b = (h >> 16) / 65535.0f;
    return a - b;
  }

  // Quantizes 0..1 channels to RGB565 with the given dither noise (in LSBs).
  static uint16_t to_rgb565(float r, float g, float b, float noise) {
    const int ri = std::clamp(static_cast<int>(std::floor(r * 31.0f + noise + 0.5f)), 0, 31);
    const int gi = std::clamp(static_cast<int>(std::floor(g * 63.0f + noise + 0.5f)), 0, 63);
    const int bi = std::clamp(static_cast<int>(std::floor(b * 31.0f + noise + 0.5f)), 0, 31);
    return static_cast<uint16_t>((ri << 11) | (gi << 5) | bi);
  }

 private:
  bool ensure_buffer_() {
    if (this->data_ != nullptr)
      return true;
    const size_t bytes = static_cast<size_t>(this->size_) * this->size_ * 2;
    this->data_ = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (this->data_ == nullptr)
      return false;
    this->dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    this->dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    this->dsc_.header.w = this->size_;
    this->dsc_.header.h = this->size_;
    this->dsc_.header.stride = this->size_ * 2;
    this->dsc_.data_size = bytes;
    this->dsc_.data = reinterpret_cast<const uint8_t *>(this->data_);
    return true;
  }

  void render_() {
    const float br = this->background_.red / 255.0f, bg = this->background_.green / 255.0f,
                bb = this->background_.blue / 255.0f;
    const float dr = this->color_.red / 255.0f - br, dg = this->color_.green / 255.0f - bg,
                db = this->color_.blue / 255.0f - bb;
    const uint16_t background = to_rgb565(br, bg, bb, 0.0f);
    const float scale = this->peak_ * this->strength_;
    const float center = this->size_ / 2.0f;
    const float inv_r2 = 1.0f / (this->radius_ * this->radius_);
    for (int y = 0; y < this->size_; y++) {
      const float dy = (y + 0.5f) - center;
      uint16_t *row = this->data_ + static_cast<size_t>(y) * this->size_;
      for (int x = 0; x < this->size_; x++) {
        const float dx = (x + 0.5f) - center;
        const float a = falloff((dx * dx + dy * dy) * inv_r2) * scale;
        if (a <= 0.0f) {
          row[x] = background;
          continue;
        }
        row[x] = to_rgb565(br + dr * a, bg + dg * a, bb + db * a, tpdf(x, y));
      }
    }
  }

  float radius_;
  float peak_;
  int size_;
  uint16_t *data_{nullptr};
  lv_image_dsc_t dsc_{};
  lv_color_t color_{};
  lv_color_t background_{};
  float strength_{0.0f};
  bool rendered_{false};
};

// Glow sources shared by every screen: the home glow, and the faint calm glow (calm view, time and radar).
inline GlowRenderer &home_glow() {
  static GlowRenderer glow(205.0f, 0.75f);
  return glow;
}
inline GlowRenderer &calm_glow() {
  static GlowRenderer glow(232.0f, 0.20f);
  return glow;
}
inline GlowRenderer &message_glow() {
  static GlowRenderer glow(205.0f, 0.75f);
  return glow;
}

// Background color of the page (or overlay) an object sits on.
inline lv_color_t page_background(lv_obj_t *obj) {
  if (obj == nullptr)
    return lv_color_black();
  lv_obj_t *parent = lv_obj_get_parent(obj);
  while (parent != nullptr && lv_obj_get_style_bg_opa(parent, LV_PART_MAIN) < LV_OPA_COVER)
    parent = lv_obj_get_parent(parent);
  return parent == nullptr ? lv_color_black() : lv_obj_get_style_bg_color(parent, LV_PART_MAIN);
}

// Shows a glow from `renderer` on `image`, tinted `color` at `strength`.
inline void show_glow(lv_obj_t *image, GlowRenderer &renderer, lv_color_t color, float strength, bool visible) {
  if (image == nullptr)
    return;
  if (!visible) {
    set_hidden(image, true);
    return;
  }
  bool changed = false;
  const void *src = renderer.get(color, page_background(image), strength, changed);
  if (src == nullptr) {
    set_hidden(image, true);
    return;
  }
  if (lv_obj_get_style_image_recolor_opa(image, LV_PART_MAIN) != LV_OPA_TRANSP)
    lv_obj_set_style_image_recolor_opa(image, LV_OPA_TRANSP, LV_PART_MAIN);
  if (lv_obj_get_style_image_opa(image, LV_PART_MAIN) != LV_OPA_COVER)
    lv_obj_set_style_image_opa(image, LV_OPA_COVER, LV_PART_MAIN);
  if (lv_image_get_src(image) != src) {
    lv_image_set_src(image, src);
  } else if (changed) {
    lv_obj_invalidate(image);
  }
  set_hidden(image, false);
}

}  // namespace AirDot::home_screen
