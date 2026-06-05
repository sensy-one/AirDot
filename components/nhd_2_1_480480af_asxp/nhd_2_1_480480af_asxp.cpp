#if defined(USE_ESP32_VARIANT_ESP32S3) || defined(USE_ESP32_VARIANT_ESP32P4)
#include "nhd_2_1_480480af_asxp.h"
#include "esphome/core/hal.h"
#include <algorithm>
#include <driver/gpio.h>
#include <esp_lcd_panel_rgb.h>

namespace esphome {
namespace nhd_2_1_480480af_asxp {

struct InitCommand {
  uint8_t command;
  const uint8_t *data;
  uint8_t length;
  uint16_t delay_ms;
};

static constexpr bool SPI16_IDLE_HIGH = false;

static constexpr uint8_t D_FF_13[] = {0x77, 0x01, 0x00, 0x00, 0x13};
static constexpr uint8_t D_EF_08[] = {0x08};
static constexpr uint8_t D_FF_10[] = {0x77, 0x01, 0x00, 0x00, 0x10};
static constexpr uint8_t D_C0[] = {0x3B, 0x00};
static constexpr uint8_t D_C1[] = {0x09, 0x05};
static constexpr uint8_t D_C2[] = {0x07, 0x02};
static constexpr uint8_t D_C3[] = {0x00};
static constexpr uint8_t D_C6[] = {0x21};
static constexpr uint8_t D_CC[] = {0x30};
static constexpr uint8_t D_B0[] = {0xC0, 0x54, 0x5C, 0x0D, 0x51, 0x06, 0x09, 0x08, 0x07,
                                   0x24, 0x03, 0x11, 0x0F, 0xAC, 0xB5, 0x7F};
static constexpr uint8_t D_B1[] = {0xC0, 0x54, 0x5C, 0x0E, 0x11, 0x07, 0x0A, 0x09, 0x08,
                                   0x24, 0x04, 0x51, 0x10, 0xAD, 0x75, 0x7F};
static constexpr uint8_t D_FF_11[] = {0x77, 0x01, 0x00, 0x00, 0x11};
static constexpr uint8_t D_B0_7D[] = {0x7D};
static constexpr uint8_t D_B1_33[] = {0x33};
static constexpr uint8_t D_B2[] = {0x87};
static constexpr uint8_t D_B3[] = {0x80};
static constexpr uint8_t D_B5[] = {0x45};
static constexpr uint8_t D_B7[] = {0x87};
static constexpr uint8_t D_B8[] = {0x33};
static constexpr uint8_t D_B9[] = {0x10};
static constexpr uint8_t D_BB[] = {0x03};
static constexpr uint8_t D_C0_03[] = {0x03};
static constexpr uint8_t D_C1_78[] = {0x78};
static constexpr uint8_t D_C2_78[] = {0x78};
static constexpr uint8_t D_D0[] = {0x88};
static constexpr uint8_t D_E0[] = {0x00, 0x18, 0x00, 0x00, 0x00, 0x20};
static constexpr uint8_t D_E1[] = {0x05, 0xA0, 0x00, 0xA0, 0x04, 0x0A, 0x00, 0xA0, 0x00, 0x44, 0x44};
static constexpr uint8_t D_E2[] = {0x11, 0x11, 0x44, 0x44, 0xEA, 0xA0, 0x00, 0x00, 0xE9, 0xA0, 0x00, 0x00};
static constexpr uint8_t D_E3[] = {0x00, 0x00, 0x11, 0x11};
static constexpr uint8_t D_E4[] = {0x44, 0x44};
static constexpr uint8_t D_E5[] = {0x06, 0xE5, 0xD8, 0xA0, 0x08, 0xE7, 0xD8, 0xA0,
                                   0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0};
static constexpr uint8_t D_E6_0011[] = {0x00, 0x00, 0x11, 0x11};
static constexpr uint8_t D_E7[] = {0x44, 0x44};
static constexpr uint8_t D_E8[] = {0x05, 0xE4, 0xD8, 0xA0, 0x07, 0xE6, 0xD8, 0xA0,
                                   0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0};
static constexpr uint8_t D_EB[] = {0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x10};
static constexpr uint8_t D_EC[] = {0x3D, 0x02, 0x00};
static constexpr uint8_t D_ED[] = {0x20, 0x76, 0x54, 0x98, 0xBA, 0xFF, 0xFF, 0xFF, 0xFF,
                                   0xFF, 0xFF, 0xAB, 0x89, 0x45, 0x67, 0x02};
static constexpr uint8_t D_EF[] = {0x08, 0x08, 0x08, 0x45, 0x3F, 0x54};
static constexpr uint8_t D_E8_0E[] = {0x00, 0x0E};
static constexpr uint8_t D_E8_0C[] = {0x00, 0x0C};
static constexpr uint8_t D_E8_00[] = {0x00, 0x00};
static constexpr uint8_t D_E6_167C[] = {0x16, 0x7C};
static constexpr uint8_t D_FF_00[] = {0x77, 0x01, 0x00, 0x00, 0x00};
static constexpr uint8_t D_3A[] = {0x55};
static constexpr uint8_t D_36[] = {0x00};
static constexpr uint8_t D_C7[] = {0x00};

#define INIT(command, data) {command, data, uint8_t(sizeof(data)), 0}
#define INIT_DELAY(command, data, ms) {command, data, uint8_t(sizeof(data)), ms}
#define INIT_NOARGS(command) {command, nullptr, 0, 0}
#define INIT_NOARGS_DELAY(command, ms) {command, nullptr, 0, ms}

static constexpr InitCommand INIT_SEQUENCE[] = {
    INIT(0xFF, D_FF_13),       INIT(0xEF, D_EF_08),       INIT(0xFF, D_FF_10),
    INIT(0xC0, D_C0),          INIT(0xC1, D_C1),          INIT(0xC2, D_C2),
    INIT(0xC3, D_C3),          INIT(0xC6, D_C6),          INIT(0xCC, D_CC),
    INIT(0xB0, D_B0),          INIT(0xB1, D_B1),          INIT(0xFF, D_FF_11),
    INIT(0xB0, D_B0_7D),       INIT(0xB1, D_B1_33),       INIT(0xB2, D_B2),
    INIT(0xB3, D_B3),          INIT(0xB5, D_B5),          INIT(0xB7, D_B7),
    INIT(0xB8, D_B8),          INIT(0xB9, D_B9),          INIT(0xBB, D_BB),
    INIT(0xC0, D_C0_03),       INIT(0xC1, D_C1_78),       INIT(0xC2, D_C2_78),
    INIT(0xD0, D_D0),          INIT(0xFF, D_FF_11),       INIT(0xE0, D_E0),
    INIT(0xE1, D_E1),          INIT(0xE2, D_E2),          INIT(0xE3, D_E3),
    INIT(0xE4, D_E4),          INIT(0xE5, D_E5),          INIT(0xE6, D_E6_0011),
    INIT(0xE7, D_E7),          INIT(0xE8, D_E8),          INIT(0xEB, D_EB),
    INIT(0xEC, D_EC),          INIT(0xED, D_ED),          INIT(0xEF, D_EF),
    INIT(0xFF, D_FF_13),       INIT(0xE8, D_E8_0E),       INIT_NOARGS_DELAY(0x11, 120),
    INIT_DELAY(0xE8, D_E8_0C, 10), INIT(0xE8, D_E8_00),   INIT(0xE6, D_E6_167C),
    INIT(0xFF, D_FF_00),       INIT(0x3A, D_3A),          INIT(0x36, D_36),
    INIT(0xC7, D_C7),          INIT_NOARGS(0x20),         INIT_NOARGS(0x29),
};

#undef INIT
#undef INIT_DELAY
#undef INIT_NOARGS
#undef INIT_NOARGS_DELAY

void Nhd21480480AfAsxp::setup() {
  this->setup_control_pins_();
  if (this->is_failed())
    return;
  this->write_init_sequence_();
  this->common_setup_();
}

void Nhd21480480AfAsxp::setup_control_pins_() {
  if (this->cs_pin_ == nullptr || this->init_scl_pin_ == nullptr || this->init_sda_pin_ == nullptr ||
      this->reset_pin_ == nullptr) {
    this->mark_failed();
    return;
  }

  this->cs_pin_->setup();
  this->init_scl_pin_->setup();
  this->init_sda_pin_->setup();
  this->reset_pin_->setup();

  this->cs_pin_->digital_write(true);
  this->init_scl_pin_->digital_write(SPI16_IDLE_HIGH);
  this->init_sda_pin_->digital_write(true);

  this->reset_pin_->digital_write(true);
  delay(5);
  this->reset_pin_->digital_write(false);
  delay(5);
  this->reset_pin_->digital_write(true);
  delay(120);
}

void Nhd21480480AfAsxp::write_16bit_word_(uint16_t value) {
  this->init_scl_pin_->digital_write(SPI16_IDLE_HIGH);
  this->cs_pin_->digital_write(false);
  delayMicroseconds(1);

  for (uint8_t bit = 0; bit != 16; bit++) {
    this->init_sda_pin_->digital_write(value & (0x8000 >> bit));
    delayMicroseconds(1);
    this->init_scl_pin_->digital_write(true);
    delayMicroseconds(1);
    this->init_scl_pin_->digital_write(false);
    delayMicroseconds(1);
  }

  delayMicroseconds(1);
  this->cs_pin_->digital_write(true);
}

void Nhd21480480AfAsxp::write_16bit_register_(uint16_t address, uint8_t value, bool is_data) {
  const uint8_t high = address >> 8;
  const uint8_t low = address & 0xFF;
  this->write_16bit_word_(0x2000 | high);
  this->write_16bit_word_(low);
  this->write_16bit_word_(uint16_t((is_data ? 0x4000 : 0x0000) | value));
}

void Nhd21480480AfAsxp::write_command_(uint8_t command, const uint8_t *data, uint8_t length) {
  if (length == 0) {
    this->write_16bit_register_(uint16_t(command) << 8, 0x00, true);
    return;
  }

  for (uint8_t arg = 0; arg != length; arg++)
    this->write_16bit_register_((uint16_t(command) << 8) | arg, data[arg], true);
}

void Nhd21480480AfAsxp::write_init_sequence_() {
  for (const auto &init : INIT_SEQUENCE) {
    this->write_command_(init.command, init.data, init.length);
    if (init.delay_ms != 0)
      delay(init.delay_ms);
  }
  this->screen_powered_ = true;
  delay(10);
}

void Nhd21480480AfAsxp::set_screen_powered(bool powered) {
  if (this->is_failed() || this->screen_powered_ == powered)
    return;

  if (powered) {
    this->write_command_(0x11, nullptr, 0);
    delay(120);
    this->write_command_(0x29, nullptr, 0);
    delay(10);
    this->screen_powered_ = true;
    return;
  }

  this->write_command_(0x28, nullptr, 0);
  delay(20);
  this->write_command_(0x10, nullptr, 0);
  delay(120);
  this->screen_powered_ = false;
}

void Nhd21480480AfAsxp::common_setup_() {
  if (this->de_pin_ == nullptr || this->hsync_pin_ == nullptr || this->vsync_pin_ == nullptr ||
      this->pclk_pin_ == nullptr) {
    this->mark_failed();
    return;
  }

  esp_lcd_rgb_panel_config_t config{};
  config.flags.fb_in_psram = 1;
  config.bounce_buffer_size_px = this->width_ * 20;
  config.dma_burst_size = 64;
  config.num_fbs = 1;
  config.timings.h_res = this->width_;
  config.timings.v_res = this->height_;
  config.timings.hsync_pulse_width = this->hsync_pulse_width_;
  config.timings.hsync_back_porch = this->hsync_back_porch_;
  config.timings.hsync_front_porch = this->hsync_front_porch_;
  config.timings.vsync_pulse_width = this->vsync_pulse_width_;
  config.timings.vsync_back_porch = this->vsync_back_porch_;
  config.timings.vsync_front_porch = this->vsync_front_porch_;
  config.timings.flags.pclk_active_neg = this->pclk_inverted_;
  config.timings.pclk_hz = this->pclk_frequency_;
  config.clk_src = LCD_CLK_SRC_PLL160M;

  for (size_t i = 0; i != DATA_PIN_COUNT; i++) {
    if (this->data_pins_[i] == nullptr) {
      this->mark_failed();
      return;
    }
    config.data_gpio_nums[i] = static_cast<gpio_num_t>(this->data_pins_[i]->get_pin());
  }
  config.data_width = DATA_PIN_COUNT;
  config.disp_gpio_num = GPIO_NUM_NC;
  config.hsync_gpio_num = static_cast<gpio_num_t>(this->hsync_pin_->get_pin());
  config.vsync_gpio_num = static_cast<gpio_num_t>(this->vsync_pin_->get_pin());
  config.de_gpio_num = static_cast<gpio_num_t>(this->de_pin_->get_pin());
  config.pclk_gpio_num = static_cast<gpio_num_t>(this->pclk_pin_->get_pin());

  esp_err_t err = esp_lcd_new_rgb_panel(&config, &this->handle_);
  if (err == ESP_OK)
    err = esp_lcd_panel_reset(this->handle_);
  if (err == ESP_OK)
    err = esp_lcd_panel_init(this->handle_);
  if (err != ESP_OK) {
    this->mark_failed();
  }
}

void Nhd21480480AfAsxp::update() {
  if (this->is_failed())
    return;
  if (this->auto_clear_enabled_)
    this->clear();
  if (this->page_ != nullptr) {
    this->page_->get_writer()(*this);
  } else if (this->writer_.has_value()) {
    (*this->writer_)(*this);
  } else {
    this->stop_poller();
  }
  if (this->buffer_ == nullptr || this->x_low_ > this->x_high_ || this->y_low_ > this->y_high_)
    return;

  const int w = this->x_high_ - this->x_low_ + 1;
  const int h = this->y_high_ - this->y_low_ + 1;
  this->write_to_display_(this->x_low_, this->y_low_, w, h, reinterpret_cast<const uint8_t *>(this->buffer_),
                          this->x_low_, this->y_low_, this->width_ - w - this->x_low_);
  this->x_low_ = this->width_;
  this->y_low_ = this->height_;
  this->x_high_ = 0;
  this->y_high_ = 0;
}

void Nhd21480480AfAsxp::draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                                       display::ColorOrder order, display::ColorBitness bitness, bool big_endian,
                                       int x_offset, int y_offset, int x_pad) {
  if (w <= 0 || h <= 0 || this->is_failed())
    return;
  if (bitness != display::COLOR_BITNESS_565 || order != display::COLOR_ORDER_RGB || big_endian) {
    if (!this->check_buffer_())
      return;
    Display::draw_pixels_at(x_start, y_start, w, h, ptr, order, bitness, big_endian, x_offset, y_offset, x_pad);
    this->write_to_display_(x_start, y_start, w, h, reinterpret_cast<const uint8_t *>(this->buffer_), x_start, y_start,
                            this->width_ - w - x_start);
  } else {
    this->write_to_display_(x_start, y_start, w, h, ptr, x_offset, y_offset, x_pad);
  }
}

void Nhd21480480AfAsxp::write_to_display_(int x_start, int y_start, int w, int h, const uint8_t *ptr, int x_offset,
                                          int y_offset, int x_pad) {
  if (this->handle_ == nullptr || ptr == nullptr) {
    this->status_set_warning();
    return;
  }

  esp_err_t err = ESP_OK;
  const auto stride_pixels = x_offset + w + x_pad;
  const auto stride_bytes = stride_pixels * 2;

  ptr += y_offset * stride_bytes + x_offset * 2;
  if (x_offset == 0 && x_pad == 0) {
    err = esp_lcd_panel_draw_bitmap(this->handle_, x_start, y_start, x_start + w, y_start + h, ptr);
  } else {
    for (int y = 0; y != h; y++) {
      err = esp_lcd_panel_draw_bitmap(this->handle_, x_start, y + y_start, x_start + w, y + y_start + 1, ptr);
      if (err != ESP_OK)
        break;
      ptr += stride_bytes;
    }
  }

  if (err == ESP_OK)
    this->status_clear_warning();
  else
    this->status_set_warning();
}

bool Nhd21480480AfAsxp::check_buffer_() {
  if (this->is_failed())
    return false;
  if (this->buffer_ != nullptr)
    return true;
  RAMAllocator<uint16_t> allocator;
  this->buffer_ = allocator.allocate(this->height_ * this->width_);
  if (this->buffer_ == nullptr) {
    this->mark_failed();
    return false;
  }
  return true;
}

void Nhd21480480AfAsxp::draw_pixel_at(int x, int y, Color color) {
  if (!this->get_clipping().inside(x, y) || this->is_failed())
    return;

  switch (this->rotation_) {
    case display::DISPLAY_ROTATION_0_DEGREES:
      break;
    case display::DISPLAY_ROTATION_90_DEGREES:
      std::swap(x, y);
      x = this->width_ - x - 1;
      break;
    case display::DISPLAY_ROTATION_180_DEGREES:
      x = this->width_ - x - 1;
      y = this->height_ - y - 1;
      break;
    case display::DISPLAY_ROTATION_270_DEGREES:
      std::swap(x, y);
      y = this->height_ - y - 1;
      break;
  }
  if (x >= this->get_width_internal() || x < 0 || y >= this->get_height_internal() || y < 0)
    return;
  if (!this->check_buffer_())
    return;

  const size_t pos = (y * this->width_) + x;
  const uint16_t new_color = display::ColorUtil::color_to_565(color);
  if (this->buffer_[pos] == new_color)
    return;
  this->buffer_[pos] = new_color;
  if (x < this->x_low_)
    this->x_low_ = x;
  if (y < this->y_low_)
    this->y_low_ = y;
  if (x > this->x_high_)
    this->x_high_ = x;
  if (y > this->y_high_)
    this->y_high_ = y;
}

void Nhd21480480AfAsxp::fill(Color color) {
  if (!this->check_buffer_())
    return;
  if (this->get_clipping().is_set()) {
    Display::fill(color);
    return;
  }

  auto *ptr_16 = reinterpret_cast<uint16_t *>(this->buffer_);
  const uint16_t new_color = display::ColorUtil::color_to_565(color);
  std::fill_n(ptr_16, this->width_ * this->height_, new_color);
  this->x_low_ = 0;
  this->y_low_ = 0;
  this->x_high_ = this->width_ - 1;
  this->y_high_ = this->height_ - 1;
}

int Nhd21480480AfAsxp::get_width() {
  switch (this->rotation_) {
    case display::DISPLAY_ROTATION_90_DEGREES:
    case display::DISPLAY_ROTATION_270_DEGREES:
      return this->get_height_internal();
    case display::DISPLAY_ROTATION_0_DEGREES:
    case display::DISPLAY_ROTATION_180_DEGREES:
    default:
      return this->get_width_internal();
  }
}

int Nhd21480480AfAsxp::get_height() {
  switch (this->rotation_) {
    case display::DISPLAY_ROTATION_0_DEGREES:
    case display::DISPLAY_ROTATION_180_DEGREES:
      return this->get_height_internal();
    case display::DISPLAY_ROTATION_90_DEGREES:
    case display::DISPLAY_ROTATION_270_DEGREES:
    default:
      return this->get_width_internal();
  }
}

}
}
#endif
