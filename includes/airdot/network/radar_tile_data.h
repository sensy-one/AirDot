#pragma once

#include "radar_map_data.h"
#include "radar_map_projection.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#ifdef USE_ESP32
#include <esp_heap_caps.h>
#endif

namespace AirDot::radar_map::tiles {

// A 512-pixel logical tile controls source LOD only. Geometry always retains
// its true geographical position and the aircraft's azimuthal projection.
inline constexpr uint8_t MAX_ZOOM = 14;
inline constexpr uint8_t MAX_TILES = 12;
inline constexpr double MERCATOR_LATITUDE = 85.0511287798066;
inline constexpr double PIXELS_PER_RANGE = 122.0;
inline constexpr double SCOPE_RADIUS = 222.0;
inline constexpr size_t MAX_TILE_BYTES = 2UL * 1024UL * 1024UL;

enum class Result : uint8_t { OK, LOWER_ZOOM, BUDGET, INVALID, OUTSIDE_COVERAGE };
struct Tile { uint8_t z{0}; uint32_t x{0}, y{0}; };
struct TilePlan {
  Tile tiles[MAX_TILES]{};
  uint8_t count{0}, zoom{0};
  bool partial_coverage{false};
};

inline uint8_t initial_zoom(double latitude, uint8_t range) {
  if (!std::isfinite(latitude) || std::fabs(latitude) > MERCATOR_LATITUDE || range < 5 || range > 50) return 0;
  const double meters_per_pixel = range * 1000.0 / PIXELS_PER_RANGE;
  const double zoom = std::floor(std::log2(156543.03392804097 * std::cos(latitude * PI / 180.0) /
                                        meters_per_pixel)) - 1.0;
  return static_cast<uint8_t>(std::clamp(zoom, 0.0, double(MAX_ZOOM)));
}

inline double mercator_y(double latitude) {
  const double radians = std::clamp(latitude, -MERCATOR_LATITUDE, MERCATOR_LATITUDE) * PI / 180.0;
  return (1.0 - std::asinh(std::tan(radians)) / PI) / 2.0;
}

inline Result plan_tiles(double latitude, double longitude, uint8_t range, uint8_t zoom, TilePlan &plan) {
  plan = TilePlan{};
  plan.zoom = zoom;
  if (!valid_coordinates(latitude, longitude) || range < 5 || range > 50 || range % 5 || zoom > MAX_ZOOM)
    return Result::INVALID;
  if (std::fabs(latitude) > MERCATOR_LATITUDE) return Result::OUTSIDE_COVERAGE;
  // Two spare display pixels cover line thickness and floating-point rounding.
  const double angular = range * ((SCOPE_RADIUS + 2.0) / PIXELS_PER_RANGE) / EARTH_RADIUS_KM;
  const double phi = latitude * PI / 180.0;
  const double south = latitude - angular * 180.0 / PI, north = latitude + angular * 180.0 / PI;
  plan.partial_coverage = south < -MERCATOR_LATITUDE || north > MERCATOR_LATITUDE;
  const bool includes_pole = phi - angular <= -PI / 2.0 || phi + angular >= PI / 2.0;
  const double longitude_delta = includes_pole ? 180.0 :
      std::asin(std::clamp(std::sin(angular) / std::cos(phi), -1.0, 1.0)) * 180.0 / PI;
  const int64_t side = int64_t(1) << zoom;
  int64_t x_first = static_cast<int64_t>(std::floor((longitude - longitude_delta + 180.0) / 360.0 * side));
  int64_t x_last = static_cast<int64_t>(std::floor((longitude + longitude_delta + 180.0) / 360.0 * side));
  if (x_last - x_first + 1 > side) x_last = x_first + side - 1;
  const int64_t y_first = std::clamp<int64_t>(static_cast<int64_t>(std::floor(mercator_y(north) * side)), 0, side - 1);
  const int64_t y_last = std::clamp<int64_t>(static_cast<int64_t>(std::floor(mercator_y(south) * side)), 0, side - 1);
  if ((x_last - x_first + 1) * (y_last - y_first + 1) > MAX_TILES) return Result::LOWER_ZOOM;
  for (int64_t y = y_first; y <= y_last; ++y) {
    for (int64_t x = x_first; x <= x_last; ++x) {
      const uint32_t wrapped_x = static_cast<uint32_t>((x % side + side) % side);
      plan.tiles[plan.count++] = Tile{zoom, wrapped_x, static_cast<uint32_t>(y)};
    }
  }
  return plan.count ? Result::OK : Result::INVALID;
}

namespace detail {
struct Span { const uint8_t *data{nullptr}; size_t size{0}; };
inline bool equals(Span value, const char *text) {
  const size_t length = std::strlen(text);
  return value.size == length && (!length || std::memcmp(value.data, text, length) == 0);
}

// Protobuf is decoded directly from the bounded HTTP buffer. Unknown fields
// are skipped by wire type; no generated object graph or heap allocation exists.
class Reader {
 public:
  explicit Reader(Span input) : input_(input) {}
  bool empty() const { return position_ == input_.size; }
  bool varint(uint64_t &value) {
    value = 0;
    for (unsigned i = 0; i < 10; ++i) {
      if (position_ == input_.size) return false;
      const uint8_t byte = input_.data[position_++];
      if (i == 9 && byte > 1) return false;
      value |= uint64_t(byte & 0x7f) << (i * 7);
      if (!(byte & 0x80)) return true;
    }
    return false;
  }
  bool field(uint32_t &number, uint8_t &wire, uint64_t &integer, Span &bytes) {
    uint64_t tag = 0;
    if (!varint(tag) || !(tag >> 3) || (tag >> 3) > 0x1fffffff) return false;
    number = static_cast<uint32_t>(tag >> 3); wire = tag & 7; bytes = Span{}; integer = 0;
    if (wire == 0) return varint(integer);
    uint64_t length = wire == 1 ? 8 : wire == 5 ? 4 : 0;
    if (wire == 2) { if (!varint(length)) return false; }
    else if (wire != 1 && wire != 5) return false;
    if (length > input_.size - position_) return false;
    bytes = Span{input_.data + position_, static_cast<size_t>(length)};
    position_ += static_cast<size_t>(length);
    return true;
  }
 private:
  Span input_;
  size_t position_{0};
};

// String values shared by the road, water and waterway layers. One table holds
// every value of a layer; the key that refers to a value decides its meaning.
enum class FeatureClass : uint8_t {
  OTHER, MOTORWAY, TRUNK, PRIMARY, SECONDARY, TERTIARY, MINOR, SERVICE,
  OCEAN, LAKE, RIVER, DOCK, POND, CANAL, TUNNEL
};
inline FeatureClass feature_class(Span value) {
  if (equals(value, "motorway")) return FeatureClass::MOTORWAY;
  if (equals(value, "trunk")) return FeatureClass::TRUNK;
  if (equals(value, "primary")) return FeatureClass::PRIMARY;
  if (equals(value, "secondary")) return FeatureClass::SECONDARY;
  if (equals(value, "tertiary")) return FeatureClass::TERTIARY;
  if (equals(value, "minor")) return FeatureClass::MINOR;
  if (equals(value, "service")) return FeatureClass::SERVICE;
  if (equals(value, "ocean")) return FeatureClass::OCEAN;
  if (equals(value, "lake")) return FeatureClass::LAKE;
  if (equals(value, "river")) return FeatureClass::RIVER;
  if (equals(value, "dock")) return FeatureClass::DOCK;
  if (equals(value, "pond")) return FeatureClass::POND;
  if (equals(value, "canal")) return FeatureClass::CANAL;
  if (equals(value, "tunnel")) return FeatureClass::TUNNEL;
  return FeatureClass::OTHER;
}
inline bool class_value(Span message, FeatureClass &value) {
  Reader reader(message); value = FeatureClass::OTHER;
  uint32_t field; uint8_t wire; uint64_t integer; Span bytes;
  while (!reader.empty()) {
    if (!reader.field(field, wire, integer, bytes)) return false;
    if (field == 1) { if (wire != 2) return false; value = feature_class(bytes); }
  }
  return true;
}

struct Point { float x{0}, y{0}; };
class Polyline {
 public:
  explicit Polyline(MapData &map) : map_(map) {}
  void style(Style value) { style_ = value; }
  bool point(double x, double y) {
    if (count_) {
      const double dx = x - points_[count_ - 1].x, dy = y - points_[count_ - 1].y;
      if (dx * dx + dy * dy < 1.0) return true;
    }
    points_[count_++] = Point{static_cast<float>(x), static_cast<float>(y)};
    return count_ < 128 || flush(true);
  }
  bool finish() { return flush(false); }
  void reset() { count_ = 0; }
 private:
  MapData &map_;
  Point points_[128]{};
  uint8_t count_{0};
  Style style_{Style::MINOR};
  bool emit(const Point &a, const Point &b) {
    double x1 = a.x, y1 = a.y, x2 = b.x, y2 = b.y;
    // Keep the full visible circle plus two pixels for strokes. Storing roads
    // outside that circle consumes capacity without contributing to the display.
    constexpr double radius = (SCOPE_RADIUS + 2.0) / PIXELS_PER_RANGE * NORMALIZED_RADIUS;
    const double dx = x2 - x1, dy = y2 - y1, length = dx * dx + dy * dy;
    if (length < 1e-12) return true;
    if (x1*x1+y1*y1 > radius*radius || x2*x2+y2*y2 > radius*radius) {
      const double bq = 2 * (x1 * dx + y1 * dy), cq = x1*x1+y1*y1-radius*radius;
      const double discriminant = bq*bq-4*length*cq;
      if (discriminant < 0) return true;
      const double root = std::sqrt(discriminant);
      const double low = std::max(0.0,(-bq-root)/(2*length)), high = std::min(1.0,(-bq+root)/(2*length));
      if (low >= high) return true;
      x2 = x1+high*dx; y2 = y1+high*dy; x1 += low*dx; y1 += low*dy;
    }
    const Segment segment{static_cast<int16_t>(std::lround(x1)),static_cast<int16_t>(std::lround(y1)),
                          static_cast<int16_t>(std::lround(x2)),static_cast<int16_t>(std::lround(y2)),style_};
    if (segment.x1 == segment.x2 && segment.y1 == segment.y2) return true;
    if (map_.count >= segment_capacity(map_)) return false;
    map_.segments[map_.count++] = segment;
    return true;
  }
  bool flush(bool keep_last) {
    if (count_ >= 2) {
      bool keep[128]{}; uint8_t first_stack[128]{}, last_stack[128]{};
      unsigned stack = 1; first_stack[0] = 0; last_stack[0] = count_ - 1;
      keep[0] = keep[count_ - 1] = true;
      while (stack) {
        --stack; const unsigned first = first_stack[stack], last = last_stack[stack];
        const double dx = points_[last].x-points_[first].x, dy = points_[last].y-points_[first].y;
        const double length = dx*dx+dy*dy;
        constexpr double error = 0.5 * NORMALIZED_RADIUS / PIXELS_PER_RANGE;
        double maximum = error*error; unsigned farthest = 0;
        for (unsigned i = first + 1; i < last; ++i) {
          const double px = points_[i].x-points_[first].x, py = points_[i].y-points_[first].y;
          const double t = length > 0 ? std::clamp((px*dx+py*dy)/length,0.0,1.0) : 0;
          const double ex = px-t*dx, ey = py-t*dy, distance = ex*ex+ey*ey;
          if (distance > maximum) { maximum = distance; farthest = i; }
        }
        if (farthest) {
          keep[farthest] = true;
          first_stack[stack] = first; last_stack[stack++] = farthest;
          first_stack[stack] = farthest; last_stack[stack++] = last;
        }
      }
      unsigned previous = 0;
      for (unsigned i = 1; i < count_; ++i) if (keep[i]) {
        if (!emit(points_[previous],points_[i])) return false;
        previous = i;
      }
    }
    if (keep_last && count_) { points_[0] = points_[count_ - 1]; count_ = 1; }
    else count_ = 0;
    return true;
  }
};
}  // namespace detail

class TileDecoder {
 public:
  explicit TileDecoder(MapData &map, void (*yield_fn)() = nullptr)
      : map_(map), projection_(map.center_latitude,map.center_longitude,map.range_km), line_(map),
        meters_per_pixel_(map.range_km*1000.0/PIXELS_PER_RANGE), yield_fn_(yield_fn) {
    map_.count = 0; map_.truncated = false; map_.has_water_mask = false;
    if (!valid_coordinates(map.center_latitude,map.center_longitude) || map.range_km < 5 ||
        map.range_km > 50 || map.range_km % 5) result_ = Result::INVALID;
    if (map_.kind == MapKind::MARINE) {
      std::memset(map_.water_mask, 0, MASK_BYTES);
#ifdef USE_ESP32
      fill_scratch_ = static_cast<uint8_t *>(heap_caps_calloc(1, MASK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
      fill_scratch_ = static_cast<uint8_t *>(std::calloc(1, MASK_BYTES));
#endif
      map_.has_water_mask = fill_scratch_ != nullptr;
    }
  }
  ~TileDecoder() {
#ifdef USE_ESP32
    heap_caps_free(fill_scratch_);
#else
    std::free(fill_scratch_);
#endif
  }
  TileDecoder(const TileDecoder &) = delete;
  TileDecoder &operator=(const TileDecoder &) = delete;
  Result decode_tile(const uint8_t *data, size_t length, const Tile &tile) {
    if (result_ != Result::OK) return result_;
    if ((length && !data) || tile.z > MAX_ZOOM || tile.x >= (uint32_t(1)<<tile.z) || tile.y >= (uint32_t(1)<<tile.z))
      return fail(Result::INVALID);
    if (length > MAX_TILE_BYTES) return fail(Result::BUDGET);
    tile_ = tile; tile_points_ = 0; tile_features_ = 0;
    detail::Reader reader(detail::Span{data,length});
    uint32_t field; uint8_t wire; uint64_t integer; detail::Span bytes;
    const bool marine = map_.kind == MapKind::MARINE;
    bool seen[3]{};
    while (!reader.empty()) {
      if (!reader.field(field,wire,integer,bytes)) return fail(Result::INVALID);
      if (field != 3) continue;
      if (wire != 2) return fail(Result::INVALID);
      // The name may occur after features. First inspect all layer metadata.
      detail::Reader names(bytes); detail::Span name;
      while (!names.empty()) {
        detail::Span value;
        if (!names.field(field,wire,integer,value)) return fail(Result::INVALID);
        if (field == 1) { if (wire != 2) return fail(Result::INVALID); name = value; }
      }
      Layer id;
      if (!marine && detail::equals(name,"transportation")) id = Layer::TRANSPORTATION;
      else if (marine && detail::equals(name,"water")) id = Layer::WATER;
      else if (marine && detail::equals(name,"waterway")) id = Layer::WATERWAY;
      else continue;
      if (seen[static_cast<uint8_t>(id)]) return fail(Result::INVALID);
      seen[static_cast<uint8_t>(id)] = true;
      const Result value = layer(bytes,id);
      if (value != Result::OK) return fail(value);
    }
    return Result::OK;
  }
 private:
  enum class Layer : uint8_t { TRANSPORTATION, WATER, WATERWAY };
  static constexpr size_t MAX_VALUES = 1024;
  static constexpr uint32_t MAX_TILE_POINTS = 250000;
  static constexpr uint32_t MAX_TILE_FEATURES = 40000;
  MapData &map_;
  Projection projection_;
  detail::Polyline line_;
  double meters_per_pixel_;
  void (*yield_fn_)(){nullptr};
  uint16_t points_since_yield_{0};
  Result result_{Result::OK};
  Tile tile_{};
  detail::FeatureClass values_[MAX_VALUES]{};
  Layer layer_{Layer::TRANSPORTATION};
  uint32_t key_count_{0}, value_count_{0}, class_key_{UINT32_MAX}, brunnel_key_{UINT32_MAX}, extent_{4096};
  // Water fill: crossings are toggled into fill_scratch_ per feature, then
  // each touched row is prefix-XORed (even-odd rule) and ORed into the mask.
  uint8_t *fill_scratch_{nullptr};
  int fill_row_min_{MASK_SIZE}, fill_row_max_{-1};
  // Coastline ring state, in tile coordinates.
  bool chain_open_{false};
  double chain_x_{0}, chain_y_{0};
  uint32_t tile_points_{0}, tile_features_{0};

  Result fail(Result result) {
    // A failed tile invalidates the complete private viewport. There is never
    // a successful first-N prefix for the caller to publish or persist.
    map_.count = 0; map_.truncated = false;
    line_.reset(); result_ = result; return result;
  }
  bool allowed(detail::FeatureClass road) const {
    using R = detail::FeatureClass;
    if (road == R::MOTORWAY || road == R::TRUNK || road == R::PRIMARY) return true;
    if (road == R::SECONDARY) return meters_per_pixel_ <= 250;
    if (road == R::TERTIARY) return meters_per_pixel_ <= 125;
    if (road == R::MINOR) return meters_per_pixel_ <= 65;
    if (road == R::SERVICE) return meters_per_pixel_ <= 50;
    return false;
  }
  // Water areas whose outline is drawn. Tunnelled water never shows.
  bool allowed_water(detail::FeatureClass value) const {
    using C = detail::FeatureClass;
    if (value == C::OCEAN || value == C::LAKE || value == C::RIVER || value == C::DOCK) return true;
    if (value == C::POND) return meters_per_pixel_ <= 65;
    return false;
  }
  bool allowed_waterway(detail::FeatureClass value) const {
    using C = detail::FeatureClass;
    return (value == C::RIVER || value == C::CANAL) && meters_per_pixel_ <= 125;
  }
  Result layer(detail::Span message, Layer id) {
    layer_ = id;
    key_count_ = value_count_ = 0; class_key_ = brunnel_key_ = UINT32_MAX; extent_ = 4096;
    uint32_t version = 0;
    detail::Reader reader(message);
    uint32_t field; uint8_t wire; uint64_t integer; detail::Span bytes;
    while (!reader.empty()) {
      if (!reader.field(field,wire,integer,bytes)) return Result::INVALID;
      if (field == 3) {
        if (wire != 2 || key_count_ == UINT32_MAX) return Result::INVALID;
        if (detail::equals(bytes,"class")) {
          if (class_key_ != UINT32_MAX) return Result::INVALID;
          class_key_ = key_count_;
        } else if (detail::equals(bytes,"brunnel")) {
          if (brunnel_key_ != UINT32_MAX) return Result::INVALID;
          brunnel_key_ = key_count_;
        }
        ++key_count_;
      } else if (field == 4) {
        if (wire != 2) return Result::INVALID;
        if (value_count_ >= MAX_VALUES) return Result::BUDGET;
        if (!detail::class_value(bytes,values_[value_count_++])) return Result::INVALID;
      } else if (field == 5) {
        if (wire != 0 || !integer || integer > 65536) return Result::INVALID;
        extent_ = static_cast<uint32_t>(integer);
      } else if (field == 15) {
        if (wire != 0 || (integer != 1 && integer != 2)) return Result::INVALID;
        version = static_cast<uint32_t>(integer);
      }
    }
    if (!version) return Result::INVALID;
    detail::Reader features(message);
    while (!features.empty()) {
      if (!features.field(field,wire,integer,bytes)) return Result::INVALID;
      if (field != 2) continue;
      if (wire != 2) return Result::INVALID;
      if (++tile_features_ > MAX_TILE_FEATURES) return Result::BUDGET;
      const Result value = feature(bytes);
      if (value != Result::OK) return value;
    }
    return Result::OK;
  }
  Result feature(detail::Span message) {
    detail::Span geometry[8]{}; unsigned geometry_count = 0;
    uint32_t type = 0;
    detail::FeatureClass road = detail::FeatureClass::OTHER, brunnel = detail::FeatureClass::OTHER;
    bool need_value = false; uint64_t key = 0;
    detail::Reader reader(message);
    uint32_t field; uint8_t wire; uint64_t integer; detail::Span bytes;
    while (!reader.empty()) {
      if (!reader.field(field,wire,integer,bytes)) return Result::INVALID;
      if (field == 2) {
        if (wire != 2) return Result::INVALID;
        detail::Reader tags(bytes);
        while (!tags.empty()) {
          if (!tags.varint(integer)) return Result::INVALID;
          if (!need_value) { key = integer; if (key >= key_count_) return Result::INVALID; }
          else {
            if (integer >= value_count_) return Result::INVALID;
            if (key == class_key_) road = values_[integer];
            else if (key == brunnel_key_) brunnel = values_[integer];
          }
          need_value = !need_value;
        }
      } else if (field == 3) {
        if (wire != 0 || integer > 3) return Result::INVALID;
        type = static_cast<uint32_t>(integer);
      } else if (field == 4) {
        if (wire != 2) return Result::INVALID;
        if (geometry_count == 8) return Result::BUDGET;
        geometry[geometry_count++] = bytes;
      }
    }
    if (need_value) return Result::INVALID;
    if (layer_ == Layer::WATER) {
      if (type != 3 || brunnel == detail::FeatureClass::TUNNEL || !allowed_water(road)) return Result::OK;
      if (!geometry_count) return Result::INVALID;
      line_.style(Style::COAST);
      const Result rings = geometry_rings(geometry,geometry_count);
      commit_fill_();
      return rings;
    }
    if (layer_ == Layer::WATERWAY) {
      if (type != 2 || brunnel == detail::FeatureClass::TUNNEL || !allowed_waterway(road)) return Result::OK;
      if (!geometry_count) return Result::INVALID;
      line_.style(Style::RIVER);
      return geometry_lines(geometry,geometry_count);
    }
    if (type != 2 || !allowed(road)) return Result::OK;
    if (!geometry_count) return Result::INVALID;
    line_.style(road == detail::FeatureClass::MOTORWAY || road == detail::FeatureClass::TRUNK ? Style::MOTORWAY :
                road == detail::FeatureClass::PRIMARY || road == detail::FeatureClass::SECONDARY ? Style::MAJOR : Style::MINOR);
    return geometry_lines(geometry,geometry_count);
  }
  // Converts a tile coordinate to the projected map plane. Returns false for
  // points outside the Mercator world, which must break the current path.
  bool tile_point_(double tile_x, double tile_y, double &x, double &y, bool &valid) {
    const double side = double(uint32_t(1) << tile_.z);
    const double world_x = (tile_.x + tile_x/extent_) / side;
    const double world_y = (tile_.y + tile_y/extent_) / side;
    valid = world_y >= 0 && world_y <= 1;
    if (!valid) return true;
    const double longitude = std::remainder(world_x * 360.0 - 180.0,360.0);
    const double latitude = std::atan(std::sinh(PI*(1.0-2.0*world_y))) * 180.0 / PI;
    return projection_.project(latitude,longitude,x,y);
  }
  bool count_point_() {
    if (++tile_points_ > MAX_TILE_POINTS) return false;
    if (yield_fn_ && ++points_since_yield_ == 1024) { points_since_yield_ = 0; yield_fn_(); }
    return true;
  }
  Result break_chain_() {
    chain_open_ = false;
    return line_.finish() ? Result::OK : Result::BUDGET;
  }
  // One polygon edge. Tiles are clipped with a buffer, so a water polygon that
  // reaches the tile edge gets an artificial outline along the clip line. Only
  // the part inside the tile's own square is kept (the neighbour draws the
  // rest), and edges lying on one tile border are dropped as clip seams.
  Result ring_edge_(double x0, double y0, double x1, double y1) {
    const double e = extent_, dx = x1 - x0, dy = y1 - y0;
    double t0 = 0, t1 = 1;
    const double p[4] = {-dx, dx, -dy, dy}, q[4] = {x0, e - x0, y0, e - y0};
    for (unsigned i = 0; i < 4; ++i) {
      if (p[i] == 0) { if (q[i] < 0) return break_chain_(); continue; }
      const double r = q[i] / p[i];
      if (p[i] < 0) { if (r > t1) return break_chain_(); if (r > t0) t0 = r; }
      else { if (r < t0) return break_chain_(); if (r < t1) t1 = r; }
    }
    const double ax = x0 + t0 * dx, ay = y0 + t0 * dy, bx = x0 + t1 * dx, by = y0 + t1 * dy;
    auto on = [](double value, double edge) { return std::fabs(value - edge) < 1e-6; };
    if ((on(ax,0) && on(bx,0)) || (on(ax,e) && on(bx,e)) || (on(ay,0) && on(by,0)) || (on(ay,e) && on(by,e)))
      return break_chain_();
    if (chain_open_ && (!on(ax,chain_x_) || !on(ay,chain_y_))) {
      const Result broken = break_chain_();
      if (broken != Result::OK) return broken;
    }
    double x, y; bool valid;
    if (!chain_open_) {
      if (!tile_point_(ax,ay,x,y,valid)) return Result::INVALID;
      if (!valid) return break_chain_();
      if (!line_.point(x,y)) return Result::BUDGET;
      chain_open_ = true;
    }
    if (!tile_point_(bx,by,x,y,valid)) return Result::INVALID;
    if (!valid) return break_chain_();
    if (!line_.point(x,y)) return Result::BUDGET;
    chain_x_ = bx; chain_y_ = by;
    return t1 < 1 ? break_chain_() : Result::OK;
  }
  // Tile coordinate -> water mask cell coordinate.
  bool cell_point_(double tile_x, double tile_y, double &x, double &y, bool &valid) {
    if (!tile_point_(tile_x, tile_y, x, y, valid)) return false;
    x = x * MASK_PIXELS_PER_RANGE / NORMALIZED_RADIUS + MASK_RADIUS;
    y = y * MASK_PIXELS_PER_RANGE / NORMALIZED_RADIUS + MASK_RADIUS;
    return true;
  }
  void fill_edge_(double x0, double y0, double x1, double y1) {
    if (fill_scratch_ == nullptr || y0 == y1) return;
    const double low = std::min(y0, y1), high = std::max(y0, y1);
    // Rows whose centre (r + 0.5) lies in [low, high).
    const int first = std::max(0, static_cast<int>(std::ceil(low - 0.5)));
    const int last = std::min(MASK_SIZE - 1, static_cast<int>(std::ceil(high - 0.5)) - 1);
    for (int row = first; row <= last; ++row) {
      const double x = x0 + (row + 0.5 - y0) * (x1 - x0) / (y1 - y0);
      int column = static_cast<int>(std::ceil(x - 0.5));  // First cell whose centre is right of x.
      if (column >= MASK_SIZE) continue;
      if (column < 0) column = 0;
      fill_scratch_[static_cast<size_t>(row) * MASK_STRIDE + (column >> 3)] ^= static_cast<uint8_t>(1U << (column & 7));
    }
    if (first <= last) {
      fill_row_min_ = std::min(fill_row_min_, first);
      fill_row_max_ = std::max(fill_row_max_, last);
    }
  }
  void commit_fill_() {
    if (fill_scratch_ == nullptr) return;
    for (int row = fill_row_min_; row <= fill_row_max_; ++row) {
      uint8_t *scratch = fill_scratch_ + static_cast<size_t>(row) * MASK_STRIDE;
      uint8_t *mask = map_.water_mask + static_cast<size_t>(row) * MASK_STRIDE;
      uint8_t carry = 0;
      for (int i = 0; i < MASK_STRIDE; ++i) {
        uint8_t inside = scratch[i];
        inside ^= static_cast<uint8_t>(inside << 1);  // LSB-first prefix XOR within the byte.
        inside ^= static_cast<uint8_t>(inside << 2);
        inside ^= static_cast<uint8_t>(inside << 4);
        if (carry) inside = static_cast<uint8_t>(~inside);
        carry = inside >> 7;
        mask[i] |= inside;
        scratch[i] = 0;
      }
    }
    fill_row_min_ = MASK_SIZE;
    fill_row_max_ = -1;
  }
  Result geometry_rings(const detail::Span *parts, unsigned count) {
    unsigned part = 0; detail::Reader reader(parts[0]);
    auto read = [&](uint32_t &value) {
      while (reader.empty() && ++part < count) reader = detail::Reader(parts[part]);
      uint64_t integer = 0;
      if (part >= count || !reader.varint(integer) || integer > UINT32_MAX) return false;
      value = static_cast<uint32_t>(integer); return true;
    };
    auto available = [&]() {
      if (!reader.empty()) return true;
      for (unsigned i = part + 1; i < count; ++i) if (parts[i].size) return true;
      return false;
    };
    int64_t cursor_x = 0, cursor_y = 0, start_x = 0, start_y = 0, previous_x = 0, previous_y = 0;
    double start_cx = 0, start_cy = 0, previous_cx = 0, previous_cy = 0;
    bool active = false, start_valid = false, previous_valid = false;
    line_.reset(); chain_open_ = false;
    while (available()) {
      uint32_t command;
      if (!read(command)) return Result::INVALID;
      const uint32_t operation = command & 7, repeats = command >> 3;
      if (operation == 7) {
        if (repeats != 1 || !active) return Result::INVALID;
        if (previous_valid && start_valid) fill_edge_(previous_cx, previous_cy, start_cx, start_cy);
        const Result closed = ring_edge_(double(previous_x),double(previous_y),double(start_x),double(start_y));
        if (closed != Result::OK) return closed;
        const Result broken = break_chain_();
        if (broken != Result::OK) return broken;
        active = false;
        continue;
      }
      if (!repeats || (operation != 1 && operation != 2) || (operation == 1 && repeats != 1) ||
          (operation == 2 && !active)) return Result::INVALID;
      for (uint32_t i = 0; i < repeats; ++i) {
        uint32_t encoded_x, encoded_y;
        if (!read(encoded_x) || !read(encoded_y)) return Result::INVALID;
        cursor_x += static_cast<int64_t>(encoded_x >> 1) ^ -static_cast<int64_t>(encoded_x & 1);
        cursor_y += static_cast<int64_t>(encoded_y >> 1) ^ -static_cast<int64_t>(encoded_y & 1);
        if (cursor_x < INT32_MIN || cursor_x > INT32_MAX || cursor_y < INT32_MIN || cursor_y > INT32_MAX)
          return Result::INVALID;
        if (!count_point_()) return Result::BUDGET;
        double cx = 0, cy = 0; bool valid = false;
        if (fill_scratch_ != nullptr && !cell_point_(double(cursor_x), double(cursor_y), cx, cy, valid))
          return Result::INVALID;
        if (operation == 1) {
          const Result broken = break_chain_();
          if (broken != Result::OK) return broken;
          start_x = previous_x = cursor_x; start_y = previous_y = cursor_y;
          start_cx = previous_cx = cx; start_cy = previous_cy = cy;
          start_valid = previous_valid = valid;
          active = true;
          continue;
        }
        if (previous_valid && valid) fill_edge_(previous_cx, previous_cy, cx, cy);
        previous_cx = cx; previous_cy = cy; previous_valid = valid;
        const Result edge = ring_edge_(double(previous_x),double(previous_y),double(cursor_x),double(cursor_y));
        if (edge != Result::OK) return edge;
        previous_x = cursor_x; previous_y = cursor_y;
      }
    }
    return break_chain_();
  }
  Result geometry_lines(const detail::Span *parts, unsigned count) {
    unsigned part = 0; detail::Reader reader(parts[0]);
    auto read = [&](uint32_t &value) {
      while (reader.empty() && ++part < count) reader = detail::Reader(parts[part]);
      uint64_t integer = 0;
      if (part >= count || !reader.varint(integer) || integer > UINT32_MAX) return false;
      value = static_cast<uint32_t>(integer); return true;
    };
    auto available = [&]() {
      if (!reader.empty()) return true;
      for (unsigned i = part + 1; i < count; ++i) if (parts[i].size) return true;
      return false;
    };
    int64_t cursor_x = 0, cursor_y = 0;
    bool active = false, has_line = false;
    line_.reset();
    while (available()) {
      uint32_t command;
      if (!read(command)) return Result::INVALID;
      const uint32_t operation = command & 7, repeats = command >> 3;
      if (!repeats || (operation != 1 && operation != 2) || (operation == 1 && repeats != 1) ||
          (operation == 2 && !active)) return Result::INVALID;
      if (operation == 1) {
        if (active && !has_line) return Result::INVALID;
        if (!line_.finish()) return Result::BUDGET;
        active = true; has_line = false;
      } else has_line = true;
      for (uint32_t i = 0; i < repeats; ++i) {
        uint32_t encoded_x, encoded_y;
        if (!read(encoded_x) || !read(encoded_y)) return Result::INVALID;
        cursor_x += static_cast<int64_t>(encoded_x >> 1) ^ -static_cast<int64_t>(encoded_x & 1);
        cursor_y += static_cast<int64_t>(encoded_y >> 1) ^ -static_cast<int64_t>(encoded_y & 1);
        if (cursor_x < INT32_MIN || cursor_x > INT32_MAX || cursor_y < INT32_MIN || cursor_y > INT32_MAX)
          return Result::INVALID;
        if (!count_point_()) return Result::BUDGET;
        // Buffered MVT coordinates may cross tile edges. Longitude wraps at
        // the antimeridian; coordinates outside the source's Mercator world
        // break the path rather than manufacture a line along the pole edge.
        double x, y; bool valid;
        if (!tile_point_(double(cursor_x),double(cursor_y),x,y,valid)) return Result::INVALID;
        if (!valid) {
          if (!line_.finish()) return Result::BUDGET;
          continue;
        }
        if (!line_.point(x,y)) return Result::BUDGET;
      }
    }
    if (!active || !has_line) return Result::INVALID;
    return line_.finish() ? Result::OK : Result::BUDGET;
  }
};

}  // namespace AirDot::radar_map::tiles
