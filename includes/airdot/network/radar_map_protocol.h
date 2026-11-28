#pragma once

#include "radar_map_data.h"
#include <algorithm>
#include <cstring>

namespace AirDot::radar_map::protocol {

// ADMP v1 is independent of C++ struct layout and host endianness. All integers
// are little endian. The 48-byte header is followed by count 9-byte segments.
//
//  0 magic "ADMP"       4 version u8        5 header length u8
//  6 range km u8        7 flags u8          8 latitude key i32
// 12 longitude key i32 16 segment count u32 20 format revision u32
// 24 created Unix u64  32 payload bytes u32 36 CRC32 u32
// 40 reserved zero bytes (8)
//
// With FLAG_WATER_MASK the segments are followed by MASK_BYTES of water mask
// (row-major, LSB first). Marine maps always carry it.
// A segment is x1,y1,x2,y2 as signed LE16, then Style as u8. Coordinates retain
// the firmware's projection: 10000 = configured range, x east, y south.
// CRC32/IEEE (reflected polynomial EDB88320, initial/final XOR FFFFFFFF) covers
// header bytes [0,36), [40,48), then every payload byte. The CRC field is skipped.
inline constexpr size_t HEADER_SIZE = 48;
inline constexpr size_t SEGMENT_SIZE = 9;
inline constexpr size_t MAX_SEGMENT_COUNT = 16384;
inline constexpr size_t MAX_ENCODED_SIZE = HEADER_SIZE + MAX_SEGMENT_COUNT * SEGMENT_SIZE + MASK_BYTES;
inline constexpr uint8_t VERSION = 1;
inline constexpr uint32_t FORMAT_REVISION = 2;
inline constexpr uint8_t FLAG_COARSE = 1;
inline constexpr uint8_t FLAG_TRUNCATED = 2;
inline constexpr uint8_t FLAG_MARINE = 4;  // Coastline map rather than roads.
inline constexpr uint8_t FLAG_WATER_MASK = 8;  // Water mask follows the segments.
inline constexpr uint8_t VALID_FLAGS = FLAG_COARSE | FLAG_TRUNCATED | FLAG_MARINE | FLAG_WATER_MASK;

struct Metadata {
  int32_t latitude_key{0}, longitude_key{0};
  uint8_t range_km{0}, flags{0};
  uint32_t count{0}, format_revision{0}, payload_length{0}, crc32{0};
  uint64_t created_unix{0};  // Zero means the creation time is unknown.
};

enum class DecodeError : uint8_t {
  NONE, ARGUMENT, HEADER, VERSION, REVISION, FLAGS, RESERVED, RANGE,
  COORDINATE_KEY, COUNT, LENGTH, KEY_MISMATCH, SEGMENT, CRC
};

namespace detail {
inline bool valid_range(uint8_t range) { return range >= 5 && range <= 50 && range % 5 == 0; }
inline bool valid_keys(int32_t latitude, int32_t longitude) {
  return latitude >= -9000000 && latitude <= 9000000 && longitude >= -18000000 && longitude <= 18000000;
}
inline void put16(uint8_t *out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value); out[1] = static_cast<uint8_t>(value >> 8);
}
inline void put32(uint8_t *out, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(value >> (i * 8));
}
inline void put64(uint8_t *out, uint64_t value) {
  for (unsigned i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>(value >> (i * 8));
}
inline uint16_t get16(const uint8_t *in) { return static_cast<uint16_t>(static_cast<unsigned>(in[0]) | static_cast<unsigned>(in[1]) << 8); }
inline uint32_t get32(const uint8_t *in) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) value |= static_cast<uint32_t>(in[i]) << (i * 8);
  return value;
}
inline uint64_t get64(const uint8_t *in) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) value |= static_cast<uint64_t>(in[i]) << (i * 8);
  return value;
}
inline int16_t signed16(uint16_t value) {
  return static_cast<int16_t>(value <= 0x7fffU ? static_cast<int32_t>(value) : static_cast<int32_t>(value) - 0x10000);
}
inline int32_t signed32(uint32_t value) {
  return static_cast<int32_t>(value <= 0x7fffffffU ? static_cast<int64_t>(value) : static_cast<int64_t>(value) - 0x100000000LL);
}
inline uint32_t crc_update(uint32_t crc, const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
  }
  return crc;
}
inline uint32_t header_crc(const uint8_t *header) {
  return crc_update(crc_update(0xffffffffU, header, 36), header + 40, HEADER_SIZE - 40);
}
inline bool valid_segment(const Segment &segment) {
  return segment.x1 >= -COVERAGE_RADIUS && segment.x1 <= COVERAGE_RADIUS &&
         segment.y1 >= -COVERAGE_RADIUS && segment.y1 <= COVERAGE_RADIUS &&
         segment.x2 >= -COVERAGE_RADIUS && segment.x2 <= COVERAGE_RADIUS &&
         segment.y2 >= -COVERAGE_RADIUS && segment.y2 <= COVERAGE_RADIUS &&
         static_cast<uint8_t>(segment.style) <= static_cast<uint8_t>(Style::RIVER);
}
inline void put_segment(uint8_t *out, const Segment &segment) {
  put16(out, static_cast<uint16_t>(segment.x1)); put16(out + 2, static_cast<uint16_t>(segment.y1));
  put16(out + 4, static_cast<uint16_t>(segment.x2)); put16(out + 6, static_cast<uint16_t>(segment.y2));
  out[8] = static_cast<uint8_t>(segment.style);
}
inline Segment get_segment(const uint8_t *in) {
  return Segment{signed16(get16(in)), signed16(get16(in + 2)), signed16(get16(in + 4)),
                 signed16(get16(in + 6)), static_cast<Style>(in[8])};
}
inline DecodeError parse_header(const uint8_t *header, Metadata &metadata) {
  if (std::memcmp(header, "ADMP", 4) || header[5] != HEADER_SIZE) return DecodeError::HEADER;
  if (header[4] != VERSION) return DecodeError::VERSION;
  metadata.range_km = header[6]; metadata.flags = header[7];
  metadata.latitude_key = detail::signed32(detail::get32(header + 8));
  metadata.longitude_key = detail::signed32(detail::get32(header + 12));
  metadata.count = detail::get32(header + 16); metadata.format_revision = detail::get32(header + 20);
  metadata.created_unix = detail::get64(header + 24); metadata.payload_length = detail::get32(header + 32);
  metadata.crc32 = detail::get32(header + 36);
  if (metadata.format_revision != FORMAT_REVISION) return DecodeError::REVISION;
  if (metadata.flags & ~VALID_FLAGS || metadata.flags & FLAG_TRUNCATED) return DecodeError::FLAGS;
  for (size_t i = 40; i < HEADER_SIZE; ++i) if (header[i]) return DecodeError::RESERVED;
  if (!detail::valid_range(metadata.range_km)) return DecodeError::RANGE;
  if (!detail::valid_keys(metadata.latitude_key, metadata.longitude_key)) return DecodeError::COORDINATE_KEY;
  if (metadata.count > MAX_SEGMENT_COUNT || metadata.count > MAX_SEGMENTS) return DecodeError::COUNT;
  const size_t mask_bytes = (metadata.flags & FLAG_WATER_MASK) ? MASK_BYTES : 0;
  if (metadata.payload_length != metadata.count * SEGMENT_SIZE + mask_bytes) return DecodeError::LENGTH;
  return DecodeError::NONE;
}
}  // namespace detail

// Header inspection alone does not authenticate a complete file. Cache/network
// readers must still validate the exact body length and Decoder::finish().
inline bool inspect_header(const uint8_t *header, size_t length, Metadata &metadata) {
  return header && length >= HEADER_SIZE && detail::parse_header(header, metadata) == DecodeError::NONE;
}

// The output header includes the complete payload checksum without relying on
// packed structs or native endianness.
inline bool make_header(const MapData &map, uint64_t created_unix, uint8_t *header) {
  if (!header || !valid_coordinates(map.center_latitude, map.center_longitude) ||
      !detail::valid_range(map.range_km) || map.truncated || map.count > MAX_SEGMENT_COUNT || map.count > MAX_SEGMENTS) return false;
  for (size_t i = 0; i < map.count; ++i) if (!detail::valid_segment(map.segments[i])) return false;
  std::memset(header, 0, HEADER_SIZE);
  std::memcpy(header, "ADMP", 4);
  header[4] = VERSION; header[5] = HEADER_SIZE; header[6] = map.range_km;
  header[7] = (map.coarse ? FLAG_COARSE : 0) | (map.truncated ? FLAG_TRUNCATED : 0) |
              (map.kind == MapKind::MARINE ? FLAG_MARINE : 0) | (map.has_water_mask ? FLAG_WATER_MASK : 0);
  detail::put32(header + 8, static_cast<uint32_t>(coordinate_key(map.center_latitude)));
  detail::put32(header + 12, static_cast<uint32_t>(coordinate_key(map.center_longitude)));
  detail::put32(header + 16, map.count); detail::put32(header + 20, FORMAT_REVISION);
  detail::put64(header + 24, created_unix);
  detail::put32(header + 32, static_cast<uint32_t>(map.count * SEGMENT_SIZE + (map.has_water_mask ? MASK_BYTES : 0)));
  uint32_t crc = detail::header_crc(header);
  uint8_t segment[SEGMENT_SIZE];
  for (size_t i = 0; i < map.count; ++i) {
    detail::put_segment(segment, map.segments[i]); crc = detail::crc_update(crc, segment, SEGMENT_SIZE);
  }
  if (map.has_water_mask) crc = detail::crc_update(crc, map.water_mask, MASK_BYTES);
  detail::put32(header + 36, crc ^ 0xffffffffU);
  return true;
}

template<typename Writer> bool encode(const MapData &map, uint64_t created_unix, Writer writer) {
  uint8_t header[HEADER_SIZE];
  if (!make_header(map, created_unix, header) || !writer(header, HEADER_SIZE)) return false;
  uint8_t chunk[SEGMENT_SIZE * 32];
  size_t used = 0;
  for (size_t i = 0; i < map.count; ++i) {
    detail::put_segment(chunk + used, map.segments[i]); used += SEGMENT_SIZE;
    if (used == sizeof(chunk)) { if (!writer(chunk, used)) return false; used = 0; }
  }
  if (used && !writer(chunk, used)) return false;
  return !map.has_water_mask || writer(map.water_mask, MASK_BYTES);
}

// The caller owns the unpublished MapData, normally allocated in PSRAM. Payload
// bytes stream directly into its segment array. Until finish() validates the
// entire body, map.count remains zero; partial or damaged data cannot be drawn.
// map.kind must be set by the caller; a blob of the other kind is a key mismatch.
class Decoder {
 public:
  Decoder(MapData &map, float latitude, float longitude, uint8_t range)
      : map_(map), latitude_(latitude), longitude_(longitude), range_(range) {
    map_.count = 0;
    if (!valid_coordinates(latitude, longitude) || !detail::valid_range(range)) error_ = DecodeError::ARGUMENT;
    else { latitude_key_ = coordinate_key(latitude); longitude_key_ = coordinate_key(longitude); }
  }
  bool feed(const uint8_t *data, size_t length) {
    if (error_ != DecodeError::NONE) return false;
    if ((length && !data) || length > MAX_ENCODED_SIZE - header_received_ - payload_received_) return fail_(DecodeError::LENGTH);
    if (finished_) return !length || fail_(DecodeError::LENGTH);
    if (header_received_ < HEADER_SIZE) {
      const size_t count = std::min(length, HEADER_SIZE - header_received_);
      if (count) std::memcpy(header_ + header_received_, data, count);
      header_received_ += count; if (count) data += count; length -= count;
      if (header_received_ == HEADER_SIZE && !read_header_()) return false;
    }
    if (!length) return true;
    if (length > metadata_.payload_length - payload_received_) return fail_(DecodeError::LENGTH);
    crc_ = detail::crc_update(crc_, data, length);
    while (length) {
      if (decoded_ >= metadata_.count && !segment_received_) {
        // Segments complete: the rest is the water mask.
        const size_t count = std::min(length, MASK_BYTES - mask_received_);
        if (!(metadata_.flags & FLAG_WATER_MASK) || !count) return fail_(DecodeError::LENGTH);
        std::memcpy(map_.water_mask + mask_received_, data, count);
        mask_received_ += count; payload_received_ += count; data += count; length -= count;
        continue;
      }
      const size_t count = std::min(length, SEGMENT_SIZE - segment_received_);
      std::memcpy(segment_ + segment_received_, data, count);
      segment_received_ += count; payload_received_ += count; data += count; length -= count;
      if (segment_received_ == SEGMENT_SIZE) {
        const Segment segment = detail::get_segment(segment_);
        if (!detail::valid_segment(segment) || decoded_ >= metadata_.count || decoded_ >= MAX_SEGMENTS) return fail_(DecodeError::SEGMENT);
        map_.segments[decoded_++] = segment; segment_received_ = 0;
      }
    }
    return true;
  }
  bool finish() {
    if (error_ != DecodeError::NONE) return false;
    if (finished_) return true;
    if (header_received_ != HEADER_SIZE || payload_received_ != metadata_.payload_length ||
        segment_received_ || decoded_ != metadata_.count) return fail_(DecodeError::LENGTH);
    if ((crc_ ^ 0xffffffffU) != metadata_.crc32) return fail_(DecodeError::CRC);
    map_.center_latitude = latitude_; map_.center_longitude = longitude_; map_.range_km = range_;
    map_.coarse = metadata_.flags & FLAG_COARSE; map_.truncated = metadata_.flags & FLAG_TRUNCATED;
    map_.has_water_mask = metadata_.flags & FLAG_WATER_MASK;
    map_.count = static_cast<uint16_t>(decoded_); finished_ = true;
    return true;
  }
  DecodeError error() const { return error_; }
  const Metadata &metadata() const { return metadata_; }
 private:
  MapData &map_;
  float latitude_, longitude_;
  uint8_t range_;
  int32_t latitude_key_{0}, longitude_key_{0};
  Metadata metadata_{};
  uint8_t header_[HEADER_SIZE]{}, segment_[SEGMENT_SIZE]{};
  size_t header_received_{0}, payload_received_{0}, segment_received_{0}, mask_received_{0};
  uint32_t decoded_{0}, crc_{0xffffffffU};
  DecodeError error_{DecodeError::NONE};
  bool finished_{false};

  bool fail_(DecodeError error) { error_ = error; map_.count = 0; return false; }
  bool read_header_() {
    const DecodeError header_error = detail::parse_header(header_, metadata_);
    if (header_error != DecodeError::NONE) return fail_(header_error);
    if (metadata_.latitude_key != latitude_key_ || metadata_.longitude_key != longitude_key_ || metadata_.range_km != range_ ||
        ((metadata_.flags & FLAG_MARINE) != 0) != (map_.kind == MapKind::MARINE))
      return fail_(DecodeError::KEY_MISMATCH);
    if (metadata_.count > segment_capacity(map_)) return fail_(DecodeError::COUNT);
    // Marine maps cached before land shading have no mask; fetch them again.
    if (map_.kind == MapKind::MARINE && !(metadata_.flags & FLAG_WATER_MASK)) return fail_(DecodeError::KEY_MISMATCH);
    crc_ = detail::header_crc(header_);
    return true;
  }
};

}  // namespace AirDot::radar_map::protocol
