#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include <lvgl.h>

#include "esphome/components/font/font.h"
#include "esphome/core/hal.h"

#include "display_runtime.h"
#include "flight_radar_client.h"

namespace AirDot::flight_radar {

inline constexpr int RADAR_SCREEN_MARGIN = 18;
inline constexpr int RADAR_TICK_COUNT = 12;
inline constexpr int RADAR_TICK_LENGTH_PX = 12;
inline constexpr int RADAR_TICK_WIDTH_PX = 2;
inline constexpr int RADAR_N_GAP_PX = 6;
inline constexpr int RADAR_TRAFFIC_EDGE_RESERVE_PX = 100;
inline constexpr int RADAR_AIRCRAFT_NOSE_PX = 14;
inline constexpr int RADAR_AIRCRAFT_TAIL_PX = 6;
inline constexpr int RADAR_AIRCRAFT_WING_PX = 8;
inline constexpr int RADAR_AIRCRAFT_LABEL_OFFSET_PX = 56;
inline constexpr int RADAR_LABEL_EDGE_MARGIN_PX = 4;
inline constexpr int RADAR_LABEL_OVERLAP_MARGIN_PX = 4;
inline constexpr int RADAR_CONNECTOR_LABEL_GAP_PX = 8;
inline constexpr int RADAR_CONNECTOR_MARKER_GAP_PX = 8;
inline constexpr int RADAR_CONNECTOR_DESIGN_PX = 20;
inline constexpr int RADAR_LABEL_KINK_PX = RADAR_CONNECTOR_DESIGN_PX;
inline constexpr int RADAR_LABEL_DIAGONAL_OFFSET_PX = RADAR_CONNECTOR_DESIGN_PX;
inline constexpr int RADAR_LABEL_EARLY_CLEARANCE_PX = 10;
inline constexpr int RADAR_CONNECTOR_EARLY_CLEARANCE_PX = 7;
inline constexpr int RADAR_EDGE_SOFT_MARGIN_PX = 20;

struct RadarScopeState {
  lv_obj_t *scope{nullptr};
  const lv_font_t *aircraft_font{nullptr};
  const lv_font_t *bearing_font{nullptr};
};

inline RadarScopeState &radar_scope_state_() {
  static RadarScopeState state{};
  return state;
}

inline lv_color_t radar_background_color_() { return AirDot::display_background_color(); }
inline lv_color_t radar_target_color_() { return AirDot::display_text_color(); }
inline lv_color_t radar_military_color_() { return AirDot::color(0xFF, 0x8C, 0x00); }

inline lv_color_t radar_aircraft_color_(const Aircraft &aircraft) {
  return aircraft.military ? radar_military_color_() : radar_target_color_();
}

inline void disable_radar_antialiasing_(lv_obj_t *scope) {
  static lv_display_t *disabled_display = nullptr;
  lv_display_t *display = scope == nullptr ? lv_display_get_default() : lv_obj_get_display(scope);
  if (display == nullptr || display == disabled_display)
    return;

  lv_display_set_antialiasing(display, false);
  disabled_display = display;
}

inline int radar_area_width_(const lv_area_t &area) {
  return static_cast<int>(area.x2 - area.x1 + 1);
}

inline int radar_area_height_(const lv_area_t &area) {
  return static_cast<int>(area.y2 - area.y1 + 1);
}

inline void draw_rect_(lv_layer_t *layer, const lv_area_t &area, lv_color_t color) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  dsc.border_width = 0;
  dsc.radius = 0;
  lv_draw_rect(layer, &dsc, &area);
}

inline void draw_pixel_(lv_layer_t *layer, lv_color_t color, int x, int y, int size = 1) {
  lv_area_t area{
      static_cast<lv_coord_t>(x),
      static_cast<lv_coord_t>(y),
      static_cast<lv_coord_t>(x + size - 1),
      static_cast<lv_coord_t>(y + size - 1),
  };
  draw_rect_(layer, area, color);
}

inline void draw_pixel_line_(lv_layer_t *layer, lv_color_t color, int x1, int y1, int x2, int y2,
                             int width = 1) {
  if (y1 == y2) {
    lv_area_t area{
        static_cast<lv_coord_t>(std::min(x1, x2)),
        static_cast<lv_coord_t>(y1),
        static_cast<lv_coord_t>(std::max(x1, x2) + width - 1),
        static_cast<lv_coord_t>(y1 + width - 1),
    };
    draw_rect_(layer, area, color);
    return;
  }

  if (x1 == x2) {
    lv_area_t area{
        static_cast<lv_coord_t>(x1),
        static_cast<lv_coord_t>(std::min(y1, y2)),
        static_cast<lv_coord_t>(x1 + width - 1),
        static_cast<lv_coord_t>(std::max(y1, y2) + width - 1),
    };
    draw_rect_(layer, area, color);
    return;
  }

  int x = x1;
  int y = y1;
  const int dx = std::abs(x2 - x1);
  const int sx = x1 < x2 ? 1 : -1;
  const int dy = -std::abs(y2 - y1);
  const int sy = y1 < y2 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    draw_pixel_(layer, color, x, y, width);
    if (x == x2 && y == y2)
      break;

    const int err2 = 2 * err;
    if (err2 >= dy) {
      err += dy;
      x += sx;
    }
    if (err2 <= dx) {
      err += dx;
      y += sy;
    }
  }
}

struct RadarTriangle {
  lv_point_t p[3]{};
};

inline RadarTriangle aircraft_triangle_points_(int center_x, int center_y, float heading_deg) {
  if (!std::isfinite(heading_deg))
    heading_deg = 0.0f;

  const float heading = heading_deg * DEG_TO_RAD;
  const float sin_h = std::sin(heading);
  const float cos_h = std::cos(heading);

  const int tip_x = center_x + static_cast<int>(std::round(sin_h * RADAR_AIRCRAFT_NOSE_PX));
  const int tip_y = center_y - static_cast<int>(std::round(cos_h * RADAR_AIRCRAFT_NOSE_PX));
  const int base_x = center_x - static_cast<int>(std::round(sin_h * RADAR_AIRCRAFT_TAIL_PX));
  const int base_y = center_y + static_cast<int>(std::round(cos_h * RADAR_AIRCRAFT_TAIL_PX));
  const int wing_x = static_cast<int>(std::round(cos_h * RADAR_AIRCRAFT_WING_PX));
  const int wing_y = static_cast<int>(std::round(sin_h * RADAR_AIRCRAFT_WING_PX));

  RadarTriangle triangle{};
  triangle.p[0].x = tip_x;
  triangle.p[0].y = tip_y;
  triangle.p[1].x = base_x + wing_x;
  triangle.p[1].y = base_y + wing_y;
  triangle.p[2].x = base_x - wing_x;
  triangle.p[2].y = base_y - wing_y;
  return triangle;
}

inline void draw_filled_triangle_(lv_layer_t *layer, lv_color_t color, const RadarTriangle &triangle) {
  int min_y = triangle.p[0].y;
  int max_y = triangle.p[0].y;
  for (int index = 1; index < 3; index++) {
    min_y = std::min<int>(min_y, triangle.p[index].y);
    max_y = std::max<int>(max_y, triangle.p[index].y);
  }

  for (int y = min_y; y <= max_y; y++) {
    float intersections[3]{};
    int intersection_count = 0;
    for (int edge = 0; edge < 3; edge++) {
      const lv_point_t &a = triangle.p[edge];
      const lv_point_t &b = triangle.p[(edge + 1) % 3];
      if (a.y == b.y)
        continue;

      const int min_edge_y = std::min<int>(a.y, b.y);
      const int max_edge_y = std::max<int>(a.y, b.y);
      if (y < min_edge_y || y > max_edge_y || (y == max_edge_y && y != max_y))
        continue;

      const float t = static_cast<float>(y - a.y) / static_cast<float>(b.y - a.y);
      intersections[intersection_count++] =
          static_cast<float>(a.x) + t * static_cast<float>(b.x - a.x);
    }

    if (intersection_count < 2)
      continue;

    std::sort(intersections, intersections + intersection_count);
    const int x1 = static_cast<int>(std::ceil(intersections[0]));
    const int x2 = static_cast<int>(std::floor(intersections[intersection_count - 1]));
    if (x2 < x1)
      continue;

    lv_area_t span{
        static_cast<lv_coord_t>(x1),
        static_cast<lv_coord_t>(y),
        static_cast<lv_coord_t>(x2),
        static_cast<lv_coord_t>(y),
    };
    draw_rect_(layer, span, color);
  }
}

inline void draw_aircraft_triangle_(lv_layer_t *layer, lv_color_t color, int center_x, int center_y,
                                    float heading_deg) {
  draw_filled_triangle_(layer, color, aircraft_triangle_points_(center_x, center_y, heading_deg));
}

struct RadarLabelPlacement {
  lv_area_t area{};
  lv_point_t connector_start{};
  lv_point_t connector_kink{};
  lv_point_t connector_end{};
  lv_color_t color{};
  const char *text{nullptr};
  const lv_font_t *font{nullptr};
  lv_text_align_t align{LV_TEXT_ALIGN_LEFT};
};

struct RadarAircraftRender {
  int x{0};
  int y{0};
  float bearing_rad{0.0f};
  float heading_deg{0.0f};
  lv_color_t color{};
  const char *text{nullptr};
};

inline lv_area_t label_area_from_top_left_(int x, int y, const lv_point_t &text_size) {
  return lv_area_t{
      static_cast<lv_coord_t>(x),
      static_cast<lv_coord_t>(y),
      static_cast<lv_coord_t>(x + text_size.x + 1),
      static_cast<lv_coord_t>(y + text_size.y + 1),
  };
}

inline int label_width_(const lv_area_t &area) {
  return static_cast<int>(area.x2 - area.x1 + 1);
}

inline int label_height_(const lv_area_t &area) {
  return static_cast<int>(area.y2 - area.y1 + 1);
}

inline bool label_area_inside_(const lv_area_t &scope_area, const lv_area_t &label_area) {
  return label_area.x1 >= scope_area.x1 + RADAR_LABEL_EDGE_MARGIN_PX &&
         label_area.y1 >= scope_area.y1 + RADAR_LABEL_EDGE_MARGIN_PX &&
         label_area.x2 <= scope_area.x2 - RADAR_LABEL_EDGE_MARGIN_PX &&
         label_area.y2 <= scope_area.y2 - RADAR_LABEL_EDGE_MARGIN_PX;
}

inline lv_area_t clamp_label_area_(const lv_area_t &scope_area, lv_area_t label_area) {
  const int width = label_width_(label_area);
  const int height = label_height_(label_area);
  const int min_x = scope_area.x1 + RADAR_LABEL_EDGE_MARGIN_PX;
  const int max_x = scope_area.x2 - RADAR_LABEL_EDGE_MARGIN_PX - width + 1;
  const int min_y = scope_area.y1 + RADAR_LABEL_EDGE_MARGIN_PX;
  const int max_y = scope_area.y2 - RADAR_LABEL_EDGE_MARGIN_PX - height + 1;
  const int x = std::clamp<int>(label_area.x1, min_x, std::max(min_x, max_x));
  const int y = std::clamp<int>(label_area.y1, min_y, std::max(min_y, max_y));
  return lv_area_t{
      static_cast<lv_coord_t>(x),
      static_cast<lv_coord_t>(y),
      static_cast<lv_coord_t>(x + width - 1),
      static_cast<lv_coord_t>(y + height - 1),
  };
}

inline bool aircraft_label_on_left_(int aircraft_x, int radar_center_x, float radial_x, float heading_deg) {
  float desired_x = radial_x;
  if (std::isfinite(heading_deg)) {
    const float heading = heading_deg * DEG_TO_RAD;
    desired_x = radial_x * 0.85f - std::sin(heading) * 1.15f;
  }

  if (std::fabs(desired_x) > 0.18f)
    return desired_x < 0.0f;
  if (aircraft_x == radar_center_x)
    return radial_x < 0.0f;
  return aircraft_x < radar_center_x;
}

inline int aircraft_label_vertical_sign_(int aircraft_y, int radar_center_y, float radial_y,
                                         float heading_deg) {
  float desired_y = radial_y;
  if (std::isfinite(heading_deg)) {
    const float heading = heading_deg * DEG_TO_RAD;
    desired_y = radial_y * 0.85f + std::cos(heading) * 1.15f;
  }

  if (std::fabs(desired_y) > 0.18f)
    return desired_y < 0.0f ? -1 : 1;
  return aircraft_y < radar_center_y ? -1 : 1;
}

inline lv_area_t aircraft_label_area_for_offset_(int aircraft_x, int aircraft_y, bool label_on_left,
                                                 int offset, int vertical_offset,
                                                 const lv_point_t &text_size) {
  const int x = label_on_left ? aircraft_x - offset - text_size.x : aircraft_x + offset;
  const int y = aircraft_y + vertical_offset - text_size.y / 2;

  return label_area_from_top_left_(x, y, text_size);
}

inline lv_area_t expand_area_(lv_area_t area, int amount) {
  area.x1 = static_cast<lv_coord_t>(area.x1 - amount);
  area.y1 = static_cast<lv_coord_t>(area.y1 - amount);
  area.x2 = static_cast<lv_coord_t>(area.x2 + amount);
  area.y2 = static_cast<lv_coord_t>(area.y2 + amount);
  return area;
}

inline int label_overlap_score_(const lv_area_t &area, const RadarLabelPlacement *placed, int placed_count) {
  int score = 0;
  for (int index = 0; index < placed_count; index++) {
    const lv_area_t &placed_area = placed[index].area;
    const int early_left = std::max<int>(area.x1, placed_area.x1 - RADAR_LABEL_EARLY_CLEARANCE_PX);
    const int early_right = std::min<int>(area.x2, placed_area.x2 + RADAR_LABEL_EARLY_CLEARANCE_PX);
    const int early_top = std::max<int>(area.y1, placed_area.y1 - RADAR_LABEL_EARLY_CLEARANCE_PX);
    const int early_bottom = std::min<int>(area.y2, placed_area.y2 + RADAR_LABEL_EARLY_CLEARANCE_PX);
    if (early_right >= early_left && early_bottom >= early_top)
      score += 1800 + (early_right - early_left + 1) * (early_bottom - early_top + 1);

    const int left = std::max<int>(area.x1, placed_area.x1 - RADAR_LABEL_OVERLAP_MARGIN_PX);
    const int right = std::min<int>(area.x2, placed_area.x2 + RADAR_LABEL_OVERLAP_MARGIN_PX);
    const int top = std::max<int>(area.y1, placed_area.y1 - RADAR_LABEL_OVERLAP_MARGIN_PX);
    const int bottom = std::min<int>(area.y2, placed_area.y2 + RADAR_LABEL_OVERLAP_MARGIN_PX);
    if (right >= left && bottom >= top)
      score += 65000 + (right - left + 1) * (bottom - top + 1) * 8;
  }
  return score;
}

inline lv_area_t aircraft_marker_area_(const RadarAircraftRender &aircraft) {
  constexpr int marker_clearance = RADAR_AIRCRAFT_NOSE_PX + RADAR_CONNECTOR_MARKER_GAP_PX;
  return lv_area_t{
      static_cast<lv_coord_t>(aircraft.x - marker_clearance),
      static_cast<lv_coord_t>(aircraft.y - marker_clearance),
      static_cast<lv_coord_t>(aircraft.x + marker_clearance),
      static_cast<lv_coord_t>(aircraft.y + marker_clearance),
  };
}

inline int aircraft_marker_overlap_score_(const lv_area_t &area, const RadarAircraftRender *aircraft,
                                          int aircraft_count) {
  int score = 0;
  for (int index = 0; index < aircraft_count; index++) {
    const lv_area_t marker_area = aircraft_marker_area_(aircraft[index]);
    const lv_area_t early_marker_area = expand_area_(marker_area, RADAR_LABEL_EARLY_CLEARANCE_PX);
    const int early_left = std::max<int>(area.x1, early_marker_area.x1);
    const int early_right = std::min<int>(area.x2, early_marker_area.x2);
    const int early_top = std::max<int>(area.y1, early_marker_area.y1);
    const int early_bottom = std::min<int>(area.y2, early_marker_area.y2);
    if (early_right >= early_left && early_bottom >= early_top)
      score += 2600 + (early_right - early_left + 1) * (early_bottom - early_top + 1);

    const int left = std::max<int>(area.x1, marker_area.x1);
    const int right = std::min<int>(area.x2, marker_area.x2);
    const int top = std::max<int>(area.y1, marker_area.y1);
    const int bottom = std::min<int>(area.y2, marker_area.y2);
    if (right >= left && bottom >= top)
      score += 120000 + (right - left + 1) * (bottom - top + 1) * 12;
  }
  return score;
}

inline int label_front_score_(const lv_area_t &area, int aircraft_x, int aircraft_y, float heading_deg) {
  if (!std::isfinite(heading_deg))
    return 0;

  const float heading = heading_deg * DEG_TO_RAD;
  const float heading_x = std::sin(heading);
  const float heading_y = -std::cos(heading);
  const float label_dx = static_cast<float>(area.x1 + label_width_(area) / 2 - aircraft_x);
  const float label_dy = static_cast<float>(area.y1 + label_height_(area) / 2 - aircraft_y);
  const float ahead = label_dx * heading_x + label_dy * heading_y;
  if (ahead <= 0.0f)
    return 0;

  return 14000 + static_cast<int>(ahead * 120.0f);
}

inline bool point_in_area_(const lv_area_t &area, int x, int y) {
  return x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
}

inline lv_area_t segment_area_(lv_point_t a, lv_point_t b, int margin) {
  return lv_area_t{
      static_cast<lv_coord_t>(std::min<int>(a.x, b.x) - margin),
      static_cast<lv_coord_t>(std::min<int>(a.y, b.y) - margin),
      static_cast<lv_coord_t>(std::max<int>(a.x, b.x) + margin),
      static_cast<lv_coord_t>(std::max<int>(a.y, b.y) + margin),
  };
}

inline bool areas_intersect_(const lv_area_t &a, const lv_area_t &b) {
  return a.x1 <= b.x2 && a.x2 >= b.x1 && a.y1 <= b.y2 && a.y2 >= b.y1;
}

inline bool segment_hits_area_(lv_point_t a, lv_point_t b, const lv_area_t &area) {
  int x = a.x;
  int y = a.y;
  const int x2 = b.x;
  const int y2 = b.y;
  const int dx = std::abs(x2 - x);
  const int sx = x < x2 ? 1 : -1;
  const int dy = -std::abs(y2 - y);
  const int sy = y < y2 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    if (point_in_area_(area, x, y))
      return true;
    if (x == x2 && y == y2)
      return false;

    const int err2 = 2 * err;
    if (err2 >= dy) {
      err += dy;
      x += sx;
    }
    if (err2 <= dx) {
      err += dx;
      y += sy;
    }
  }
}

inline int64_t segment_orientation_(const lv_point_t &a, const lv_point_t &b, const lv_point_t &c) {
  return static_cast<int64_t>(b.x - a.x) * static_cast<int64_t>(c.y - a.y) -
         static_cast<int64_t>(b.y - a.y) * static_cast<int64_t>(c.x - a.x);
}

inline bool segments_intersect_(const lv_point_t &a1, const lv_point_t &a2,
                                const lv_point_t &b1, const lv_point_t &b2) {
  const int64_t o1 = segment_orientation_(a1, a2, b1);
  const int64_t o2 = segment_orientation_(a1, a2, b2);
  const int64_t o3 = segment_orientation_(b1, b2, a1);
  const int64_t o4 = segment_orientation_(b1, b2, a2);

  return ((o1 > 0 && o2 < 0) || (o1 < 0 && o2 > 0)) &&
         ((o3 > 0 && o4 < 0) || (o3 < 0 && o4 > 0));
}

inline int edge_soft_score_(const lv_area_t &scope_area, const lv_area_t &area) {
  const int left_gap = area.x1 - scope_area.x1;
  const int right_gap = scope_area.x2 - area.x2;
  const int top_gap = area.y1 - scope_area.y1;
  const int bottom_gap = scope_area.y2 - area.y2;
  int score = 0;
  const int gaps[] = {left_gap, right_gap, top_gap, bottom_gap};
  for (int gap : gaps) {
    if (gap < RADAR_EDGE_SOFT_MARGIN_PX)
      score += (RADAR_EDGE_SOFT_MARGIN_PX - gap) * 120;
  }
  return score;
}

inline lv_point_t marker_connector_point_(int aircraft_x, int aircraft_y, float heading_deg,
                                          const lv_point_t &toward) {
  const float dx = static_cast<float>(toward.x - aircraft_x);
  const float dy = static_cast<float>(toward.y - aircraft_y);
  const float length = std::sqrt(dx * dx + dy * dy);
  if (length <= 0.001f)
    return lv_point_t{static_cast<lv_coord_t>(aircraft_x), static_cast<lv_coord_t>(aircraft_y)};

  const float unit_x = dx / length;
  const float unit_y = dy / length;
  const RadarTriangle triangle = aircraft_triangle_points_(aircraft_x, aircraft_y, heading_deg);
  float marker_edge = 0.0f;
  for (const auto &point : triangle.p) {
    const float projection =
        static_cast<float>(point.x - aircraft_x) * unit_x +
        static_cast<float>(point.y - aircraft_y) * unit_y;
    marker_edge = std::max(marker_edge, projection);
  }

  const float connector_distance = marker_edge + RADAR_CONNECTOR_MARKER_GAP_PX;
  return lv_point_t{
      static_cast<lv_coord_t>(
          aircraft_x + static_cast<int>(std::round(unit_x * connector_distance))),
      static_cast<lv_coord_t>(
          aircraft_y + static_cast<int>(std::round(unit_y * connector_distance))),
  };
}

inline void connector_points_for_area_(const lv_area_t &area, int aircraft_x, int aircraft_y,
                                       float heading_deg, lv_point_t &start, lv_point_t &kink,
                                       lv_point_t &end) {
  const int label_center_x = area.x1 + label_width_(area) / 2;
  const int label_center_y = area.y1 + label_height_(area) / 2;
  const bool label_on_left = label_center_x < aircraft_x;

  start.x = label_on_left
      ? static_cast<lv_coord_t>(area.x2 + RADAR_CONNECTOR_LABEL_GAP_PX)
      : static_cast<lv_coord_t>(area.x1 - RADAR_CONNECTOR_LABEL_GAP_PX);
  start.y = static_cast<lv_coord_t>(label_center_y);
  kink.x = label_on_left
      ? static_cast<lv_coord_t>(start.x + RADAR_LABEL_KINK_PX)
      : static_cast<lv_coord_t>(start.x - RADAR_LABEL_KINK_PX);
  kink.y = start.y;
  end = marker_connector_point_(aircraft_x, aircraft_y, heading_deg, kink);
}

inline int connector_length_score_(const lv_point_t &kink, const lv_point_t &end) {
  const int dx = static_cast<int>(kink.x) - static_cast<int>(end.x);
  const int dy = static_cast<int>(kink.y) - static_cast<int>(end.y);
  const int length = static_cast<int>(std::round(std::sqrt(static_cast<float>(dx * dx + dy * dy))));
  if (length <= 42)
    return 0;
  return (length - 42) * 260;
}

inline int connector_score_(const lv_area_t &area, int aircraft_x, int aircraft_y,
                            float heading_deg,
                            const RadarLabelPlacement *placed, int placed_count,
                            const RadarAircraftRender *aircraft, int aircraft_count) {
  lv_point_t start{};
  lv_point_t kink{};
  lv_point_t end{};
  connector_points_for_area_(area, aircraft_x, aircraft_y, heading_deg, start, kink, end);

  int score = connector_length_score_(kink, end);
  for (int index = 0; index < placed_count; index++) {
    const RadarLabelPlacement &placed_label = placed[index];
    if (segment_hits_area_(kink, end, placed_label.area) || segment_hits_area_(kink, start, placed_label.area))
      score += 30000;
    const lv_area_t expanded_label = expand_area_(placed_label.area, RADAR_CONNECTOR_EARLY_CLEARANCE_PX);
    if (segment_hits_area_(kink, end, expanded_label) || segment_hits_area_(kink, start, expanded_label))
      score += 4500;
    const lv_area_t candidate_diagonal_area = segment_area_(kink, end, RADAR_CONNECTOR_EARLY_CLEARANCE_PX);
    const lv_area_t candidate_horizontal_area = segment_area_(kink, start, RADAR_CONNECTOR_EARLY_CLEARANCE_PX);
    const lv_area_t placed_diagonal_area = segment_area_(
        placed_label.connector_kink, placed_label.connector_end, RADAR_CONNECTOR_EARLY_CLEARANCE_PX);
    const lv_area_t placed_horizontal_area = segment_area_(
        placed_label.connector_kink, placed_label.connector_start, RADAR_CONNECTOR_EARLY_CLEARANCE_PX);
    if (areas_intersect_(candidate_diagonal_area, placed_diagonal_area) ||
        areas_intersect_(candidate_diagonal_area, placed_horizontal_area) ||
        areas_intersect_(candidate_horizontal_area, placed_diagonal_area))
      score += 2200;
    if (segments_intersect_(kink, end, placed_label.connector_kink, placed_label.connector_end))
      score += 28000;
    if (segments_intersect_(kink, end, placed_label.connector_kink, placed_label.connector_start))
      score += 20000;
    if (segments_intersect_(kink, start, placed_label.connector_kink, placed_label.connector_end))
      score += 20000;
  }

  for (int index = 0; index < aircraft_count; index++) {
    if (aircraft[index].x == aircraft_x && aircraft[index].y == aircraft_y)
      continue;
    const lv_area_t hard_marker_area = aircraft_marker_area_(aircraft[index]);
    if (segment_hits_area_(kink, end, hard_marker_area) || segment_hits_area_(kink, start, hard_marker_area))
      score += 65000;
    const lv_area_t marker_area = expand_area_(hard_marker_area, RADAR_CONNECTOR_EARLY_CLEARANCE_PX);
    if (segment_hits_area_(kink, end, marker_area) || segment_hits_area_(kink, start, marker_area))
      score += 9000;
  }

  return score;
}

inline lv_area_t place_aircraft_label_area_(const lv_area_t &scope_area, int aircraft_x, int aircraft_y,
                                            int radar_center_x, int radar_center_y, float radial_x, float radial_y,
                                            float heading_deg, const lv_point_t &text_size,
                                            const RadarLabelPlacement *placed, int placed_count,
                                            const RadarAircraftRender *aircraft, int aircraft_count) {
  const bool preferred_left = aircraft_label_on_left_(aircraft_x, radar_center_x, radial_x, heading_deg);
  const int vertical_sign = aircraft_label_vertical_sign_(aircraft_y, radar_center_y, radial_y, heading_deg);
  constexpr int offset_adjustments[] = {0, 6, 12, -4, 18};
  constexpr int vertical_magnitudes[] = {
      RADAR_LABEL_DIAGONAL_OFFSET_PX,
      RADAR_LABEL_DIAGONAL_OFFSET_PX + 4,
      RADAR_LABEL_DIAGONAL_OFFSET_PX + 8,
      RADAR_LABEL_DIAGONAL_OFFSET_PX + 14,
  };

  lv_area_t best_area = clamp_label_area_(
      scope_area,
      aircraft_label_area_for_offset_(
          aircraft_x, aircraft_y, preferred_left, RADAR_AIRCRAFT_LABEL_OFFSET_PX,
          vertical_sign * RADAR_LABEL_DIAGONAL_OFFSET_PX, text_size));
  int best_score = 0x7fffffff;

  for (int side_index = 0; side_index < 2; side_index++) {
    const bool label_on_left = side_index == 0 ? preferred_left : !preferred_left;
    const int side_penalty = side_index == 0 ? 0 : 7600;

    for (int offset_adjustment : offset_adjustments) {
      const int offset = std::max(
          RADAR_AIRCRAFT_LABEL_OFFSET_PX - 22,
          RADAR_AIRCRAFT_LABEL_OFFSET_PX + offset_adjustment);
      const int inward_offset_penalty = offset_adjustment < 0 ? 4500 : 0;

      for (int vertical_side = 0; vertical_side < 2; vertical_side++) {
        const int vertical_direction = vertical_side == 0 ? vertical_sign : -vertical_sign;
        const int vertical_flip_penalty = vertical_side == 0 ? 0 : 6200;

        for (int vertical_magnitude : vertical_magnitudes) {
          const int vertical_offset = vertical_direction * vertical_magnitude;
          const lv_area_t raw_area = aircraft_label_area_for_offset_(
              aircraft_x, aircraft_y, label_on_left, offset, vertical_offset, text_size);
          const bool inside = label_area_inside_(scope_area, raw_area);
          const lv_area_t candidate = inside ? raw_area : clamp_label_area_(scope_area, raw_area);
          const int overlap_score = label_overlap_score_(candidate, placed, placed_count);
          const int marker_score = aircraft_marker_overlap_score_(candidate, aircraft, aircraft_count);
          const int connector_penalty = connector_score_(
              candidate, aircraft_x, aircraft_y, heading_deg, placed, placed_count,
              aircraft, aircraft_count);
          const int front_score = label_front_score_(candidate, aircraft_x, aircraft_y, heading_deg);
          const int edge_penalty = inside ? 0 : 20000;
          const int soft_edge_penalty = edge_soft_score_(scope_area, candidate);
          const int clamp_shift_penalty =
              std::abs(static_cast<int>(candidate.x1) - static_cast<int>(raw_area.x1)) * 240 +
              std::abs(static_cast<int>(candidate.y1) - static_cast<int>(raw_area.y1)) * 240;
          const int shape_penalty =
              std::abs(vertical_magnitude - RADAR_LABEL_KINK_PX) * 150;
          const int distance_penalty =
              std::abs(offset - RADAR_AIRCRAFT_LABEL_OFFSET_PX) * 42 +
              std::abs(vertical_magnitude - RADAR_LABEL_DIAGONAL_OFFSET_PX) * 95;
          const int score = overlap_score + marker_score + connector_penalty + front_score +
              edge_penalty + soft_edge_penalty + side_penalty + vertical_flip_penalty +
              inward_offset_penalty + clamp_shift_penalty + shape_penalty + distance_penalty;
          if (score < best_score) {
            best_score = score;
            best_area = candidate;
            if (score == 0)
              return best_area;
          }
        }
      }
    }
  }

  return best_area;
}

inline lv_text_align_t label_alignment_for_side_(bool label_on_left) {
  return label_on_left ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT;
}

inline void set_label_connector_points_(RadarLabelPlacement &placement, int aircraft_x, int aircraft_y,
                                        float heading_deg) {
  const int label_center_x = placement.area.x1 + label_width_(placement.area) / 2;
  const int label_center_y = placement.area.y1 + label_height_(placement.area) / 2;
  const bool label_on_left = label_center_x < aircraft_x;

  placement.connector_start.x = label_on_left
      ? static_cast<lv_coord_t>(placement.area.x2 + RADAR_CONNECTOR_LABEL_GAP_PX)
      : static_cast<lv_coord_t>(placement.area.x1 - RADAR_CONNECTOR_LABEL_GAP_PX);
  placement.connector_start.y = static_cast<lv_coord_t>(label_center_y);
  placement.connector_kink.x = label_on_left
      ? static_cast<lv_coord_t>(placement.connector_start.x + RADAR_LABEL_KINK_PX)
      : static_cast<lv_coord_t>(placement.connector_start.x - RADAR_LABEL_KINK_PX);
  placement.connector_kink.y = placement.connector_start.y;
  placement.connector_end = marker_connector_point_(aircraft_x, aircraft_y, heading_deg, placement.connector_kink);
}

inline void draw_label_in_area_(lv_layer_t *layer, const RadarLabelPlacement &placement) {
  if (placement.text == nullptr || placement.text[0] == '\0')
    return;

  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.text = placement.text;
  dsc.font = placement.font != nullptr ? placement.font : lv_font_get_default();
  dsc.color = placement.color;
  dsc.opa = LV_OPA_COVER;
  dsc.align = placement.align;
  dsc.text_local = 1;

  lv_draw_label(layer, &dsc, &placement.area);
}

inline void draw_label_(lv_layer_t *layer, const lv_area_t &area, const lv_font_t *font, lv_color_t color,
                        int x, int y, const char *text, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
  if (text == nullptr || text[0] == '\0')
    return;

  const lv_font_t *draw_font = font != nullptr ? font : lv_font_get_default();
  lv_point_t text_size{};
  lv_text_get_size(&text_size, text, draw_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

  RadarLabelPlacement placement{};
  placement.area = clamp_label_area_(area, label_area_from_top_left_(x, y, text_size));
  placement.color = color;
  placement.text = text;
  placement.font = draw_font;
  placement.align = align;
  draw_label_in_area_(layer, placement);
}

inline RadarLabelPlacement place_aircraft_label_(const lv_area_t &scope_area, const lv_font_t *font,
                                                 lv_color_t color, int radar_center_x, int aircraft_x,
                                                 int radar_center_y, int aircraft_y, float bearing_rad,
                                                 float heading_deg, const char *text,
                                                 const RadarLabelPlacement *placed, int placed_count,
                                                 const RadarAircraftRender *aircraft, int aircraft_count) {
  const lv_font_t *draw_font = font != nullptr ? font : lv_font_get_default();
  lv_point_t text_size{};
  lv_text_get_size(&text_size, text, draw_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

  const float radial_x = std::sin(bearing_rad);
  const float radial_y = -std::cos(bearing_rad);

  RadarLabelPlacement placement{};
  placement.area = place_aircraft_label_area_(
      scope_area, aircraft_x, aircraft_y, radar_center_x, radar_center_y, radial_x, radial_y,
      heading_deg, text_size, placed, placed_count, aircraft, aircraft_count);
  placement.color = color;
  placement.text = text;
  placement.font = draw_font;
  placement.align = label_alignment_for_side_(
      placement.area.x1 + label_width_(placement.area) / 2 < aircraft_x);
  set_label_connector_points_(placement, aircraft_x, aircraft_y, heading_deg);
  return placement;
}

inline void draw_compass_(lv_layer_t *layer, const lv_area_t &scope_area, const lv_font_t *font,
                          int center_x, int center_y, int outer_radius) {
  if (outer_radius <= 0)
    return;

  const lv_color_t color = radar_target_color_();
  for (int index = 0; index < RADAR_TICK_COUNT; index++) {
    const float angle = (index * 30.0f) * DEG_TO_RAD;
    const int outer_x = center_x + static_cast<int>(std::round(std::sin(angle) * outer_radius));
    const int outer_y = center_y - static_cast<int>(std::round(std::cos(angle) * outer_radius));
    const int inner_x = center_x + static_cast<int>(
        std::round(std::sin(angle) * (outer_radius - RADAR_TICK_LENGTH_PX)));
    const int inner_y = center_y - static_cast<int>(
        std::round(std::cos(angle) * (outer_radius - RADAR_TICK_LENGTH_PX)));
    draw_pixel_line_(layer, color, inner_x, inner_y, outer_x, outer_y, RADAR_TICK_WIDTH_PX);
  }

  const lv_font_t *draw_font = font != nullptr ? font : lv_font_get_default();
  lv_point_t text_size{};
  lv_text_get_size(&text_size, "N", draw_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  const int x = center_x - text_size.x / 2;
  const int y = center_y - outer_radius + RADAR_TICK_LENGTH_PX + RADAR_N_GAP_PX;
  draw_label_(layer, scope_area, draw_font, color, x, y, "N", LV_TEXT_ALIGN_CENTER);
}

inline void draw_aircraft_(lv_layer_t *layer, const lv_area_t &scope_area, const lv_font_t *font,
                           int center_x, int center_y, int radius, const Snapshot &snapshot) {
  if (!snapshot.valid)
    return;

  const uint32_t now = esphome::millis();
  if (static_cast<uint32_t>(now - snapshot.received_ms) > 30000UL)
    return;

  RadarLabelPlacement placed_labels[FLIGHT_RADAR_MAX_AIRCRAFT]{};
  int placed_label_count = 0;
  RadarAircraftRender aircraft_render[FLIGHT_RADAR_MAX_AIRCRAFT]{};
  int aircraft_render_count = 0;
  const float display_range_km =
      snapshot.range_km >= FLIGHT_RADAR_RADIUS_MIN_KM && snapshot.range_km <= FLIGHT_RADAR_RADIUS_MAX_KM
          ? static_cast<float>(snapshot.range_km)
          : FLIGHT_RADAR_RADIUS_KM;
  const int count = std::min<int>(snapshot.count, FLIGHT_RADAR_MAX_AIRCRAFT);
  for (int index = 0; index < count; index++) {
    const Aircraft &aircraft = snapshot.aircraft[index];
    const float ratio = std::clamp(aircraft.distance_km / display_range_km, 0.0f, 1.0f);
    const float bearing = aircraft.bearing_deg * DEG_TO_RAD;
    const int x = center_x + static_cast<int>(std::round(std::sin(bearing) * radius * ratio));
    const int y = center_y - static_cast<int>(std::round(std::cos(bearing) * radius * ratio));
    const float heading = std::isfinite(aircraft.track_deg) ? aircraft.track_deg : aircraft.bearing_deg;
    const lv_color_t aircraft_color = radar_aircraft_color_(aircraft);

    aircraft_render[aircraft_render_count++] = RadarAircraftRender{
        x,
        y,
        bearing,
        heading,
        aircraft_color,
        aircraft.callsign[0] == '\0' ? "--" : aircraft.callsign,
    };
  }

  for (int index = 0; index < aircraft_render_count; index++) {
    const RadarAircraftRender &aircraft = aircraft_render[index];
    draw_aircraft_triangle_(layer, aircraft.color, aircraft.x, aircraft.y, aircraft.heading_deg);
  }

  std::sort(aircraft_render, aircraft_render + aircraft_render_count,
            [center_x, center_y](const RadarAircraftRender &a, const RadarAircraftRender &b) {
              const int adx = a.x - center_x;
              const int ady = a.y - center_y;
              const int bdx = b.x - center_x;
              const int bdy = b.y - center_y;
              return adx * adx + ady * ady < bdx * bdx + bdy * bdy;
            });

  for (int index = 0; index < aircraft_render_count; index++) {
    const RadarAircraftRender &aircraft = aircraft_render[index];
    RadarLabelPlacement label = place_aircraft_label_(
        scope_area, font, aircraft.color, center_x, aircraft.x, center_y, aircraft.y,
        aircraft.bearing_rad, aircraft.heading_deg, aircraft.text,
        placed_labels, placed_label_count, aircraft_render, aircraft_render_count);
    draw_pixel_line_(layer, aircraft.color, label.connector_kink.x, label.connector_kink.y,
                     label.connector_end.x, label.connector_end.y, RADAR_TICK_WIDTH_PX);
    draw_pixel_line_(layer, aircraft.color, label.connector_kink.x, label.connector_kink.y,
                     label.connector_start.x, label.connector_start.y, RADAR_TICK_WIDTH_PX);
    draw_label_in_area_(layer, label);

    if (placed_label_count < FLIGHT_RADAR_MAX_AIRCRAFT)
      placed_labels[placed_label_count++] = label;
  }
}

inline void draw_radar_scope_(lv_event_t *event) {
  auto *state = static_cast<RadarScopeState *>(lv_event_get_user_data(event));
  lv_obj_t *scope = static_cast<lv_obj_t *>(lv_event_get_target(event));
  lv_layer_t *layer = lv_event_get_layer(event);
  if (state == nullptr || scope == nullptr || layer == nullptr || scope != state->scope)
    return;

  lv_area_t area;
  lv_obj_get_coords(scope, &area);
  draw_rect_(layer, area, radar_background_color_());

  const int width = radar_area_width_(area);
  const int height = radar_area_height_(area);
  const int size = std::min(width, height);
  const int center_x = area.x1 + width / 2;
  const int center_y = area.y1 + height / 2;
  const int radius = std::max(20, size / 2 - RADAR_SCREEN_MARGIN);

  draw_compass_(layer, area, state->bearing_font, center_x, center_y, radius);
  draw_aircraft_(layer, area, state->aircraft_font, center_x, center_y,
                 std::max(20, radius - RADAR_TRAFFIC_EDGE_RESERVE_PX), current_snapshot());
}

inline void prepare_radar_scope_(lv_obj_t *scope, const lv_font_t *aircraft_font, const lv_font_t *bearing_font) {
  if (scope == nullptr)
    return;

  auto &state = radar_scope_state_();
  state.aircraft_font = aircraft_font;
  state.bearing_font = bearing_font != nullptr ? bearing_font : aircraft_font;
  disable_radar_antialiasing_(scope);
  if (state.scope != scope) {
    state.scope = scope;
    lv_obj_add_event_cb(scope, draw_radar_scope_, LV_EVENT_DRAW_MAIN, &state);
  }

  lv_obj_set_style_bg_color(scope, radar_background_color_(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scope, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(scope, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(scope, 0, LV_PART_MAIN);
  lv_obj_remove_flag(scope, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_invalidate(scope);
}

template<typename ScopeWidget>
inline void refresh_radar_scope(ScopeWidget *scope, const lv_font_t *aircraft_font, const lv_font_t *bearing_font) {
  prepare_radar_scope_(AirDot::lv_obj(scope), aircraft_font, bearing_font);
}

template<typename ScopeWidget>
inline void refresh_radar_scope(ScopeWidget *scope, const esphome::font::Font *aircraft_font,
                                const esphome::font::Font *bearing_font) {
  const lv_font_t *aircraft_lv_font = aircraft_font == nullptr ? nullptr : aircraft_font->get_lv_font();
  const lv_font_t *bearing_lv_font = bearing_font == nullptr ? nullptr : bearing_font->get_lv_font();
  prepare_radar_scope_(AirDot::lv_obj(scope), aircraft_lv_font, bearing_lv_font);
}

}  // namespace AirDot::flight_radar
