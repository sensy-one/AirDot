#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>

namespace AirDot::connectivity {

enum class ConnectivityStatus : uint8_t {
  DISABLED = 0,
  CONFIG_MISSING,
  CONFIG_INVALID,
  CONNECTING,
  CONNECTED,
  AUTH_FAILED,
  DNS_FAILED,
  TIMEOUT,
  RATE_LIMITED,
  SERVER_ERROR,
  OFFLINE,
  RETRY_WAIT,
};

enum class ConnectivityError : uint8_t {
  NONE = 0,
  CONFIG_MISSING,
  CONFIG_INVALID,
  AUTH_FAILED,
  DNS_FAILED,
  TIMEOUT,
  RATE_LIMITED,
  SERVER_ERROR,
  OFFLINE,
  INVALID_RESPONSE,
  RESOURCE_EXHAUSTED,
  UNSUPPORTED,
};

enum class Service : uint8_t {
  WIFI = 0,
  MQTT,
  HOME_ASSISTANT,
  WEATHER_LOCATION,
  WEATHER_GEOCODING,
  WEATHER,
  TIME_SYNC,
  COUNT,
};

struct ValidationResult {
  bool ok{false};
  ConnectivityStatus status{ConnectivityStatus::CONFIG_INVALID};
  ConnectivityError error{ConnectivityError::CONFIG_INVALID};
};

struct HealthCheckResult {
  ConnectivityStatus status{ConnectivityStatus::DISABLED};
  ConnectivityError error{ConnectivityError::NONE};
  uint32_t last_success_ms{0};
  uint32_t next_retry_ms{0};

  bool healthy() const { return this->status == ConnectivityStatus::CONNECTED && this->error == ConnectivityError::NONE; }
};

inline ConnectivityStatus status_for_error(ConnectivityError error) {
  switch (error) {
    case ConnectivityError::NONE:
      return ConnectivityStatus::CONNECTED;
    case ConnectivityError::CONFIG_MISSING:
      return ConnectivityStatus::CONFIG_MISSING;
    case ConnectivityError::CONFIG_INVALID:
      return ConnectivityStatus::CONFIG_INVALID;
    case ConnectivityError::AUTH_FAILED:
      return ConnectivityStatus::AUTH_FAILED;
    case ConnectivityError::DNS_FAILED:
      return ConnectivityStatus::DNS_FAILED;
    case ConnectivityError::TIMEOUT:
      return ConnectivityStatus::TIMEOUT;
    case ConnectivityError::RATE_LIMITED:
      return ConnectivityStatus::RATE_LIMITED;
    case ConnectivityError::SERVER_ERROR:
    case ConnectivityError::INVALID_RESPONSE:
    case ConnectivityError::RESOURCE_EXHAUSTED:
      return ConnectivityStatus::SERVER_ERROR;
    case ConnectivityError::OFFLINE:
      return ConnectivityStatus::OFFLINE;
    case ConnectivityError::UNSUPPORTED:
      return ConnectivityStatus::CONFIG_INVALID;
  }
  return ConnectivityStatus::CONFIG_INVALID;
}

inline std::string trim_copy(const std::string &value) {
  const auto begin = std::find_if(value.begin(), value.end(), [](unsigned char c) {
    return !std::isspace(c);
  });
  const auto end = std::find_if(value.rbegin(), value.rend(), [](unsigned char c) {
    return !std::isspace(c);
  }).base();
  if (begin >= end)
    return {};
  return std::string(begin, end);
}

inline std::string ascii_lower_copy(const std::string &value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (const char c : value)
    lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return lowered;
}

inline bool parse_port(const std::string &value, uint16_t &port) {
  const std::string trimmed = trim_copy(value);
  if (trimmed.empty())
    return false;

  uint32_t parsed = 0;
  for (const char c : trimmed) {
    if (c < '0' || c > '9')
      return false;
    parsed = parsed * 10U + static_cast<uint32_t>(c - '0');
    if (parsed > 65535U)
      return false;
  }

  if (parsed == 0)
    return false;

  port = static_cast<uint16_t>(parsed);
  return true;
}

inline bool is_valid_ipv4_address(const std::string &value) {
  size_t index = 0;
  for (uint8_t part = 0; part < 4; part++) {
    if (index >= value.size() || value[index] < '0' || value[index] > '9')
      return false;

    uint16_t octet = 0;
    uint8_t digits = 0;
    while (index < value.size() && value[index] >= '0' && value[index] <= '9') {
      octet = static_cast<uint16_t>(octet * 10U + static_cast<uint16_t>(value[index] - '0'));
      if (octet > 255 || ++digits > 3)
        return false;
      index++;
    }

    if (part < 3) {
      if (index >= value.size() || value[index] != '.')
        return false;
      index++;
    }
  }
  return index == value.size();
}

inline bool is_hex_char_(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

inline bool is_valid_ipv6_literal(const std::string &value) {
  if (value.empty() || value.size() > 45)
    return false;

  uint8_t colon_count = 0;
  bool saw_double_colon = false;
  size_t segment_length = 0;
  for (size_t index = 0; index < value.size(); index++) {
    const char c = value[index];
    if (c == ':') {
      colon_count++;
      if (index > 0 && value[index - 1] == ':') {
        if (saw_double_colon)
          return false;
        saw_double_colon = true;
      } else if (segment_length == 0 && index != 0) {
        return false;
      }
      segment_length = 0;
      continue;
    }
    if (!is_hex_char_(c))
      return false;
    if (++segment_length > 4)
      return false;
  }

  if (colon_count < 2)
    return false;
  if (segment_length == 0 && !saw_double_colon)
    return false;
  return true;
}

inline bool is_valid_hostname(const std::string &value) {
  if (value.empty() || value.size() > 253)
    return false;

  size_t label_start = 0;
  while (label_start < value.size()) {
    const size_t dot = value.find('.', label_start);
    const size_t label_end = dot == std::string::npos ? value.size() : dot;
    const size_t label_length = label_end - label_start;
    if (label_length == 0 || label_length > 63)
      return false;

    for (size_t index = label_start; index < label_end; index++) {
      const char c = value[index];
      const bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
      if (!alnum && c != '-')
        return false;
      if ((index == label_start || index + 1 == label_end) && c == '-')
        return false;
    }

    if (dot == std::string::npos)
      break;
    label_start = dot + 1;
  }
  return true;
}

inline bool is_valid_host_or_ip(const std::string &value) {
  const std::string trimmed = trim_copy(value);
  if (trimmed.empty())
    return false;
  if (is_valid_ipv4_address(trimmed) || is_valid_ipv6_literal(trimmed))
    return true;
  bool dotted_numeric = false;
  bool only_digits_and_dots = true;
  for (const char c : trimmed) {
    if (c == '.') {
      dotted_numeric = true;
      continue;
    }
    if (c < '0' || c > '9') {
      only_digits_and_dots = false;
      break;
    }
  }
  if (dotted_numeric && only_digits_and_dots)
    return false;
  return is_valid_hostname(trimmed);
}

struct HostPortInput {
  std::string host;
  uint16_t port{0};
  bool host_valid{false};
  bool port_valid{false};
};

inline HostPortInput normalize_host_port_input(const std::string &input, uint16_t fallback_port,
                                               uint16_t default_port, bool fallback_port_valid = true) {
  HostPortInput normalized{};
  normalized.port = fallback_port_valid ? (fallback_port == 0 ? default_port : fallback_port) : 0;
  normalized.port_valid = normalized.port != 0;

  std::string value = trim_copy(input);
  if (value.empty())
    return normalized;

  bool allow_embedded_port = true;
  const auto scheme_pos = value.find("://");
  if (scheme_pos != std::string::npos) {
    const std::string scheme = ascii_lower_copy(value.substr(0, scheme_pos));
    allow_embedded_port = scheme == "mqtt" || scheme == "mqtts" || scheme == "tcp" || scheme == "http" ||
                          scheme == "https";
    value = value.substr(scheme_pos + 3);
  }

  const auto at_pos = value.rfind('@');
  if (at_pos != std::string::npos)
    value = value.substr(at_pos + 1);

  const auto path_pos = value.find_first_of("/?#");
  if (path_pos != std::string::npos)
    value = value.substr(0, path_pos);

  value = trim_copy(value);
  if (value.empty())
    return normalized;

  std::string host = value;
  if (value.front() == '[') {
    const auto closing = value.find(']');
    if (closing != std::string::npos) {
      host = value.substr(1, closing - 1);
      if (allow_embedded_port && closing + 1 < value.size() && value[closing + 1] == ':') {
        uint16_t parsed_port = normalized.port;
        normalized.port_valid = parse_port(value.substr(closing + 2), parsed_port);
        if (normalized.port_valid)
          normalized.port = parsed_port;
      }
    }
  } else {
    const auto last_colon = value.rfind(':');
    if (last_colon != std::string::npos && value.find(':') == last_colon) {
      uint16_t parsed_port = normalized.port;
      const bool parsed = parse_port(value.substr(last_colon + 1), parsed_port);
      if (parsed) {
        host = value.substr(0, last_colon);
        if (allow_embedded_port)
          normalized.port = parsed_port;
      }
      normalized.port_valid = parsed;
    }
  }

  normalized.host = trim_copy(host);
  while (!normalized.host.empty() && normalized.host.back() == '.')
    normalized.host.pop_back();
  normalized.host_valid = is_valid_host_or_ip(normalized.host);
  return normalized;
}

inline ValidationResult ok_result() {
  return {true, ConnectivityStatus::CONNECTED, ConnectivityError::NONE};
}

inline ValidationResult invalid_result(ConnectivityError error) {
  return {false, status_for_error(error), error};
}

inline ValidationResult validate_wifi_config(const std::string &ssid, const std::string &password) {
  const std::string trimmed_ssid = trim_copy(ssid);
  if (trimmed_ssid.empty())
    return invalid_result(ConnectivityError::CONFIG_MISSING);
  if (trimmed_ssid.size() > 32)
    return invalid_result(ConnectivityError::CONFIG_INVALID);
  if (password.size() > 64)
    return invalid_result(ConnectivityError::CONFIG_INVALID);
  if (!password.empty() && password.size() < 8)
    return invalid_result(ConnectivityError::CONFIG_INVALID);
  return ok_result();
}

inline bool mqtt_topic_prefix_is_valid(const std::string &prefix) {
  if (prefix.empty() || prefix.size() > 64)
    return false;
  if (prefix.front() == '/' || prefix.back() == '/')
    return false;

  bool last_was_slash = false;
  for (const unsigned char c : prefix) {
    if (c == '#' || c == '+' || c == ' ' || c < 0x21 || c > 0x7E)
      return false;
    if (c == '/') {
      if (last_was_slash)
        return false;
      last_was_slash = true;
    } else {
      last_was_slash = false;
    }
  }
  return true;
}

inline ValidationResult validate_mqtt_config(bool enabled, const std::string &host, uint16_t port,
                                             const std::string &topic_prefix) {
  if (!enabled)
    return {true, ConnectivityStatus::DISABLED, ConnectivityError::NONE};
  if (trim_copy(host).empty())
    return invalid_result(ConnectivityError::CONFIG_MISSING);
  if (!is_valid_host_or_ip(host))
    return invalid_result(ConnectivityError::CONFIG_INVALID);
  if (port == 0)
    return invalid_result(ConnectivityError::CONFIG_INVALID);
  if (!mqtt_topic_prefix_is_valid(topic_prefix))
    return invalid_result(ConnectivityError::CONFIG_INVALID);
  return ok_result();
}

inline bool is_valid_http_url(const std::string &url) {
  const std::string trimmed = trim_copy(url);
  const std::string lowered = ascii_lower_copy(trimmed);
  size_t offset = std::string::npos;
  if (lowered.rfind("http://", 0) == 0)
    offset = 7;
  else if (lowered.rfind("https://", 0) == 0)
    offset = 8;
  if (offset == std::string::npos)
    return false;

  const auto end = trimmed.find_first_of("/?#", offset);
  std::string authority = end == std::string::npos ? trimmed.substr(offset) : trimmed.substr(offset, end - offset);
  const auto at_pos = authority.rfind('@');
  if (at_pos != std::string::npos)
    authority = authority.substr(at_pos + 1);

  const auto parsed = normalize_host_port_input(authority, 80, 80);
  return parsed.host_valid && parsed.port_valid;
}

inline ValidationResult validate_weather_location(bool custom_location, const std::string &city) {
  (void) custom_location;
  (void) city;
  return ok_result();
}

inline ValidationResult validate_ntp_server(const std::string &server) {
  if (trim_copy(server).empty())
    return invalid_result(ConnectivityError::CONFIG_MISSING);
  if (!is_valid_host_or_ip(server))
    return invalid_result(ConnectivityError::CONFIG_INVALID);
  return ok_result();
}

inline bool time_reached(uint32_t now, uint32_t target) {
  return target == 0 || static_cast<int32_t>(now - target) >= 0;
}

inline uint32_t mix32(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352dU;
  value ^= value >> 15;
  value *= 0x846ca68bU;
  value ^= value >> 16;
  return value;
}

struct RetryPolicy {
  uint32_t initial_delay_ms{5000};
  uint32_t max_delay_ms{300000};
  uint8_t jitter_percent{20};

  uint32_t next_delay_ms(uint32_t current_delay_ms, uint32_t entropy) const {
    const uint32_t base = current_delay_ms < this->initial_delay_ms ? this->initial_delay_ms : current_delay_ms;
    const uint32_t capped_base = std::min(base, this->max_delay_ms);
    const uint32_t jitter_max = (capped_base / 100U) * this->jitter_percent;
    const uint32_t jitter = jitter_max == 0 ? 0 : mix32(entropy) % (jitter_max + 1U);
    return std::min(this->max_delay_ms, capped_base + jitter);
  }

  uint32_t next_base_delay_ms(uint32_t current_delay_ms) const {
    const uint32_t base = current_delay_ms < this->initial_delay_ms ? this->initial_delay_ms : current_delay_ms;
    return base >= this->max_delay_ms / 2U ? this->max_delay_ms : base * 2U;
  }
};

struct RetryState {
  uint32_t current_delay_ms{0};
  uint32_t next_retry_ms{0};

  void reset() {
    this->current_delay_ms = 0;
    this->next_retry_ms = 0;
  }

  bool due(uint32_t now) const { return time_reached(now, this->next_retry_ms); }

  uint32_t record_failure(uint32_t now, const RetryPolicy &policy, uint32_t entropy) {
    const uint32_t delay = policy.next_delay_ms(this->current_delay_ms, entropy);
    this->next_retry_ms = now + delay;
    this->current_delay_ms = policy.next_base_delay_ms(this->current_delay_ms);
    return delay;
  }
};

struct ServiceHealthState {
  std::atomic<uint8_t> status{static_cast<uint8_t>(ConnectivityStatus::DISABLED)};
  std::atomic<uint8_t> error{static_cast<uint8_t>(ConnectivityError::NONE)};
  std::atomic<uint32_t> last_success_ms{0};
  std::atomic<uint32_t> next_retry_ms{0};
};

inline ServiceHealthState &service_health_state(Service service) {
  static ServiceHealthState states[static_cast<size_t>(Service::COUNT)]{};
  return states[static_cast<size_t>(service)];
}

inline void set_service_status(Service service, ConnectivityStatus status, ConnectivityError error,
                               uint32_t now_ms = 0, uint32_t next_retry_ms = 0) {
  auto &state = service_health_state(service);
  state.status.store(static_cast<uint8_t>(status), std::memory_order_release);
  state.error.store(static_cast<uint8_t>(error), std::memory_order_release);
  state.next_retry_ms.store(next_retry_ms, std::memory_order_release);
  if (status == ConnectivityStatus::CONNECTED)
    state.last_success_ms.store(now_ms, std::memory_order_release);
}

inline HealthCheckResult get_service_status(Service service) {
  const auto &state = service_health_state(service);
  return {
      static_cast<ConnectivityStatus>(state.status.load(std::memory_order_acquire)),
      static_cast<ConnectivityError>(state.error.load(std::memory_order_acquire)),
      state.last_success_ms.load(std::memory_order_acquire),
      state.next_retry_ms.load(std::memory_order_acquire),
  };
}

inline bool service_healthy(Service service) { return get_service_status(service).healthy(); }

}  // namespace AirDot::connectivity
