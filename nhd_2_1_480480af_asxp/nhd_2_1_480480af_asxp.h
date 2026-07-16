#pragma once

#if defined(USE_ESP32_VARIANT_ESP32S3) || defined(USE_ESP32_VARIANT_ESP32P4)
#include "esphome/components/display/display.h"
#include "esphome/core/gpio.h"
#include "esp_lcd_panel_ops.h"

namespace esphome {
namespace nhd_2_1_480480af_asxp {

class Nhd21480480AfAsxp : public display::Display {
 public:
  Nhd21480480AfAsxp(int width, int height) : width_(width), height_(height) {}

  void setup() override;
  void update() override;
  void fill(Color color) override;
  void draw_pixel_at(int x, int y, Color color) override;
  void draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr, display::ColorOrder order,
                      display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset, int x_pad) override;
  display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_COLOR; }
  int get_width() override;
  int get_height() override;
  int get_width_internal() override { return this->width_; }
  int get_height_internal() override { return this->height_; }
  void set_screen_powered(bool powered);
  bool is_screen_powered() const { return this->screen_powered_; }
  void set_pclk_frequency(uint32_t pclk_frequency);
  void set_low_bandwidth_mode(bool enabled);

  void add_data_pin(InternalGPIOPin *data_pin, size_t index) {
    if (index >= DATA_PIN_COUNT) {
      this->mark_failed();
      return;
    }
    this->data_pins_[index] = data_pin;
  }
  void set_cs_pin(GPIOPin *cs_pin) { this->cs_pin_ = cs_pin; }
  void set_de_pin(InternalGPIOPin *de_pin) { this->de_pin_ = de_pin; }
  void set_hsync_pin(InternalGPIOPin *hsync_pin) { this->hsync_pin_ = hsync_pin; }
  void set_init_scl_pin(GPIOPin *init_scl_pin) { this->init_scl_pin_ = init_scl_pin; }
  void set_init_sda_pin(GPIOPin *init_sda_pin) { this->init_sda_pin_ = init_sda_pin; }
  void set_pclk_inverted(bool inverted) { this->pclk_inverted_ = inverted; }
  void set_pclk_pin(InternalGPIOPin *pclk_pin) { this->pclk_pin_ = pclk_pin; }
  void set_reset_pin(GPIOPin *reset_pin) { this->reset_pin_ = reset_pin; }
  void set_vsync_pin(InternalGPIOPin *vsync_pin) { this->vsync_pin_ = vsync_pin; }
  void set_hsync_back_porch(uint16_t hsync_back_porch) { this->hsync_back_porch_ = hsync_back_porch; }
  void set_hsync_front_porch(uint16_t hsync_front_porch) { this->hsync_front_porch_ = hsync_front_porch; }
  void set_hsync_pulse_width(uint16_t hsync_pulse_width) { this->hsync_pulse_width_ = hsync_pulse_width; }
  void set_vsync_back_porch(uint16_t vsync_back_porch) { this->vsync_back_porch_ = vsync_back_porch; }
  void set_vsync_front_porch(uint16_t vsync_front_porch) { this->vsync_front_porch_ = vsync_front_porch; }
  void set_vsync_pulse_width(uint16_t vsync_pulse_width) { this->vsync_pulse_width_ = vsync_pulse_width; }

 protected:
  void common_setup_();
  bool check_buffer_();
  void clear_panel_framebuffer_();
  void setup_control_pins_();
  void write_16bit_register_(uint16_t address, uint8_t value, bool is_data);
  void write_16bit_word_(uint16_t value);
  void write_command_(uint8_t command, const uint8_t *data, uint8_t length);
  void write_init_sequence_();
  void apply_pclk_frequency_(uint32_t pclk_frequency);
  void write_to_display_(int x_start, int y_start, int w, int h, const uint8_t *ptr, int x_offset, int y_offset,
                         int x_pad);

  static constexpr size_t DATA_PIN_COUNT = 16;

  GPIOPin *cs_pin_{nullptr};
  GPIOPin *init_scl_pin_{nullptr};
  GPIOPin *init_sda_pin_{nullptr};
  GPIOPin *reset_pin_{nullptr};
  InternalGPIOPin *de_pin_{nullptr};
  InternalGPIOPin *pclk_pin_{nullptr};
  InternalGPIOPin *hsync_pin_{nullptr};
  InternalGPIOPin *vsync_pin_{nullptr};
  InternalGPIOPin *data_pins_[DATA_PIN_COUNT] = {};
  uint16_t hsync_pulse_width_ = 4;
  uint16_t hsync_back_porch_ = 50;
  uint16_t hsync_front_porch_ = 50;
  uint16_t vsync_pulse_width_ = 2;
  uint16_t vsync_back_porch_ = 50;
  uint16_t vsync_front_porch_ = 50;
  uint32_t pclk_frequency_ = 8 * 1000 * 1000;
  bool pclk_inverted_{true};
  size_t width_;
  size_t height_;
  uint16_t *buffer_{nullptr};
  uint16_t x_low_{1};
  uint16_t y_low_{1};
  uint16_t x_high_{0};
  uint16_t y_high_{0};
  bool screen_powered_{false};
  bool low_bandwidth_mode_{false};
  esp_lcd_panel_handle_t handle_{};
};

}
}
#endif
