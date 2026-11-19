#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "radar_map_data.h"

namespace AirDot::radar_map {

inline constexpr int FADE_START_PERCENT = 75;

inline uint16_t map_gray_rgb565(uint8_t gray, int x, int y) {
  if (gray == 255) return 0xFFFF;
  static constexpr uint8_t threshold[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
  uint16_t level = gray >> 4;
  if ((gray & 15) > threshold[(y & 3) * 4 + (x & 3)]) ++level;
  if (level == 16) return 0xFFFF;
  return static_cast<uint16_t>((level << 12) | (level << 7) | (level << 1));
}

// NORMALIZED_RADIUS uses the configured aircraft range.
inline void rasterize_map(const MapData &map, uint16_t *pixels, int width, int height,
                          int center_x, int center_y, int pixels_per_range, int clip_radius,
                          bool dark) {
  if (pixels == nullptr || width <= 0 || height <= 0 || pixels_per_range <= 0 || clip_radius <= 0)
    return;
  std::fill_n(pixels, static_cast<size_t>(width) * height, dark ? 0x0000 : 0xFFFF);
  const int radius_squared = clip_radius * clip_radius;
  const int fade_radius = clip_radius * FADE_START_PERCENT / 100;
  const int fade_start_squared = fade_radius * fade_radius;
  const int fade_span = std::max(1, radius_squared - fade_start_squared);

  auto plot = [&](int x, int y, int gray) {
    if (x < 0 || x >= width || y < 0 || y >= height)
      return;
    const int dx = x - center_x;
    const int dy = y - center_y;
    const int distance_squared = dx * dx + dy * dy;
    if (distance_squared > radius_squared)
      return;
    if (distance_squared > fade_start_squared) {
      const int weight = std::max(0, radius_squared - distance_squared);
      gray = dark ? gray * weight / fade_span : 255 - (255 - gray) * weight / fade_span;
    }
    auto &pixel = pixels[static_cast<size_t>(y) * width + x];
    const uint16_t color = map_gray_rgb565(static_cast<uint8_t>(gray), x, y);
    pixel = dark ? std::max(pixel, color) : std::min(pixel, color);
  };

  const Style passes[] = {Style::WATER, Style::MINOR, Style::MAJOR, Style::MOTORWAY};
  for (const auto style : passes) {
    const int gray = dark ? (style == Style::MOTORWAY ? 72 : style == Style::MAJOR ? 52 :
                            style == Style::MINOR ? 35 : 28)
                          : (style == Style::MOTORWAY ? 181 : style == Style::MAJOR ? 202 :
                             style == Style::MINOR ? 221 : 229);
    for (size_t i = 0; i < std::min<size_t>(map.count, MAX_SEGMENTS); ++i) {
      const auto &segment = map.segments[i];
      if (segment.style != style)
        continue;
      int x = center_x + static_cast<int>(std::lround(
          static_cast<double>(segment.x1) * pixels_per_range / NORMALIZED_RADIUS));
      int y = center_y + static_cast<int>(std::lround(
          static_cast<double>(segment.y1) * pixels_per_range / NORMALIZED_RADIUS));
      const int x2 = center_x + static_cast<int>(std::lround(
          static_cast<double>(segment.x2) * pixels_per_range / NORMALIZED_RADIUS));
      const int y2 = center_y + static_cast<int>(std::lround(
          static_cast<double>(segment.y2) * pixels_per_range / NORMALIZED_RADIUS));
      const int dx = std::abs(x2 - x);
      const int sx = x < x2 ? 1 : -1;
      const int dy = -std::abs(y2 - y);
      const int sy = y < y2 ? 1 : -1;
      int error = dx + dy;
      while (true) {
        plot(x, y, gray);
        if (style == Style::MOTORWAY) {
          if (dx >= -dy) plot(x, y + 1, gray);
          else plot(x + 1, y, gray);
        }
        if (x == x2 && y == y2)
          break;
        const int twice_error = 2 * error;
        if (twice_error >= dy) { error += dy; x += sx; }
        if (twice_error <= dx) { error += dx; y += sy; }
      }
    }
  }
}

}  // namespace AirDot::radar_map
