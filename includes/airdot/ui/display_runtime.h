#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <lvgl.h>
#include "sensor_history_graph.h"
#include "i18n_texts.h"
#include "classification.h"

namespace AirDot {

namespace onboarding {
UiLanguage load_ui_language();
bool load_dark_mode_enabled();
}

inline lv_color_t color(uint8_t red, uint8_t green, uint8_t blue) { return lv_color_make(red, green, blue); }

inline lv_color_t display_background_color() {
  return onboarding::load_dark_mode_enabled() ? color(0x00, 0x00, 0x00) : color(0xFF, 0xFF, 0xFF);
}

inline lv_color_t display_text_color() {
  return onboarding::load_dark_mode_enabled() ? color(0xFF, 0xFF, 0xFF) : color(0x00, 0x00, 0x00);
}

inline char ascii_lower_(char value) {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

inline bool ascii_equals_ignore_case_(const std::string &value, const char *expected) {
  if (expected == nullptr)
    return false;

  size_t index = 0;
  while (index < value.size() && expected[index] != '\0') {
    if (ascii_lower_(value[index]) != ascii_lower_(expected[index]))
      return false;
    index++;
  }
  return index == value.size() && expected[index] == '\0';
}

inline bool parse_hex_digit_(char value, uint8_t &digit) {
  if (value >= '0' && value <= '9') {
    digit = static_cast<uint8_t>(value - '0');
    return true;
  }
  if (value >= 'A' && value <= 'F') {
    digit = static_cast<uint8_t>(value - 'A' + 10);
    return true;
  }
  if (value >= 'a' && value <= 'f') {
    digit = static_cast<uint8_t>(value - 'a' + 10);
    return true;
  }
  return false;
}

inline bool parse_display_color(const std::string &value, uint32_t &rgb) {
  size_t begin = 0;
  size_t end = value.size();
  while (begin < end && (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r' || value[begin] == '\n'))
    begin++;
  while (end > begin &&
         (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r' || value[end - 1] == '\n'))
    end--;

  std::string text = value.substr(begin, end - begin);
  if (text.empty() || ascii_equals_ignore_case_(text, "default") || ascii_equals_ignore_case_(text, "none"))
    return false;

  auto named_color = [&](const char *name, uint32_t color) -> bool {
    if (!ascii_equals_ignore_case_(text, name))
      return false;
    rgb = color;
    return true;
  };
  if (named_color("white", 0xFFFFFF) || named_color("black", 0x000000) || named_color("red", 0xFF2B2B) ||
      named_color("orange", 0xFF8C00) || named_color("yellow", 0xFFD400) || named_color("green", 0x20E840) ||
      named_color("blue", 0x00A3FF) || named_color("cyan", 0x00D4FF) || named_color("purple", 0x8B5CFF) ||
      named_color("pink", 0xFF006E)) {
    return true;
  }

  if (!text.empty() && text[0] == '#')
    text.erase(0, 1);
  else if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    text.erase(0, 2);

  if (text.size() != 6)
    return false;

  uint32_t parsed = 0;
  for (char current : text) {
    uint8_t digit = 0;
    if (!parse_hex_digit_(current, digit))
      return false;
    parsed = (parsed << 4) | digit;
  }

  rgb = parsed;
  return true;
}

inline std::string truncate_text(std::string text, size_t max_length) {
  if (text.size() > max_length)
    text.resize(max_length);
  return text;
}

inline bool decode_utf8_codepoint_(const std::string &text, size_t &index, uint32_t &codepoint) {
  if (index >= text.size())
    return false;

  const unsigned char first = static_cast<unsigned char>(text[index]);
  if (first < 0x80) {
    codepoint = first;
    index++;
    return true;
  }

  uint32_t value = 0;
  size_t length = 0;
  uint32_t minimum = 0;
  if ((first & 0xE0) == 0xC0) {
    value = first & 0x1F;
    length = 2;
    minimum = 0x80;
  } else if ((first & 0xF0) == 0xE0) {
    value = first & 0x0F;
    length = 3;
    minimum = 0x800;
  } else if ((first & 0xF8) == 0xF0) {
    value = first & 0x07;
    length = 4;
    minimum = 0x10000;
  } else {
    codepoint = '?';
    index++;
    return false;
  }

  if (index + length > text.size()) {
    codepoint = '?';
    index++;
    return false;
  }

  for (size_t offset = 1; offset < length; offset++) {
    const unsigned char current = static_cast<unsigned char>(text[index + offset]);
    if ((current & 0xC0) != 0x80) {
      codepoint = '?';
      index++;
      return false;
    }
    value = (value << 6) | (current & 0x3F);
  }

  if (value < minimum || (value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF) {
    codepoint = '?';
    index++;
    return false;
  }

  codepoint = value;
  index += length;
  return true;
}

inline bool alert_font_codepoint_supported_(uint32_t codepoint) {
  if (codepoint == '\n' || (codepoint >= 0x20 && codepoint <= 0x7E) ||
      (codepoint >= 0xA1 && codepoint <= 0x17F)) {
    return true;
  }

  static constexpr uint32_t SUPPORTED[] = {
      0x2010, 0x2011, 0x2013, 0x2014, 0x2015, 0x2017, 0x2018, 0x2019, 0x201A, 0x201B,
      0x201C, 0x201D, 0x201E, 0x2020, 0x2021, 0x2022, 0x2025, 0x2026, 0x2027, 0x2030,
      0x2032, 0x2033, 0x2039, 0x203A, 0x203C, 0x2044,
      0x2070, 0x2074, 0x2075, 0x2076, 0x2077, 0x2078, 0x2079, 0x207A, 0x207B, 0x207C,
      0x207D, 0x207E, 0x207F, 0x2080, 0x2081, 0x2082, 0x2083, 0x2084, 0x2085, 0x2086,
      0x2087, 0x2088, 0x2089, 0x208A, 0x208B, 0x208C, 0x208D, 0x208E,
      0x20A3, 0x20A4, 0x20A6, 0x20A7, 0x20A8, 0x20A9, 0x20AA, 0x20AB, 0x20AC, 0x20B1,
      0x20B9, 0x20BA, 0x20BC, 0x20BD, 0x2105, 0x2113, 0x2116, 0x2122, 0x2126, 0x212E,
      0x2139, 0x23F0, 0x25A0, 0x25CA, 0x25CB, 0x25CF, 0x2665, 0x2699, 0x26A0, 0x26A1,
      0x2705, 0x2714, 0x2716, 0x274C, 0x2753, 0x2754, 0x2755, 0x2757, 0x2764, 0x2B50,
      0x1F319, 0x1F321, 0x1F3A5, 0x1F3B5, 0x1F3E0, 0x1F440, 0x1F4A1, 0x1F4A7,
      0x1F4A8, 0x1F4CD, 0x1F4E6, 0x1F4F1, 0x1F4F7, 0x1F4FA, 0x1F504, 0x1F507,
      0x1F50A, 0x1F50B, 0x1F50C, 0x1F511, 0x1F512, 0x1F513, 0x1F514, 0x1F525,
      0x1F5D1, 0x1F697, 0x1F6A8, 0x1F6AA, 0x1F6CF, 0x1F6DC, 0x1F9CD, 0x1F9F9,
      0x1FA9F, 0x1FAAB,
  };
  return std::binary_search(SUPPORTED, SUPPORTED + sizeof(SUPPORTED) / sizeof(SUPPORTED[0]), codepoint);
}

inline uint32_t alert_symbol_codepoint_(uint32_t codepoint) {
  switch (codepoint) {
    case 0x2605:
      return 0x2B50;
    case 0x26D4:
      return 0x274C;
    case 0x2713:
      return 0x2714;
    case 0x2715:
    case 0x2717:
    case 0x2718:
      return 0x2716;
    default:
      return 0;
  }
}

inline bool alert_text_drop_codepoint_(uint32_t codepoint) {
  return codepoint == 0x200D || codepoint == 0x20E3 || (codepoint >= 0x2600 && codepoint <= 0x27BF) ||
         (codepoint >= 0xFE00 && codepoint <= 0xFE0F) || (codepoint >= 0x1F000 && codepoint <= 0x1FAFF) ||
         (codepoint >= 0xE0020 && codepoint <= 0xE007F);
}

inline void append_utf8_codepoint_(std::string &text, uint32_t codepoint) {
  if (codepoint <= 0x7F) {
    text += static_cast<char>(codepoint);
  } else if (codepoint <= 0x7FF) {
    text += static_cast<char>(0xC0 | (codepoint >> 6));
    text += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint <= 0xFFFF) {
    text += static_cast<char>(0xE0 | (codepoint >> 12));
    text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    text += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else {
    text += static_cast<char>(0xF0 | (codepoint >> 18));
    text += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
    text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    text += static_cast<char>(0x80 | (codepoint & 0x3F));
  }
}

inline std::string sanitize_alert_text(const std::string &text, size_t max_length) {
  std::string sanitized;
  sanitized.reserve(std::min(text.size(), max_length));

  size_t index = 0;
  bool previous_was_cr = false;
  while (index < text.size() && sanitized.size() < max_length) {
    uint32_t codepoint = '?';
    const bool valid = decode_utf8_codepoint_(text, index, codepoint);
    if (!valid)
      codepoint = '?';

    if (codepoint == '\r') {
      codepoint = '\n';
      previous_was_cr = true;
    } else if (codepoint == '\n' && previous_was_cr) {
      previous_was_cr = false;
      continue;
    } else {
      previous_was_cr = false;
    }

    if (codepoint == '\t' || codepoint == 0xA0)
      codepoint = ' ';
    if (codepoint >= 0x0300 && codepoint <= 0x036F)
      continue;
    const uint32_t symbol_codepoint = alert_symbol_codepoint_(codepoint);
    if (symbol_codepoint != 0)
      codepoint = symbol_codepoint;
    if (!alert_font_codepoint_supported_(codepoint)) {
      if (alert_text_drop_codepoint_(codepoint))
        continue;
      codepoint = '?';
    }

    std::string encoded;
    append_utf8_codepoint_(encoded, codepoint);
    if (sanitized.size() + encoded.size() > max_length)
      break;
    sanitized += encoded;
  }
  return sanitized;
}

inline lv_color_t light_metric_color() { return color(0xFF, 0xD4, 0x00); }
inline lv_color_t pressure_metric_color() { return color(0x8B, 0x5C, 0xFF); }
inline lv_color_t temperature_metric_color() { return color(0xFF, 0x5A, 0x3D); }
inline lv_color_t humidity_metric_color() { return color(0x00, 0xD4, 0xFF); }

inline lv_color_t weather_code_color(int code) {
  switch (weather_code_group(code)) {
    case 0:
    case 1:
      return color(0x20, 0xE8, 0x40);
    case 2:
    case 3:
    case 8:
      return color(0xFF, 0xD4, 0x00);
    case 4:
    case 5:
    case 7:
    case 9:
    case 10:
      return color(0xFF, 0x8C, 0x00);
    case 6:
    case 11:
      return color(0xFF, 0x2B, 0x2B);
    case 12:
      return color(0xFF, 0x00, 0x6E);
    default:
      return display_text_color();
  }
}

inline lv_obj_t *lv_obj(lv_obj_t *obj) { return obj; }

template<typename Widget> lv_obj_t *lv_obj(Widget *widget) {
  if (widget == nullptr)
    return nullptr;
  return widget->obj;
}

template<typename LabelWidget> void set_formatted_label(LabelWidget *label, float value, const char *format) {
  lv_obj_t *obj = lv_obj(label);
  if (obj == nullptr)
    return;

  const bool placeholder = !std::isfinite(value);
  set_value_placeholder_spacing(obj, placeholder);

  char text[16];
  if (!placeholder) {
    std::snprintf(text, sizeof(text), format == nullptr ? "%.0f" : format, value);
  } else {
    std::snprintf(text, sizeof(text), "%s", VALUE_PLACEHOLDER_TEXT);
  }
  lv_label_set_text(obj, text);
}

template<typename LabelWidget> void set_text_color(LabelWidget *label, lv_color_t text_color) {
  lv_obj_t *obj = lv_obj(label);
  if (obj == nullptr)
    return;

  lv_obj_set_style_text_color(obj, text_color, LV_PART_MAIN);
  lv_obj_set_style_text_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_invalidate(obj);
}

template<typename LabelWidget> void set_history_placeholder_label(LabelWidget *label, const char *pattern) {
  lv_obj_t *obj = lv_obj(label);
  if (obj == nullptr)
    return;

  const char *current = lv_label_get_text(obj);
  if (current != nullptr && std::strcmp(current, VALUE_PLACEHOLDER_TEXT) != 0 && std::strcmp(current, "--") != 0)
    return;

  SensorHistoryGraph::set_pattern_label(obj, pattern, VALUE_PLACEHOLDER_TEXT);
}

template<typename MinLabelWidget, typename MaxLabelWidget>
void set_history_placeholder_labels(MinLabelWidget *min_label, MaxLabelWidget *max_label,
                                    const HistoryText &history_text) {
  set_history_placeholder_label(min_label, history_text.min_pattern);
  set_history_placeholder_label(max_label, history_text.max_pattern);
}

template<typename CurrentLabelWidget, typename LineWidget, typename MinLabelWidget, typename MaxLabelWidget,
         typename EmptyLabelWidget>
void update_plain_history(SensorHistoryGraph &history, CurrentLabelWidget *current_label, LineWidget *line,
                          MinLabelWidget *min_label, MaxLabelWidget *max_label, EmptyLabelWidget *empty_label,
                          float value, const char *format, lv_color_t line_color,
                          const char *min_pattern = nullptr, const char *max_pattern = nullptr) {
  const auto labels = history_text(onboarding::load_ui_language());
  const char *effective_min_pattern = min_pattern == nullptr ? labels.min_pattern : min_pattern;
  const char *effective_max_pattern = max_pattern == nullptr ? labels.max_pattern : max_pattern;
  SensorHistoryGraph::set_value_label(current_label, value, format);
  lv_obj_t *line_obj = lv_obj(line);
  if (line_obj != nullptr)
    lv_obj_set_style_line_color(line_obj, line_color, LV_PART_MAIN);
  history.update(line, value, min_label, max_label, empty_label, format, effective_min_pattern, effective_max_pattern);
}

template<typename CurrentLabelWidget, typename LineWidget, typename MinLabelWidget, typename MaxLabelWidget,
         typename EmptyLabelWidget>
void update_high_bad_history(SensorHistoryGraph &history, CurrentLabelWidget *current_label, LineWidget *line,
                             MinLabelWidget *min_label, MaxLabelWidget *max_label, EmptyLabelWidget *empty_label,
                             float value, const char *format, float graph_min, float graph_max,
                             AirQualityStatusThresholds thresholds, const char *min_pattern = nullptr,
                             const char *max_pattern = nullptr) {
  const auto labels = history_text(onboarding::load_ui_language());
  const char *effective_min_pattern = min_pattern == nullptr ? labels.min_pattern : min_pattern;
  const char *effective_max_pattern = max_pattern == nullptr ? labels.max_pattern : max_pattern;
  SensorHistoryGraph::set_value_label(current_label, value, format);
  history.update_colored(line, value, min_label, max_label, empty_label, format, graph_min, graph_max,
                         thresholds.balanced, thresholds.moderate, thresholds.poor, thresholds.unhealthy,
                         thresholds.balanced_inclusive, thresholds.moderate_inclusive, thresholds.poor_inclusive,
                         thresholds.unhealthy_inclusive, effective_min_pattern, effective_max_pattern);
}

inline lv_color_t air_quality_status_color(int level) {
  if (level < 0)
    return display_text_color();
  if (level >= 4)
    return color(0xFF, 0x00, 0x6E);
  if (level == 3)
    return color(0xFF, 0x2B, 0x2B);
  if (level == 2)
    return color(0xFF, 0x8C, 0x00);
  if (level == 1)
    return color(0xFF, 0xD4, 0x00);
  return color(0x20, 0xE8, 0x40);
}

inline lv_color_t color_for_thresholds(float value, AirQualityStatusThresholds thresholds) {
  return air_quality_status_color(threshold_level(value, thresholds));
}

inline lv_color_t metric_color(AirQualityProfile profile, AirQualityMetric metric, float value) {
  return color_for_thresholds(value, metric_status_thresholds(profile, metric));
}

}  // namespace AirDot
