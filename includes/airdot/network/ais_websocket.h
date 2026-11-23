#pragma once

// Minimal RFC 6455 WebSocket client over esp_tls, for the aisstream.io feed.
//
// IDF's transport_ws / esp_websocket_client discard the RSV1 bit and do not
// expose the handshake response headers, so they cannot use permessage-deflate
// (RFC 7692). aisstream applies bandwidth limits to uncompressed connections,
// so this client negotiates deflate with no context takeover and inflates
// each message with the ROM tinfl decoder.
//
// Single-threaded: one task owns a Socket. Buffers live in PSRAM.

#ifdef USE_ESP32

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <strings.h>

#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_tls.h>
#include <lwip/sockets.h>
#include <mbedtls/base64.h>
#include <miniz.h>

#include "esphome/core/hal.h"

namespace AirDot::ship_radar::ws {

inline constexpr size_t RX_BUFFER_BYTES = 20UL * 1024UL;
inline constexpr size_t MESSAGE_BUFFER_BYTES = 16UL * 1024UL;
inline constexpr size_t INFLATE_BUFFER_BYTES = 32UL * 1024UL;
inline constexpr size_t HANDSHAKE_BYTES = 2048;
inline constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
// aisstream's upgrade response was observed to take 6-14 s.
inline constexpr uint32_t HANDSHAKE_TIMEOUT_MS = 30000;
inline constexpr int READ_TIMEOUT_MS = 500;

enum class Result : uint8_t {
  OK,             // A complete message is in the output.
  NONE,           // Nothing complete yet.
  CLOSED,         // Peer closed or the connection failed.
  PROTOCOL,       // Malformed frame or oversized message.
  INFLATE,        // Compressed payload could not be decoded.
};

enum class ConnectError : uint8_t { NONE, MEMORY, TLS, HANDSHAKE, REJECTED };

namespace detail {
inline void *psram_alloc(size_t bytes) {
  void *memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return memory != nullptr ? memory : heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
}
inline bool header_value(const char *headers, const char *name, char *out, size_t out_size) {
  const size_t name_length = std::strlen(name);
  for (const char *line = headers; line != nullptr && *line != '\0';) {
    if (strncasecmp(line, name, name_length) == 0 && line[name_length] == ':') {
      const char *value = line + name_length + 1;
      while (*value == ' ' || *value == '\t') ++value;
      size_t length = 0;
      while (value[length] != '\0' && value[length] != '\r' && value[length] != '\n') ++length;
      while (length > 0 && (value[length - 1] == ' ' || value[length - 1] == '\t')) --length;
      if (length >= out_size) length = out_size - 1;
      std::memcpy(out, value, length);
      out[length] = '\0';
      return true;
    }
    line = std::strchr(line, '\n');
    if (line != nullptr) ++line;
  }
  return false;
}
inline bool contains_token(const char *value, const char *token) {
  const size_t length = std::strlen(token);
  for (const char *cursor = value; *cursor != '\0'; ++cursor)
    if (strncasecmp(cursor, token, length) == 0) return true;
  return false;
}
}  // namespace detail

class Socket {
 public:
  Socket() = default;
  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;
  ~Socket() {
    close();
    heap_caps_free(rx_);
    heap_caps_free(message_);
    heap_caps_free(inflated_);
    heap_caps_free(inflator_);
  }

  bool connected() const { return tls_ != nullptr; }
  bool deflate() const { return deflate_; }

  ConnectError connect(const char *host, const char *path, bool offer_deflate) {
    close();
    if (!allocate_()) return ConnectError::MEMORY;
    tls_ = esp_tls_init();
    if (tls_ == nullptr) return ConnectError::MEMORY;
    esp_tls_cfg_t config{};
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = CONNECT_TIMEOUT_MS;
    if (esp_tls_conn_new_sync(host, static_cast<int>(std::strlen(host)), 443, &config, tls_) != 1) {
      close();
      return ConnectError::TLS;
    }
    int fd = -1;
    if (esp_tls_get_conn_sockfd(tls_, &fd) == ESP_OK && fd >= 0) {
      timeval timeout{};
      timeout.tv_sec = 0;
      timeout.tv_usec = READ_TIMEOUT_MS * 1000;
      setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    }
    const ConnectError handshake = handshake_(host, path, offer_deflate);
    if (handshake != ConnectError::NONE) close();
    return handshake;
  }

  void close() {
    if (tls_ != nullptr) {
      esp_tls_conn_destroy(tls_);
      tls_ = nullptr;
    }
    rx_length_ = message_length_ = 0;
    message_active_ = message_compressed_ = deflate_ = false;
  }

  bool send_text(const char *data, size_t length) { return send_frame_(0x1, reinterpret_cast<const uint8_t *>(data), length); }
  bool send_ping() { return send_frame_(0x9, nullptr, 0); }
  // A polite close; the caller still calls close() afterwards.
  void send_close() { const uint8_t code[2] = {0x03, 0xE8}; send_frame_(0x8, code, sizeof(code)); }

  // Reads what is available (blocking up to READ_TIMEOUT_MS) and returns the
  // next complete data message. Control frames are handled internally.
  // last_rx_ms is updated on any received bytes.
  Result poll(const char *&data, size_t &length, uint32_t &last_rx_ms) {
    if (tls_ == nullptr) return Result::CLOSED;
    for (;;) {
      const Result parsed = parse_frame_(data, length);
      if (parsed != Result::NONE) return parsed;
      if (rx_length_ >= RX_BUFFER_BYTES) return Result::PROTOCOL;
      const ssize_t received = esp_tls_conn_read(tls_, rx_ + rx_length_, RX_BUFFER_BYTES - rx_length_);
      if (received > 0) {
        rx_length_ += static_cast<size_t>(received);
        last_rx_ms = esphome::millis();
        continue;
      }
      if (received == 0) return Result::CLOSED;
      if (received == ESP_TLS_ERR_SSL_WANT_READ || received == ESP_TLS_ERR_SSL_WANT_WRITE ||
          received == ESP_TLS_ERR_SSL_TIMEOUT)
        return Result::NONE;
      return Result::CLOSED;
    }
  }

 private:
  esp_tls_t *tls_{nullptr};
  uint8_t *rx_{nullptr};
  uint8_t *message_{nullptr};
  uint8_t *inflated_{nullptr};
  tinfl_decompressor *inflator_{nullptr};
  size_t rx_length_{0}, message_length_{0};
  bool message_active_{false}, message_compressed_{false}, deflate_{false};

  bool allocate_() {
    if (rx_ == nullptr) rx_ = static_cast<uint8_t *>(detail::psram_alloc(RX_BUFFER_BYTES));
    if (message_ == nullptr) message_ = static_cast<uint8_t *>(detail::psram_alloc(MESSAGE_BUFFER_BYTES + 4));
    if (inflated_ == nullptr) inflated_ = static_cast<uint8_t *>(detail::psram_alloc(INFLATE_BUFFER_BYTES + 1));
    if (inflator_ == nullptr) inflator_ = static_cast<tinfl_decompressor *>(detail::psram_alloc(sizeof(tinfl_decompressor)));
    return rx_ != nullptr && message_ != nullptr && inflated_ != nullptr && inflator_ != nullptr;
  }

  bool write_all_(const uint8_t *data, size_t length) {
    while (length > 0) {
      const ssize_t written = esp_tls_conn_write(tls_, data, length);
      if (written > 0) {
        data += written;
        length -= static_cast<size_t>(written);
        continue;
      }
      if (written == ESP_TLS_ERR_SSL_WANT_READ || written == ESP_TLS_ERR_SSL_WANT_WRITE) {
        esphome::delay(5);
        continue;
      }
      return false;
    }
    return true;
  }

  // Client frames are always masked (RFC 6455 5.3) and never compressed.
  bool send_frame_(uint8_t opcode, const uint8_t *payload, size_t length) {
    if (tls_ == nullptr || length > 4096) return false;
    uint8_t header[8];
    size_t header_length = 0;
    header[header_length++] = static_cast<uint8_t>(0x80 | opcode);
    if (length < 126) {
      header[header_length++] = static_cast<uint8_t>(0x80 | length);
    } else {
      header[header_length++] = 0x80 | 126;
      header[header_length++] = static_cast<uint8_t>(length >> 8);
      header[header_length++] = static_cast<uint8_t>(length);
    }
    const uint32_t mask_word = esp_random();
    uint8_t mask[4];
    std::memcpy(mask, &mask_word, sizeof(mask));
    std::memcpy(header + header_length, mask, 4);
    header_length += 4;
    if (!write_all_(header, header_length)) return false;
    uint8_t chunk[256];
    for (size_t offset = 0; offset < length;) {
      const size_t count = std::min(sizeof(chunk), length - offset);
      for (size_t i = 0; i < count; ++i) chunk[i] = payload[offset + i] ^ mask[(offset + i) & 3];
      if (!write_all_(chunk, count)) return false;
      offset += count;
    }
    return true;
  }

  ConnectError handshake_(const char *host, const char *path, bool offer_deflate) {
    uint8_t nonce[16];
    esp_fill_random(nonce, sizeof(nonce));
    unsigned char key[32];
    size_t key_length = 0;
    if (mbedtls_base64_encode(key, sizeof(key) - 1, &key_length, nonce, sizeof(nonce)) != 0)
      return ConnectError::HANDSHAKE;
    key[key_length] = '\0';

    char request[512];
    const int request_length = std::snprintf(
        request, sizeof(request),
        "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n%s"
        "User-Agent: AirDot\r\n\r\n",
        path, host, reinterpret_cast<const char *>(key),
        offer_deflate ? "Sec-WebSocket-Extensions: permessage-deflate; client_no_context_takeover; "
                        "server_no_context_takeover\r\n" : "");
    if (request_length <= 0 || static_cast<size_t>(request_length) >= sizeof(request) ||
        !write_all_(reinterpret_cast<const uint8_t *>(request), static_cast<size_t>(request_length)))
      return ConnectError::TLS;

    // Read the response headers into rx_. Bytes after them are frame data.
    const uint32_t started = esphome::millis();
    size_t header_end = 0;
    while (header_end == 0) {
      if (esphome::millis() - started > HANDSHAKE_TIMEOUT_MS || rx_length_ >= HANDSHAKE_BYTES)
        return ConnectError::HANDSHAKE;
      const ssize_t received = esp_tls_conn_read(tls_, rx_ + rx_length_, HANDSHAKE_BYTES - rx_length_);
      if (received > 0) {
        rx_length_ += static_cast<size_t>(received);
        for (size_t i = 3; i < rx_length_; ++i)
          if (rx_[i - 3] == '\r' && rx_[i - 2] == '\n' && rx_[i - 1] == '\r' && rx_[i] == '\n') {
            header_end = i + 1;
            break;
          }
        continue;
      }
      if (received == 0) return ConnectError::HANDSHAKE;
      if (received != ESP_TLS_ERR_SSL_WANT_READ && received != ESP_TLS_ERR_SSL_WANT_WRITE &&
          received != ESP_TLS_ERR_SSL_TIMEOUT)
        return ConnectError::HANDSHAKE;
    }

    // The inflate buffer is idle during the handshake; keep the copy off the stack.
    char *headers = reinterpret_cast<char *>(inflated_);
    std::memcpy(headers, rx_, header_end);
    headers[header_end] = '\0';
    std::memmove(rx_, rx_ + header_end, rx_length_ - header_end);
    rx_length_ -= header_end;

    if (std::strncmp(headers, "HTTP/1.1 101", 12) != 0)
      return std::strncmp(headers, "HTTP/1.1 4", 10) == 0 ? ConnectError::REJECTED : ConnectError::HANDSHAKE;

    // Sec-WebSocket-Accept is not checked: aisstream's front proxy returns a
    // value that does not match the RFC 6455 hash of our key (observed with
    // several clients). TLS already authenticates the server; the 101 status
    // and the extension negotiation below are what matter.
    char extensions[160];
    deflate_ = false;
    if (detail::header_value(headers, "Sec-WebSocket-Extensions", extensions, sizeof(extensions)) &&
        detail::contains_token(extensions, "permessage-deflate")) {
      // Per-message inflate needs a fresh window for each message.
      if (!offer_deflate || !detail::contains_token(extensions, "server_no_context_takeover"))
        return ConnectError::HANDSHAKE;
      deflate_ = true;
    }
    return ConnectError::NONE;
  }

  void consume_(size_t count) {
    std::memmove(rx_, rx_ + count, rx_length_ - count);
    rx_length_ -= count;
  }

  Result inflate_(const char *&data, size_t &length) {
    // RFC 7692 7.2.2: append the empty stored block removed by the sender.
    message_[message_length_++] = 0x00;
    message_[message_length_++] = 0x00;
    message_[message_length_++] = 0xFF;
    message_[message_length_++] = 0xFF;
    tinfl_init(inflator_);
    size_t in_size = message_length_;
    size_t out_size = INFLATE_BUFFER_BYTES;
    const tinfl_status status = tinfl_decompress(
        inflator_, message_, &in_size, inflated_, inflated_, &out_size,
        TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF | TINFL_FLAG_HAS_MORE_INPUT);
    if (status < TINFL_STATUS_DONE || status == TINFL_STATUS_HAS_MORE_OUTPUT) return Result::INFLATE;
    inflated_[out_size] = '\0';
    data = reinterpret_cast<const char *>(inflated_);
    length = out_size;
    return Result::OK;
  }

  Result parse_frame_(const char *&data, size_t &length) {
    while (rx_length_ >= 2) {
      const uint8_t first = rx_[0], second = rx_[1];
      const bool fin = first & 0x80, rsv1 = first & 0x40;
      const uint8_t opcode = first & 0x0F;
      if ((first & 0x30) != 0 || (second & 0x80) != 0) return Result::PROTOCOL;  // RSV2/3, masked server frame
      size_t header = 2;
      uint64_t payload = second & 0x7F;
      if (payload == 126) {
        if (rx_length_ < 4) return Result::NONE;
        payload = (uint64_t(rx_[2]) << 8) | rx_[3];
        header = 4;
      } else if (payload == 127) {
        if (rx_length_ < 10) return Result::NONE;
        payload = 0;
        for (int i = 0; i < 8; ++i) payload = (payload << 8) | rx_[2 + i];
        header = 10;
      }
      if (payload > RX_BUFFER_BYTES - header) return Result::PROTOCOL;
      if (rx_length_ < header + payload) return Result::NONE;
      const uint8_t *body = rx_ + header;
      const size_t body_length = static_cast<size_t>(payload);

      if (opcode >= 0x8) {  // Control frame.
        if (!fin || body_length > 125) return Result::PROTOCOL;
        if (opcode == 0x8) {
          consume_(header + body_length);
          return Result::CLOSED;
        }
        if (opcode == 0x9) {
          uint8_t pong[125];
          std::memcpy(pong, body, body_length);
          consume_(header + body_length);
          if (!send_frame_(0xA, pong, body_length)) return Result::CLOSED;
          continue;
        }
        consume_(header + body_length);  // Pong or reserved.
        continue;
      }

      if (opcode == 0x0) {
        if (!message_active_) return Result::PROTOCOL;
      } else if (opcode == 0x1 || opcode == 0x2) {
        if (message_active_) return Result::PROTOCOL;
        if (rsv1 && !deflate_) return Result::PROTOCOL;
        message_active_ = true;
        message_compressed_ = rsv1;
        message_length_ = 0;
      } else {
        return Result::PROTOCOL;
      }
      if (message_length_ + body_length > MESSAGE_BUFFER_BYTES) return Result::PROTOCOL;
      std::memcpy(message_ + message_length_, body, body_length);
      message_length_ += body_length;
      consume_(header + body_length);
      if (!fin) continue;

      message_active_ = false;
      if (message_compressed_) return inflate_(data, length);
      message_[message_length_] = '\0';
      data = reinterpret_cast<const char *>(message_);
      length = message_length_;
      return Result::OK;
    }
    return Result::NONE;
  }
};

}  // namespace AirDot::ship_radar::ws

#endif  // USE_ESP32
