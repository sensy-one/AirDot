#pragma once
#ifdef USE_ESP32

#include <esp_http_server.h>
#include <string>
#include "esphome/core/helpers.h"

namespace esphome::web_server_idf {



size_t url_decode(char *str);

bool request_has_header(httpd_req_t *req, const char *name);
optional<std::string> request_get_header(httpd_req_t *req, const char *name);
optional<std::string> query_key_value(const char *query_url, size_t query_len, const char *key);
bool query_has_key(const char *query_url, size_t query_len, const char *key);


inline bool char_equals_ci(char a, char b) { return ::tolower(a) == ::tolower(b); }


bool str_ncmp_ci(const char *s1, const char *s2, size_t n);


const char *strcasestr_n(const char *haystack, size_t haystack_len, const char *needle);

}
#endif
