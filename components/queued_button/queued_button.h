#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/hal.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

namespace esphome::queued_button {

class QueuedButton : public binary_sensor::BinarySensor, public Component {
 public:
  void set_pin(InternalGPIOPin *pin) { pin_ = pin; }
  uint32_t event_time_ms() const { return event_time_ms_; }
  bool pressed_now() const { return pin_ && pin_->digital_read(); }
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void setup() override {
    pin_->setup();
    isr_pin_ = pin_->to_isr();
    raw_ = last_level_ = pin_->digital_read();
    raw_since_ = last_time_ = millis();
    stable_ = false;
    event_time_ms_ = raw_since_;
    publish_initial_state(false);
    pin_->attach_interrupt(&QueuedButton::interrupt_, this, gpio::INTERRUPT_ANY_EDGE);
  }

  void loop() override {
    if (draining_) return;
    draining_ = true;
    for (unsigned n = 0; n < QUEUE_SIZE; ++n) {
      Edge edge{};
      bool recovery = false;
      portENTER_CRITICAL(&mux_);
      if (overflow_) {
        edge = {last_time_, last_level_};
        head_ = tail_ = count_ = 0;
        overflow_ = false;
        recovery = true;
      } else if (count_) {
        edge = queue_[tail_];
        tail_ = (tail_ + 1) % QUEUE_SIZE;
        --count_;
      } else {
        portEXIT_CRITICAL(&mux_);
        break;
      }
      portEXIT_CRITICAL(&mux_);
      if (!recovery) qualify_(edge.time_ms);
      if (recovery || raw_ != edge.level) {
        raw_ = edge.level;
        raw_since_ = edge.time_ms;
      }
    }
    const uint32_t now = millis();
    portENTER_CRITICAL(&mux_);
    const bool pending = count_ != 0 || overflow_;
    portEXIT_CRITICAL(&mux_);
    if (!pending) qualify_(now);
    draining_ = false;
  }

 protected:
  static constexpr uint8_t QUEUE_SIZE = 32;
  static constexpr uint32_t DEBOUNCE_MS = 30;
  struct Edge { uint32_t time_ms; bool level; };
  InternalGPIOPin *pin_{nullptr};
  ISRInternalGPIOPin isr_pin_;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  Edge queue_[QUEUE_SIZE]{};
  uint8_t head_{0}, tail_{0}, count_{0};
  bool overflow_{false}, last_level_{false}, raw_{false}, stable_{false}, draining_{false};
  uint32_t last_time_{0}, raw_since_{0}, event_time_ms_{0};

  void qualify_(uint32_t time_ms) {
    if (raw_ != stable_ && static_cast<uint32_t>(time_ms - raw_since_) >= DEBOUNCE_MS) {
      stable_ = raw_;
      event_time_ms_ = raw_since_ + DEBOUNCE_MS;
      publish_state(stable_);
    }
  }

  static void IRAM_ATTR interrupt_(QueuedButton *button) {
    const bool level = button->isr_pin_.digital_read();
    const uint32_t now = millis();
    portENTER_CRITICAL_ISR(&button->mux_);
    if (level != button->last_level_) {
      button->last_level_ = level;
      button->last_time_ = now;
      if (button->count_ == QUEUE_SIZE) button->overflow_ = true;
      if (!button->overflow_) {
        button->queue_[button->head_] = {now, level};
        button->head_ = (button->head_ + 1) % QUEUE_SIZE;
        ++button->count_;
      }
    }
    portEXIT_CRITICAL_ISR(&button->mux_);
    button->enable_loop_soon_any_context();
  }
};

}  // namespace esphome::queued_button
