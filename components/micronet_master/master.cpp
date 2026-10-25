// SPDX-License-Identifier: GPL-3.0-or-later
#include "master.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <cstdio>
namespace esphome { namespace micronet_master {
static const char *const TAG="micronet_master";
void Master::setup() {
  if (!hub_->transport()) { ESP_LOGE(TAG,"No transmit-capable radio configured"); mark_failed(); return; }
  if (!network_id_) {
    uint8_t mac[6]; get_mac_address_raw(mac);
    network_id_=0x81000000U|(uint32_t(mac[3])<<16)|(uint32_t(mac[4])<<8)|mac[5];
  }
  preference_=global_preferences->make_preference<Record>(0x4d4e5431U^network_id_,true);
  Record record{}; Pairing saved;
  if (preference_.load(&record) && record.version==1 && record.payload<=82) {
    saved={record.network,record.wind,uint8_t(record.payload)};
  }
  engine_.initialize(network_id_,saved,micros());
  hub_->set_network_id(network_id_); hub_->set_device_type(2);
  // An impossible device ID prevents publishing unpaired nearby wind devices.
  hub_->set_device_id(engine_.paired()?engine_.saved().wind:0);
  initialized_=true; update_entities_();
}
void Master::start_pairing() {
  if (!initialized_ || is_failed()) return;
  pair_requested_=!engine_.start_pairing(micros()); save_failed_=false; update_entities_();
}
void Master::on_packet(const uint8_t *data, size_t length) {
  if (!initialized_) return;
  engine_.receive(data,length,millis());
  if (engine_.take_changed()) {
    hub_->set_device_id(engine_.saved().wind);
    save_pair_(); update_entities_();
  }
}
void Master::save_pair_() {
  const auto &p=engine_.saved();
  Record record{1,p.network,p.wind,p.payload};
  save_failed_=!preference_.save(&record) || !global_preferences->sync();
  if (save_failed_) ESP_LOGE(TAG,"Pairing works in RAM but flash save failed; retry pairing before reboot");
}
void Master::on_transmitted(bool success, uint32_t sync_end, uint32_t end) {
  engine_.completed(success,sync_end,end,millis());
}
void Master::loop() {
  if (!initialized_) return;
  if (pair_requested_ && engine_.start_pairing(micros())) pair_requested_=false;
  Frame frame;
  if (!hub_->transport()->busy() && engine_.next(micros(),millis(),frame)) {
    if (!hub_->transport()->transmit(frame.data.data(),frame.length,frame.preamble_ms))
      engine_.completed(false,micros(),micros(),millis());
  }
  if (millis()-last_entities_>=250) { last_entities_=millis(); update_entities_(); }
}
void Master::update_entities_() {
  if (paired_) paired_->publish_state(engine_.paired());
  if (online_) online_->publish_state(engine_.online(millis()));
  if (pairing_) pairing_->publish_state(engine_.pairing() || pair_requested_);
  char id[9];
  if (engine_.paired()) std::snprintf(id,sizeof(id),"%08X",unsigned(engine_.saved().wind));
  else id[0]=0;
  if (wind_id_ && wind_id_->state!=id) wind_id_->publish_state(id);
  const char *state="Unpaired";
  if (pair_requested_) state="Pairing requested; waiting for radio";
  else if (save_failed_) state="Paired in RAM; flash save failed";
  else switch (engine_.phase()) {
    case Phase::WAKE: state=engine_.pairing()?"Pairing: waking sensors":"Waking paired sensor"; break;
    case Phase::WAIT: state="Pairing: prepare (15 seconds)"; break;
    case Phase::JOIN: state="Pairing: accepting wind sensor (60 seconds)"; break;
    case Phase::CONFIRM: state="Pairing: confirming wind data"; break;
    case Phase::RUNNING: state=engine_.online(millis())?"Paired; receiving wind":"Paired; waiting for wind"; break;
    case Phase::FAILED: state="Pairing failed; no confirmed wind sensor"; break;
    default: break;
  }
  if (status_ && status_->state!=state) status_->publish_state(state);
}
void Master::dump_config() {
  ESP_LOGCONFIG(TAG,"Standalone Micronet master network=%08X; missed async windows=%u",unsigned(network_id_),unsigned(engine_.missed_windows()));
}
}}
