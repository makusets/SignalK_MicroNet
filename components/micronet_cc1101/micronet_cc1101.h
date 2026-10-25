// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/gpio.h"
#include <atomic>
#include "esphome/components/spi/spi.h"
#include "esphome/components/micronet/micronet.h"
namespace esphome { namespace micronet_cc1101 {
class MicronetCC1101 : public Component, public micronet::RadioTransport,
    public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_1MHZ> {
 public:
  void set_parent(micronet::Micronet *parent) { parent_ = parent; }
  void set_frequency(float frequency) { frequency_ = frequency; }
  void set_gdo0_pin(InternalGPIOPin *pin) { gdo0_pin_=pin; }
  bool transmit(const uint8_t *data, size_t length, uint32_t preamble_ms) override;
  bool busy() const override { return transmitting_ || reception_started_; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
 protected:
  static void IRAM_ATTR edge_(MicronetCC1101 *self);
  void service_tx_();
  void finish_tx_(bool success);
  void write_fifo_(const uint8_t *data, size_t length);
  InternalGPIOPin *gdo0_pin_{nullptr};
  ISRInternalGPIOPin isr_gdo0_;
  std::atomic<bool> capture_tx_{false}, sync_seen_{false}, end_seen_{false};
  std::atomic<uint32_t> sync_end_us_{0}, tx_end_us_{0};
  bool transmitting_{false}, preamble_pending_{false};
  uint32_t fill_at_{0}, tx_timeout_{0};
  uint8_t tx_data_[48]{};
  size_t tx_length_{0};
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
