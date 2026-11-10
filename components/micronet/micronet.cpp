// SPDX-License-Identifier: GPL-3.0-or-later
#include "micronet.h"
#include "packet_log.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
namespace esphome { namespace micronet {
static const char *const TAG = "micronet";
bool Micronet::receive(const uint8_t *data, size_t length, float rssi) {
  log_packet_debug(TAG,"RX frame",data,length);
  Decoded packet;
  if (!decode(data, length, packet)) {
    ++rejected_; ESP_LOGD(TAG,"RX rejected: invalid packet length or checksum"); return false;
  }
  if (listener_) listener_->on_packet(data, length);
  // During pairing device ID zero blocks wind publication, but RSSI from the
  // selected network/type is still useful for finding the sensor.
  if (signal_strength_ && std::isfinite(rssi) &&
      (!filter_network_ || packet.network_id==network_id_) &&
      (!filter_type_ || uint8_t(packet.device_id>>24)==device_type_) &&
      (!filter_device_ || device_id_==0 || packet.device_id==device_id_)) {
    signal_strength_->publish_state(rssi); signal_updated_=millis(); signal_valid_=true;
    ESP_LOGD(TAG,"RX signal: device=%08X RSSI=%.1f dBm",unsigned(packet.device_id),rssi);
  }
  if ((filter_network_ && packet.network_id != network_id_) ||
      (filter_device_ && packet.device_id != device_id_) ||
      (filter_type_ && uint8_t(packet.device_id >> 24) != device_type_)) {
    ++filtered_; ESP_LOGD(TAG,"RX not published: measurement filter (master still inspected the packet)"); return false;
  }
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
  if (signal_valid_ && now-signal_updated_>=stale_after_) {
    signal_valid_=false; signal_strength_->publish_state(NAN);
  }
  for (size_t i = 0; i < COUNT; ++i) {
    if (valid_[i] && now - updated_[i] >= stale_after_) {
      valid_[i] = false;
      if (sensors_[i]) sensors_[i]->publish_state(NAN);
    }
  }
}
void Micronet::dump_config() {
  ESP_LOGCONFIG(TAG, "Micronet bridge; accepted=%u rejected=%u filtered=%u", unsigned(accepted_), unsigned(rejected_), unsigned(filtered_));
  if (filter_network_) ESP_LOGCONFIG(TAG, "Network filter: %08X", unsigned(network_id_));
}
}}
