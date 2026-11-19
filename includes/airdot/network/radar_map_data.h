#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace AirDot::radar_map {

inline constexpr int NORMALIZED_RADIUS = 10000;
inline constexpr int COVERAGE_RADIUS = 20000;
inline constexpr size_t MAX_SEGMENTS = 16384;
inline constexpr double PI = 3.14159265358979323846;
inline constexpr double EARTH_RADIUS_KM = 6371.0;

enum class Style : uint8_t { MINOR, MAJOR, MOTORWAY, WATER };
enum class Status : uint8_t { EMPTY, LOADING, READY, FAILED, OFFLINE, UNAVAILABLE };
struct Segment { int16_t x1, y1, x2, y2; Style style; };
struct MapData {
  float center_latitude{0};
  float center_longitude{0};
  uint8_t range_km{25};
  uint16_t count{0};
  bool truncated{false};
  bool coarse{false};
  Segment segments[MAX_SEGMENTS]{};
};

inline bool valid_coordinates(double latitude, double longitude) {
  return std::isfinite(latitude) && std::isfinite(longitude) && latitude >= -90 && latitude <= 90 && longitude >= -180 && longitude <= 180;
}
inline int32_t coordinate_key(double coordinate) { return static_cast<int32_t>(std::llround(coordinate * 100000.0)); }
inline bool matches(const MapData &map, float latitude, float longitude, uint8_t range) {
  return map.range_km == range && coordinate_key(map.center_latitude) == coordinate_key(latitude) && coordinate_key(map.center_longitude) == coordinate_key(longitude);
}

}  // namespace AirDot::radar_map
