#ifdef USE_ESP32

#include <cstdarg>
#include <memory>
#include <cstring>
#include <cctype>
#include <cinttypes>

#include "esphome/core/helpers.h"

#include "esp_tls_crypto.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "utils.h"
#include "web_server_idf.h"

#ifdef USE_WEBSERVER_AUTH_DIGEST
#include <esp_random.h>
#include <esp_rom_md5.h>
#endif

#ifdef USE_WEBSERVER_OTA
#include <multipart_parser.h>
#include "multipart.h"
#endif

#ifdef USE_WEBSERVER
#include "esphome/components/web_server/web_server.h"
#include "esphome/components/web_server/list_entities.h"
#endif


#include <cerrno>
#include <sys/socket.h>

namespace esphome::web_server_idf {


#ifndef HTTPD_401
#define HTTPD_401 "401 Unauthorized"
#endif
#ifndef HTTPD_409
#define HTTPD_409 "409 Conflict"
#endif
#ifndef HTTPD_422
#define HTTPD_422 "422 Unprocessable Entity"
#endif

#define CRLF_STR "\r\n"
#define CRLF_LEN (sizeof(CRLF_STR) - 1)

static constexpr size_t AIRDOT_HTTPD_MAX_FORM_BODY_LEN = 4096;



static constexpr size_t RECV_CHUNK_SIZE = 1460;
static constexpr size_t YIELD_INTERVAL_BYTES = 16 * 1024;



namespace {

DefaultHeaders default_headers_instance;
}

DefaultHeaders &DefaultHeaders::Instance() { return default_headers_instance; }

namespace {

/**
 * Sends data on a socket in non-blocking mode.
 *
 * @param hd      HTTP server handle (unused).
 * @param sockfd  Socket file descriptor.
 * @param buf     Buffer to send.
 * @param buf_len Length of buffer.
 * @param flags   Flags for send().
 * @return
 *   - Number of bytes sent on success.
 *   - HTTPD_SOCK_ERR_INVALID if buf is nullptr.
 *   - HTTPD_SOCK_ERR_TIMEOUT if the send buffer is full (EAGAIN/EWOULDBLOCK).
 *   - HTTPD_SOCK_ERR_FAIL for other errors.
 */
[[maybe_unused]] int nonblocking_send(httpd_handle_t hd, int sockfd, const char *buf, size_t buf_len, int flags) {
  if (buf == nullptr) {
    return HTTPD_SOCK_ERR_INVALID;
  }


  int ret = send(sockfd, buf, buf_len, flags | MSG_DONTWAIT);
  if (ret < 0) {
    const int err = errno;
    if (err == EAGAIN || err == EWOULDBLOCK) {

      return HTTPD_SOCK_ERR_TIMEOUT;
    }
    return HTTPD_SOCK_ERR_FAIL;
  }
  return ret;
}
}

void AsyncWebServer::safe_close_with_shutdown(httpd_handle_t hd, int sockfd) {
















  shutdown(sockfd, SHUT_RD);


  close(sockfd);
}

void AsyncWebServer::end() {
  if (this->server_) {
    httpd_stop(this->server_);
    this->server_ = nullptr;
  }
}

void AsyncWebServer::begin() {
  if (this->server_) {
    this->end();
  }



  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;
  config.server_port = this->port_;
  config.uri_match_fn = [](const char * /*unused*/, const char * /*unused*/, size_t /*unused*/) { return true; };




  config.lru_purge_enable = true;

  config.close_fn = AsyncWebServer::safe_close_with_shutdown;
  if (httpd_start(&this->server_, &config) == ESP_OK) {
    const httpd_uri_t handler_get = {
        .uri = "",
        .method = HTTP_GET,
        .handler = AsyncWebServer::request_handler,
        .user_ctx = this,
    };
    httpd_register_uri_handler(this->server_, &handler_get);

    const httpd_uri_t handler_post = {
        .uri = "",
        .method = HTTP_POST,
        .handler = AsyncWebServer::request_post_handler,
        .user_ctx = this,
    };
    httpd_register_uri_handler(this->server_, &handler_post);

    const httpd_uri_t handler_options = {
        .uri = "",
        .method = HTTP_OPTIONS,
        .handler = AsyncWebServer::request_handler,
        .user_ctx = this,
    };
    httpd_register_uri_handler(this->server_, &handler_options);
  }
}

esp_err_t AsyncWebServer::request_post_handler(httpd_req_t *r) {
  auto content_type = request_get_header(r, "Content-Type");

  if (!request_has_header(r, "Content-Length")) {
    httpd_resp_send_err(r, HTTPD_411_LENGTH_REQUIRED, nullptr);
    return ESP_OK;
  }

  if (content_type.has_value()) {
    const char *content_type_char = content_type.value().c_str();


    size_t content_type_len = strlen(content_type_char);
    if (strcasestr_n(content_type_char, content_type_len, "application/x-www-form-urlencoded") != nullptr) {

#ifdef USE_WEBSERVER_OTA
    } else if (strcasestr_n(content_type_char, content_type_len, "multipart/form-data") != nullptr) {
      auto *server = static_cast<AsyncWebServer *>(r->user_ctx);
      return server->handle_multipart_upload_(r, content_type_char);
#endif
    } else {


      auto *server = static_cast<AsyncWebServer *>(r->user_ctx);
      return server->handle_raw_body_(r, content_type_char);
    }
  }



  if (r->content_len > AIRDOT_HTTPD_MAX_FORM_BODY_LEN) {
    httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, nullptr);
    return ESP_FAIL;
  }

  std::string post_query;
  if (r->content_len > 0) {
    post_query.resize(r->content_len);
    const int ret = httpd_req_recv(r, &post_query[0], r->content_len);
    if (ret <= 0) {
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
        httpd_resp_send_err(r, HTTPD_408_REQ_TIMEOUT, nullptr);
        return ESP_ERR_TIMEOUT;
      }
      httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, nullptr);
      return ESP_FAIL;
    }
  }

  AsyncWebServerRequest req(r, std::move(post_query));
  return static_cast<AsyncWebServer *>(r->user_ctx)->request_handler_(&req);
}

esp_err_t AsyncWebServer::request_handler(httpd_req_t *r) {
  AsyncWebServerRequest req(r);
  return static_cast<AsyncWebServer *>(r->user_ctx)->request_handler_(&req);
}

esp_err_t AsyncWebServer::request_handler_(AsyncWebServerRequest *request) const {
  for (auto *handler : this->handlers_) {
    if (handler->canHandle(request)) {


      handler->handleRequest(request);
      return ESP_OK;
    }
  }
  if (this->on_not_found_) {
    this->on_not_found_(request);
    return ESP_OK;
  }
  return ESP_ERR_NOT_FOUND;
}

esp_err_t AsyncWebServer::handle_raw_body_(httpd_req_t *r, const char *content_type) {
  AsyncWebServerRequest req(r);
  AsyncWebHandler *handler = nullptr;
  for (auto *h : this->handlers_) {
    if (h->canHandle(&req)) {
      handler = h;
      break;
    }
  }

  if (handler == nullptr) {

    return this->request_handler_(&req);
  }

  const size_t total = r->content_len;
  if (total > 0) {
    auto buffer = std::make_unique_for_overwrite<char[]>(RECV_CHUNK_SIZE);
    size_t bytes_since_yield = 0;

    for (size_t index = 0; index < total;) {
      int recv_len = httpd_req_recv(r, buffer.get(), std::min(total - index, RECV_CHUNK_SIZE));

      if (recv_len <= 0) {
        httpd_resp_send_err(r, recv_len == HTTPD_SOCK_ERR_TIMEOUT ? HTTPD_408_REQ_TIMEOUT : HTTPD_400_BAD_REQUEST,
                            nullptr);
        return recv_len == HTTPD_SOCK_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
      }

      handler->handleBody(&req, reinterpret_cast<uint8_t *>(buffer.get()), recv_len, index, total);
      index += recv_len;
      bytes_since_yield += recv_len;

      if (bytes_since_yield > YIELD_INTERVAL_BYTES) {
        vTaskDelay(1);
        bytes_since_yield = 0;
      }
    }
  }

  handler->handleRequest(&req);
  return ESP_OK;
}

AsyncWebServerRequest::~AsyncWebServerRequest() {
  delete this->rsp_;
  for (auto *param : this->params_) {
    delete param;
  }
}

bool AsyncWebServerRequest::hasHeader(const char *name) const { return request_has_header(*this, name); }

optional<std::string> AsyncWebServerRequest::get_header(const char *name) const {
  return request_get_header(*this, name);
}

StringRef AsyncWebServerRequest::url_to(std::span<char, URL_BUF_SIZE> buffer) const {
  const char *uri = this->req_->uri;
  const char *query_start = strchr(uri, '?');
  size_t uri_len = query_start ? static_cast<size_t>(query_start - uri) : strlen(uri);
  size_t copy_len = std::min(uri_len, URL_BUF_SIZE - 1);
  memcpy(buffer.data(), uri, copy_len);
  buffer[copy_len] = '\0';

  size_t decoded_len = url_decode(buffer.data());
  return StringRef(buffer.data(), decoded_len);
}

void AsyncWebServerRequest::redirect(const std::string &url) {
  httpd_resp_set_status(*this, "302 Found");
  httpd_resp_set_hdr(*this, "Location", url.c_str());
  httpd_resp_set_hdr(*this, "Connection", "close");
  httpd_resp_send(*this, nullptr, 0);
}

void AsyncWebServerRequest::init_response_(AsyncWebServerResponse *rsp, int code, const char *content_type) {

  const char *status;
  switch (code) {
    case 200:
      status = HTTPD_200;
      break;
    case 204:
      status = HTTPD_204;
      break;
    case 400:
      status = HTTPD_400;
      break;
    case 401:
      status = HTTPD_401;
      break;
    case 404:
      status = HTTPD_404;
      break;
    case 409:
      status = HTTPD_409;
      break;
    case 422:
      status = HTTPD_422;
      break;
    default:
      status = HTTPD_500;
      break;
  }
  httpd_resp_set_status(*this, status);

  if (content_type && *content_type) {
    httpd_resp_set_type(*this, content_type);
  }
  httpd_resp_set_hdr(*this, "Accept-Ranges", "none");

  for (const auto &header : DefaultHeaders::Instance().headers_) {
    httpd_resp_set_hdr(*this, header.name, header.value);
  }

  delete this->rsp_;
  this->rsp_ = rsp;
}

#ifdef USE_WEBSERVER_AUTH

#ifdef USE_WEBSERVER_AUTH_DIGEST
namespace {


void bytes_to_hex(const uint8_t *data, size_t len, char *out) {
  static const char HEX[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out[i * 2] = HEX[data[i] >> 4];
    out[i * 2 + 1] = HEX[data[i] & 0x0f];
  }
  out[len * 2] = '\0';
}




StringRef digest_param(StringRef params, const char *key) {
  size_t key_len = strlen(key);
  const char *base = params.c_str();
  size_t n = params.size();
  size_t i = 0;
  while (i < n) {
    while (i < n && (base[i] == ' ' || base[i] == ','))
      i++;
    size_t name_start = i;
    while (i < n && base[i] != '=' && base[i] != ',')
      i++;
    if (i >= n || base[i] == ',')
      continue;
    size_t name_len = i - name_start;
    while (name_len > 0 && base[name_start + name_len - 1] == ' ')
      name_len--;
    i++;
    const char *val_start;
    size_t val_len;
    if (i < n && base[i] == '"') {
      i++;
      val_start = base + i;
      while (i < n && base[i] != '"')
        i++;
      val_len = (base + i) - val_start;
      if (i < n)
        i++;
    } else {
      val_start = base + i;
      while (i < n && base[i] != ',')
        i++;
      val_len = (base + i) - val_start;
    }
    if (name_len == key_len && memcmp(base + name_start, key, key_len) == 0)
      return StringRef(val_start, val_len);
    while (i < n && base[i] != ',')
      i++;
  }
  return StringRef();
}



bool check_digest_auth(const char *username, const char *password, const std::string &header, const char *method) {
  const size_t prefix_len = sizeof("Digest ") - 1;
  StringRef params(header.c_str() + prefix_len, header.size() - prefix_len);

  if (digest_param(params, "username") != username)
    return false;

  StringRef realm = digest_param(params, "realm");
  StringRef nonce = digest_param(params, "nonce");
  StringRef uri = digest_param(params, "uri");
  StringRef qop = digest_param(params, "qop");
  StringRef nc = digest_param(params, "nc");
  StringRef cnonce = digest_param(params, "cnonce");
  StringRef response = digest_param(params, "response");
  if (response.size() != 32)
    return false;



  md5_context_t ctx;
  uint8_t digest[16];


  char ha1[33];
  esp_rom_md5_init(&ctx);
  esp_rom_md5_update(&ctx, username, strlen(username));
  esp_rom_md5_update(&ctx, ":", 1);
  esp_rom_md5_update(&ctx, realm.c_str(), realm.size());
  esp_rom_md5_update(&ctx, ":", 1);
  esp_rom_md5_update(&ctx, password, strlen(password));
  esp_rom_md5_final(digest, &ctx);
  bytes_to_hex(digest, sizeof(digest), ha1);


  char ha2[33];
  esp_rom_md5_init(&ctx);
  esp_rom_md5_update(&ctx, method, strlen(method));
  esp_rom_md5_update(&ctx, ":", 1);
  esp_rom_md5_update(&ctx, uri.c_str(), uri.size());
  esp_rom_md5_final(digest, &ctx);
  bytes_to_hex(digest, sizeof(digest), ha2);


  char expected[33];
  esp_rom_md5_init(&ctx);
  esp_rom_md5_update(&ctx, ha1, 32);
  esp_rom_md5_update(&ctx, ":", 1);
  esp_rom_md5_update(&ctx, nonce.c_str(), nonce.size());
  esp_rom_md5_update(&ctx, ":", 1);
  esp_rom_md5_update(&ctx, nc.c_str(), nc.size());
  esp_rom_md5_update(&ctx, ":", 1);
  esp_rom_md5_update(&ctx, cnonce.c_str(), cnonce.size());
  esp_rom_md5_update(&ctx, ":", 1);
  esp_rom_md5_update(&ctx, qop.c_str(), qop.size());
  esp_rom_md5_update(&ctx, ":", 1);
  esp_rom_md5_update(&ctx, ha2, 32);
  esp_rom_md5_final(digest, &ctx);
  bytes_to_hex(digest, sizeof(digest), expected);


  uint8_t result = 0;
  for (size_t i = 0; i < 32; i++)
    result |= static_cast<uint8_t>(expected[i] ^ response[i]);
  return result == 0;
}

}
#endif

bool AsyncWebServerRequest::authenticate(const char *username, const char *password) const {
  if (username == nullptr || password == nullptr || *username == 0) {
    return true;
  }
  auto auth = this->get_header("Authorization");
  if (!auth.has_value()) {
    return false;
  }

  auto *auth_str = auth.value().c_str();

#ifdef USE_WEBSERVER_AUTH_DIGEST

  const auto auth_prefix_len = sizeof("Digest ") - 1;
  if (strncmp("Digest ", auth_str, auth_prefix_len) != 0) {
    return false;
  }
  return check_digest_auth(username, password, auth.value(), http_method_str(this->method()));
#else
  const auto auth_prefix_len = sizeof("Basic ") - 1;
  if (strncmp("Basic ", auth_str, auth_prefix_len) != 0) {
    return false;
  }


  constexpr size_t max_user_info_len = 256;
  char user_info[max_user_info_len];
  size_t user_len = strlen(username);
  size_t pass_len = strlen(password);
  size_t user_info_len = user_len + 1 + pass_len;

  if (user_info_len >= max_user_info_len) {
    return false;
  }

  memcpy(user_info, username, user_len);
  user_info[user_len] = ':';
  memcpy(user_info + user_len + 1, password, pass_len);
  user_info[user_info_len] = '\0';



  constexpr size_t max_digest_len = 350;
  char digest[max_digest_len];
  size_t out;
  esp_crypto_base64_encode(reinterpret_cast<uint8_t *>(digest), max_digest_len, &out,
                           reinterpret_cast<const uint8_t *>(user_info), user_info_len);




  const char *provided = auth_str + auth_prefix_len;
  size_t digest_len = out;


  size_t provided_len = auth.value().size() - auth_prefix_len;



  volatile size_t result = digest_len ^ provided_len;



  for (size_t i = 0; i < digest_len; i++) {
    char provided_ch = (i < provided_len) ? provided[i] : 0;
    result |= static_cast<uint8_t>(digest[i] ^ provided_ch);
  }
  return result == 0;
#endif
}

void AsyncWebServerRequest::requestAuthentication() const {
  httpd_resp_set_hdr(*this, "Connection", "keep-alive");
#ifdef USE_WEBSERVER_AUTH_DIGEST




  uint8_t random_bytes[16];
  char nonce[33];
  char opaque[33];
  char header[160];
  esp_fill_random(random_bytes, sizeof(random_bytes));
  bytes_to_hex(random_bytes, sizeof(random_bytes), nonce);
  esp_fill_random(random_bytes, sizeof(random_bytes));
  bytes_to_hex(random_bytes, sizeof(random_bytes), opaque);
  snprintf(header, sizeof(header), R"(Digest realm="Login Required", qop="auth", nonce="%s", opaque="%s")", nonce,
           opaque);
  httpd_resp_set_hdr(*this, "WWW-Authenticate", header);
#else
  httpd_resp_set_hdr(*this, "WWW-Authenticate", "Basic realm=\"Login Required\"");
#endif
  httpd_resp_send_err(*this, HTTPD_401_UNAUTHORIZED, nullptr);
}
#endif

AsyncWebParameter *AsyncWebServerRequest::getParam(const char *name) {

  for (auto *param : this->params_) {
    if (param->name() == name) {
      return param;
    }
  }


  auto val = this->find_query_value_(name);



  if (!val.has_value()) {
    return nullptr;
  }

  auto *param = new AsyncWebParameter(name, val.value());
  this->params_.push_back(param);
  return param;
}




template<typename Func>
static auto search_query_sources(httpd_req_t *req, const std::string &post_query, const char *name, Func func)
    -> decltype(func(nullptr, size_t{0}, name)) {
  if (!post_query.empty()) {
    auto result = func(post_query.c_str(), post_query.size(), name);
    if (result) {
      return result;
    }
  }



  auto len = httpd_req_get_url_query_len(req);
  if (len == 0) {
    return {};
  }
  const char *query = strchr(req->uri, '?');
  if (query == nullptr) {
    return {};
  }
  query++;
  return func(query, len, name);
}

optional<std::string> AsyncWebServerRequest::find_query_value_(const char *name) const {
  return search_query_sources(this->req_, this->post_query_, name,
                              [](const char *q, size_t len, const char *k) { return query_key_value(q, len, k); });
}

bool AsyncWebServerRequest::hasArg(const char *name) {
  return search_query_sources(this->req_, this->post_query_, name, query_has_key);
}

std::string AsyncWebServerRequest::arg(const char *name) {
  auto val = this->find_query_value_(name);
  if (val.has_value()) {
    return std::move(val.value());
  }
  return {};
}

void AsyncWebServerResponse::addHeader(const char *name, const char *value) {
  httpd_resp_set_hdr(*this->req_, name, value);
}

void AsyncResponseStream::print(float value) {


  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%f", value);
  this->content_.append(buf, len);
}

void AsyncResponseStream::printf(const char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  const int length = vsnprintf(nullptr, 0, fmt, args);
  va_end(args);

  std::string str;
  str.resize(length);

  va_start(args, fmt);
  vsnprintf(&str[0], length + 1, fmt, args);
  va_end(args);

  this->print(str);
}

#ifdef USE_WEBSERVER
AsyncEventSource::~AsyncEventSource() {
  LockGuard guard{this->pending_mutex_};
  for (auto *vec : {&this->sessions_, &this->pending_sessions_}) {
    for (auto *ses : *vec) {
      delete ses;
    }
  }
}

void AsyncEventSource::handleRequest(AsyncWebServerRequest *request) {


  auto *rsp = new AsyncEventSourceResponse(request, this, this->web_server_);
  {
    LockGuard guard{this->pending_mutex_};
    this->pending_sessions_.push_back(rsp);
    this->has_pending_sessions_.store(true, std::memory_order_release);
  }
  this->web_server_->enable_loop_soon_any_context();
}





bool AsyncEventSource::loop() {

  if (this->has_pending_sessions_.load(std::memory_order_acquire)) {
    this->adopt_pending_sessions_main_loop_();
  }




  for (size_t i = 0; i < this->sessions_.size();) {
    auto *ses = this->sessions_[i];

    if (ses->fd_.load() == 0) {

      delete ses;

      this->sessions_[i] = this->sessions_.back();
      this->sessions_.pop_back();
    } else {
      ses->loop();
      ++i;
    }
  }
  return !this->sessions_.empty();
}

void AsyncEventSource::adopt_pending_sessions_main_loop_() {
  std::vector<AsyncEventSourceResponse *> incoming;
  {
    LockGuard guard{this->pending_mutex_};
    incoming.swap(this->pending_sessions_);
    this->has_pending_sessions_.store(false, std::memory_order_relaxed);
  }
  for (auto *rsp : incoming) {

    if (rsp->fd_.load() == 0) {
      delete rsp;
      continue;
    }
    this->sessions_.push_back(rsp);


    rsp->start_session_main_loop_();
    if (this->on_connect_) {
      this->on_connect_(rsp);
    }
  }
}


void AsyncEventSource::try_send_nodefer(const char *message, size_t message_len, const char *event, uint32_t id,
                                        uint32_t reconnect) {
  for (auto *ses : this->sessions_) {
    if (ses->fd_.load() != 0) {
      ses->try_send_nodefer(message, message_len, event, id, reconnect);
    }
  }
}

void AsyncEventSource::deferrable_send_state(void *source, const char *event_type,
                                             message_generator_t *message_generator) {

  if (this->empty())
    return;
  for (auto *ses : this->sessions_) {
    if (ses->fd_.load() != 0) {
      ses->deferrable_send_state(source, event_type, message_generator);
    }
  }
}

AsyncEventSourceResponse::AsyncEventSourceResponse(const AsyncWebServerRequest *request,
                                                   esphome::web_server_idf::AsyncEventSource *server,
                                                   esphome::web_server::WebServer *ws)
    : server_(server), web_server_(ws), entities_iterator_(ws, server) {

  httpd_req_t *req = *request;

  httpd_resp_set_status(req, HTTPD_200);
  httpd_resp_set_type(req, "text/event-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Connection", "keep-alive");

  for (const auto &header : DefaultHeaders::Instance().headers_) {
    httpd_resp_set_hdr(req, header.name, header.value);
  }

  httpd_resp_send_chunk(req, CRLF_STR, CRLF_LEN);

  req->sess_ctx = this;
  req->free_ctx = AsyncEventSourceResponse::destroy;

  this->hd_ = req->handle;
  this->fd_.store(httpd_req_to_sockfd(req));


  httpd_sess_set_send_override(this->hd_, this->fd_.load(), nonblocking_send);
}


void AsyncEventSourceResponse::start_session_main_loop_() {
  auto *ws = this->web_server_;


  auto message = ws->get_config_json();
  this->try_send_nodefer(message.c_str(), message.size(), "ping", millis(), 30000);

#ifdef USE_WEBSERVER_SORTING
  for (auto &group : ws->sorting_groups_) {
    json::JsonBuilder builder;
    JsonObject root = builder.root();
    root["name"] = group.second.name;
    root["sorting_weight"] = group.second.weight;
    message = builder.serialize();



    this->try_send_nodefer(message.c_str(), message.size(), "sorting_group");
  }
#endif

  this->entities_iterator_.begin(ws->include_internal_);
}


void AsyncEventSourceResponse::destroy(void *ptr) {
  auto *rsp = static_cast<AsyncEventSourceResponse *>(ptr);
  int fd = rsp->fd_.exchange(0);




}


void AsyncEventSourceResponse::deq_push_back_with_dedup_(void *source, message_generator_t *message_generator) {
  DeferredEvent item(source, message_generator);


  for (auto &event : this->deferred_queue_) {
    if (event == item) {
      return;
    }
  }
  this->deferred_queue_.push_back(item);
}

void AsyncEventSourceResponse::process_deferred_queue_() {
  while (!deferred_queue_.empty()) {
    DeferredEvent &de = deferred_queue_.front();
    auto message = de.message_generator_(web_server_, de.source_);
    if (this->try_send_nodefer(message.c_str(), message.size(), "state")) {

      deferred_queue_.erase(deferred_queue_.begin());
    } else {
      break;
    }
  }
}

void AsyncEventSourceResponse::process_buffer_() {
  if (event_buffer_.empty()) {
    return;
  }
  if (event_bytes_sent_ == event_buffer_.size()) {
    event_buffer_.resize(0);
    event_bytes_sent_ = 0;
    return;
  }

  size_t remaining = event_buffer_.size() - event_bytes_sent_;
  int bytes_sent =
      httpd_socket_send(this->hd_, this->fd_.load(), event_buffer_.c_str() + event_bytes_sent_, remaining, 0);
  if (bytes_sent == HTTPD_SOCK_ERR_TIMEOUT) {





    this->consecutive_send_failures_++;
    if (this->consecutive_send_failures_ >= MAX_CONSECUTIVE_SEND_FAILURES) {
      this->fd_.store(0);
      this->deferred_queue_.clear();
    }
    return;
  }
  if (bytes_sent == HTTPD_SOCK_ERR_FAIL) {

    return;
  }
  if (bytes_sent <= 0) {
    return;
  }


  this->consecutive_send_failures_ = 0;
  event_bytes_sent_ += bytes_sent;


  if (event_bytes_sent_ < event_buffer_.size()) {
  }

  if (event_bytes_sent_ == event_buffer_.size()) {
    event_buffer_.resize(0);
    event_bytes_sent_ = 0;
  }
}

void AsyncEventSourceResponse::loop() {
  process_buffer_();
  process_deferred_queue_();
  if (!this->entities_iterator_.completed())
    this->entities_iterator_.advance();
}

bool AsyncEventSourceResponse::try_send_nodefer(const char *message, size_t message_len, const char *event, uint32_t id,
                                                uint32_t reconnect) {
  if (this->fd_.load() == 0) {
    return false;
  }

  process_buffer_();
  if (!event_buffer_.empty()) {

    return false;
  }


  const char chunk_len_header[] = "        " CRLF_STR;
  const int chunk_len_header_len = sizeof(chunk_len_header) - 1;

  event_buffer_.append(chunk_len_header);



  constexpr size_t num_buf_size = 32;
  char num_buf[num_buf_size];

  if (reconnect) {
    int len = snprintf(num_buf, num_buf_size, "retry: %" PRIu32 CRLF_STR, reconnect);
    event_buffer_.append(num_buf, len);
  }

  if (id) {
    int len = snprintf(num_buf, num_buf_size, "id: %" PRIu32 CRLF_STR, id);
    event_buffer_.append(num_buf, len);
  }

  if (event && *event) {
    event_buffer_.append("event: ", sizeof("event: ") - 1);
    event_buffer_.append(event);
    event_buffer_.append(CRLF_STR, CRLF_LEN);
  }


  if (message) {





    const char *first_n = static_cast<const char *>(memchr(message, '\n', message_len));
    const char *first_r = static_cast<const char *>(memchr(message, '\r', message_len));

    if (first_n == nullptr && first_r == nullptr) {

      event_buffer_.append("data: ", sizeof("data: ") - 1);
      event_buffer_.append(message, message_len);
      event_buffer_.append(CRLF_STR CRLF_STR, CRLF_LEN * 2);
    } else {

      const char *line_start = message;
      const char *msg_end = message + message_len;


      const char *next_n = first_n;
      const char *next_r = first_r;

      while (line_start <= msg_end) {
        const char *line_end;
        const char *next_line;

        if (next_n == nullptr && next_r == nullptr) {

          event_buffer_.append("data: ", sizeof("data: ") - 1);
          event_buffer_.append(line_start, msg_end - line_start);
          event_buffer_.append(CRLF_STR, CRLF_LEN);
          break;
        }


        if (next_n != nullptr && next_r != nullptr) {
          if (next_r + 1 == next_n) {

            line_end = next_r;
            next_line = next_n + 1;
          } else {

            line_end = (next_r < next_n) ? next_r : next_n;
            next_line = line_end + 1;
          }
        } else if (next_n != nullptr) {

          line_end = next_n;
          next_line = next_n + 1;
        } else {

          line_end = next_r;
          next_line = next_r + 1;
        }


        event_buffer_.append("data: ", sizeof("data: ") - 1);
        event_buffer_.append(line_start, line_end - line_start);
        event_buffer_.append(CRLF_STR, CRLF_LEN);

        line_start = next_line;


        if (line_start >= msg_end) {
          break;
        }


        next_n = static_cast<const char *>(memchr(line_start, '\n', msg_end - line_start));
        next_r = static_cast<const char *>(memchr(line_start, '\r', msg_end - line_start));
      }


      event_buffer_.append(CRLF_STR, CRLF_LEN);
    }
  }

  if (event_buffer_.size() == static_cast<size_t>(chunk_len_header_len)) {

    event_buffer_.resize(0);
    return true;
  }

  event_buffer_.append(CRLF_STR, CRLF_LEN);


  int chunk_len = event_buffer_.size() - CRLF_LEN - chunk_len_header_len;
  char chunk_len_str[9];
  snprintf(chunk_len_str, 9, "%08x", chunk_len);
  std::memcpy(&event_buffer_[0], chunk_len_str, 8);

  event_bytes_sent_ = 0;
  process_buffer_();

  return true;
}

void AsyncEventSourceResponse::deferrable_send_state(void *source, const char *event_type,
                                                     message_generator_t *message_generator) {


  if (!this->entities_iterator_.completed() && 0 != strcmp(event_type, "state_detail_all"))
    return;

  if (source == nullptr)
    return;
  if (event_type == nullptr)
    return;
  if (message_generator == nullptr)
    return;

  if (0 != strcmp(event_type, "state_detail_all") && 0 != strcmp(event_type, "state")) {
  }

  process_buffer_();
  process_deferred_queue_();

  if (!event_buffer_.empty() || !deferred_queue_.empty()) {


    deq_push_back_with_dedup_(source, message_generator);
  } else {
    auto message = message_generator(web_server_, source);
    if (!this->try_send_nodefer(message.c_str(), message.size(), "state")) {
      deq_push_back_with_dedup_(source, message_generator);
    }
  }
}
#endif

#ifdef USE_WEBSERVER_OTA
esp_err_t AsyncWebServer::handle_multipart_upload_(httpd_req_t *r, const char *content_type) {

  const char *boundary_start;
  size_t boundary_len;
  if (!parse_multipart_boundary(content_type, &boundary_start, &boundary_len)) {
    httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, nullptr);
    return ESP_FAIL;
  }

  AsyncWebServerRequest req(r);
  AsyncWebHandler *handler = nullptr;
  for (auto *h : this->handlers_) {
    if (h->canHandle(&req)) {
      handler = h;
      break;
    }
  }

  if (!handler) {
    httpd_resp_send_err(r, HTTPD_404_NOT_FOUND, nullptr);
    return ESP_OK;
  }


  std::string filename;
  size_t index = 0;

  auto reader = std::make_unique<MultipartReader>("--" + std::string(boundary_start, boundary_len));


  reader->set_data_callback([&](const uint8_t *data, size_t len) {
    if (!reader->has_file() || !len)
      return;

    if (filename.empty()) {
      filename = reader->get_current_part().filename;
      handler->handleUpload(&req, filename, 0, nullptr, 0, false);
    }

    handler->handleUpload(&req, filename, index, const_cast<uint8_t *>(data), len, false);
    index += len;
  });

  reader->set_part_complete_callback([&]() {
    if (index > 0) {
      handler->handleUpload(&req, filename, index, nullptr, 0, true);
      filename.clear();
      index = 0;
    }
  });

  auto buffer = std::make_unique_for_overwrite<char[]>(RECV_CHUNK_SIZE);
  size_t bytes_since_yield = 0;

  for (size_t remaining = r->content_len; remaining > 0;) {
    int recv_len = httpd_req_recv(r, buffer.get(), std::min(remaining, RECV_CHUNK_SIZE));

    if (recv_len <= 0) {
      httpd_resp_send_err(r, recv_len == HTTPD_SOCK_ERR_TIMEOUT ? HTTPD_408_REQ_TIMEOUT : HTTPD_400_BAD_REQUEST,
                          nullptr);
      return recv_len == HTTPD_SOCK_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
    }

    if (reader->parse(buffer.get(), recv_len) != static_cast<size_t>(recv_len)) {
      httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, nullptr);
      return ESP_FAIL;
    }

    remaining -= recv_len;
    bytes_since_yield += recv_len;

    if (bytes_since_yield > YIELD_INTERVAL_BYTES) {
      vTaskDelay(1);
      bytes_since_yield = 0;
    }
  }

  handler->handleRequest(&req);
  return ESP_OK;
}
#endif

}

#endif
