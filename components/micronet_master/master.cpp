// SPDX-License-Identifier: GPL-3.0-or-later
#include "master.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <cstdio>
namespace esphome { namespace micronet_master {
static const char *const TAG="micronet_master";
const char *Master::radio_error_() const {
  auto *transport=hub_->transport();
  if (!transport) return "No transmit-capable radio configured";
  if (!transport->ready()) {
    const char *reason=transport->failure_reason();
    return reason?reason:"Radio initialization failed";
  }
  return nullptr;
}
void Master::setup() {
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
  if (const char *error=radio_error_()) {
    ESP_LOGE(TAG,"Micronet master disabled: %s",error); mark_failed();
  }
}
void Master::start_pairing() {
  if (!initialized_) return;
  if (const char *error=radio_error_()) {
    ESP_LOGW(TAG,"Pairing blocked: %s",error); update_entities_(); return;
  }
  if (is_failed()) return;
  ESP_LOGI(TAG,"Pair button pressed: network=%08X",unsigned(network_id_));
  pair_requested_=!engine_.start_pairing(micros()); save_failed_=false; update_entities_();
}
void Master::on_packet(const uint8_t *data, size_t length) {
  if (!initialized_) return;
  const uint32_t before=engine_.candidate_id();
  engine_.receive(data,length,millis());
  if (length>=14) {
    const uint32_t source=micronet::be32(data+4);
    if (micronet::be32(data)!=network_id_) {
      ESP_LOGD(TAG,"RX ignored: network=%08X expected=%08X",unsigned(micronet::be32(data)),unsigned(network_id_));
    } else if (uint8_t(source>>24)!=2) {
      ESP_LOGD(TAG,"RX ignored: device=%08X is not a wind transducer",unsigned(source));
    } else if (data[8]==3) {
      if (engine_.candidate_id()==source && engine_.candidate_id()!=before)
        ESP_LOGI(TAG,"Wind join request accepted: device=%08X payload=%u",unsigned(source),engine_.candidate_payload());
      else ESP_LOGD(TAG,"Wind join request: device=%08X selected=%08X (may be duplicate, invalid or outside join phase)",
        unsigned(source),unsigned(engine_.candidate_id()));
    } else if (data[8]==2 && engine_.candidate_id()==source) {
      ESP_LOGD(TAG,"Candidate wind data: device=%08X confirmation_mask=%u (speed=1 angle=2)",unsigned(source),engine_.confirmation_mask());
    }
    if (data[8]==6 && length>=18 && data[14]==0xe2 && data[15]==9 &&
        micronet::checksum(data+14,length-15)==data[length-1])
      ESP_LOGW(TAG,"Wind low-battery alert received from device=%08X",unsigned(source));
  }
  if (engine_.take_changed()) {
    hub_->set_device_id(engine_.saved().wind);
    save_pair_(); update_entities_();
  }
}
void Master::save_pair_() {
  const auto &p=engine_.saved();
  ESP_LOGI(TAG,"Wind pairing confirmed: network=%08X device=%08X payload=%u; saving association",
    unsigned(p.network),unsigned(p.wind),p.payload);
  Record record{1,p.network,p.wind,p.payload};
  save_failed_=!preference_.save(&record) || !global_preferences->sync();
  if (save_failed_) ESP_LOGE(TAG,"Pairing works in RAM but flash save failed; retry pairing before reboot");
}
void Master::on_transmitted(bool success, uint32_t sync_end, uint32_t end) {
  engine_.completed(success,sync_end,end,millis());
}
void Master::loop() {
  if (!initialized_ || is_failed() || radio_error_()) return;
  if (pair_requested_ && engine_.start_pairing(micros())) pair_requested_=false;
  Frame frame;
  if (!hub_->transport()->busy() && engine_.next(micros(),millis(),frame)) {
    if (!hub_->transport()->transmit(frame.data.data(),frame.length,frame.preamble_ms))
      engine_.completed(false,micros(),micros(),millis());
  }
  if (millis()-last_entities_>=250) { last_entities_=millis(); update_entities_(); }
}
void Master::report_status_(const char *state) {
  if (logged_status_!=state) {
    if (engine_.failure()!=Failure::NONE) {
      ESP_LOGW(TAG,"%s (last candidate=%08X)",state,unsigned(engine_.last_candidate_id()));
    } else ESP_LOGI(TAG,"%s",state);
    logged_status_=state;
  }
  if (status_ && status_->state!=state) status_->publish_state(state);
}
void Master::update_entities_() {
  const char *radio_error=radio_error_();
  if (paired_) paired_->publish_state(engine_.paired());
  if (online_) online_->publish_state(!radio_error && engine_.online(millis()));
  if (pairing_) pairing_->publish_state(!radio_error && (engine_.pairing() || pair_requested_));
  char id[9];
  if (engine_.paired()) std::snprintf(id,sizeof(id),"%08X",unsigned(engine_.saved().wind));
  else id[0]=0;
  if (wind_id_ && wind_id_->state!=id) wind_id_->publish_state(id);
  if (radio_error) {
    const std::string message=std::string("Radio unavailable: ")+radio_error;
    report_status_(message.c_str());
    return;
  }
  const char *state="Unpaired";
  if (pair_requested_) state="Pairing requested; waiting for radio";
  else if (save_failed_) state="Paired in RAM; flash save failed";
  else if (engine_.failure()!=Failure::NONE) {
    switch (engine_.failure()) {
      case Failure::TX_FAILED: state="Pairing failed: radio transmission failed"; break;
      case Failure::NO_JOIN_REQUEST: state="Pairing timed out: no wind sensor join request"; break;
      case Failure::NO_WIND_DATA: state="Pairing timed out: sensor joined but no confirmed wind data"; break;
      case Failure::MISSING_SPEED: state="Pairing timed out: wind speed missing"; break;
      case Failure::MISSING_ANGLE: state="Pairing timed out: wind angle missing"; break;
      default: break;
    }
  }
  else switch (engine_.phase()) {
    case Phase::WAKE: state=engine_.pairing()?"Pairing: waking sensors":"Waking paired sensor"; break;
    case Phase::WAIT: state="Pairing: prepare (15 seconds)"; break;
    case Phase::JOIN: state="Pairing: accepting wind sensor (60 seconds)"; break;
    case Phase::CONFIRM: state="Pairing: confirming wind data"; break;
    case Phase::RUNNING: state=engine_.online(millis())?"Paired; receiving wind":"Paired; waiting for wind"; break;
    case Phase::FAILED: state="Pairing failed; no confirmed wind sensor"; break;
    default: break;
  }
  report_status_(state);
}
void Master::dump_config() {
  ESP_LOGCONFIG(TAG,"Standalone Micronet master network=%08X; missed async windows=%u",unsigned(network_id_),unsigned(engine_.missed_windows()));
}
}}
