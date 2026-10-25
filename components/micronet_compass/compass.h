// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/sensor/sensor.h"
#include "math.h"
#include <array>
namespace esphome { namespace micronet_compass {
class Compass : public sensor::Sensor, public PollingComponent {
 public:
  void set_input(size_t index, sensor::Sensor *sensor) {
    inputs_[index] = sensor;
    sensor->add_on_state_callback([this, index](float value) { values_[index] = value; updated_[index] = millis(); seen_[index] = true; });
  }
  void set_heading_offset(float degrees) { offset_ = degrees * 0.017453292519943295f; }
  void set_stale_after(uint32_t ms) { stale_after_ = ms; }
  void update() override {
    for (size_t i = 0; i < 6; ++i) {
      if (!seen_[i] || millis() - updated_[i] >= stale_after_) { publish_state(NAN); return; }
    }
    publish_state(heading(values_[0], values_[1], values_[2], values_[3], values_[4], values_[5], offset_));
  }
 protected:
  std::array<sensor::Sensor *, 6> inputs_{};
  std::array<float, 6> values_{};
  std::array<uint32_t, 6> updated_{};
  std::array<bool, 6> seen_{};
  float offset_{0};
  uint32_t stale_after_{2000};
};
}}
