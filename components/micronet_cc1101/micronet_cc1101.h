// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/gpio.h"
#include <atomic>
#ifdef USE_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
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
  bool ready() const override { return initialized_ && !is_failed(); }
  const char *failure_reason() const override { return failure_reason_; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
 protected:
  static void IRAM_ATTR edge_(MicronetCC1101 *self);
  void log_diagnostics_();
  void service_tx_();
  void start_tx_();
  bool pump_tx_();
  void stop_tx_(bool success);
  void fill_tx_(size_t count);
#ifdef USE_ESP32
  static void tx_task_(void *arg);
  TaskHandle_t tx_task_handle_{nullptr};
#endif
  void finish_tx_(bool success);
  void write_fifo_(const uint8_t *data, size_t length);
  void read_fifo_(uint8_t *data, size_t length);
  InternalGPIOPin *gdo0_pin_{nullptr};
  ISRInternalGPIOPin isr_gdo0_;
  std::atomic<bool> capture_tx_{false}, sync_seen_{false}, end_seen_{false};
  std::atomic<uint32_t> sync_end_us_{0}, tx_end_us_{0};
  bool initialized_{false}, transmitting_{false};
  const char *failure_reason_{"CC1101 SPI identification failed"};
  // The worker exclusively owns SPI between notification and tx_done_.
  // Publishing and logging remain in the ESPHome loop after RX is restored.
  std::atomic<bool> tx_done_{false};
  bool tx_success_{false}, tx_fixed_{false};
  uint8_t tx_error_bytes_{0}, tx_error_state_{0};
  uint32_t tx_preamble_bytes_{0}, tx_total_{0}, tx_written_{0};
  uint8_t chip_part_{0xff}, chip_version_{0xff};
  uint8_t rate_exponent_{0},rate_mantissa_{0};
  double baud_{0};
  uint32_t tx_timeout_{0}, tx_strobe_us_{0}, tx_preamble_ms_{0};
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
  uint32_t started_{0}, overflows_{0}, timeouts_{0}, header_rejects_{0}, rx_frames_{0}, last_diagnostics_{0}, unstable_samples_{0}, deferred_reads_{0};
  HighFrequencyLoopRequester fast_loop_;
};
}}
