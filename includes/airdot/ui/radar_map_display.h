#pragma once

#include <cstdlib>
#include <cstdint>
#include <lvgl.h>
#include <src/misc/cache/instance/lv_image_cache.h>
#ifdef USE_ESP32
#include <esp_heap_caps.h>
#include "esphome/core/hal.h"
#endif

#include "radar_map_client.h"
#include "radar_map_raster.h"

namespace AirDot::radar_map {

struct RenderCache {
  uint16_t *pixels{nullptr};
  size_t allocated_pixels{0};
  lv_image_dsc_t image{};
  float latitude{0}, longitude{0};
  uint8_t range_km{0};
  int width{0}, height{0}, pixels_per_range{0}, clip_radius{0};
  bool dark{true}, valid{false};
  uint32_t revision{0}, source_revision{0};
};
inline RenderCache &render_cache(MapKind kind = MapKind::ROADS) {
  static RenderCache caches[MAP_KIND_COUNT];
  return caches[static_cast<size_t>(kind) < MAP_KIND_COUNT ? static_cast<size_t>(kind) : 0];
}

namespace map_render_detail {
struct Key {
  float latitude{0}, longitude{0};
  uint8_t range{0};
  int width{0}, height{0}, scale{0}, radius{0};
  bool dark{true};
};
inline bool same(const Key &a,const Key &b) {
  return coordinate_key(a.latitude)==coordinate_key(b.latitude) && coordinate_key(a.longitude)==coordinate_key(b.longitude) &&
      a.range==b.range && a.width==b.width && a.height==b.height && a.scale==b.scale && a.radius==b.radius && a.dark==b.dark;
}
inline bool matches_cache(const RenderCache &cache,const Key &key) {
  return cache.valid && coordinate_key(cache.latitude)==coordinate_key(key.latitude) &&
      coordinate_key(cache.longitude)==coordinate_key(key.longitude) && cache.range_km==key.range &&
      cache.width==key.width && cache.height==key.height && cache.pixels_per_range==key.scale &&
      cache.clip_radius==key.radius && cache.dark==key.dark;
}
struct Job {
  uint16_t *pixels{nullptr};
  Key key;
  uint32_t generation{0}, source_revision{0};
  size_t cleared{0}, segment{0};
  int land_row{0};
  bool line_active{false}, motorway{false};
  int x{0}, y{0}, x2{0}, y2{0}, dx{0}, dy{0}, sx{0}, sy{0}, error{0}, gray{0};
};
struct State {
  Key wanted;
  Job job;
  uint32_t generation{1};
  bool requested{false};
};
inline State &state(MapKind kind = MapKind::ROADS) {
  static State values[MAP_KIND_COUNT];
  return values[static_cast<size_t>(kind) < MAP_KIND_COUNT ? static_cast<size_t>(kind) : 0];
}
inline void cancel_job(MapKind kind) { auto &job=state(kind).job;std::free(job.pixels);job=Job{}; }
inline uint32_t now_micros() {
#ifdef USE_ESP32
  return esphome::micros();
#else
  return 0;
#endif
}
inline bool expired(uint32_t started) { return uint32_t(now_micros()-started)>=2000; }
inline void plot(Job &job,int x,int y) {
  const auto &key=job.key;
  if(x<0||y<0||x>=key.width||y>=key.height)return;
  const int dx=x-key.width/2,dy=y-key.height/2,distance=dx*dx+dy*dy,radius=key.radius*key.radius;
  if(distance>radius)return;
  int gray=job.gray;
  const int fade_radius=key.radius*FADE_START_PERCENT/100,fade_start=fade_radius*fade_radius;
  if(distance>fade_start) {
    const int weight=std::max(0,radius-distance),span=std::max(1,radius-fade_start);
    gray=key.dark?gray*weight/span:255-(255-gray)*weight/span;
  }
  auto &pixel=job.pixels[static_cast<size_t>(y)*key.width+x];
  const uint16_t color=map_gray_rgb565(static_cast<uint8_t>(gray),x,y);
  pixel=key.dark?std::max(pixel,color):std::min(pixel,color);
}
inline void begin_line(Job &job,const Segment &segment) {
  const auto &key=job.key;
  job.x=key.width/2+static_cast<int>(std::lround(double(segment.x1)*key.scale/NORMALIZED_RADIUS));
  job.y=key.height/2+static_cast<int>(std::lround(double(segment.y1)*key.scale/NORMALIZED_RADIUS));
  job.x2=key.width/2+static_cast<int>(std::lround(double(segment.x2)*key.scale/NORMALIZED_RADIUS));
  job.y2=key.height/2+static_cast<int>(std::lround(double(segment.y2)*key.scale/NORMALIZED_RADIUS));
  job.dx=std::abs(job.x2-job.x);job.sx=job.x<job.x2?1:-1;
  job.dy=-std::abs(job.y2-job.y);job.sy=job.y<job.y2?1:-1;job.error=job.dx+job.dy;
  const auto style=segment.style;job.motorway=style==Style::MOTORWAY;
  job.gray=key.dark?(style==Style::MOTORWAY?72:style==Style::MAJOR?52:style==Style::MINOR?35:
                     style==Style::COAST?80:style==Style::RIVER?44:28):
                    (style==Style::MOTORWAY?181:style==Style::MAJOR?202:style==Style::MINOR?221:
                     style==Style::COAST?170:style==Style::RIVER?210:229);
  job.line_active=true;
}
// Land is everything inside the scope that the water mask does not cover.
// Kept close to the background so the coastline still reads first.
inline constexpr uint8_t LAND_GRAY_DARK = 34;
inline constexpr uint8_t LAND_GRAY_LIGHT = 238;
inline bool fill_land_slice(Job &job,const MapData &map,uint32_t started) {
  if(!map.has_water_mask)return true;
  const auto &key=job.key;
  const int cx=key.width/2,cy=key.height/2,radius=key.radius*key.radius;
  const int fade_radius=key.radius*FADE_START_PERCENT/100,fade_start=fade_radius*fade_radius;
  const int span=std::max(1,radius-fade_start);
  const int base=key.dark?LAND_GRAY_DARK:LAND_GRAY_LIGHT;
  while(job.land_row<key.height) {
    if(expired(started))return false;
    const int y=job.land_row++,dy=y-cy;
    if(dy*dy>radius)continue;
    const int cell_y=dy*MASK_PIXELS_PER_RANGE/key.scale+MASK_RADIUS;
    uint16_t *row=job.pixels+static_cast<size_t>(y)*key.width;
    for(int x=0;x<key.width;++x) {
      const int dx=x-cx,distance=dx*dx+dy*dy;
      if(distance>radius)continue;
      if(water_at(map,dx*MASK_PIXELS_PER_RANGE/key.scale+MASK_RADIUS,cell_y))continue;
      int gray=base;
      if(distance>fade_start) {
        const int weight=std::max(0,radius-distance);
        gray=key.dark?gray*weight/span:255-(255-gray)*weight/span;
      }
      row[x]=map_gray_rgb565(static_cast<uint8_t>(gray),x,y);
    }
  }
  return true;
}
inline bool draw_slice(Job &job,const MapData &map,uint32_t started) {
  const size_t count=std::min<size_t>(map.count,MAX_SEGMENTS);
  unsigned completed=0,pixels=0;
  while(completed<32&&pixels<2048&&!expired(started)) {
    if(!job.line_active) {
      if(job.segment>=count)return true;
      const auto &segment=map.segments[job.segment++];
      if(static_cast<uint8_t>(segment.style)>static_cast<uint8_t>(Style::RIVER)){++completed;continue;}
      begin_line(job,segment);
    }
    plot(job,job.x,job.y);
    if(job.motorway) {
      if(job.dx>=-job.dy)plot(job,job.x,job.y+1);
      else plot(job,job.x+1,job.y);
    }
    if(job.x==job.x2&&job.y==job.y2) {job.line_active=false;++completed;}
    else {
      const int twice_error=2*job.error;
      if(twice_error>=job.dy){job.error+=job.dy;job.x+=job.sx;}
      if(twice_error<=job.dx){job.error+=job.dx;job.y+=job.sy;}
    }
    ++pixels;
  }
  return !job.line_active&&job.segment>=count;
}
}  // namespace map_render_detail

inline bool prepare_map(const lv_area_t &area,float latitude,float longitude,
                        uint8_t range_km,int pixels_per_range,int clip_radius,bool dark,
                        MapKind kind=MapKind::ROADS) {
  const int width=area.x2-area.x1+1,height=area.y2-area.y1+1;
  if(!valid_coordinates(latitude,longitude)||width<=0||height<=0||width>640||height>640||
      pixels_per_range<=0||pixels_per_range>640||clip_radius<=0||clip_radius>640)return false;
  const map_render_detail::Key key{latitude,longitude,range_km,width,height,pixels_per_range,clip_radius,dark};
  auto &state=map_render_detail::state(kind);
  if(!state.requested||!map_render_detail::same(state.wanted,key)) {state.wanted=key;state.requested=true;++state.generation;}
  return map_render_detail::matches_cache(render_cache(kind),key);
}

// Call from the main loop. DRAW only queues requests and consumes complete images.
inline bool service_map_render(MapKind kind=MapKind::ROADS) {
  using namespace map_render_detail;
  auto &request=state(kind);auto &cache=render_cache(kind);auto &job=request.job;
  if(!request.requested)return false;
  const uint32_t source=map_revision(kind);
  if(matches_cache(cache,request.wanted)&&cache.source_revision==source) {
    if(job.pixels)cancel_job(kind);
    return false;
  }
  if(job.pixels&&(job.generation!=request.generation||job.source_revision!=source||!same(job.key,request.wanted)))cancel_job(kind);
  if(!job.pixels) {
    if(!with_map(request.wanted.latitude,request.wanted.longitude,request.wanted.range,[](const MapData&){},kind))return false;
    const size_t count=static_cast<size_t>(request.wanted.width)*request.wanted.height;
#ifdef USE_ESP32
    job.pixels=static_cast<uint16_t*>(heap_caps_malloc(count*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#else
    job.pixels=static_cast<uint16_t*>(std::malloc(count*sizeof(uint16_t)));
#endif
    if(!job.pixels)return false;
    job.key=request.wanted;job.generation=request.generation;job.source_revision=source;
  }
  const uint32_t started=now_micros();
  const size_t count=static_cast<size_t>(job.key.width)*job.key.height;
  size_t cleared=0;
  while(job.cleared<count&&cleared<4096&&!expired(started)) {
    const size_t block=std::min<size_t>(256,count-job.cleared);
    std::fill_n(job.pixels+job.cleared,block,job.key.dark?0x0000:0xFFFF);
    job.cleared+=block;cleared+=block;
  }
  if(job.cleared<count||expired(started))return false;
  bool complete=false;
  if(!with_map(job.key.latitude,job.key.longitude,job.key.range,[&](const MapData &map){
        if(fill_land_slice(job,map,started))complete=draw_slice(job,map,started);
      },kind))return false;
  if(!complete)return false;
  if(job.generation!=request.generation||job.source_revision!=map_revision(kind)) {cancel_job(kind);return false;}
  lv_image_cache_drop(&cache.image);
  auto *previous=cache.pixels;cache.pixels=job.pixels;job.pixels=nullptr;
  cache.allocated_pixels=count;
  cache.image={};cache.image.header.magic=LV_IMAGE_HEADER_MAGIC;cache.image.header.cf=LV_COLOR_FORMAT_RGB565;
  cache.image.header.w=job.key.width;cache.image.header.h=job.key.height;cache.image.header.stride=job.key.width*sizeof(uint16_t);
  cache.image.data_size=count*sizeof(uint16_t);cache.image.data=reinterpret_cast<const uint8_t*>(cache.pixels);
  cache.latitude=job.key.latitude;cache.longitude=job.key.longitude;cache.range_km=job.key.range;
  cache.width=job.key.width;cache.height=job.key.height;cache.pixels_per_range=job.key.scale;cache.clip_radius=job.key.radius;cache.dark=job.key.dark;
  cache.source_revision=job.source_revision;cache.valid=true;++cache.revision;
  job=Job{};std::free(previous);
  return true;
}

inline bool draw_map(lv_layer_t *layer,const lv_area_t &area,float latitude,float longitude,
                     uint8_t range_km,int pixels_per_range,int clip_radius,bool dark,
                     MapKind kind=MapKind::ROADS) {
  if(!layer||!prepare_map(area,latitude,longitude,range_km,pixels_per_range,clip_radius,dark,kind))return false;
  lv_draw_image_dsc_t image;lv_draw_image_dsc_init(&image);image.src=&render_cache(kind).image;image.opa=LV_OPA_COVER;
  lv_draw_image(layer,&image,&area);return true;
}

// Frees a kind's rendered image and any job in progress, e.g. after its page
// has been hidden for a while. The next prepare_map() renders it again.
inline void release_render(MapKind kind) {
  using namespace map_render_detail;
  auto &cache=render_cache(kind);auto &request=state(kind);
  if(request.job.pixels)cancel_job(kind);
  request.requested=false;
  if(!cache.pixels)return;
  lv_image_cache_drop(&cache.image);
  std::free(cache.pixels);
  const uint32_t revision=cache.revision+1;
  cache=RenderCache{};cache.revision=revision;
}
inline bool render_requested(MapKind kind) { return map_render_detail::state(kind).requested; }

}  // namespace AirDot::radar_map
