#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <esp_heap_caps.h>
#include <lvgl.h>

namespace AirDot {

inline constexpr const char *VALUE_PLACEHOLDER_TEXT = "--";
inline constexpr int32_t VALUE_PLACEHOLDER_LETTER_SPACE = 1;

inline void set_value_placeholder_spacing(lv_obj_t *label, bool placeholder) {
  if (label == nullptr)
    return;
  lv_obj_set_style_text_letter_space(label, placeholder ? VALUE_PLACEHOLDER_LETTER_SPACE : 0, LV_PART_MAIN);
}

// Edge-to-edge history graph for the per-sensor history pages.
//
// The last 24 hours (5-minute buckets) are drawn as a filled area that runs under the round edge of the
// screen, with a white line on top and a soft glow behind the value. For air-quality metrics the fill is
// shaded by height using the configured status thresholds (Good, Moderate, Poor, ...); other metrics use
// their metric color. Until enough 5-minute buckets exist, the last 15 minutes of 5-second samples are used.
//
// The whole page background is rendered into one shared, opaque 480x480 RGB565 buffer in PSRAM (blended onto
// the page background and dithered so gradients don't band), used by every
// history page's image widget. Only the visible page renders into it: on page load, and when new data
// arrives (at most every RENDER_INTERVAL_MS, or right away when a new bucket starts).
class SensorHistoryGraph {
 public:
  static constexpr int SCREEN = 480;
  static constexpr uint32_t GRAPH_HISTORY_MS = 15UL * 60UL * 1000UL;
  static constexpr uint32_t WINDOW_STEP_MS = 5UL * 60UL * 1000UL;
  static constexpr uint32_t STAT_HISTORY_MS = 24UL * 60UL * 60UL * 1000UL;
  static constexpr uint32_t SAMPLE_BUCKET_MS = 5UL * 1000UL;
  static constexpr int POINT_COUNT = static_cast<int>(GRAPH_HISTORY_MS / SAMPLE_BUCKET_MS);
  static constexpr uint32_t STAT_BUCKET_MS = 5 * 60 * 1000;
  static constexpr int STAT_BUCKET_COUNT = static_cast<int>(STAT_HISTORY_MS / STAT_BUCKET_MS);
  static constexpr uint32_t RENDER_INTERVAL_MS = 30UL * 1000UL;

  // Screen rows of the graph: the highest value sits at CURVE_TOP, the lowest at CURVE_BOTTOM, and the fill
  // continues to the bottom edge.
  static constexpr int CURVE_TOP = 262;
  static constexpr int CURVE_BOTTOM = 420;
  static constexpr int FILL_TOP = CURVE_TOP;
  static constexpr float FILL_OPA_TOP = 0.65f;
  static constexpr float FILL_OPA_BOTTOM = 0.05f;
  static constexpr float LINE_HALF_WIDTH = 1.75f;
  static constexpr float LINE_OPA = 0.85f;
  // Glow behind the value: centered above the graph.
  static constexpr int GLOW_CENTER_Y = 200;
  static constexpr float GLOW_RADIUS = 205.0f;
  static constexpr float GLOW_OPA = 0.30f;

  enum class ColorMode : uint8_t {
    NONE,
    HIGH_BAD,
  };

  void reset() {
    for (auto &bucket : this->sample_buckets_)
      bucket = {};
    for (auto &bucket : this->stat_buckets_)
      bucket = {};

    this->count_ = 0;
    this->history_start_ms_ = 0;
    this->history_started_ = false;
    this->last_render_ms_ = 0;
    this->last_render_bucket_ = 0;
    if (owner_() == this)
      owner_() = nullptr;
  }

  static void set_value_label(lv_obj_t *label, float value, const char *format) {
    if (label == nullptr)
      return;
    if (!std::isfinite(value)) {
      set_value_placeholder_spacing(label, true);
      lv_label_set_text(label, VALUE_PLACEHOLDER_TEXT);
      return;
    }

    set_value_placeholder_spacing(label, false);
    char text[24];
    std::snprintf(text, sizeof(text), format == nullptr ? "%.0f" : format, value);
    lv_label_set_text(label, text);
  }

  template<typename LabelWidget> static void set_value_label(LabelWidget *label, float value, const char *format) {
    set_value_label(as_obj_(label), value, format);
  }

  template<typename LabelWidget> static void set_pattern_label(LabelWidget *label, const char *pattern,
                                                               const char *value) {
    set_pattern_label_(as_obj_(label), pattern, value);
  }

  // Renders the graph for the history page whose graph image is `image` (call from the page's on_load).
  template<typename ImageWidget> static void render_for(ImageWidget *image) {
    lv_obj_t *obj = as_obj_(image);
    for (auto &entry : registry_()) {
      if (entry.image == obj && entry.graph != nullptr) {
        entry.graph->render_(obj);
        return;
      }
    }
  }

  template<typename LineWidget, typename MinLabelWidget, typename MaxLabelWidget, typename EmptyLabelWidget>
  void update(LineWidget *line, float value, MinLabelWidget *min_label, MaxLabelWidget *max_label,
              EmptyLabelWidget *empty_label, const char *format, const char *min_pattern = nullptr,
              const char *max_pattern = nullptr) {
    this->color_mode_ = ColorMode::NONE;
    this->fixed_range_ = false;
    this->update_obj_(this->as_obj_(line), value, this->as_obj_(min_label), this->as_obj_(max_label),
                      this->as_obj_(empty_label), format, min_pattern, max_pattern);
  }

  template<typename LineWidget, typename MinLabelWidget, typename MaxLabelWidget, typename EmptyLabelWidget>
  void update_colored(LineWidget *line, float value, MinLabelWidget *min_label, MaxLabelWidget *max_label,
                      EmptyLabelWidget *empty_label, const char *format, float graph_min, float graph_max,
                      float balanced_value, float moderate_value, float poor_value, float unhealthy_value,
                      bool balanced_inclusive = true, bool moderate_inclusive = true, bool poor_inclusive = true,
                      bool unhealthy_inclusive = true, const char *min_pattern = nullptr,
                      const char *max_pattern = nullptr) {
    this->fixed_range_ = graph_min < graph_max;
    this->color_mode_ = balanced_value < moderate_value && moderate_value < poor_value &&
                                poor_value < unhealthy_value
                            ? ColorMode::HIGH_BAD
                            : ColorMode::NONE;
    this->fixed_min_ = graph_min;
    this->fixed_max_ = graph_max;
    this->balanced_value_ = balanced_value;
    this->moderate_value_ = moderate_value;
    this->poor_value_ = poor_value;
    this->unhealthy_value_ = unhealthy_value;
    this->balanced_inclusive_ = balanced_inclusive;
    this->moderate_inclusive_ = moderate_inclusive;
    this->poor_inclusive_ = poor_inclusive;
    this->unhealthy_inclusive_ = unhealthy_inclusive;
    this->update_obj_(this->as_obj_(line), value, this->as_obj_(min_label), this->as_obj_(max_label),
                      this->as_obj_(empty_label), format, min_pattern, max_pattern);
  }

 protected:
  static lv_obj_t *as_obj_(lv_obj_t *obj) { return obj; }

  template<typename Widget> static lv_obj_t *as_obj_(Widget *widget) {
    if (widget == nullptr)
      return nullptr;
    return widget->obj;
  }

  static void set_hidden_(lv_obj_t *obj, bool hidden) {
    if (obj == nullptr)
      return;
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hidden)
      return;
    if (hidden) {
      lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
  }

  struct RegistryEntry {
    lv_obj_t *image{nullptr};
    SensorHistoryGraph *graph{nullptr};
  };

  static std::array<RegistryEntry, 16> &registry_() {
    static std::array<RegistryEntry, 16> entries{};
    return entries;
  }

  static SensorHistoryGraph *&owner_() {
    static SensorHistoryGraph *owner = nullptr;
    return owner;
  }

  static lv_image_dsc_t *shared_dsc_() {
    static lv_image_dsc_t dsc{};
    static bool initialized = false;
    if (!initialized) {
      const size_t size = static_cast<size_t>(SCREEN) * SCREEN * 2;
      auto *data = static_cast<uint8_t *>(heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (data == nullptr)
        return nullptr;
      dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
      dsc.header.cf = LV_COLOR_FORMAT_RGB565;
      dsc.header.w = SCREEN;
      dsc.header.h = SCREEN;
      dsc.header.stride = SCREEN * 2;
      dsc.data_size = size;
      dsc.data = data;
      initialized = true;
    }
    return &dsc;
  }

  void register_image_(lv_obj_t *image) {
    if (this->image_ == image)
      return;
    this->image_ = image;
    for (auto &entry : registry_()) {
      if (entry.image == image || entry.image == nullptr) {
        entry.image = image;
        entry.graph = this;
        break;
      }
    }
    lv_image_dsc_t *dsc = shared_dsc_();
    if (dsc != nullptr)
      lv_image_set_src(image, dsc);
  }

  static bool on_active_screen_(lv_obj_t *obj) {
    return obj != nullptr && lv_obj_get_screen(obj) == lv_screen_active();
  }

  void update_obj_(lv_obj_t *image, float value, lv_obj_t *min_label, lv_obj_t *max_label, lv_obj_t *empty_label,
                   const char *format, const char *min_pattern, const char *max_pattern) {
    if (image == nullptr)
      return;
    this->register_image_(image);
    if (!std::isfinite(value)) {
      set_hidden_(empty_label, false);
      set_pattern_label_(min_label, min_pattern, VALUE_PLACEHOLDER_TEXT);
      set_pattern_label_(max_label, max_pattern, VALUE_PLACEHOLDER_TEXT);
      this->count_ = 0;
      this->current_value_ = NAN;
      if (on_active_screen_(image))
        this->render_(image);
      return;
    }

    set_hidden_(empty_label, true);
    this->current_value_ = value;
    this->record_sample_(value);
    this->record_stat_(value);
    this->update_stat_labels_(min_label, max_label, format, min_pattern, max_pattern);

    if (!on_active_screen_(image))
      return;
    const uint32_t now = esphome::millis();
    const uint32_t bucket = static_cast<uint32_t>(esphome::millis_64() / STAT_BUCKET_MS);
    if (owner_() != this || this->last_render_ms_ == 0 || bucket != this->last_render_bucket_ ||
        now - this->last_render_ms_ >= RENDER_INTERVAL_MS)
      this->render_(image);
  }

  struct SampleBucket {
    uint32_t index{0};
    float sum{0.0f};
    uint16_t count{0};
    bool valid{false};
  };

  struct StatBucket {
    uint32_t index{0};
    float minimum{0.0f};
    float maximum{0.0f};
    float sum{0.0f};
    uint16_t count{0};
    bool valid{false};
  };

  void record_sample_(float value) {
    const uint64_t now = esphome::millis_64();
    if (!this->history_started_) {
      this->history_start_ms_ = now;
      this->history_started_ = true;
    }

    const uint32_t bucket_index = static_cast<uint32_t>(now / SAMPLE_BUCKET_MS);
    auto &bucket = this->sample_buckets_[bucket_index % POINT_COUNT];
    if (!bucket.valid || bucket.index != bucket_index) {
      bucket.index = bucket_index;
      bucket.sum = value;
      bucket.count = 1;
      bucket.valid = true;
      return;
    }

    bucket.sum += value;
    if (bucket.count < UINT16_MAX)
      bucket.count++;
  }

  void record_stat_(float value) {
    const uint32_t bucket_index = static_cast<uint32_t>(esphome::millis_64() / STAT_BUCKET_MS);
    auto &bucket = this->stat_buckets_[bucket_index % STAT_BUCKET_COUNT];
    if (!bucket.valid || bucket.index != bucket_index) {
      bucket.index = bucket_index;
      bucket.minimum = value;
      bucket.maximum = value;
      bucket.sum = value;
      bucket.count = 1;
      bucket.valid = true;
      return;
    }

    bucket.minimum = std::min(bucket.minimum, value);
    bucket.maximum = std::max(bucket.maximum, value);
    bucket.sum += value;
    if (bucket.count < UINT16_MAX)
      bucket.count++;
  }

  // Fills `out` oldest to newest and returns the number of points. Uses 5-minute bucket averages for up to
  // 24 hours, or the 5-second samples of the last 15 minutes while fewer than three buckets exist.
  int build_series_(float *out) const {
    const uint32_t current_bucket = static_cast<uint32_t>(esphome::millis_64() / STAT_BUCKET_MS);
    int count = 0;
    bool have_value = false;
    float last_value = 0.0f;
    int valid_buckets = 0;
    for (int i = 0; i < STAT_BUCKET_COUNT; i++) {
      const uint32_t bucket_index = current_bucket - (STAT_BUCKET_COUNT - 1) + i;
      const auto &bucket = this->stat_buckets_[bucket_index % STAT_BUCKET_COUNT];
      if (bucket.valid && bucket.index == bucket_index && bucket.count > 0) {
        last_value = bucket.sum / bucket.count;
        have_value = true;
        valid_buckets++;
      }
      if (!have_value)
        continue;
      out[count++] = last_value;
    }
    if (valid_buckets >= 3)
      return count;

    const uint32_t current_sample = static_cast<uint32_t>(esphome::millis_64() / SAMPLE_BUCKET_MS);
    count = 0;
    have_value = false;
    for (int i = 0; i < POINT_COUNT; i++) {
      const uint32_t bucket_index = current_sample - (POINT_COUNT - 1) + i;
      const auto &bucket = this->sample_buckets_[bucket_index % POINT_COUNT];
      if (bucket.valid && bucket.index == bucket_index && bucket.count > 0) {
        last_value = bucket.sum / bucket.count;
        have_value = true;
      }
      if (!have_value)
        continue;
      out[count++] = last_value;
    }
    return count;
  }

  bool stat_min_max_(float &minimum, float &maximum) const {
    const uint32_t current_bucket = static_cast<uint32_t>(esphome::millis_64() / STAT_BUCKET_MS);
    bool found = false;

    for (const auto &bucket : this->stat_buckets_) {
      if (!bucket.valid)
        continue;
      if (uint32_t(current_bucket - bucket.index) >= STAT_BUCKET_COUNT)
        continue;

      if (!found) {
        minimum = bucket.minimum;
        maximum = bucket.maximum;
        found = true;
      } else {
        minimum = std::min(minimum, bucket.minimum);
        maximum = std::max(maximum, bucket.maximum);
      }
    }
    return found;
  }

  static void format_pattern_(char *output, size_t output_size, const char *pattern, const char *value) {
    if (output == nullptr || output_size == 0)
      return;

    const char *safe_pattern = pattern == nullptr ? "{value}" : pattern;
    const char *safe_value = value == nullptr ? VALUE_PLACEHOLDER_TEXT : value;
    const char *placeholder = std::strstr(safe_pattern, "{value}");
    if (placeholder == nullptr) {
      std::snprintf(output, output_size, "%s %s", safe_value, safe_pattern);
      return;
    }

    std::snprintf(output, output_size, "%.*s%s%s", static_cast<int>(placeholder - safe_pattern), safe_pattern,
                  safe_value, placeholder + 7);
  }

  static void set_pattern_label_(lv_obj_t *label, const char *pattern, const char *value) {
    if (label == nullptr)
      return;

    char text[48];
    format_pattern_(text, sizeof(text), pattern, value);
    set_value_placeholder_spacing(label, value != nullptr && std::strcmp(value, VALUE_PLACEHOLDER_TEXT) == 0);
    const char *current = lv_label_get_text(label);
    if (current != nullptr && std::strcmp(current, text) == 0)
      return;
    lv_label_set_text(label, text);
  }

  static void label_stat_(lv_obj_t *label, const char *pattern, float value, const char *format) {
    if (label == nullptr)
      return;

    char number[24];
    std::snprintf(number, sizeof(number), format == nullptr ? "%.0f" : format, value);
    set_pattern_label_(label, pattern, number);
  }

  void update_stat_labels_(lv_obj_t *min_label, lv_obj_t *max_label, const char *format, const char *min_pattern,
                           const char *max_pattern) {
    if (min_label == nullptr && max_label == nullptr)
      return;

    float minimum = 0.0f;
    float maximum = 0.0f;
    if (!this->stat_min_max_(minimum, maximum)) {
      set_pattern_label_(min_label, min_pattern, VALUE_PLACEHOLDER_TEXT);
      set_pattern_label_(max_label, max_pattern, VALUE_PLACEHOLDER_TEXT);
      return;
    }

    label_stat_(min_label, min_pattern, minimum, format);
    label_stat_(max_label, max_pattern, maximum, format);
  }

  static float catmull_rom_(const float *p, int n, int offset, float t) {
    const float p0 = p[std::clamp(offset - 1, 0, n - 1)];
    const float p1 = p[offset];
    const float p2 = p[std::min(offset + 1, n - 1)];
    const float p3 = p[std::clamp(offset + 2, 0, n - 1)];
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
  }

  lv_color_t fill_color_for_value_(float value) const {
    if (this->color_mode_ == ColorMode::HIGH_BAD)
      return this->color_for_value_(value);
    return this->base_color_;
  }

  // Triangular dither noise in -1..1 (LSBs).
  static float tpdf_(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFF) / 65535.0f - (h >> 16) / 65535.0f;
  }

  static uint16_t rgb565_dither_(float r, float g, float b, float noise) {
    const int ri = std::clamp(static_cast<int>(std::floor(r * 31.0f + noise + 0.5f)), 0, 31);
    const int gi = std::clamp(static_cast<int>(std::floor(g * 63.0f + noise + 0.5f)), 0, 63);
    const int bi = std::clamp(static_cast<int>(std::floor(b * 31.0f + noise + 0.5f)), 0, 31);
    return static_cast<uint16_t>((ri << 11) | (gi << 5) | bi);
  }

  void render_(lv_obj_t *image) {
    lv_image_dsc_t *dsc = shared_dsc_();
    if (dsc == nullptr || image == nullptr)
      return;
    const uint32_t render_started_ms = esphome::millis();
    this->register_image_(image);
    this->base_color_ = lv_obj_get_style_line_color(image, LV_PART_MAIN);

    static float series[STAT_BUCKET_COUNT > POINT_COUNT ? STAT_BUCKET_COUNT : POINT_COUNT];
    static float smooth[STAT_BUCKET_COUNT > POINT_COUNT ? STAT_BUCKET_COUNT : POINT_COUNT];
    static float curve[SCREEN];
    const int n = this->count_ == 0 && !std::isfinite(this->current_value_) ? 0 : this->build_series_(series);
    const bool have_curve = n >= 2;

    float lo = 0.0f;
    float hi = 1.0f;
    if (have_curve) {
      static constexpr float weights[] = {1.0f, 2.0f, 3.0f, 2.0f, 1.0f};
      for (int i = 0; i < n; i++) {
        float total = 0.0f;
        float weight_total = 0.0f;
        for (int k = -2; k <= 2; k++) {
          total += series[std::clamp(i + k, 0, n - 1)] * weights[k + 2];
          weight_total += weights[k + 2];
        }
        smooth[i] = total / weight_total;
      }
      float data_min = smooth[0];
      float data_max = smooth[0];
      for (int i = 1; i < n; i++) {
        data_min = std::min(data_min, smooth[i]);
        data_max = std::max(data_max, smooth[i]);
      }
      if (this->color_mode_ == ColorMode::HIGH_BAD) {
        // Keep the Moderate limit in view so a good day reads low and green.
        lo = this->fixed_range_ ? std::min(this->fixed_min_, data_min) : data_min;
        hi = std::max(data_max, this->moderate_value_) * 1.12f;
        if (hi - lo < 0.001f)
          hi = lo + 1.0f;
      } else {
        const float range = data_max - data_min;
        const float padding = range < 0.001f ? 0.5f : range * 0.15f;
        lo = data_min - padding;
        hi = data_max + padding;
      }
      for (int x = 0; x < SCREEN; x++) {
        const float pos = (x * (n - 1.0f)) / (SCREEN - 1);
        const int offset = std::min(static_cast<int>(pos), n - 2);
        const float value = catmull_rom_(smooth, n, offset, pos - offset);
        const float normalized = std::clamp((value - lo) / (hi - lo), -0.05f, 1.05f);
        curve[x] = CURVE_BOTTOM - normalized * (CURVE_BOTTOM - CURVE_TOP);
      }
    }

    // Per-row fill color and opacity.
    static lv_color_t row_color[SCREEN];
    static float row_opa[SCREEN];
    for (int y = 0; y < SCREEN; y++) {
      const float value = lo + (CURVE_BOTTOM - (y + 0.5f)) / (CURVE_BOTTOM - CURVE_TOP) * (hi - lo);
      row_color[y] = this->fill_color_for_value_(value);
      const float t = std::clamp((y - FILL_TOP) / static_cast<float>(SCREEN - FILL_TOP), 0.0f, 1.0f);
      row_opa[y] = FILL_OPA_TOP + (FILL_OPA_BOTTOM - FILL_OPA_TOP) * t;
    }

    const lv_color_t glow_color = std::isfinite(this->current_value_)
                                      ? this->fill_color_for_value_(this->current_value_)
                                      : this->base_color_;
    const float gr = glow_color.red / 255.0f, gg = glow_color.green / 255.0f, gb = glow_color.blue / 255.0f;

    static float glow_lut[1024];
    static bool glow_lut_ready = false;
    if (!glow_lut_ready) {
      for (int i = 0; i < 1024; i++) {
        const float t2 = i / 1023.0f;
        glow_lut[i] = std::exp(-t2 / (0.55f * 0.55f)) * (1.0f - t2 * t2) * GLOW_OPA;
      }
      glow_lut_ready = true;
    }
    static float line_half[SCREEN];
    if (have_curve) {
      for (int x = 0; x < SCREEN; x++) {
        const float slope = (curve[std::min(x + 1, SCREEN - 1)] - curve[std::max(x - 1, 0)]) * 0.5f;
        line_half[x] = LINE_HALF_WIDTH * std::sqrt(1.0f + slope * slope);
      }
    }

    auto *colors = reinterpret_cast<uint16_t *>(const_cast<uint8_t *>(dsc->data));
    lv_obj_t *page = lv_obj_get_screen(image);
    const lv_color_t background = page != nullptr ? lv_obj_get_style_bg_color(page, LV_PART_MAIN) : lv_color_black();
    const float bgr = background.red / 255.0f, bgg = background.green / 255.0f, bgb = background.blue / 255.0f;
    for (int y = 0; y < SCREEN; y++) {
      const float fr = row_color[y].red / 255.0f, fg = row_color[y].green / 255.0f, fb = row_color[y].blue / 255.0f;
      const float dy = (y + 0.5f) - GLOW_CENTER_Y;
      for (int x = 0; x < SCREEN; x++) {
        // Glow.
        const float dx = (x + 0.5f) - SCREEN / 2.0f;
        const float t2 = (dx * dx + dy * dy) / (GLOW_RADIUS * GLOW_RADIUS);
        float a = 0.0f, r = 0.0f, g = 0.0f, b = 0.0f;
        if (t2 < 1.0f) {
          const float pos = t2 * 1023.0f;
          const int i0 = static_cast<int>(pos);
          const int i1 = std::min(i0 + 1, 1023);
          a = glow_lut[i0] + (glow_lut[i1] - glow_lut[i0]) * (pos - i0);
          r = gr;
          g = gg;
          b = gb;
        }
        if (have_curve) {
          // Fill below the curve, anti-aliased at its edge.
          const float cover = std::clamp((y + 1.0f) - curve[x], 0.0f, 1.0f);
          if (cover > 0.0f) {
            const float fa = row_opa[y] * cover;
            const float out_a = fa + a * (1.0f - fa);
            if (out_a > 0.0f) {
              r = (fr * fa + r * a * (1.0f - fa)) / out_a;
              g = (fg * fa + g * a * (1.0f - fa)) / out_a;
              b = (fb * fa + b * a * (1.0f - fa)) / out_a;
            }
            a = out_a;
          }
          // White line on top.
          const float half = line_half[x];
          const float line_cover = std::clamp(half + 0.5f - std::fabs((y + 0.5f) - curve[x]), 0.0f, 1.0f);
          if (line_cover > 0.0f) {
            const float la = LINE_OPA * line_cover;
            const float out_a = la + a * (1.0f - la);
            r = (la + r * a * (1.0f - la)) / out_a;
            g = (la + g * a * (1.0f - la)) / out_a;
            b = (la + b * a * (1.0f - la)) / out_a;
            a = out_a;
          }
        }
        const size_t index = static_cast<size_t>(y) * SCREEN + x;
        if (a <= 0.0f) {
          colors[index] = rgb565_dither_(bgr, bgg, bgb, 0.0f);
          continue;
        }
        colors[index] = rgb565_dither_(bgr + (r - bgr) * a, bgg + (g - bgg) * a, bgb + (b - bgb) * a, tpdf_(x, y));
      }
    }

    const uint32_t render_ms = esphome::millis() - render_started_ms;
    if (render_ms > 150)
      ESP_LOGW("history", "Rendering the history graph took %u ms", static_cast<unsigned>(render_ms));
    owner_() = this;
    this->last_render_ms_ = std::max<uint32_t>(1, esphome::millis());
    this->last_render_bucket_ = static_cast<uint32_t>(esphome::millis_64() / STAT_BUCKET_MS);
#if defined(LV_CACHE_DEF_SIZE) && LV_CACHE_DEF_SIZE > 0
    lv_image_cache_drop(dsc);
#endif
    set_hidden_(image, false);
    lv_obj_invalidate(image);
  }

  lv_color_t color_for_value_(float value) const {
    if (!std::isfinite(value))
      return lv_color_make(0xFF, 0xFF, 0xFF);
    if (threshold_matches_(value, this->balanced_value_, this->balanced_inclusive_))
      return lv_color_make(0x20, 0xE8, 0x40);
    if (threshold_matches_(value, this->moderate_value_, this->moderate_inclusive_))
      return lv_color_make(0xFF, 0xD4, 0x00);
    if (threshold_matches_(value, this->poor_value_, this->poor_inclusive_))
      return lv_color_make(0xFF, 0x8C, 0x00);
    if (threshold_matches_(value, this->unhealthy_value_, this->unhealthy_inclusive_))
      return lv_color_make(0xFF, 0x2B, 0x2B);
    return lv_color_make(0xFF, 0x00, 0x6E);
  }

  static bool threshold_matches_(float value, float threshold, bool inclusive) {
    return inclusive ? value <= threshold : value < threshold;
  }

  SampleBucket sample_buckets_[POINT_COUNT]{};
  StatBucket stat_buckets_[STAT_BUCKET_COUNT]{};
  lv_obj_t *image_{nullptr};
  uint64_t history_start_ms_{0};
  uint32_t last_render_ms_{0};
  uint32_t last_render_bucket_{0};
  int count_{0};
  bool history_started_{false};
  float current_value_{NAN};
  lv_color_t base_color_{lv_color_make(0xFF, 0xFF, 0xFF)};

  ColorMode color_mode_{ColorMode::NONE};
  bool fixed_range_{false};
  float fixed_min_{0.0f};
  float fixed_max_{0.0f};
  float balanced_value_{0.0f};
  float moderate_value_{0.0f};
  float poor_value_{0.0f};
  float unhealthy_value_{0.0f};
  bool balanced_inclusive_{true};
  bool moderate_inclusive_{true};
  bool poor_inclusive_{true};
  bool unhealthy_inclusive_{true};
};

}  // namespace AirDot
