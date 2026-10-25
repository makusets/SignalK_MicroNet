// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "engine.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/micronet/micronet.h"
#include "esphome/components/button/button.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
namespace esphome { namespace micronet_master {
class Master : public Component, public micronet::PacketListener {
 public:
  void set_hub(micronet::Micronet *hub) { hub_=hub; hub->set_listener(this); }
  void set_network_id(uint32_t id) { network_id_=id; }
  void set_paired(binary_sensor::BinarySensor *s) { paired_=s; }
  void set_online(binary_sensor::BinarySensor *s) { online_=s; }
  void set_pairing(binary_sensor::BinarySensor *s) { pairing_=s; }
  void set_status(text_sensor::TextSensor *s) { status_=s; }
  void set_wind_id(text_sensor::TextSensor *s) { wind_id_=s; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void start_pairing();
  void on_packet(const uint8_t *data, size_t length) override;
  void on_transmitted(bool success, uint32_t sync_end, uint32_t end) override;
 protected:
  void update_entities_();
  void save_pair_();
  struct Record { uint32_t version,network,wind,payload; };
  micronet::Micronet *hub_{nullptr};
  uint32_t network_id_{0},last_entities_{0};
  bool initialized_{false},save_failed_{false},pair_requested_{false};
  Engine engine_;
  ESPPreferenceObject preference_;
  binary_sensor::BinarySensor *paired_{nullptr},*online_{nullptr},*pairing_{nullptr};
  text_sensor::TextSensor *status_{nullptr},*wind_id_{nullptr};
};
class PairButton : public button::Button {
 public:
  void set_parent(Master *parent) { parent_=parent; }
 protected:
  void press_action() override { parent_->start_pairing(); }
  Master *parent_{nullptr};
};
}}
