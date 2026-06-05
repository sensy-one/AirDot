#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "esphome/core/hal.h"
#include <lvgl.h>

namespace AirDot {

inline constexpr const char *VALUE_PLACEHOLDER_TEXT = "--";
inline constexpr int32_t VALUE_PLACEHOLDER_LETTER_SPACE = 1;

inline void set_value_placeholder_spacing(lv_obj_t *label, bool placeholder) {
  if (label == nullptr)
    return;
  lv_obj_set_style_text_letter_space(label, placeholder ? VALUE_PLACEHOLDER_LETTER_SPACE : 0, LV_PART_MAIN);
}

class SensorHistoryGraph {
 public:
  static constexpr int WIDTH = 345;
  static constexpr int HEIGHT = 120;
  static constexpr uint32_t GRAPH_HISTORY_MS = 15UL * 60UL * 1000UL;
  static constexpr uint32_t WINDOW_STEP_MS = 5UL * 60UL * 1000UL;
  static constexpr uint32_t STAT_HISTORY_MS = 24UL * 60UL * 60UL * 1000UL;
  static constexpr uint32_t SAMPLE_BUCKET_MS = 5UL * 1000UL;
  static constexpr int POINT_COUNT = static_cast<int>(GRAPH_HISTORY_MS / SAMPLE_BUCKET_MS);
  static constexpr int DRAW_POINT_COUNT = WIDTH;
  static constexpr int CURRENT_DOT_DIAMETER = 11;
  static constexpr int CURRENT_DOT_RADIUS = CURRENT_DOT_DIAMETER / 2;
  static constexpr int CURRENT_DOT_INSET = CURRENT_DOT_RADIUS + 6;
  static constexpr uint32_t STAT_BUCKET_MS = 5 * 60 * 1000;
  static constexpr int STAT_BUCKET_COUNT = static_cast<int>(STAT_HISTORY_MS / STAT_BUCKET_MS);

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
    this->draw_point_count_ = 0;

    if (this->line_ != nullptr)
      lv_obj_invalidate(this->line_);
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
    this->color_mode_ = this->fixed_range_ && balanced_value < moderate_value && moderate_value < poor_value &&
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
    if (hidden) {
      lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
  }

  void update_obj_(lv_obj_t *line, float value, lv_obj_t *min_label, lv_obj_t *max_label, lv_obj_t *empty_label,
                   const char *format, const char *min_pattern, const char *max_pattern) {
    if (line == nullptr)
      return;
    if (!std::isfinite(value)) {
      set_hidden_(empty_label, false);
      set_pattern_label_(min_label, min_pattern, VALUE_PLACEHOLDER_TEXT);
      set_pattern_label_(max_label, max_pattern, VALUE_PLACEHOLDER_TEXT);
      this->draw_point_count_ = 0;
      this->count_ = 0;
      lv_obj_set_style_line_opa(line, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_invalidate(line);
      return;
    }

    set_hidden_(empty_label, true);
    this->record_sample_(value);
    this->record_stat_(value);
    this->update_stat_labels_(min_label, max_label, format, min_pattern, max_pattern);
    this->ensure_custom_draw_(line);
    this->redraw_(line);
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

  uint32_t active_window_ms_() const {
    if (!this->history_started_)
      return WINDOW_STEP_MS;

    const uint64_t elapsed = esphome::millis_64() - this->history_start_ms_;
    uint32_t steps = static_cast<uint32_t>((elapsed + WINDOW_STEP_MS - 1) / WINDOW_STEP_MS);
    if (steps < 1)
      steps = 1;

    const uint64_t window_ms = static_cast<uint64_t>(steps) * WINDOW_STEP_MS;
    return static_cast<uint32_t>(std::min<uint64_t>(window_ms, GRAPH_HISTORY_MS));
  }

  void prepare_samples_() {
    const uint32_t current_bucket = static_cast<uint32_t>(esphome::millis_64() / SAMPLE_BUCKET_MS);
    const int visible_bucket_count =
        std::clamp(static_cast<int>(this->active_window_ms_() / SAMPLE_BUCKET_MS), 2, POINT_COUNT);
    this->count_ = 0;
    bool have_value = false;
    float last_value = 0.0f;

    for (int i = 0; i < visible_bucket_count; i++) {
      const uint32_t bucket_index = current_bucket - (visible_bucket_count - 1) + i;
      const auto &bucket = this->sample_buckets_[bucket_index % POINT_COUNT];

      if (bucket.valid && bucket.index == bucket_index && bucket.count > 0) {
        last_value = bucket.sum / bucket.count;
        have_value = true;
      }
      if (!have_value)
        continue;

      this->samples_[this->count_] = last_value;
      this->count_++;
    }
  }

  void record_stat_(float value) {
    const uint32_t bucket_index = static_cast<uint32_t>(esphome::millis_64() / STAT_BUCKET_MS);
    auto &bucket = this->stat_buckets_[bucket_index % STAT_BUCKET_COUNT];
    if (!bucket.valid || bucket.index != bucket_index) {
      bucket.index = bucket_index;
      bucket.minimum = value;
      bucket.maximum = value;
      bucket.valid = true;
      return;
    }

    bucket.minimum = std::min(bucket.minimum, value);
    bucket.maximum = std::max(bucket.maximum, value);
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

  float sample_(int offset) const { return this->samples_[offset]; }

  float smoothed_sample_(int offset) const {
    static constexpr float weights[] = {1.0f, 2.0f, 3.0f, 2.0f, 1.0f};

    float total = 0.0f;
    float weight_total = 0.0f;
    for (int i = -2; i <= 2; i++) {
      const int clamped = std::clamp(offset + i, 0, this->count_ - 1);
      const float weight = weights[i + 2];
      total += this->sample_(clamped) * weight;
      weight_total += weight;
    }
    return total / weight_total;
  }

  float curve_(int offset, float t) const {
    const float p0 = this->smooth_samples_[std::clamp(offset - 1, 0, this->count_ - 1)];
    const float p1 = this->smooth_samples_[offset];
    const float p2 = this->smooth_samples_[offset + 1];
    const float p3 = this->smooth_samples_[std::clamp(offset + 2, 0, this->count_ - 1)];
    const float t2 = t * t;
    const float t3 = t2 * t;

    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
  }

  void set_point_(int point, int x, float value, float minimum, float range) {
    const float normalized = std::clamp((value - minimum) / range, 0.0f, 1.0f);
    const int draw_height = std::max(1, HEIGHT - 1 - CURRENT_DOT_INSET * 2);

    this->points_[point].x = x;
    this->points_[point].y =
        CURRENT_DOT_INSET + draw_height - static_cast<int>(normalized * draw_height + 0.5f);
  }

  void redraw_(lv_obj_t *line) {
    this->prepare_samples_();

    if (this->count_ < 2)
      return;

    for (int i = 0; i < this->count_; i++)
      this->smooth_samples_[i] = this->smoothed_sample_(i);

    float minimum = this->fixed_min_;
    float maximum = this->fixed_max_;
    if (!this->fixed_range_) {
      minimum = this->smooth_samples_[0];
      maximum = minimum;
      for (int i = 1; i < this->count_; i++) {
        const float value = this->smooth_samples_[i];
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
      }

      const float sample_range = maximum - minimum;
      if (sample_range < 0.001f) {
        minimum -= 0.5f;
        maximum += 0.5f;
      } else {
        const float padding = sample_range * 0.15f;
        minimum -= padding;
        maximum += padding;
      }
    } else {
      minimum = this->fixed_min_;
      maximum = this->fixed_max_;
      float sample_minimum = this->smooth_samples_[0];
      float sample_maximum = sample_minimum;
      for (int i = 1; i < this->count_; i++) {
        const float value = this->smooth_samples_[i];
        sample_minimum = std::min(sample_minimum, value);
        sample_maximum = std::max(sample_maximum, value);
      }

      const float padding = (this->fixed_max_ - this->fixed_min_) * 0.05f;
      if (sample_minimum < minimum)
        minimum = sample_minimum - padding;
      if (sample_maximum > maximum)
        maximum = sample_maximum + padding;
    }
    const float range = maximum - minimum;
    this->draw_min_ = minimum;
    this->draw_max_ = maximum;

    int point = 0;
    const int draw_width = std::max(2, WIDTH - CURRENT_DOT_INSET * 2);
    for (int x = 0; x < draw_width; x++) {
      const float sample_pos = (x * (this->count_ - 1.0f)) / (draw_width - 1);
      const int offset = std::min(static_cast<int>(sample_pos), this->count_ - 2);
      const float t = sample_pos - offset;

      const float point_value = this->curve_(offset, t);
      this->set_point_(point, x + CURRENT_DOT_INSET, point_value, minimum, range);
      point++;
    }

    this->draw_point_count_ = point;
    lv_line_set_points(line, this->points_, point);
    lv_obj_invalidate(line);
  }

  float samples_[POINT_COUNT]{};
  float smooth_samples_[POINT_COUNT]{};
  lv_point_precise_t points_[DRAW_POINT_COUNT]{};
  SampleBucket sample_buckets_[POINT_COUNT]{};
  StatBucket stat_buckets_[STAT_BUCKET_COUNT]{};
  lv_obj_t *line_{nullptr};
  uint64_t history_start_ms_{0};
  int count_{0};
  bool history_started_{false};

  static void draw_event_(lv_event_t *event) {
    auto *self = static_cast<SensorHistoryGraph *>(lv_event_get_user_data(event));
    if (self == nullptr || self->count_ < 2 || self->draw_point_count_ < 2)
      return;

    lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_target(event));
    lv_layer_t *layer = lv_event_get_layer(event);
    if (obj == nullptr || layer == nullptr)
      return;

    lv_area_t obj_area;
    lv_obj_get_coords(obj, &obj_area);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_obj_get_style_line_color(obj, LV_PART_MAIN);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_opa = LV_OPA_TRANSP;
    dsc.radius = 0;

    const int width = std::max<int>(1, lv_obj_get_style_line_width(obj, LV_PART_MAIN));
    for (int i = 1; i < self->draw_point_count_; i++) {
      if (self->color_mode_ != ColorMode::NONE)
        dsc.bg_color = self->color_for_value_((self->value_for_point_(self->points_[i - 1]) +
                                               self->value_for_point_(self->points_[i])) *
                                              0.5f);
      self->draw_segment_(layer, &dsc, obj_area, self->points_[i - 1], self->points_[i], width);
    }

    const auto &current_point = self->points_[self->draw_point_count_ - 1];
    if (self->color_mode_ != ColorMode::NONE)
      dsc.bg_color = self->color_for_value_(self->value_for_point_(current_point));
    dsc.radius = 0;
    self->draw_current_dot_(layer, &dsc, obj_area, current_point);
  }

  void ensure_custom_draw_(lv_obj_t *line) {
    if (this->line_ == line)
      return;

    this->line_ = line;
    lv_obj_set_style_line_opa(line, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_event_cb(line, SensorHistoryGraph::draw_event_, LV_EVENT_DRAW_MAIN, this);
  }

  static void draw_box_(lv_layer_t *layer, const lv_draw_rect_dsc_t *dsc, const lv_area_t &clip_area, int x, int y,
                        int width) {
    const int before = width / 2;
    const int after = width - before - 1;
    lv_area_t area{
        static_cast<lv_coord_t>(x - before),
        static_cast<lv_coord_t>(y - before),
        static_cast<lv_coord_t>(x + after),
        static_cast<lv_coord_t>(y + after),
    };

    area.x1 = std::max(area.x1, clip_area.x1);
    area.y1 = std::max(area.y1, clip_area.y1);
    area.x2 = std::min(area.x2, clip_area.x2);
    area.y2 = std::min(area.y2, clip_area.y2);
    if (area.x1 > area.x2 || area.y1 > area.y2)
      return;

    lv_draw_rect(layer, dsc, &area);
  }

  static void draw_segment_(lv_layer_t *layer, const lv_draw_rect_dsc_t *dsc, const lv_area_t &obj_area,
                            const lv_point_precise_t &from, const lv_point_precise_t &to, int width) {
    int x0 = obj_area.x1 + from.x;
    int y0 = obj_area.y1 + from.y;
    const int x1 = obj_area.x1 + to.x;
    const int y1 = obj_area.y1 + to.y;
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
      draw_box_(layer, dsc, obj_area, x0, y0, width);
      if (x0 == x1 && y0 == y1)
        break;

      const int e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
  }

  static void draw_current_dot_(lv_layer_t *layer, const lv_draw_rect_dsc_t *dsc, const lv_area_t &obj_area,
                                const lv_point_precise_t &point) {
    const int radius = CURRENT_DOT_RADIUS;
    const int center_x = obj_area.x1 + point.x;
    const int center_y = obj_area.y1 + point.y;

    static constexpr int spans[CURRENT_DOT_DIAMETER] = {2, 3, 4, 5, 5, 5, 5, 5, 4, 3, 2};
    for (int y = -radius; y <= radius; y++) {
      const int x_span = spans[y + radius];
      lv_area_t area{
          static_cast<lv_coord_t>(center_x - x_span),
          static_cast<lv_coord_t>(center_y + y),
          static_cast<lv_coord_t>(center_x + x_span),
          static_cast<lv_coord_t>(center_y + y),
      };

      lv_draw_rect(layer, dsc, &area);
    }
  }

  lv_color_t color_for_value_(float value) const {
    switch (this->color_mode_) {
      case ColorMode::HIGH_BAD:
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
      case ColorMode::NONE:
      default:
        return lv_color_make(0xFF, 0xFF, 0xFF);
    }
  }

  float value_for_point_(const lv_point_precise_t &point) const {
    const float draw_height = static_cast<float>(std::max(1, HEIGHT - 1 - CURRENT_DOT_INSET * 2));
    const float normalized =
        1.0f - std::clamp((static_cast<float>(point.y) - CURRENT_DOT_INSET) / draw_height, 0.0f, 1.0f);
    return this->draw_min_ + normalized * (this->draw_max_ - this->draw_min_);
  }

  static bool threshold_matches_(float value, float threshold, bool inclusive) {
    return inclusive ? value <= threshold : value < threshold;
  }

  ColorMode color_mode_{ColorMode::NONE};
  bool fixed_range_{false};
  float fixed_min_{0.0f};
  float fixed_max_{0.0f};
  float draw_min_{0.0f};
  float draw_max_{0.0f};
  float balanced_value_{0.0f};
  float moderate_value_{0.0f};
  float poor_value_{0.0f};
  float unhealthy_value_{0.0f};
  bool balanced_inclusive_{true};
  bool moderate_inclusive_{true};
  bool poor_inclusive_{true};
  bool unhealthy_inclusive_{true};
  int draw_point_count_{0};
};

}  // namespace AirDot
