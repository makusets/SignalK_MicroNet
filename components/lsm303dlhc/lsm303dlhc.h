// SPDX-License-Identifier: GPL-3.0-or-later
// LSM303DLHC register interpretation informed by MicroNav (Ronan Demoment,
// Dietmar Warning); new ESPHome implementation, 2026-10-25.
#pragma once
#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include <array>
namespace esphome { namespace lsm303dlhc {
class LSM303DLHC : public PollingComponent, public i2c::I2CDevice {
 public:
  void set_sensor(size_t index, sensor::Sensor *sensor) { sensors_[index] = sensor; }
  void setup() override;
  void update() override;
 protected:
  i2c::I2CDevice accelerometer_;
  std::array<sensor::Sensor *, 6> sensors_{};
};
}}
