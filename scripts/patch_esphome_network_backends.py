from __future__ import annotations

import os
from pathlib import Path
import tempfile

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
header = project_dir / "src/esphome/components/mqtt/mqtt_backend_esp32.h"
source = project_dir / "src/esphome/components/mqtt/mqtt_backend_esp32.cpp"
mqtt_client = project_dir / "src/esphome/components/mqtt/mqtt_client.cpp"
mqtt_client_header = project_dir / "src/esphome/components/mqtt/mqtt_client.h"
api_server = project_dir / "src/esphome/components/api/api_server.cpp"
wifi_component = project_dir / "src/esphome/components/wifi/wifi_component.cpp"
wifi_component_header = project_dir / "src/esphome/components/wifi/wifi_component.h"
web_server_idf = project_dir / "src/esphome/components/web_server_idf/web_server_idf.cpp"

_pending_texts: dict[Path, str] = {}


def read_patch_text(path):
    path = Path(path)
    if path in _pending_texts:
        return _pending_texts[path]
    if not path.is_file():
        raise RuntimeError(f"Expected ESPHome network backend file not found: {path}")
    return path.read_text(encoding="utf-8")


def stage_patch_text(path, text):
    _pending_texts[Path(path)] = text


def apply_pending_changes():
    for path, text in sorted(_pending_texts.items(), key=lambda item: str(item[0])):
        current = path.read_text(encoding="utf-8")
        if current == text:
            continue

        fd, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as temporary:
                temporary.write(text)
            os.replace(temporary_name, path)
        except Exception:
            try:
                os.unlink(temporary_name)
            except FileNotFoundError:
                pass
            raise


def replace_once(path, old, new):
    text = read_patch_text(path)
    if new in text:
        return
    candidates = (old,) if isinstance(old, str) else old
    for candidate in candidates:
        if candidate in text:
            stage_patch_text(path, text.replace(candidate, new, 1))
            return
    raise RuntimeError(f"Expected ESPHome network backend snippet not found in {path}")


def replace_range(path, start, end, new):
    text = read_patch_text(path)
    if new in text:
        return
    start_index = text.find(start)
    if start_index == -1:
        raise RuntimeError(f"Expected ESPHome network backend start snippet not found in {path}")
    end_index = text.find(end, start_index)
    if end_index == -1:
        raise RuntimeError(f"Expected ESPHome network backend end snippet not found in {path}")
    end_index += len(end)
    stage_patch_text(path, text[:start_index] + new + text[end_index:])


replace_once(
    header,
    (
        """  void set_keep_alive(uint16_t keep_alive) final { this->keep_alive_ = keep_alive; }
  void set_client_id(const char *client_id) final { this->client_id_ = client_id; }
  void set_clean_session(bool clean_session) final { this->clean_session_ = clean_session; }

  void set_credentials(const char *username, const char *password) final {
    if (username)
      this->username_ = username;
    if (password)
      this->password_ = password;
  }
  void set_will(const char *topic, uint8_t qos, bool retain, const char *payload) final {
    if (topic)
      this->lwt_topic_ = topic;
    this->lwt_qos_ = qos;
    if (payload)
      this->lwt_message_ = payload;
    this->lwt_retain_ = retain;
  }
  void set_server(network::IPAddress ip, uint16_t port) final {
    char ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
    this->host_ = ip.str_to(ip_buf);
    this->port_ = port;
  }
  void set_server(const char *host, uint16_t port) final {
    this->host_ = host;
    this->port_ = port;
  }
""",
        """  void set_keep_alive(uint16_t keep_alive) final { this->keep_alive_ = keep_alive; }
  void set_client_id(const char *client_id) final { this->client_id_ = client_id; }
  void set_clean_session(bool clean_session) final { this->clean_session_ = clean_session; }

  void set_credentials(const char *username, const char *password) final {
    this->username_ = username != nullptr ? username : "";
    this->password_ = password != nullptr ? password : "";
  }
  void set_will(const char *topic, uint8_t qos, bool retain, const char *payload) final {
    if (topic)
      this->lwt_topic_ = topic;
    this->lwt_qos_ = qos;
    if (payload)
      this->lwt_message_ = payload;
    this->lwt_retain_ = retain;
  }
  void set_server(network::IPAddress ip, uint16_t port) final {
    char ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
    this->host_ = ip.str_to(ip_buf);
    this->port_ = port;
  }
  void set_server(const char *host, uint16_t port) final {
    this->host_ = host;
    this->port_ = port;
  }
""",
    ),
    """  void set_keep_alive(uint16_t keep_alive) final {
    if (this->keep_alive_ != keep_alive) {
      this->keep_alive_ = keep_alive;
      this->config_dirty_ = true;
    }
  }
  void set_client_id(const char *client_id) final {
    std::string next_client_id = client_id != nullptr ? client_id : "";
    if (this->client_id_ != next_client_id) {
      this->client_id_ = next_client_id;
      this->config_dirty_ = true;
    }
  }
  void set_clean_session(bool clean_session) final {
    if (this->clean_session_ != clean_session) {
      this->clean_session_ = clean_session;
      this->config_dirty_ = true;
    }
  }

  void set_credentials(const char *username, const char *password) final {
    std::string next_username = username != nullptr ? username : "";
    std::string next_password = password != nullptr ? password : "";
    if (this->username_ != next_username || this->password_ != next_password) {
      this->username_ = next_username;
      this->password_ = next_password;
      this->config_dirty_ = true;
    }
  }
  void set_will(const char *topic, uint8_t qos, bool retain, const char *payload) final {
    std::string next_topic = topic != nullptr ? topic : "";
    std::string next_payload = payload != nullptr ? payload : "";
    if (this->lwt_topic_ != next_topic || this->lwt_qos_ != qos || this->lwt_retain_ != retain ||
        this->lwt_message_ != next_payload) {
      this->lwt_topic_ = next_topic;
      this->lwt_qos_ = qos;
      this->lwt_retain_ = retain;
      this->lwt_message_ = next_payload;
      this->config_dirty_ = true;
    }
  }
  void set_server(network::IPAddress ip, uint16_t port) final {
    char ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
    this->set_server(ip.str_to(ip_buf), port);
  }
  void set_server(const char *host, uint16_t port) final {
    std::string next_host = host != nullptr ? host : "";
    if (this->host_ != next_host || this->port_ != port) {
      this->host_ = next_host;
      this->port_ = port;
      this->config_dirty_ = true;
    }
  }
""",
)

replace_once(
    header,
    (
        """  void connect() final {
    if (!is_initalized_) {
      if (initialize_()) {
        esp_mqtt_client_start(handler_.get());
      }
    }
  }
  void disconnect() final {
    if (is_initalized_)
      esp_mqtt_client_disconnect(handler_.get());
  }
""",
        """  void connect() final {
    if (!is_initalized_) {
      if (!initialize_())
        return;
    } else {
      if (!configure_() || esp_mqtt_set_config(handler_.get(), &mqtt_cfg_) != ESP_OK)
        return;
    }

    if (is_initalized_ && !is_started_) {
      if (esp_mqtt_client_start(handler_.get()) == ESP_OK)
        is_started_ = true;
      return;
    }

    if (!is_connected_)
      esp_mqtt_client_reconnect(handler_.get());
  }
  void disconnect() final {
    if (is_initalized_ && is_started_) {
      esp_mqtt_client_disconnect(handler_.get());
      is_connected_ = false;
    }
  }
""",
        """  void connect() final {
    if (!is_initalized_) {
      if (!initialize_())
        return;
      this->config_dirty_ = false;
    } else if (this->config_dirty_) {
      if (!configure_() || esp_mqtt_set_config(handler_.get(), &mqtt_cfg_) != ESP_OK)
        return;
      this->config_dirty_ = false;
    }

    if (is_initalized_ && !is_started_) {
      if (esp_mqtt_client_start(handler_.get()) == ESP_OK)
        is_started_ = true;
      return;
    }

    if (!is_connected_)
      esp_mqtt_client_reconnect(handler_.get());
  }
  void disconnect() final {
    if (is_initalized_ && is_started_) {
      esp_mqtt_client_disconnect(handler_.get());
      is_connected_ = false;
    }
  }
""",
    ),
    """  void connect() final {
    if (!is_initalized_) {
      if (!initialize_())
        return;
      this->config_dirty_ = false;
    } else if (this->config_dirty_) {
      if (!configure_() || esp_mqtt_set_config(handler_.get(), &mqtt_cfg_) != ESP_OK)
        return;
      this->config_dirty_ = false;
    }

    if (is_initalized_ && !is_started_) {
      if (esp_mqtt_client_start(handler_.get()) == ESP_OK)
        is_started_ = true;
      return;
    }

    if (!is_connected_)
      esp_mqtt_client_reconnect(handler_.get());
  }
  void disconnect() final {
    if (is_initalized_ && is_started_ && is_connected_)
      esp_mqtt_client_disconnect(handler_.get());
    is_connected_ = false;
  }
""",
)

replace_once(
    mqtt_client,
    (
        """  // Force disconnect first
  this->mqtt_backend_.disconnect();
""",
        """  // Force disconnect first
  if (this->mqtt_backend_.connected())
    this->mqtt_backend_.disconnect();
""",
    ),
    """  // Disconnect only when the backend reports an active session. On ESP-IDF,
  // disconnecting while a socket connect is in progress can block the main loop.
  if (this->mqtt_backend_.connected())
    this->mqtt_backend_.disconnect();
""",
)

replace_once(
    mqtt_client,
    """  this->mqtt_backend_.set_on_disconnect([this](MQTTClientDisconnectReason reason) {
    if (this->state_ == MQTT_CLIENT_DISABLED)
      return;
    this->state_ = MQTT_CLIENT_DISCONNECTED;
    this->disconnect_reason_ = reason;
  });
""",
    """  this->mqtt_backend_.set_on_disconnect([this](MQTTClientDisconnectReason reason) {
    if (this->state_ == MQTT_CLIENT_DISABLED)
      return;
    this->note_connect_failure_(reason);
  });
""",
)

replace_once(
    mqtt_client,
    (
        """    this->state_ = MQTT_CLIENT_DISCONNECTED;
    this->disconnect_reason_ = MQTTClientDisconnectReason::DNS_RESOLVE_ERROR;
    this->on_disconnect_.call(MQTTClientDisconnectReason::DNS_RESOLVE_ERROR);
""",
        """    this->state_ = MQTT_CLIENT_DISCONNECTED;
    this->note_connect_failure_(MQTTClientDisconnectReason::DNS_RESOLVE_ERROR);
    this->on_disconnect_.call(MQTTClientDisconnectReason::DNS_RESOLVE_ERROR);
""",
        """    this->disconnect_reason_ = MQTTClientDisconnectReason::DNS_RESOLVE_ERROR;
    this->on_disconnect_.call(MQTTClientDisconnectReason::DNS_RESOLVE_ERROR);
""",
        """    this->state_ = MQTT_CLIENT_DISCONNECTED;
    this->note_connect_failure_(MQTTClientDisconnectReason::DNS_RESOLVE_ERROR);
    this->on_disconnect_.call(MQTTClientDisconnectReason::DNS_RESOLVE_ERROR);
""",
    ),
    """    this->note_connect_failure_(MQTTClientDisconnectReason::DNS_RESOLVE_ERROR);
    this->on_disconnect_.call(MQTTClientDisconnectReason::DNS_RESOLVE_ERROR);
""",
)

replace_once(
    mqtt_client,
    (
        """void MQTTClientComponent::reset_reconnect_backoff_() {
  this->reconnect_delay_ = MQTT_RECONNECT_INITIAL_DELAY_MS;
  this->next_connect_attempt_ = millis();
}

bool MQTTClientComponent::mqtt_retry_due_(uint32_t now) const {
  return this->next_connect_attempt_ == 0 || static_cast<int32_t>(now - this->next_connect_attempt_) >= 0;
}

void MQTTClientComponent::note_connect_failure_(MQTTClientDisconnectReason reason) {
  const uint32_t now = millis();
  const uint32_t delay = this->reconnect_delay_ < MQTT_RECONNECT_INITIAL_DELAY_MS
                             ? MQTT_RECONNECT_INITIAL_DELAY_MS
                             : this->reconnect_delay_;

  this->state_ = MQTT_CLIENT_DISCONNECTED;
  this->disconnect_reason_ = reason;
  this->next_connect_attempt_ = now + delay;
  this->reconnect_delay_ =
      delay >= MQTT_RECONNECT_MAX_DELAY_MS / 2 ? MQTT_RECONNECT_MAX_DELAY_MS : delay * 2;
  this->status_set_warning();
}

void MQTTClientComponent::start_connect_() {
""",
        """void MQTTClientComponent::start_connect_() {
""",
    ),
    """void MQTTClientComponent::reset_reconnect_backoff_() {
  this->reconnect_delay_ = MQTT_RECONNECT_INITIAL_DELAY_MS;
  this->next_connect_attempt_ = millis();
}

bool MQTTClientComponent::mqtt_retry_due_(uint32_t now) const {
  return this->next_connect_attempt_ == 0 || static_cast<int32_t>(now - this->next_connect_attempt_) >= 0;
}

uint32_t MQTTClientComponent::mqtt_reconnect_delay_with_jitter_(uint32_t base_delay, uint32_t seed) const {
  seed ^= seed >> 16;
  seed *= 0x7feb352dU;
  seed ^= seed >> 15;
  seed *= 0x846ca68bU;
  seed ^= seed >> 16;

  const uint32_t jitter_max = base_delay / 5U;
  const uint32_t jitter = jitter_max == 0 ? 0 : seed % (jitter_max + 1U);
  return base_delay > MQTT_RECONNECT_MAX_DELAY_MS - jitter ? MQTT_RECONNECT_MAX_DELAY_MS : base_delay + jitter;
}

void MQTTClientComponent::note_connect_failure_(MQTTClientDisconnectReason reason) {
  const uint32_t now = millis();
  const uint32_t delay = this->reconnect_delay_ < MQTT_RECONNECT_INITIAL_DELAY_MS
                             ? MQTT_RECONNECT_INITIAL_DELAY_MS
                             : this->reconnect_delay_;

  this->state_ = MQTT_CLIENT_DISCONNECTED;
  this->disconnect_reason_ = reason;
  const uint32_t retry_delay = this->mqtt_reconnect_delay_with_jitter_(delay, now ^ static_cast<uint32_t>(reason));
  this->next_connect_attempt_ = now + retry_delay;
  this->reconnect_delay_ =
      delay >= MQTT_RECONNECT_MAX_DELAY_MS / 2 ? MQTT_RECONNECT_MAX_DELAY_MS : delay * 2;
  this->status_set_warning();
}

void MQTTClientComponent::start_connect_() {
""",
)

replace_once(
    mqtt_client,
    """    if (millis() - this->connect_begin_ > 60000) {
      this->state_ = MQTT_CLIENT_DISCONNECTED;
      this->start_dnslookup_();
    }
""",
    """    if (millis() - this->connect_begin_ > 60000)
      this->note_connect_failure_(MQTTClientDisconnectReason::TCP_DISCONNECTED);
""",
)

replace_once(
    mqtt_client,
    """  this->state_ = MQTT_CLIENT_CONNECTED;
  this->sent_birth_message_ = false;
  this->status_clear_warning();
""",
    """  this->state_ = MQTT_CLIENT_CONNECTED;
  this->sent_birth_message_ = false;
  this->reset_reconnect_backoff_();
  this->status_clear_warning();
""",
)

replace_range(
    mqtt_client,
    """  if (this->disconnect_reason_.has_value()) {""",
    """  const uint32_t now = App.get_loop_component_start_time();
""",
    """  const uint32_t now = App.get_loop_component_start_time();

  if (this->disconnect_reason_.has_value())
    this->disconnect_reason_.reset();
""",
)

replace_once(
    mqtt_client,
    """    case MQTT_CLIENT_DISCONNECTED:
      if (now - this->connect_begin_ > 5000) {
        this->start_dnslookup_();
      }
      break;
""",
    """    case MQTT_CLIENT_DISCONNECTED:
      if (network::is_connected() && this->mqtt_retry_due_(now))
        this->start_dnslookup_();
      break;
""",
)

replace_once(
    mqtt_client,
    """  this->state_ = MQTT_CLIENT_DISCONNECTED;
  this->last_connected_ = millis();
  this->start_dnslookup_();
""",
    """  this->state_ = MQTT_CLIENT_DISCONNECTED;
  this->last_connected_ = millis();
  this->reset_reconnect_backoff_();
  this->start_dnslookup_();
""",
)

replace_once(
    api_server,
    (
        """void APIServer::setup() {
  ControllerRegistry::register_controller(this);
""",
        """void APIServer::setup() {
  this->shutting_down_ = false;
  this->clients_.clear();

  ControllerRegistry::register_controller(this);
""",
    ),
    """void APIServer::setup() {
  this->shutting_down_ = false;
  this->clients_.clear();

  ControllerRegistry::register_controller(this);
""",
)

replace_once(
    mqtt_client_header,
    (
        """  void check_dnslookup_();
  void note_connect_failure_(MQTTClientDisconnectReason reason);
  void reset_reconnect_backoff_();
  bool mqtt_retry_due_(uint32_t now) const;
""",
        """  void check_dnslookup_();
""",
    ),
    """  void check_dnslookup_();
  void note_connect_failure_(MQTTClientDisconnectReason reason);
  void reset_reconnect_backoff_();
  bool mqtt_retry_due_(uint32_t now) const;
  uint32_t mqtt_reconnect_delay_with_jitter_(uint32_t base_delay, uint32_t seed) const;
""",
)

replace_once(
    mqtt_client_header,
    """  uint32_t reboot_timeout_{300000};
  uint32_t connect_begin_;
  uint32_t last_connected_{0};
  optional<MQTTClientDisconnectReason> disconnect_reason_{};
""",
    """  static constexpr uint32_t MQTT_RECONNECT_INITIAL_DELAY_MS = 5000;
  static constexpr uint32_t MQTT_RECONNECT_MAX_DELAY_MS = 300000;

  uint32_t reboot_timeout_{300000};
  uint32_t connect_begin_{0};
  uint32_t last_connected_{0};
  uint32_t next_connect_attempt_{0};
  uint32_t reconnect_delay_{MQTT_RECONNECT_INITIAL_DELAY_MS};
  optional<MQTTClientDisconnectReason> disconnect_reason_{};
""",
)

replace_once(
    header,
    """  bool initialize_();
""",
    """  bool configure_();
  bool initialize_();
""",
)

replace_once(
    header,
    (
        """  bool is_connected_{false};
  bool is_initalized_{false};
""",
        """  bool is_connected_{false};
  bool is_initalized_{false};
  bool is_started_{false};
""",
    ),
    """  bool is_connected_{false};
  bool is_initalized_{false};
  bool is_started_{false};
  bool config_dirty_{true};
""",
)

replace_range(
    source,
    """bool MQTTBackendESP32::initialize_() {
  mqtt_cfg_.broker.address.hostname = this->host_.c_str();
""",
    """  auto *mqtt_client = esp_mqtt_client_init(&mqtt_cfg_);
""",
    """bool MQTTBackendESP32::configure_() {
  mqtt_cfg_ = {};

  mqtt_cfg_.broker.address.hostname = this->host_.c_str();
  mqtt_cfg_.broker.address.port = this->port_;
  mqtt_cfg_.session.keepalive = this->keep_alive_;
  mqtt_cfg_.session.disable_clean_session = !this->clean_session_;
  mqtt_cfg_.network.disable_auto_reconnect = true;

  if (!this->username_.empty()) {
    mqtt_cfg_.credentials.username = this->username_.c_str();
    if (!this->password_.empty()) {
      mqtt_cfg_.credentials.authentication.password = this->password_.c_str();
    }
  }

  if (!this->lwt_topic_.empty()) {
    mqtt_cfg_.session.last_will.topic = this->lwt_topic_.c_str();
    this->mqtt_cfg_.session.last_will.qos = this->lwt_qos_;
    this->mqtt_cfg_.session.last_will.retain = this->lwt_retain_;

    if (!this->lwt_message_.empty()) {
      mqtt_cfg_.session.last_will.msg = this->lwt_message_.c_str();
      mqtt_cfg_.session.last_will.msg_len = this->lwt_message_.size();
    }
  }

  if (!this->client_id_.empty()) {
    mqtt_cfg_.credentials.client_id = this->client_id_.c_str();
  }
  if (ca_certificate_.has_value()) {
    mqtt_cfg_.broker.verification.certificate = ca_certificate_.value().c_str();
    mqtt_cfg_.broker.verification.skip_cert_common_name_check = skip_cert_cn_check_;
    mqtt_cfg_.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;

    if (this->cl_certificate_.has_value() && this->cl_key_.has_value()) {
      mqtt_cfg_.credentials.authentication.certificate = this->cl_certificate_.value().c_str();
      mqtt_cfg_.credentials.authentication.key = this->cl_key_.value().c_str();
    }
  } else {
    mqtt_cfg_.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
  }

  return true;
}

bool MQTTBackendESP32::initialize_() {
  if (!this->configure_())
    return false;

  auto *mqtt_client = esp_mqtt_client_init(&mqtt_cfg_);
""",
)

replace_range(
    source,
    """    case MQTT_EVENT_ERROR:
""",
    """      break;
""",
    """    case MQTT_EVENT_ERROR:
      break;
""",
)

replace_once(
    wifi_component_header,
    """  // 4-byte members
  float output_power_{NAN};
  uint32_t action_started_;
  uint32_t last_connected_{0};
  uint32_t reboot_timeout_{};
  uint32_t roaming_last_check_{0};
  uint32_t roaming_scan_end_{0};  // Timestamp when last roaming scan completed
""",
    """  static constexpr uint32_t WIFI_RECONNECT_INITIAL_COOLDOWN_MS = 5000;
  static constexpr uint32_t WIFI_RECONNECT_MAX_COOLDOWN_MS = 300000;

  // 4-byte members
  float output_power_{NAN};
  uint32_t action_started_;
  uint32_t last_connected_{0};
  uint32_t reboot_timeout_{};
  uint32_t active_retry_cooldown_{WIFI_RECONNECT_INITIAL_COOLDOWN_MS};
  uint32_t retry_cooldown_{WIFI_RECONNECT_INITIAL_COOLDOWN_MS};
  uint32_t roaming_last_check_{0};
  uint32_t roaming_scan_end_{0};  // Timestamp when last roaming scan completed
""",
)

replace_once(
    web_server_idf,
    """static const char *const TAG = "web_server_idf";
""",
    """static const char *const TAG = "web_server_idf";
static constexpr size_t AIRDOT_HTTPD_MAX_FORM_BODY_LEN = 4096;
""",
)

replace_once(
    web_server_idf,
    """  // Handle regular form data
  if (r->content_len > CONFIG_HTTPD_MAX_REQ_HDR_LEN) {
    ESP_LOGW(TAG, "Request size is to big: %zu", r->content_len);
    httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, nullptr);
    return ESP_FAIL;
  }
""",
    """  // Handle regular form data. ESPHome's default uses the request-header
  // limit here, but the setup form can legitimately exceed that when saving
  // MQTT credentials and browser-generated time-zone data.
  if (r->content_len > AIRDOT_HTTPD_MAX_FORM_BODY_LEN) {
    ESP_LOGW(TAG, "Request size is too big: %zu", r->content_len);
    httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, nullptr);
    return ESP_FAIL;
  }
""",
)

replace_once(
    web_server_idf,
    """    const int ret = httpd_req_recv(r, &post_query[0], r->content_len + 1);
""",
    """    const int ret = httpd_req_recv(r, &post_query[0], r->content_len);
""",
)

replace_once(
    wifi_component,
    """/// Cooldown duration in milliseconds after adapter restart or repeated failures
/// Allows WiFi hardware to stabilize before next connection attempt
static constexpr uint32_t WIFI_COOLDOWN_DURATION_MS = 500;
""",
    """/// Fallback cooldown duration in milliseconds after immediate hardware-level failures.
/// Normal retry cycles use WiFiComponent's exponential cooldown state.
static constexpr uint32_t WIFI_COOLDOWN_DURATION_MS = 5000;
""",
)

replace_once(
    wifi_component,
    """        uint32_t cooldown_duration = portal_active ? WIFI_COOLDOWN_WITH_AP_ACTIVE_MS : WIFI_COOLDOWN_DURATION_MS;
""",
    """        uint32_t cooldown_duration =
            portal_active ? WIFI_COOLDOWN_WITH_AP_ACTIVE_MS : this->active_retry_cooldown_;
""",
)

replace_once(
    wifi_component,
    (
        """    this->state_ = WIFI_COMPONENT_STATE_STA_CONNECTED;
    this->num_retried_ = 0;
    this->print_connect_params_();
""",
        """    this->state_ = WIFI_COMPONENT_STATE_STA_CONNECTED;
    // Refresh is_connected() cache; loop()'s refresh ran before this transition.
    this->update_connected_state_();
    this->num_retried_ = 0;
    this->print_connect_params_();
""",
    ),
    """    this->state_ = WIFI_COMPONENT_STATE_STA_CONNECTED;
    // Refresh is_connected() cache; loop()'s refresh ran before this transition.
    this->update_connected_state_();
    this->num_retried_ = 0;
    this->active_retry_cooldown_ = WIFI_RECONNECT_INITIAL_COOLDOWN_MS;
    this->retry_cooldown_ = WIFI_RECONNECT_INITIAL_COOLDOWN_MS;
    this->print_connect_params_();
""",
)

replace_once(
    wifi_component,
    (
        """      // Always enter cooldown after restart (or skip-restart) to allow stabilization
      // Use extended cooldown when AP is active to avoid constant scanning that blocks DNS
      this->state_ = WIFI_COMPONENT_STATE_COOLDOWN;
      this->action_started_ = millis();
""",
        """      // Always enter cooldown after restart (or skip-restart) to allow stabilization.
      // Runtime STA retries use exponential backoff so bad SSIDs/passwords do not
      // keep retry activity bounded.
      this->active_retry_cooldown_ = this->retry_cooldown_;
      this->retry_cooldown_ = this->retry_cooldown_ >= WIFI_RECONNECT_MAX_COOLDOWN_MS / 2
                                  ? WIFI_RECONNECT_MAX_COOLDOWN_MS
                                  : this->retry_cooldown_ * 2;
      this->state_ = WIFI_COMPONENT_STATE_COOLDOWN;
      this->action_started_ = millis();
""",
        """      // Always enter cooldown after restart (or skip-restart) to allow stabilization.
      // Runtime STA retries use exponential backoff so bad SSIDs/passwords do not
      // keep the radio retry loop busy forever.
      uint32_t retry_entropy = millis() ^ this->retry_cooldown_ ^ this->num_retried_;
      retry_entropy ^= retry_entropy >> 16;
      retry_entropy *= 0x7feb352dU;
      retry_entropy ^= retry_entropy >> 15;
      retry_entropy *= 0x846ca68bU;
      retry_entropy ^= retry_entropy >> 16;
      const uint32_t retry_jitter_max = this->retry_cooldown_ / 5U;
      const uint32_t retry_jitter =
          retry_jitter_max == 0 ? 0 : retry_entropy % (retry_jitter_max + 1U);
      this->active_retry_cooldown_ =
          this->retry_cooldown_ > WIFI_RECONNECT_MAX_COOLDOWN_MS - retry_jitter
              ? WIFI_RECONNECT_MAX_COOLDOWN_MS
              : this->retry_cooldown_ + retry_jitter;
      this->retry_cooldown_ = this->retry_cooldown_ >= WIFI_RECONNECT_MAX_COOLDOWN_MS / 2
                                  ? WIFI_RECONNECT_MAX_COOLDOWN_MS
                                  : this->retry_cooldown_ * 2;
      this->state_ = WIFI_COMPONENT_STATE_COOLDOWN;
      this->action_started_ = millis();
""",
    ),
    """      // Always enter cooldown after restart (or skip-restart) to allow stabilization.
      // Runtime STA retries use exponential backoff so bad SSIDs/passwords do not
      // keep the radio retry loop busy forever.
      {
        uint32_t retry_entropy = millis() ^ this->retry_cooldown_ ^ this->num_retried_;
        retry_entropy ^= retry_entropy >> 16;
        retry_entropy *= 0x7feb352dU;
        retry_entropy ^= retry_entropy >> 15;
        retry_entropy *= 0x846ca68bU;
        retry_entropy ^= retry_entropy >> 16;
        const uint32_t retry_jitter_max = this->retry_cooldown_ / 5U;
        const uint32_t retry_jitter =
            retry_jitter_max == 0 ? 0 : retry_entropy % (retry_jitter_max + 1U);
        this->active_retry_cooldown_ =
            this->retry_cooldown_ > WIFI_RECONNECT_MAX_COOLDOWN_MS - retry_jitter
                ? WIFI_RECONNECT_MAX_COOLDOWN_MS
                : this->retry_cooldown_ + retry_jitter;
      }
      this->retry_cooldown_ = this->retry_cooldown_ >= WIFI_RECONNECT_MAX_COOLDOWN_MS / 2
                                  ? WIFI_RECONNECT_MAX_COOLDOWN_MS
                                  : this->retry_cooldown_ * 2;
      this->state_ = WIFI_COMPONENT_STATE_COOLDOWN;
      this->action_started_ = millis();
""",
)

apply_pending_changes()
