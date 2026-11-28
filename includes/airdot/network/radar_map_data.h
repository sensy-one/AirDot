#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace AirDot::radar_map {

inline constexpr int NORMALIZED_RADIUS = 10000;
inline constexpr int COVERAGE_RADIUS = 20000;
inline constexpr size_t MAX_SEGMENTS = 16384;
// Marine maps share the NVS partition with the road map and also carry the
// water mask, so they are capped lower: a full marine blob (~20 NVS pages)
// still leaves room to rewrite a full road map (~38 pages). A tile set over
// budget retries at a lower zoom, as for roads. Real coastal maps measured
// 1,400-2,200 segments.
inline constexpr size_t MAX_MARINE_SEGMENTS = 6144;
inline constexpr double PI = 3.14159265358979323846;
inline constexpr double EARTH_RADIUS_KM = 6371.0;

// Append only: the numeric values are persisted in the ADMP cache.
enum class Style : uint8_t { MINOR, MAJOR, MOTORWAY, WATER, COAST, RIVER };
enum class MapKind : uint8_t { ROADS, MARINE };
inline constexpr size_t MAP_KIND_COUNT = 2;
// Marine maps carry a water mask: one bit per display pixel of the scope,
// centred on the map origin, at the fixed radar scale (122 px per range,
// 222 px visible radius). A clear bit inside the scope is land.
inline constexpr int MASK_PIXELS_PER_RANGE = 122;
inline constexpr int MASK_RADIUS = 222;
inline constexpr int MASK_SIZE = 2 * MASK_RADIUS;
inline constexpr int MASK_STRIDE = (MASK_SIZE + 7) / 8;
inline constexpr size_t MASK_BYTES = static_cast<size_t>(MASK_SIZE) * MASK_STRIDE;
enum class Status : uint8_t { EMPTY, LOADING, READY, FAILED, OFFLINE, UNAVAILABLE };
struct Segment { int16_t x1, y1, x2, y2; Style style; };
struct MapData {
  float center_latitude{0};
  float center_longitude{0};
  uint8_t range_km{25};
  uint16_t count{0};
  bool truncated{false};
  bool coarse{false};
  MapKind kind{MapKind::ROADS};
  bool has_water_mask{false};
  Segment segments[MAX_SEGMENTS]{};
  uint8_t water_mask[MASK_BYTES]{};
};
// Cell coordinates: 0..MASK_SIZE-1, origin at the top-left of the scope square.
inline bool water_at(const MapData &map, int x, int y) {
  if (x < 0 || y < 0 || x >= MASK_SIZE || y >= MASK_SIZE) return false;
  return (map.water_mask[static_cast<size_t>(y) * MASK_STRIDE + (x >> 3)] >> (x & 7)) & 1;
}

inline size_t segment_capacity(const MapData &map) {
  return map.kind == MapKind::MARINE ? MAX_MARINE_SEGMENTS : MAX_SEGMENTS;
}
inline bool valid_coordinates(double latitude, double longitude) {
  return std::isfinite(latitude) && std::isfinite(longitude) && latitude >= -90 && latitude <= 90 && longitude >= -180 && longitude <= 180;
}
inline int32_t coordinate_key(double coordinate) { return static_cast<int32_t>(std::llround(coordinate * 100000.0)); }
inline bool matches(const MapData &map, float latitude, float longitude, uint8_t range) {
  return map.range_km == range && coordinate_key(map.center_latitude) == coordinate_key(latitude) && coordinate_key(map.center_longitude) == coordinate_key(longitude);
}

}  // namespace AirDot::radar_map
