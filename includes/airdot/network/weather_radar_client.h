#pragma once

#include "weather_radar_data.h"

#ifdef USE_ESP32
#include "weather_radar_png.h"
#include "weather_client.h"
#include <atomic>
#include <cstdio>
#include <ctime>
#include <new>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "esphome/core/hal.h"
#include "esphome/components/network/util.h"

namespace AirDot::weather_radar {
namespace detail {
inline constexpr const char *HOST="https://api.librewxr.net";
inline constexpr const char *CATALOG="https://api.librewxr.net/public/weather-maps.json";
inline constexpr uint32_t REFRESH_MS=300000;
enum class TaskState:uint8_t {IDLE,RUNNING,SUCCESS,FAILED};
struct Request {float lat{0},lon{0};uint8_t range{0};uint32_t generation{0};};
inline Request &wanted(){static Request value;return value;}
inline Request &in_flight(){static Request value;return value;}
inline std::atomic<TaskState> &task(){static std::atomic<TaskState> value{TaskState::IDLE};return value;}
inline std::atomic<uint32_t> &generation(){static std::atomic<uint32_t> value{0};return value;}
inline std::atomic<bool> &paused(){static std::atomic<bool> value{false};return value;}
inline std::atomic<Status> &status(){static std::atomic<Status> value{Status::EMPTY};return value;}
inline std::atomic<RadarData*> &pending(){static std::atomic<RadarData*> value{nullptr};return value;}
inline std::atomic<uint8_t> &percentage(){static std::atomic<uint8_t> value{0};return value;}
inline RadarData *&current(){static RadarData*value=nullptr;return value;}
inline uint32_t &next_request(){static uint32_t value=0;return value;}
inline uint8_t &failures(){static uint8_t value=0;return value;}
inline bool &unavailable(){static bool value=false;return value;}
inline SemaphoreHandle_t mutex(){static auto value=xSemaphoreCreateMutex();return value;}
class Lock {
 public:explicit Lock(uint32_t ms=2){const auto handle=mutex();held_=handle&&xSemaphoreTake(handle,pdMS_TO_TICKS(ms))==pdTRUE;}
  ~Lock(){if(held_)xSemaphoreGive(mutex());}bool held()const{return held_;}
 private:bool held_{false};
};
inline bool same(const Request&a,const Request&b){return a.range==b.range&&coordinate_key(a.lat)==coordinate_key(b.lat)&&coordinate_key(a.lon)==coordinate_key(b.lon);}
inline bool obsolete(const Request&r){return generation().load(std::memory_order_acquire)!=r.generation;}
inline bool fail(bool missing=false){unavailable()=unavailable()||missing;return false;}
inline uint64_t unix_now(){const std::time_t now=std::time(nullptr);return now>=VALID_CLOCK?static_cast<uint64_t>(now):0;}
inline void destroy(RadarData*data){if(data){radar_free(data->coverage);data->~RadarData();radar_free(data);}}
class Buffer {
 public:explicit Buffer(size_t size):data_(static_cast<uint8_t*>(radar_allocate(size))){}
  ~Buffer(){radar_free(data_);}uint8_t*data()const{return data_;}
 private:uint8_t*data_;
};

class Http {
 public:
  Http():started_(esphome::millis()){
    esp_http_client_config_t config{};config.url=CATALOG;config.method=HTTP_METHOD_GET;config.timeout_ms=7000;
    config.buffer_size=1024;config.buffer_size_tx=512;config.addr_type=HTTP_ADDR_TYPE_INET;
    config.disable_auto_redirect=true;config.keep_alive_enable=true;config.user_agent="AirDot (+https://github.com/sensy-one/AirDot)";
    config.event_handler=header_event;config.user_data=this;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    config.crt_bundle_attach=esp_crt_bundle_attach;
#endif
    client_=esp_http_client_init(&config);
    if(client_){esp_http_client_set_header(client_,"Accept","application/json,image/png");esp_http_client_set_header(client_,"Accept-Encoding","identity");}
  }
  ~Http(){if(client_)esp_http_client_cleanup(client_);}
  template<class Sink>bool get(const char*url,size_t limit,const Request&r,unsigned frame,unsigned frames,Sink sink){
    if(!client_||expired(r)||esp_http_client_set_url(client_,url)!=ESP_OK)return stop();
    encoded_=false;esp_http_client_set_timeout_ms(client_,7000);
    if(esp_http_client_open(client_,0)!=ESP_OK)return stop();
    const uint32_t headers=esphome::millis();int64_t length;
    do{if(expired(r))return stop();esp_http_client_set_timeout_ms(client_,1500);length=esp_http_client_fetch_headers(client_);
      if(length!=-ESP_ERR_HTTP_EAGAIN)break;vTaskDelay(1);
    }while(esphome::millis()-headers<15000);
    if(length<0||esp_http_client_get_status_code(client_)!=200||encoded_||static_cast<uint64_t>(length)>limit)return stop();
    uint32_t last_data=esphome::millis();size_t received=0;uint8_t bytes[1024];
    for(;;){if(expired(r)||esphome::millis()-last_data>=10000)return stop();
      esp_http_client_set_timeout_ms(client_,1500);const int count=esp_http_client_read(client_,reinterpret_cast<char*>(bytes),sizeof(bytes));
      if(count==-ESP_ERR_HTTP_EAGAIN){vTaskDelay(1);continue;}if(count<0)return stop();if(!count)break;
      received+=static_cast<size_t>(count);total_+=static_cast<size_t>(count);
      if(received>limit||total_>3UL*1024*1024)return stop();last_data=esphome::millis();
      if(!sink(bytes,static_cast<size_t>(count)))return stop();
      if(frames){const uint32_t part=length>0?std::min<uint64_t>(100,uint64_t(received)*100/length):uint64_t(received)*100/(received+16384);
        percentage().store(static_cast<uint8_t>(5+(frame*100+part)*90/(frames*100)));}
      vTaskDelay(1);
    }
    if(!received||!esp_http_client_is_complete_data_received(client_))return stop();
    return true;
  }
 private:
  esp_http_client_handle_t client_{nullptr};uint32_t started_;size_t total_{0};bool encoded_{false};
  bool expired(const Request&r)const{return obsolete(r)||esphome::millis()-started_>=120000;}
  bool stop(){if(client_)esp_http_client_close(client_);return fail();}
  static esp_err_t header_event(esp_http_client_event_t*event){
    if(event->event_id!=HTTP_EVENT_ON_HEADER||!event->user_data||!event->header_key||!event->header_value)return ESP_OK;
    constexpr const char*key="content-encoding";size_t i=0;
    while(key[i]&&event->header_key[i]){char c=event->header_key[i];if(c>='A'&&c<='Z')c+='a'-'A';if(c!=key[i])return ESP_OK;++i;}
    if(!key[i]&&!event->header_key[i]&&std::strcmp(event->header_value,"identity"))static_cast<Http*>(event->user_data)->encoded_=true;
    return ESP_OK;
  }
};

inline bool populate(Http&http,RadarData&data,const Metadata&meta,const Request&r){
  data.frame_count=meta.frame_count;data.generated_time=meta.generated_time;data.observed_time=meta.observed_time;
  data.center_latitude=r.lat;data.center_longitude=r.lon;data.range_km=r.range;
  data.coverage=static_cast<uint8_t*>(radar_allocate(PIXEL_COUNT*(meta.frame_count+1)));
  Buffer lookup_mem(PIXEL_COUNT*sizeof(uint32_t)),tile(TILE_SIZE*TILE_SIZE);
  if(!data.coverage||!lookup_mem.data()||!tile.data())return fail();
  auto*lookup=reinterpret_cast<uint32_t*>(lookup_mem.data());Sampling sampling(r.lat,r.lon,r.range);const uint8_t zoom=sampling.zoom();
  for(int y=0;y<HEIGHT;++y){if(obsolete(r))return false;
    for(int x=0;x<WIDTH;++x){const size_t index=size_t(y)*WIDTH+x;lookup[index]=sampling.source_index(x,y,zoom);
      const int dx=x-WIDTH/2,dy=y-HEIGHT/2;const bool visible=dx*dx+dy*dy<=CLIP_RADIUS*CLIP_RADIUS;
      if(visible&&lookup[index]==UINT32_MAX)return fail(true);data.coverage[index]=lookup[index]==UINT32_MAX?0:255;}
    if(!(y&3))vTaskDelay(1);
  }
  percentage().store(5);
  for(unsigned i=0;i<meta.frame_count;++i){auto&frame=data.frames[i];frame.valid_time=meta.frames[i].valid_time;frame.forecast=meta.frames[i].forecast;frame.intensity=data.coverage+PIXEL_COUNT*(i+1);
    char url[256];const int length=std::snprintf(url,sizeof(url),"%s%s/512/%u/%.5f/%.5f/255/0_0.png",HOST,meta.frames[i].path,zoom,r.lat,r.lon);
    if(length<=0||static_cast<size_t>(length)>=sizeof(url))return fail();
    void*memory=radar_allocate(sizeof(PngDecoder));if(!memory)return fail();auto*decoder=new(memory)PngDecoder(tile.data());
    bool ok=http.get(url,512*1024,r,i,meta.frame_count,[&](const uint8_t*bytes,size_t count){return decoder->feed(bytes,count);});
    if(ok)ok=decoder->finish();decoder->~PngDecoder();radar_free(decoder);if(!ok)return fail();
    for(int y=0;y<HEIGHT;++y){if(obsolete(r))return false;for(int x=0;x<WIDTH;++x){const size_t index=size_t(y)*WIDTH+x;frame.intensity[index]=lookup[index]==UINT32_MAX?0:tile.data()[lookup[index]];}if(!(y&7))vTaskDelay(1);}
  }
  if(is_stale(data,reference_time(data,unix_now(),esphome::millis())))return fail(true);
  return true;
}
inline bool fetch_requested(const Request&r,RadarData*&data){
  Http http;Buffer bytes(32768);if(!bytes.data())return fail();size_t used=0;
  if(!http.get(CATALOG,32768,r,0,0,[&](const uint8_t*input,size_t size){std::memcpy(bytes.data()+used,input,size);used+=size;return true;}))return false;
  const uint32_t fetched_ms=esphome::millis();Metadata meta;MetadataParser parser;
  if(!parser.parse(reinterpret_cast<const char*>(bytes.data()),used,static_cast<uint32_t>(unix_now()),meta))return fail();
  const uint64_t now=unix_now()?unix_now():meta.generated_time;
  if(meta.frame_count<2||meta.generated_time>now+300||meta.observed_time>now||now>uint64_t(meta.generated_time)+1200||now>uint64_t(meta.observed_time)+1800)return fail(true);
  void*memory=radar_allocate(sizeof(RadarData));if(!memory)return fail();data=new(memory)RadarData{};data->fetched_ms=fetched_ms;
  return populate(http,*data,meta,r);
}
inline void worker(void*){
  const Request request=in_flight();RadarData*data=nullptr;bool ok=fetch_requested(request,data);
  if(obsolete(request))ok=false;
  if(ok){percentage().store(99);pending().store(data,std::memory_order_release);}else destroy(data);
  AirDot::time_weather::release_weather_http_request_slot_();
  task().store(ok?TaskState::SUCCESS:TaskState::FAILED,std::memory_order_release);vTaskDelete(nullptr);
}
}  // namespace detail

inline void pause_background_requests(bool pause){
  if(detail::paused().exchange(pause)!=pause&&pause&&detail::task().load()==detail::TaskState::RUNNING)
    detail::generation().fetch_add(1);
}
inline Status status_for(float lat,float lon,uint8_t range){
  if(!valid_location(lat,lon,range))return Status::UNAVAILABLE;
  if(!detail::same(detail::wanted(),detail::Request{lat,lon,range}))return Status::EMPTY;
  return !esphome::network::is_connected()?Status::OFFLINE:detail::status().load();
}
inline uint8_t percent_for(float lat,float lon,uint8_t range){
  if(!detail::same(detail::wanted(),detail::Request{lat,lon,range}))return 0;
  if(detail::status().load()==Status::READY)return 100;
  return detail::obsolete(detail::in_flight())?0:detail::percentage().load();
}
template<class Callback>bool with_data(float lat,float lon,uint8_t range,Callback callback){
  detail::Lock lock;if(!lock.held()||!detail::current()||!matches(*detail::current(),lat,lon,range))return false;callback(*detail::current());return true;
}
inline bool consume_result(){
  const auto state=detail::task().load(std::memory_order_acquire);
  if(state!=detail::TaskState::SUCCESS&&state!=detail::TaskState::FAILED)return false;
  if(state==detail::TaskState::SUCCESS){detail::Lock lock(20);if(!lock.held())return false;auto*data=detail::pending().exchange(nullptr,std::memory_order_acq_rel);
    if(data&&!detail::obsolete(detail::in_flight())&&matches(*data,detail::wanted().lat,detail::wanted().lon,detail::wanted().range)){auto*old=detail::current();detail::current()=data;detail::destroy(old);detail::failures()=0;detail::next_request()=esphome::millis()+detail::REFRESH_MS;detail::status().store(Status::READY);detail::percentage().store(100);}
    else detail::destroy(data);
  }else if(!detail::obsolete(detail::in_flight())&&detail::same(detail::wanted(),detail::in_flight())){
    const uint32_t wait=std::min<uint32_t>(60000UL<<std::min<unsigned>(detail::failures(),3),300000);
    if(detail::failures()<4)++detail::failures();detail::next_request()=esphome::millis()+wait;detail::status().store(detail::unavailable()?Status::UNAVAILABLE:Status::FAILED);
  }
  detail::task().store(detail::TaskState::IDLE,std::memory_order_release);return true;
}
inline bool request_radar(float lat,float lon,uint8_t range,bool allow_network=true){
  if(!valid_location(lat,lon,range))return false;detail::Request request{lat,lon,range};
  if(!detail::same(request,detail::wanted())){detail::wanted()=request;detail::generation().fetch_add(1);detail::next_request()=0;detail::failures()=0;detail::status().store(Status::EMPTY);}
  if(detail::task().load()!=detail::TaskState::IDLE||detail::paused().load()||!allow_network||!esphome::network::is_connected())return false;
  if(detail::next_request()&&static_cast<int32_t>(esphome::millis()-detail::next_request())<0)return false;
  if(!AirDot::time_weather::acquire_weather_http_request_slot_(AirDot::connectivity::Service::WEATHER))return false;
  request.generation=detail::generation().load();detail::in_flight()=request;detail::unavailable()=false;detail::percentage().store(0);
  detail::task().store(detail::TaskState::RUNNING);detail::status().store(Status::LOADING);
  if(xTaskCreate(detail::worker,"weather_radar",12288,nullptr,1,nullptr)!=pdPASS){AirDot::time_weather::release_weather_http_request_slot_();detail::task().store(detail::TaskState::FAILED);return false;}
  return true;
}
}  // namespace AirDot::weather_radar
#endif
