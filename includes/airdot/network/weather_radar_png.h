#pragma once
#include "weather_radar_data.h"
#include "weather_radar_palette.h"
#include <cstdlib>
#ifdef USE_ESP32
#include "esphome/components/weather_radar_png/pngle.h"
#else
#include <pngle.h>
#endif
#ifdef USE_ESP32
#include <esp_heap_caps.h>
#endif

namespace AirDot::weather_radar {
inline void *radar_allocate(size_t bytes) {
#ifdef USE_ESP32
  return heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
  return std::malloc(bytes);
#endif
}
inline void radar_free(void *p) { std::free(p); }

// Decodes directly to one byte per source pixel, with no RGBA image allocation.
// HTTP completion plus finish() (including IEND and every chunk CRC) is required.
class PngDecoder {
 public:
  explicit PngDecoder(uint8_t *pixels):pixels_(pixels) {
    png_=static_cast<pngle_t *>(radar_allocate(PNGLE_T_SIZE));
    if(!png_)return;std::memset(png_,0,PNGLE_T_SIZE);pngle_reset(png_);
    pngle_set_user_data(png_,this);pngle_set_init_callback(png_,init_);
    pngle_set_draw_callback(png_,draw_);pngle_set_done_callback(png_,done_);
    if(pixels_)std::memset(pixels_,0,TILE_SIZE*TILE_SIZE);
  }
  ~PngDecoder(){if(png_){pngle_reset(png_);radar_free(png_);}}
  PngDecoder(const PngDecoder &)=delete;PngDecoder &operator=(const PngDecoder &)=delete;
  bool feed(const uint8_t *data,size_t length) {
    if(!png_||!pixels_||bad_||(!data&&length)||received_+length>512*1024)return false;
    if(!structure_(data,length))return bad_=true,false;
    received_+=length;
    while(length){if(done_flag_)return bad_=true,false;
      const size_t n=std::min(length,sizeof(pending_)-used_);if(!n)return bad_=true,false;
      std::memcpy(pending_+used_,data,n);used_+=n;data+=n;length-=n;
      if(!header_checked_){if(used_<33)continue;
        static constexpr uint8_t sig[]={137,80,78,71,13,10,26,10};
        auto be=[](const uint8_t*p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};
        if(std::memcmp(pending_,sig,8)||be(pending_+8)!=13||std::memcmp(pending_+12,"IHDR",4)||
           be(pending_+16)!=TILE_SIZE||be(pending_+20)!=TILE_SIZE||!supported_(pending_[24],pending_[25])||pending_[26]||pending_[27]||pending_[28]!=0)return bad_=true,false;
        header_checked_=true;
      }
      int fed=pngle_feed(png_,pending_,used_);if(fed<0||bad_)return false;
      if(size_t(fed)>used_)return bad_=true,false;
      used_-=size_t(fed);if(used_)std::memmove(pending_,pending_+fed,used_);
      if(done_flag_&&used_)return bad_=true,false;
    }
    return true;
  }
  bool finish() const {return png_&&header_checked_&&initialized_&&done_flag_&&!bad_&&!used_&&end_seen_&&drawn_==TILE_SIZE*TILE_SIZE;}
 private:
  pngle_t *png_{nullptr};uint8_t *pixels_;bool bad_{false},initialized_{false},header_checked_{false},done_flag_{false};
  uint8_t pending_[4096]{};size_t used_{0},received_{0};uint32_t drawn_{0};
  uint8_t structure_header_[8]{};unsigned signature_left_{8},structure_used_{0};uint32_t structure_left_{0};bool structure_iend_{false},end_seen_{false};
  static bool supported_(uint8_t depth,uint8_t color){return (color==6&&depth==8)||(color==3&&(depth==1||depth==2||depth==4||depth==8));}
  bool structure_(const uint8_t *data,size_t length){
    while(length){if(end_seen_)return false;if(signature_left_){size_t n=std::min<size_t>(length,signature_left_);signature_left_-=n;data+=n;length-=n;continue;}
      if(structure_left_){size_t n=std::min<size_t>(length,structure_left_);structure_left_-=n;data+=n;length-=n;if(!structure_left_&&structure_iend_)end_seen_=true;continue;}
      structure_header_[structure_used_++]=*data++;--length;
      if(structure_used_==8){uint32_t n=uint32_t(structure_header_[0])<<24|uint32_t(structure_header_[1])<<16|uint32_t(structure_header_[2])<<8|structure_header_[3];
        if(n>512*1024)return false;structure_iend_=!std::memcmp(structure_header_+4,"IEND",4);if(structure_iend_&&n)return false;structure_left_=n+4;structure_used_=0;}
    }return true;
  }
  static void init_(pngle_t *p,uint32_t w,uint32_t h){auto &s=*static_cast<PngDecoder*>(pngle_get_user_data(p));s.initialized_=w==TILE_SIZE&&h==TILE_SIZE;if(!s.initialized_)s.bad_=true;}
  static void done_(pngle_t *p){static_cast<PngDecoder*>(pngle_get_user_data(p))->done_flag_=true;}
  static void draw_(pngle_t*p,uint32_t x,uint32_t y,uint32_t w,uint32_t h,const uint8_t rgba[4]) {
    auto &s=*static_cast<PngDecoder*>(pngle_get_user_data(p));if(s.bad_||!s.initialized_)return;
    if(x>=TILE_SIZE||y>=TILE_SIZE||w>TILE_SIZE-x||h>TILE_SIZE-y){s.bad_=true;return;}
    s.drawn_+=w*h;if(s.drawn_>TILE_SIZE*TILE_SIZE){s.bad_=true;return;}
    uint8_t level=0;if(!raw_intensity(rgba,level)){s.bad_=true;return;}
    for(uint32_t row=y;row<y+h;++row)std::memset(s.pixels_+row*TILE_SIZE+x,level,w);
  }
};
}  // namespace AirDot::weather_radar
