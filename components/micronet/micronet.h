// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "protocol.h"
#include "transport.h"

namespace esphome { namespace micronet {
class Micronet : public Component {
 public:
  void set_network_id(uint32_t id) { network_id_ = id; filter_network_ = true; }
  void set_device_id(uint32_t id) { device_id_ = id; filter_device_ = true; }
  void set_device_type(uint8_t type) { device_type_ = type; filter_type_ = true; }
  void set_stale_after(uint32_t ms) { stale_after_ = ms; }
  void set_sensor(Measurement kind, sensor::Sensor *s) { sensors_[size_t(kind)] = s; }
  void set_listener(PacketListener *listener) { listener_ = listener; }
  void set_transport(RadioTransport *transport) { transport_ = transport; }
  RadioTransport *transport() const { return transport_; }
  void transmitted(bool success, uint32_t sync_end, uint32_t end) {
    if (listener_) listener_->on_transmitted(success, sync_end, end);
  }
  // Radio adapters deliver bytes AFTER sync 0x99, with the complete 14-byte header.
  bool receive(const uint8_t *data, size_t length);
  void loop() override;
  void dump_config() override;
 protected:
  PacketListener *listener_{nullptr};
  RadioTransport *transport_{nullptr};
  static constexpr size_t COUNT = size_t(Measurement::COUNT);
  std::array<sensor::Sensor *, COUNT> sensors_{};
  std::array<uint32_t, COUNT> updated_{};
  std::array<bool, COUNT> valid_{};
  uint32_t network_id_{0}, device_id_{0}, stale_after_{10000};
  uint8_t device_type_{0};
  bool filter_network_{false}, filter_device_{false}, filter_type_{false};
  uint32_t accepted_{0}, rejected_{0}, filtered_{0};
};
}}
