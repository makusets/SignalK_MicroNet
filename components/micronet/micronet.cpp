// SPDX-License-Identifier: GPL-3.0-or-later
#include "micronet.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
namespace esphome { namespace micronet {
static const char *const TAG = "micronet";
bool Micronet::receive(const uint8_t *data, size_t length) {
  Decoded packet;
  if (!decode(data, length, packet)) { ++rejected_; return false; }
  if ((filter_network_ && packet.network_id != network_id_) ||
      (filter_device_ && packet.device_id != device_id_) ||
      (filter_type_ && uint8_t(packet.device_id >> 24) != device_type_)) { ++filtered_; return false; }
  ++accepted_;
  ESP_LOGD(TAG, "RX network=%08X device=%08X type=%02X readings=%u", unsigned(packet.network_id),
           unsigned(packet.device_id), packet.message_id, unsigned(packet.count));
  for (size_t i = 0; i < packet.count; ++i) {
    auto reading = packet.readings[i]; size_t index = size_t(reading.kind);
    updated_[index] = millis(); valid_[index] = true;
    if (sensors_[index]) sensors_[index]->publish_state(reading.value);
  }
  return true;
}
void Micronet::loop() {
  uint32_t now = millis();
  for (size_t i = 0; i < COUNT; ++i) {
    if (valid_[i] && now - updated_[i] >= stale_after_) {
      valid_[i] = false;
      if (sensors_[i]) sensors_[i]->publish_state(NAN);
    }
  }
}
void Micronet::dump_config() {
  ESP_LOGCONFIG(TAG, "Micronet receive-only bridge; accepted=%u rejected=%u filtered=%u", unsigned(accepted_), unsigned(rejected_), unsigned(filtered_));
  if (filter_network_) ESP_LOGCONFIG(TAG, "Network filter: %08X", unsigned(network_id_));
}
}}
