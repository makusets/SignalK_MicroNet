// SPDX-License-Identifier: GPL-3.0-or-later
#include "lsm303dlhc.h"
#include <cmath>
namespace esphome { namespace lsm303dlhc {
void LSM303DLHC::setup() {
  accelerometer_.set_i2c_bus(bus_); accelerometer_.set_i2c_address(0x19);
  uint8_t identity[3];
  if (!read_bytes(0x0a, identity, 3) || identity[0] != 'H' || identity[1] != '4' || identity[2] != '3' ||
      !accelerometer_.write_byte(0x20, 0x47) || !accelerometer_.write_byte(0x23, 0x88) ||
      !write_byte(0x00, 0x10) || !write_byte(0x01, 0x20) || !write_byte(0x02, 0x00)) mark_failed();
}
void LSM303DLHC::update() {
  uint8_t m[6]{}, a[6]{}; float values[6];
  bool ok = read_bytes(0x03, m, 6) && accelerometer_.read_bytes(0x28 | 0x80, a, 6);
  auto signed_word = [](unsigned v) { return v >= 32768 ? int(v) - 65536 : int(v); };
  int mx = signed_word((unsigned(m[0]) << 8) | m[1]);
  int my = signed_word((unsigned(m[4]) << 8) | m[5]);
  int mz = signed_word((unsigned(m[2]) << 8) | m[3]);
  ok = ok && mx != -4096 && my != -4096 && mz != -4096;
  if (!ok) { status_set_warning(); for (auto *sensor : sensors_) if (sensor) sensor->publish_state(NAN); return; }
  status_clear_warning();
  values[0] = mx * (100.0f / 1100.0f); values[1] = my * (100.0f / 1100.0f); values[2] = mz * (100.0f / 980.0f);
  for (size_t i = 0; i < 3; ++i) values[i + 3] = signed_word((unsigned(a[2*i+1]) << 8) | a[2*i]) / 16 * 0.00980665f;
  for (size_t i = 0; i < 6; ++i) if (sensors_[i]) sensors_[i]->publish_state(values[i]);
}
}}
