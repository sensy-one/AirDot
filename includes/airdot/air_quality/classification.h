#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace AirDot {
enum class AirQualityProfile : uint8_t {
  GLOBAL_WHO_EEA_STRICT = 0,
  EUROPE_EEA,
  NORTH_AMERICA_US_EPA_2024,
  UK_DAQI_5,
  INDIA_NAQI_5,
  CHINA_CN_AQI_5,
  AUSTRALIA_NSW_1H,
};

enum class AirQualityMetric : uint8_t {
  PM1,
  PM25,
  PM4,
  PM10,
  VOC,
  NOX,
  CO2,
};

struct AirQualityStatusThresholds {
  float balanced;
  float moderate;
  float poor;
  float unhealthy;
  bool balanced_inclusive{true};
  bool moderate_inclusive{true};
  bool poor_inclusive{true};
  bool unhealthy_inclusive{true};
};

inline AirQualityProfile normalize_air_quality_profile(uint8_t value) {
  switch (static_cast<AirQualityProfile>(value)) {
    case AirQualityProfile::EUROPE_EEA:
      return AirQualityProfile::EUROPE_EEA;
    case AirQualityProfile::NORTH_AMERICA_US_EPA_2024:
      return AirQualityProfile::NORTH_AMERICA_US_EPA_2024;
    case AirQualityProfile::UK_DAQI_5:
      return AirQualityProfile::UK_DAQI_5;
    case AirQualityProfile::INDIA_NAQI_5:
      return AirQualityProfile::INDIA_NAQI_5;
    case AirQualityProfile::CHINA_CN_AQI_5:
      return AirQualityProfile::CHINA_CN_AQI_5;
    case AirQualityProfile::AUSTRALIA_NSW_1H:
      return AirQualityProfile::AUSTRALIA_NSW_1H;
    case AirQualityProfile::GLOBAL_WHO_EEA_STRICT:
    default:
      return AirQualityProfile::GLOBAL_WHO_EEA_STRICT;
  }
}

inline const char *air_quality_profile_value(AirQualityProfile profile) {
  switch (profile) {
    case AirQualityProfile::EUROPE_EEA:
      return "EUROPE_EEA";
    case AirQualityProfile::NORTH_AMERICA_US_EPA_2024:
      return "NORTH_AMERICA_US_EPA_2024";
    case AirQualityProfile::UK_DAQI_5:
      return "UK_DAQI_5";
    case AirQualityProfile::INDIA_NAQI_5:
      return "INDIA_NAQI_5";
    case AirQualityProfile::CHINA_CN_AQI_5:
      return "CHINA_CN_AQI_5";
    case AirQualityProfile::AUSTRALIA_NSW_1H:
      return "AUSTRALIA_NSW_1H";
    case AirQualityProfile::GLOBAL_WHO_EEA_STRICT:
    default:
      return "GLOBAL_WHO_EEA_STRICT";
  }
}

inline const char *air_quality_profile_label(AirQualityProfile profile) {
  switch (profile) {
    case AirQualityProfile::EUROPE_EEA:
      return "Europe EEA";
    case AirQualityProfile::NORTH_AMERICA_US_EPA_2024:
      return "North America US EPA 2024";
    case AirQualityProfile::UK_DAQI_5:
      return "UK DAQI 5";
    case AirQualityProfile::INDIA_NAQI_5:
      return "India NAQI 5";
    case AirQualityProfile::CHINA_CN_AQI_5:
      return "China CN AQI 5";
    case AirQualityProfile::AUSTRALIA_NSW_1H:
      return "Australia NSW 1H";
    case AirQualityProfile::GLOBAL_WHO_EEA_STRICT:
    default:
      return "Global WHO/EEA Strict";
  }
}

inline AirQualityProfile air_quality_profile_from_value(const std::string &value) {
  for (uint8_t index = 0; index <= static_cast<uint8_t>(AirQualityProfile::AUSTRALIA_NSW_1H); index++) {
    const auto profile = normalize_air_quality_profile(index);
    if (value == air_quality_profile_value(profile))
      return profile;
  }
  return AirQualityProfile::GLOBAL_WHO_EEA_STRICT;
}

inline bool threshold_matches_(float value, float threshold, bool inclusive) {
  return inclusive ? value <= threshold : value < threshold;
}

inline int threshold_level(float value, const AirQualityStatusThresholds &thresholds) {
  if (!std::isfinite(value))
    return -1;
  if (threshold_matches_(value, thresholds.balanced, thresholds.balanced_inclusive))
    return 0;
  if (threshold_matches_(value, thresholds.moderate, thresholds.moderate_inclusive))
    return 1;
  if (threshold_matches_(value, thresholds.poor, thresholds.poor_inclusive))
    return 2;
  if (threshold_matches_(value, thresholds.unhealthy, thresholds.unhealthy_inclusive))
    return 3;
  return 4;
}

inline float normalize_nox_index(float value) {
  if (!std::isfinite(value))
    return NAN;

  return std::clamp(std::round(value), 1.0f, 500.0f);
}

inline bool air_quality_metric_value_valid(AirQualityMetric metric, float value) {
  if (!std::isfinite(value))
    return false;

  switch (metric) {
    case AirQualityMetric::PM1:
    case AirQualityMetric::PM25:
    case AirQualityMetric::PM4:
    case AirQualityMetric::PM10:
      return value >= 0.0f && value <= 5000.0f;
    case AirQualityMetric::VOC:
    case AirQualityMetric::NOX:
      return value >= 1.0f && value <= 500.0f;
    case AirQualityMetric::CO2:
      return value >= 250.0f && value <= 10000.0f;
  }
  return false;
}

inline float sanitized_air_quality_metric_value(AirQualityMetric metric, float value) {
  return air_quality_metric_value_valid(metric, value) ? value : NAN;
}

inline AirQualityStatusThresholds pm25_status_thresholds(AirQualityProfile profile) {
  switch (profile) {
    case AirQualityProfile::NORTH_AMERICA_US_EPA_2024:
      return {9.0f, 35.4f, 55.4f, 125.4f};
    case AirQualityProfile::UK_DAQI_5:
      return {11.0f, 35.0f, 53.0f, 70.0f};
    case AirQualityProfile::INDIA_NAQI_5:
      return {30.0f, 60.0f, 90.0f, 120.0f};
    case AirQualityProfile::CHINA_CN_AQI_5:
      return {35.0f, 75.0f, 115.0f, 150.0f};
    case AirQualityProfile::AUSTRALIA_NSW_1H:
      return {25.0f, 50.0f, 100.0f, 300.0f, false, true, true, false};
    case AirQualityProfile::EUROPE_EEA:
    case AirQualityProfile::GLOBAL_WHO_EEA_STRICT:
    default:
      return {5.0f, 15.0f, 50.0f, 90.0f};
  }
}

inline AirQualityStatusThresholds pm10_status_thresholds(AirQualityProfile profile) {
  switch (profile) {
    case AirQualityProfile::NORTH_AMERICA_US_EPA_2024:
      return {54.0f, 154.0f, 254.0f, 354.0f};
    case AirQualityProfile::UK_DAQI_5:
      return {16.0f, 50.0f, 75.0f, 100.0f};
    case AirQualityProfile::INDIA_NAQI_5:
      return {50.0f, 100.0f, 250.0f, 350.0f};
    case AirQualityProfile::CHINA_CN_AQI_5:
      return {50.0f, 150.0f, 250.0f, 350.0f};
    case AirQualityProfile::AUSTRALIA_NSW_1H:
      return {50.0f, 100.0f, 200.0f, 600.0f, false, true, true, false};
    case AirQualityProfile::EUROPE_EEA:
    case AirQualityProfile::GLOBAL_WHO_EEA_STRICT:
    default:
      return {15.0f, 45.0f, 120.0f, 195.0f};
  }
}

inline AirQualityStatusThresholds indoor_status_thresholds(AirQualityMetric metric) {
  switch (metric) {
    case AirQualityMetric::VOC:
      return {199.0f, 249.0f, 349.0f, 399.0f};
    case AirQualityMetric::NOX:
      return {49.0f, 99.0f, 299.0f, 349.0f};
    case AirQualityMetric::CO2:
      return {599.0f, 999.0f, 1499.0f, 2499.0f};
    default:
      return {0.0f, 0.0f, 0.0f, 0.0f};
  }
}

inline AirQualityStatusThresholds metric_status_thresholds(AirQualityProfile profile, AirQualityMetric metric) {
  switch (metric) {
    case AirQualityMetric::PM1:
    case AirQualityMetric::PM25:
    case AirQualityMetric::PM4:
      return pm25_status_thresholds(profile);
    case AirQualityMetric::PM10:
      return pm10_status_thresholds(profile);
    case AirQualityMetric::VOC:
    case AirQualityMetric::NOX:
    case AirQualityMetric::CO2:
      return indoor_status_thresholds(metric);
  }
  return {0.0f, 0.0f, 0.0f};
}

inline int air_quality_level(AirQualityProfile profile, float pm25, float pm10, float voc, float nox, float co2) {
  int level = -1;
  level = std::max(level, threshold_level(pm25, pm25_status_thresholds(profile)));
  level = std::max(level, threshold_level(pm10, pm10_status_thresholds(profile)));
  level = std::max(level, threshold_level(voc, indoor_status_thresholds(AirQualityMetric::VOC)));
  level = std::max(level, threshold_level(nox, indoor_status_thresholds(AirQualityMetric::NOX)));
  level = std::max(level, threshold_level(co2, indoor_status_thresholds(AirQualityMetric::CO2)));
  return level;
}

inline int sanitized_air_quality_level(AirQualityProfile profile, float pm25, float pm10, float voc, float nox,
                                       float co2) {
  return air_quality_level(
      profile,
      sanitized_air_quality_metric_value(AirQualityMetric::PM25, pm25),
      sanitized_air_quality_metric_value(AirQualityMetric::PM10, pm10),
      sanitized_air_quality_metric_value(AirQualityMetric::VOC, voc),
      sanitized_air_quality_metric_value(AirQualityMetric::NOX, nox),
      sanitized_air_quality_metric_value(AirQualityMetric::CO2, co2));
}
}  // namespace AirDot
