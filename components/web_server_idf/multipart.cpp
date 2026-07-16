#include "esphome/core/defines.h"
#if defined(USE_ESP32) && defined(USE_WEBSERVER_OTA)
#include "multipart.h"
#include "utils.h"
#include <cstring>
#include "multipart_parser.h"

namespace esphome::web_server_idf {

MultipartReader::MultipartReader(const std::string &boundary) {

  memset(&settings_, 0, sizeof(settings_));
  settings_.on_header_field = on_header_field;
  settings_.on_header_value = on_header_value;
  settings_.on_part_data = on_part_data;
  settings_.on_part_data_end = on_part_data_end;


  parser_ = multipart_parser_init(boundary.c_str(), &settings_);
  if (parser_) {
    multipart_parser_set_data(parser_, this);
  } else {
  }
}

MultipartReader::~MultipartReader() {
  if (parser_) {
    multipart_parser_free(parser_);
  }
}

size_t MultipartReader::parse(const char *data, size_t len) {
  if (!parser_) {
    return 0;
  }

  size_t parsed = multipart_parser_execute(parser_, data, len);

  if (parsed != len) {
  }

  return parsed;
}

void MultipartReader::process_header_(const char *value, size_t length) {

  const char *field = current_header_field_.c_str();
  size_t field_len = current_header_field_.length();

  if (str_startswith_case_insensitive(field, field_len, "content-disposition")) {

    extract_header_param(value, length, "name", current_part_.name);
    extract_header_param(value, length, "filename", current_part_.filename);
  } else if (str_startswith_case_insensitive(field, field_len, "content-type")) {
    str_trim(value, length, current_part_.content_type);
  }


  current_header_field_.clear();
}

int MultipartReader::on_header_field(multipart_parser *parser, const char *at, size_t length) {
  MultipartReader *reader = static_cast<MultipartReader *>(multipart_parser_get_data(parser));
  reader->current_header_field_.assign(at, length);
  return 0;
}

int MultipartReader::on_header_value(multipart_parser *parser, const char *at, size_t length) {
  MultipartReader *reader = static_cast<MultipartReader *>(multipart_parser_get_data(parser));
  reader->process_header_(at, length);
  return 0;
}

int MultipartReader::on_part_data(multipart_parser *parser, const char *at, size_t length) {
  MultipartReader *reader = static_cast<MultipartReader *>(multipart_parser_get_data(parser));

  if (reader->has_file() && reader->data_callback_) {




    reader->data_callback_(reinterpret_cast<const uint8_t *>(at), length);
  }
  return 0;
}

int MultipartReader::on_part_data_end(multipart_parser *parser) {
  MultipartReader *reader = static_cast<MultipartReader *>(multipart_parser_get_data(parser));
  if (reader->part_complete_callback_) {
    reader->part_complete_callback_();
  }

  reader->current_part_ = Part{};
  return 0;
}




bool str_startswith_case_insensitive(const char *str, size_t str_len, const char *prefix) {
  size_t prefix_len = strlen(prefix);
  if (str_len < prefix_len) {
    return false;
  }
  return str_ncmp_ci(str, prefix, prefix_len);
}




void extract_header_param(const char *header, size_t header_len, const char *param, std::string &out) {
  size_t param_len = strlen(param);
  size_t search_pos = 0;

  while (search_pos < header_len) {

    const char *found = strcasestr_n(header + search_pos, header_len - search_pos, param);
    if (!found) {
      out.clear();
      return;
    }
    size_t pos = found - header;


    if (pos > 0 && header[pos - 1] != ' ' && header[pos - 1] != ';' && header[pos - 1] != '\t') {
      search_pos = pos + 1;
      continue;
    }


    pos += param_len;


    while (pos < header_len && (header[pos] == ' ' || header[pos] == '\t')) {
      pos++;
    }

    if (pos >= header_len || header[pos] != '=') {
      search_pos = pos;
      continue;
    }

    pos++;


    while (pos < header_len && (header[pos] == ' ' || header[pos] == '\t')) {
      pos++;
    }

    if (pos >= header_len) {
      out.clear();
      return;
    }


    if (header[pos] == '"') {
      pos++;
      const char *end = static_cast<const char *>(memchr(header + pos, '"', header_len - pos));
      if (end) {
        out.assign(header + pos, end - (header + pos));
        return;
      }

      out.clear();
      return;
    }


    size_t end = pos;
    while (end < header_len && header[end] != ';' && header[end] != ',' && header[end] != ' ' && header[end] != '\t') {
      end++;
    }

    out.assign(header + pos, end - pos);
    return;
  }

  out.clear();
}




bool parse_multipart_boundary(const char *content_type, const char **boundary_start, size_t *boundary_len) {
  if (!content_type) {
    return false;
  }

  size_t content_type_len = strlen(content_type);


  if (!strcasestr_n(content_type, content_type_len, "multipart/form-data")) {
    return false;
  }


  const char *b = strcasestr_n(content_type, content_type_len, "boundary=");
  if (!b) {
    return false;
  }

  const char *start = b + 9;


  while (*start == ' ' || *start == '\t') {
    start++;
  }

  if (!*start) {
    return false;
  }


  const char *end = start;
  if (*end == '"') {

    start++;
    end++;
    while (*end && *end != '"') {
      end++;
    }
    *boundary_len = end - start;
  } else {

    while (*end && *end != ' ' && *end != ';' && *end != '\r' && *end != '\n' && *end != '\t') {
      end++;
    }
    *boundary_len = end - start;
  }

  if (*boundary_len == 0) {
    return false;
  }

  *boundary_start = start;

  return true;
}


void str_trim(const char *str, size_t len, std::string &out) {
  const char *start = str;
  const char *end = str + len;
  while (start < end && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n'))
    start++;
  while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
    end--;
  out.assign(start, end - start);
}

}
#endif
