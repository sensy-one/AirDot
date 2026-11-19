#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace AirDot::weather_radar {
inline constexpr int WIDTH = 480, HEIGHT = 480;
inline constexpr int PIXELS_PER_RANGE = 122, CLIP_RADIUS = 222;
inline constexpr size_t PIXEL_COUNT = size_t(WIDTH) * HEIGHT;
inline constexpr unsigned MAX_FRAMES = 6, TILE_SIZE = 512;
inline constexpr double PI = 3.14159265358979323846, EARTH_KM = 6371.0, MERCATOR_LIMIT = 85.0511287798066;
inline constexpr uint32_t VALID_CLOCK = 1609459200;
enum class Status : uint8_t { EMPTY, LOADING, READY, FAILED, OFFLINE, UNAVAILABLE };
struct Frame { uint32_t valid_time{0}; bool forecast{false}; uint8_t *intensity{nullptr}; };
struct RadarData {
  float center_latitude{0}, center_longitude{0};
  uint8_t range_km{0}, frame_count{0};
  uint32_t generated_time{0}, observed_time{0}, fetched_ms{0};
  uint8_t *coverage{nullptr};
  Frame frames[MAX_FRAMES]{};
};
inline bool valid_location(double lat, double lon, uint8_t range) {
  return std::isfinite(lat) && std::isfinite(lon) && std::fabs(lat) <= MERCATOR_LIMIT &&
         lon >= -180 && lon <= 180 && range >= 5 && range <= 50 && range % 5 == 0;
}
inline int32_t coordinate_key(double value) { return static_cast<int32_t>(std::llround(value * 100000.0)); }
inline bool matches(const RadarData &data, float lat, float lon, uint8_t range) {
  return valid_location(lat,lon,range) && data.range_km == range &&
         coordinate_key(data.center_latitude) == coordinate_key(lat) && coordinate_key(data.center_longitude) == coordinate_key(lon);
}
inline uint64_t reference_time(const RadarData &data,uint64_t unix_now,uint32_t monotonic_now) {
  return unix_now>=VALID_CLOCK?unix_now:data.generated_time>=VALID_CLOCK?
      uint64_t(data.generated_time)+uint32_t(monotonic_now-data.fetched_ms)/1000:0;
}
inline bool frame_current(const Frame &frame, uint64_t now) {
  return frame.valid_time && (!frame.forecast || (now >= VALID_CLOCK && frame.valid_time > now && frame.valid_time <= now + 3600));
}
inline bool future_available(const RadarData &data, uint64_t now) {
  for (unsigned i=0;i<data.frame_count;++i) if (data.frames[i].forecast && frame_current(data.frames[i],now)) return true;
  return false;
}
inline bool is_stale(const RadarData &data, uint64_t now) {
  return now < VALID_CLOCK || !data.generated_time || !data.observed_time ||
      data.generated_time > now + 300 || data.observed_time > now ||
      now > uint64_t(data.generated_time) + 1200 || now > uint64_t(data.observed_time) + 1800 || !future_available(data,now);
}
inline bool center_covered(const RadarData &data) {
  return data.coverage && data.coverage[(HEIGHT/2)*WIDTH + WIDTH/2] != 0;
}
inline double mercator_x(double lon) { return (lon + 180.0) / 360.0; }
inline double mercator_y(double lat) {
  return (1.0 - std::asinh(std::tan(std::clamp(lat,-MERCATOR_LIMIT,MERCATOR_LIMIT)*PI/180.0))/PI)*0.5;
}

class Sampling {
 public:
  Sampling(double lat,double lon,uint8_t range) : lat_(lat),lon_(lon),range_(range),
      sin_lat_(std::sin(lat*PI/180)),cos_lat_(std::cos(lat*PI/180)) {
    double longitude=coordinate_key(lon)/100000.0;if(longitude>=180)longitude-=360;
    source_x_=mercator_x(longitude);source_y_=mercator_y(coordinate_key(lat)/100000.0);
  }
  bool location(int x,int y,double &lat,double &lon) const {
    const double east=x-WIDTH/2.0,north=HEIGHT/2.0-y,rho=std::hypot(east,north);
    if (rho>CLIP_RADIUS) return false;
    if (rho==0) {lat=lat_;lon=lon_;return true;}
    const double angle=rho*range_/(PIXELS_PER_RANGE*EARTH_KM),sine=std::sin(angle),cosine=std::cos(angle);
    lat=std::asin(std::clamp(cosine*sin_lat_+north*sine*cos_lat_/rho,-1.0,1.0))*180/PI;
    lon=std::remainder(lon_+std::atan2(east*sine,rho*cos_lat_*cosine-north*sin_lat_*sine)*180/PI,360.0);
    return std::fabs(lat)<=MERCATOR_LIMIT;
  }
  uint8_t zoom() const {
    const double angular=range_*(CLIP_RADIUS+1.0)/(PIXELS_PER_RANGE*EARTH_KM),phi=lat_*PI/180;
    const double south=lat_-angular*180/PI,north=lat_+angular*180/PI;
    if (south < -MERCATOR_LIMIT || north > MERCATOR_LIMIT) return 0;
    const double delta=std::asin(std::clamp(std::sin(angular)/std::cos(phi),-1.0,1.0))/ (2*PI);
    for (int z=12;z>=0;--z) {
      double ox,oy;origin(z,ox,oy);
      const double world=double(TILE_SIZE)*(uint32_t(1)<<z),cx=source_x_*world;
      double left=cx-delta*world-ox;left-=std::floor(left/world)*world;
      if (left+2*delta*world<TILE_SIZE && mercator_y(north)*world>=oy && mercator_y(south)*world<oy+TILE_SIZE)
        return static_cast<uint8_t>(z);
    }
    return 0;
  }
  uint32_t source_index(int x,int y,uint8_t zoom) const {
    if (zoom>12) return UINT32_MAX;
    double lat,lon;if(!location(x,y,lat,lon))return UINT32_MAX;
    double ox,oy;origin(zoom,ox,oy);
    const double world=double(TILE_SIZE)*(uint32_t(1)<<zoom);
    double sx=mercator_x(lon)*world-ox;sx-=std::floor(sx/world)*world;
    const double sy=mercator_y(lat)*world-oy;
    if (sx<0||sy<0||sx>=TILE_SIZE||sy>=TILE_SIZE)return UINT32_MAX;
    return static_cast<uint32_t>(sy)*TILE_SIZE+static_cast<uint32_t>(sx);
  }
 private:
  double lat_,lon_;uint8_t range_;double sin_lat_,cos_lat_,source_x_{0},source_y_{0};
  static double round_even(double value) {
    const double low=std::floor(value),fraction=value-low;
    return fraction<0.5?low:fraction>0.5?low+1:std::fmod(low,2.0)==0?low:low+1;
  }
  void origin(uint8_t zoom,double &x,double &y) const {
    const double world=double(TILE_SIZE)*(uint32_t(1)<<zoom);
    x=round_even(source_x_*world-TILE_SIZE/2.0);x-=std::floor(x/world)*world;
    y=std::clamp(round_even(source_y_*world-TILE_SIZE/2.0),0.0,world-TILE_SIZE);
  }
};

struct FrameInfo {uint32_t valid_time{0};bool forecast{false};char path[96]{};};
struct Metadata {uint32_t generated_time{0},observed_time{0};uint8_t frame_count{0};FrameInfo frames[MAX_FRAMES]{};};

class MetadataParser {
 public:
  bool parse(const char *input,size_t length,uint32_t now,Metadata &out) {
    if(!input||!length||length>32768)return false;
    for(unsigned pass=0;pass<2;++pass){
      p_=input;end_=input+length;out_=&out;out=Metadata{};now_=now;depth_=future_count_=0;
      past_seen_=future_seen_=host_seen_=false;last_future_=0;
      if(!value_(Role::ROOT)||(space_(),p_!=end_)||!past_seen_||!future_seen_||!host_seen_||out.generated_time<VALID_CLOCK)return false;
      if(now<VALID_CLOCK){now=out.generated_time;continue;}
      if(!out.observed_time)return false;
      out.frame_count=1;
      const unsigned count=std::min<unsigned>(future_count_,MAX_FRAMES-1);
      for(unsigned i=0;i<count;++i){const unsigned index=count<2?0:(i*(future_count_-1)+(count-1)/2)/(count-1);out.frames[out.frame_count++]=future_[index];}
      return true;
    }
    return false;
  }
 private:
  enum class Role {SKIP,ROOT,RADAR,PAST,FUTURE,OBSERVATION,FORECAST};
  const char *p_{nullptr},*end_{nullptr};Metadata *out_{nullptr};uint32_t now_{0},last_future_{0};
  unsigned depth_{0},future_count_{0};bool past_seen_{false},future_seen_{false},host_seen_{false};FrameInfo future_[16]{};
  void space_(){while(p_<end_&&(*p_==' '||*p_=='\n'||*p_=='\r'||*p_=='\t'))++p_;}
  bool take_(char c){space_();if(p_==end_||*p_!=c)return false;++p_;return true;}
  bool string_(char *out,size_t cap){
    if(!take_('"'))return false;size_t n=0;bool overflow=false;
    while(p_<end_){unsigned char c=*p_++;if(c=='"'){if(cap)out[std::min(n,cap-1)]=0;return !overflow;}
      if(c<32)return false;
      if(c=='\\'){if(p_==end_)return false;c=*p_++;if(c=='u'){if(end_-p_<4)return false;unsigned code=0;
          for(unsigned i=0;i<4;++i){const char h=*p_++;unsigned d;if(h>='0'&&h<='9')d=h-'0';else if(h>='a'&&h<='f')d=h-'a'+10;else if(h>='A'&&h<='F')d=h-'A'+10;else return false;code=code*16+d;}
          if(cap&&(code==0||code>=128))return false;c=code<128?static_cast<unsigned char>(code):'?';
        }else if(c!='"'&&c!='\\'&&c!='/'){if(c=='b'||c=='f'||c=='n'||c=='r'||c=='t')c=' ';else return false;}}
      if(cap){if(n+1<cap)out[n]=static_cast<char>(c);else overflow=true;}++n;
    }return false;
  }
  bool number_(uint32_t *out=nullptr){space_();const char*start=p_;if(p_<end_&&*p_=='-')++p_;if(p_==end_)return false;
    if(*p_=='0')++p_;else{if(*p_<'1'||*p_>'9')return false;while(p_<end_&&*p_>='0'&&*p_<='9')++p_;}
    bool integer=true;if(p_<end_&&*p_=='.'){integer=false;++p_;const char*q=p_;while(p_<end_&&*p_>='0'&&*p_<='9')++p_;if(p_==q)return false;}
    if(p_<end_&&(*p_=='e'||*p_=='E')){integer=false;++p_;if(p_<end_&&(*p_=='+'||*p_=='-'))++p_;const char*q=p_;while(p_<end_&&*p_>='0'&&*p_<='9')++p_;if(p_==q)return false;}
    if(out){if(!integer||*start=='-'||p_-start>10)return false;uint64_t n=0;for(auto*q=start;q<p_;++q)n=n*10+(*q-'0');if(n>UINT32_MAX)return false;*out=static_cast<uint32_t>(n);}return true;
  }
  bool path_(const char*path)const{
    if(std::strncmp(path,"/v2/radar/",10)||!path[10])return false;
    for(auto*p=path;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='/'||*p=='_'||*p=='-'))return false;
    return true;
  }
  bool value_(Role role){space_();if(p_==end_||++depth_>10)return false;
    if((role==Role::ROOT||role==Role::RADAR||role==Role::OBSERVATION||role==Role::FORECAST)&&*p_!='{')return false;
    if((role==Role::PAST||role==Role::FUTURE)&&*p_!='[')return false;
    bool ok=false;if(*p_=='{')ok=object_(role);else if(*p_=='[')ok=array_(role);else if(*p_=='"')ok=string_(nullptr,0);else if(*p_=='-'||(*p_>='0'&&*p_<='9'))ok=number_();
    else for(const char*word:{"true","false","null"}){const size_t n=std::strlen(word);if(size_t(end_-p_)>=n&&!std::memcmp(p_,word,n)){p_+=n;ok=true;break;}}
    --depth_;return ok;
  }
  bool array_(Role role){if(!take_('['))return false;space_();if(p_<end_&&*p_==']'){++p_;return true;}unsigned count=0;
    for(;;){const Role item=role==Role::PAST?Role::OBSERVATION:role==Role::FUTURE?Role::FORECAST:Role::SKIP;
      if(++count>128||!value_(item))return false;space_();if(p_==end_)return false;if(*p_==']'){++p_;return true;}if(*p_++!=',')return false;}
  }
  bool object_(Role role){if(!take_('{'))return false;FrameInfo frame;bool time=false,path=false;space_();if(p_<end_&&*p_=='}'){++p_;return role!=Role::OBSERVATION&&role!=Role::FORECAST;}
    for(;;){char key[48];if(!string_(key,sizeof(key))||!take_(':'))return false;
      if(role==Role::ROOT&&!std::strcmp(key,"generated")){if(out_->generated_time||!number_(&out_->generated_time))return false;}
      else if(role==Role::ROOT&&!std::strcmp(key,"host")){char host[64];if(host_seen_||!string_(host,sizeof(host))||std::strcmp(host,"https://api.librewxr.net"))return false;host_seen_=true;}
      else if(role==Role::ROOT&&!std::strcmp(key,"radar")){if(!value_(Role::RADAR))return false;}
      else if(role==Role::RADAR&&!std::strcmp(key,"past")){if(past_seen_)return false;past_seen_=true;if(!value_(Role::PAST))return false;}
      else if(role==Role::RADAR&&!std::strcmp(key,"nowcast")){if(future_seen_)return false;future_seen_=true;if(!value_(Role::FUTURE))return false;}
      else if((role==Role::OBSERVATION||role==Role::FORECAST)&&!std::strcmp(key,"time")){if(time||!number_(&frame.valid_time))return false;time=true;}
      else if((role==Role::OBSERVATION||role==Role::FORECAST)&&!std::strcmp(key,"path")){if(path||!string_(frame.path,sizeof(frame.path)))return false;path=true;}
      else if(!value_(Role::SKIP))return false;
      space_();if(p_==end_)return false;if(*p_=='}'){++p_;break;}if(*p_++!=',')return false;
    }
    if(role==Role::OBSERVATION||role==Role::FORECAST){
      if(!time||!path||!frame.valid_time||!path_(frame.path))return false;
      if(role==Role::OBSERVATION){if(frame.valid_time<=now_&&frame.valid_time>out_->observed_time){out_->observed_time=frame.valid_time;out_->frames[0]=frame;}}
      else{if(frame.valid_time<=last_future_)return false;last_future_=frame.valid_time;
        if(frame.valid_time>now_&&uint64_t(frame.valid_time)<=uint64_t(now_)+3600){if(future_count_==16)return false;frame.forecast=true;future_[future_count_++]=frame;}}
    }
    return true;
  }
};

}  // namespace AirDot::weather_radar
