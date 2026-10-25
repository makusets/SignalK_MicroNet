// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/micronet/micronet.h"
namespace esphome { namespace micronet_cc1101 {
class MicronetCC1101 : public Component,
    public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_1MHZ> {
 public:
  void set_parent(micronet::Micronet *parent) { parent_ = parent; }
  void set_frequency(float frequency) { frequency_ = frequency; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
 protected:
  void write_reg_(uint8_t address, uint8_t value);
  uint8_t read_reg_(uint8_t address);
  void strobe_(uint8_t command);
  void restart_();
  micronet::Micronet *parent_{nullptr};
  float frequency_{869.84f};
  uint8_t packet_[micronet::MAX_PACKET]{};
  size_t used_{0}, expected_{0};
  bool reception_started_{false};
  uint32_t started_{0}, overflows_{0}, timeouts_{0};
  HighFrequencyLoopRequester fast_loop_;
};
}}
