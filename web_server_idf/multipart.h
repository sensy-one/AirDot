#pragma once
#include "esphome/core/defines.h"
#if defined(USE_ESP32) && defined(USE_WEBSERVER_OTA)

#include <cctype>
#include <cstring>
#include <esp_http_server.h>
#include <functional>
#include <multipart_parser.h>
#include <string>
#include <utility>

namespace esphome::web_server_idf {


class MultipartReader {
 public:
  struct Part {
    std::string name;
    std::string filename;
    std::string content_type;
  };






  using DataCallback = std::function<void(const uint8_t *data, size_t len)>;
  using PartCompleteCallback = std::function<void()>;

  explicit MultipartReader(const std::string &boundary);
  ~MultipartReader();


  void set_data_callback(DataCallback &&callback) { data_callback_ = std::move(callback); }
  void set_part_complete_callback(PartCompleteCallback &&callback) { part_complete_callback_ = std::move(callback); }


  size_t parse(const char *data, size_t len);


  const Part &get_current_part() const { return current_part_; }


  bool has_file() const { return !current_part_.filename.empty(); }

 private:
  static int on_header_field(multipart_parser *parser, const char *at, size_t length);
  static int on_header_value(multipart_parser *parser, const char *at, size_t length);
  static int on_part_data(multipart_parser *parser, const char *at, size_t length);
  static int on_part_data_end(multipart_parser *parser);

  multipart_parser *parser_{nullptr};
  multipart_parser_settings settings_{};

  Part current_part_;
  std::string current_header_field_;

  DataCallback data_callback_;
  PartCompleteCallback part_complete_callback_;

  void process_header_(const char *value, size_t length);
};




bool str_startswith_case_insensitive(const char *str, size_t str_len, const char *prefix);




void extract_header_param(const char *header, size_t header_len, const char *param, std::string &out);




bool parse_multipart_boundary(const char *content_type, const char **boundary_start, size_t *boundary_len);


void str_trim(const char *str, size_t len, std::string &out);

}
#endif
