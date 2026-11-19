#pragma once
#include <cstdint>

namespace AirDot::weather_radar {
// LibreWXR scheme255 preserves its scalar index: dBZ=index/2-32.
inline bool raw_intensity(const uint8_t rgba[4],uint8_t &value) {
  if (!rgba[3]) {value=0;return true;}
  if (rgba[3]!=255 || rgba[0]!=rgba[1] || rgba[1]!=rgba[2]) return false;
  value=rgba[0];return true;
}
}  // namespace AirDot::weather_radar
