#pragma once

#include <cmath>
#include <cstdint>

namespace AirDot::radar_map {

// North-up azimuthal equidistant projection, matching the aircraft layer's
// great-circle distance and initial bearing. Cache the origin once per map;
// bearing unit vectors avoid atan2 followed immediately by sin/cos per vertex.
class Projection {
 public:
  Projection(double latitude, double longitude, uint8_t range)
      : longitude_(longitude), valid_(valid_coordinates_(latitude, longitude) && range != 0) {
    if (!valid_) return;
    const double radians = latitude * PI_ / 180.0;
    sin_latitude_ = std::sin(radians); cos_latitude_ = std::cos(radians);
    units_per_radian_ = 6371.0 * 10000.0 / range;
  }

  bool project(double latitude, double longitude, double &x, double &y) const {
    if (!valid_ || !valid_coordinates_(latitude, longitude)) return false;
    const double radians = latitude * PI_ / 180.0;
    const double delta = std::remainder((longitude - longitude_) * PI_ / 180.0, 2.0 * PI_);
    const double sin_latitude = std::sin(radians), cos_latitude = std::cos(radians);
    const double sin_delta = std::sin(delta), cos_delta = std::cos(delta);
    const double east = sin_delta * cos_latitude;
    const double north = cos_latitude_ * sin_latitude - sin_latitude_ * cos_latitude * cos_delta;
    const double dot = sin_latitude_ * sin_latitude + cos_latitude_ * cos_latitude * cos_delta;
    const double direction_length = std::hypot(east, north);
    if (direction_length <= 2e-15) {
      // The origin (including longitude aliases at either pole) has no bearing
      // but is exactly at the screen center. The antipode has no unique bearing
      // at all and is outside every supported map's coverage; break the line.
      if (dot < 0) return false;
      x = 0; y = 0; return true;
    }
    const double scale = units_per_radian_ * std::atan2(direction_length, dot) / direction_length;
    x = east * scale; y = -north * scale;
    return std::isfinite(x) && std::isfinite(y);
  }

 private:
  static constexpr double PI_ = 3.14159265358979323846;
  double longitude_{0}, sin_latitude_{0}, cos_latitude_{1}, units_per_radian_{0};
  bool valid_{false};
  static bool valid_coordinates_(double latitude, double longitude) {
    return std::isfinite(latitude) && std::isfinite(longitude) && latitude >= -90 && latitude <= 90 &&
           longitude >= -180 && longitude <= 180;
  }
};

}  // namespace AirDot::radar_map
