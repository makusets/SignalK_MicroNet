// SPDX-License-Identifier: GPL-3.0-or-later
#include "micronet_cc1101.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
namespace esphome { namespace micronet_cc1101 {
static const char *const TAG = "micronet_cc1101";
void MicronetCC1101::write_reg_(uint8_t address, uint8_t value) {
  enable(); delayMicroseconds(10); transfer_byte(address); transfer_byte(value); disable();
}
uint8_t MicronetCC1101::read_reg_(uint8_t address) {
  enable(); delayMicroseconds(10);
  // Status registers require the burst bit even for a single read.
  transfer_byte(address | (address >= 0x30 ? 0xc0 : 0x80));
  uint8_t value = transfer_byte(0); disable(); return value;
}
void MicronetCC1101::strobe_(uint8_t command) {
  enable(); delayMicroseconds(10); transfer_byte(command); disable();
}
void MicronetCC1101::restart_() {
  strobe_(0x36); delayMicroseconds(100); // SIDLE
  strobe_(0x3a); // SFRX; never flush in RX
  write_reg_(0x06, 255); // Fixed length, adjusted after receiving the Micronet header.
  used_ = expected_ = 0; reception_started_ = false;
  strobe_(0x34); // SRX
}
void MicronetCC1101::setup() {
  spi_setup();
  strobe_(0x30); delay(2); // SRES
  uint8_t part = read_reg_(0x30), version = read_reg_(0x31);
  if (part != 0 || version == 0 || version == 0xff) {
    ESP_LOGE(TAG, "CC1101 not found: part=%02X version=%02X", part, version);
    mark_failed(); return;
  }
  // 26 MHz crystal. 2-FSK, 76.77 kbaud, +/-38.09 kHz deviation,
  // 203 kHz RX bandwidth, 16/16 sync 0x5599, no whitening/CRC/address bytes.
  const uint8_t config[][2] = {
    {0x00, 0x2e}, {0x01, 0x2e}, {0x02, 0x2e}, // GDOs high impedance
    {0x03, 0x07}, {0x04, 0x55}, {0x05, 0x99},
    {0x07, 0x00}, {0x08, 0x00}, {0x0a, 0x00}, {0x0b, 0x06},
    {0x10, 0x8a}, {0x11, 0x83}, {0x12, 0x02}, {0x13, 0x22}, {0x14, 0xf8},
    {0x15, 0x44}, {0x17, 0x00}, {0x18, 0x18},
    {0x19, 0x16}, {0x1a, 0x6c}, {0x1b, 0x43}, {0x1c, 0x40}, {0x1d, 0x91},
    {0x21, 0x56}, {0x22, 0x10}, {0x23, 0xe9}, {0x24, 0x2a}, {0x25, 0x00}, {0x26, 0x1f},
    {0x2c, 0x81}, {0x2d, 0x35}, {0x2e, 0x09}
  };
  for (auto &reg : config) write_reg_(reg[0], reg[1]);
  uint32_t frequency_word = uint32_t(frequency_ * 65536.0 / 26.0 + 0.5);
  write_reg_(0x0d, frequency_word >> 16); write_reg_(0x0e, frequency_word >> 8); write_reg_(0x0f, frequency_word);
  restart_(); fast_loop_.start();
}
void MicronetCC1101::loop() {
  // Poll fast enough to drain the 64-byte FIFO during packets up to 96 bytes.
  // Leave one byte while receiving (CC1101 RX FIFO erratum); drain the final
  // byte after the hardware fixed-length packet counter has reached its end.
  uint8_t count = read_reg_(0x3b), confirm = read_reg_(0x3b);
  if ((count | confirm) & 0x80) { ++overflows_; restart_(); return; }
  if (count != confirm) return;
  count &= 0x7f;
  if (count > 64) { ++overflows_; restart_(); return; }
  if (count && !reception_started_) { started_ = millis(); reception_started_ = true; }
  bool complete = expected_ && used_ + count >= expected_;
  size_t available = complete ? count : (count ? count - 1 : 0);
  while (available-- && used_ < sizeof(packet_)) {
    packet_[used_++] = read_reg_(0x3f);
    if (used_ == 14) {
      if (!micronet::header_valid(packet_, used_)) { restart_(); return; }
      expected_ = size_t(packet_[12]) + 2;
      write_reg_(0x06, expected_);
    }
    if (expected_ && used_ == expected_) {
      parent_->receive(packet_, used_); restart_(); return;
    }
  }
  if (reception_started_ && millis() - started_ > 30) { ++timeouts_; restart_(); }
}
void MicronetCC1101::dump_config() {
  ESP_LOGCONFIG(TAG, "Micronet CC1101 RX at %.3f MHz; overflow=%u timeout=%u", frequency_, unsigned(overflows_), unsigned(timeouts_));
}
}}
