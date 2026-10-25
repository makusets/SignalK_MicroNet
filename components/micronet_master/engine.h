// SPDX-License-Identifier: GPL-3.0-or-later
// New standalone scheduler. Protocol facts: MicroNav/MicronetToNMEA and
// mnsnoop/esp32Micronet's published pairing captures (see docs/standalone.md).
#pragma once
#include "esphome/components/micronet/protocol.h"
#include <array>
namespace esphome { namespace micronet_master {
using micronet::be32;
using micronet::checksum;
inline bool reached(uint32_t now, uint32_t deadline) { return int32_t(now-deadline) >= 0; }
inline void put32(uint8_t *p, uint32_t v) {
  p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v;
}
struct Pairing {
  uint32_t network{0}, wind{0};
  uint8_t payload{12};
  bool valid() const { return network && uint8_t(wind >> 24)==2 && payload>=6 && payload<=82; }
};
struct Frame {
  std::array<uint8_t, 48> data{};
  size_t length{0};
  uint32_t preamble_ms{0};
};
inline Frame header(uint32_t network, uint8_t message, uint8_t flags) {
  Frame f; put32(f.data.data(),network); put32(f.data.data()+4,network);
  f.data[8]=message; f.data[9]=flags; f.data[11]=checksum(f.data.data(),11);
  f.length=14; f.data[12]=f.data[13]=12; return f;
}
inline Frame poll_frame(uint32_t network, uint32_t wind, uint8_t payload, uint8_t flags) {
  Frame f=header(network,1,flags);
  // The master's first entry reserves its own packet's payload, as in captures.
  size_t count=wind ? 2 : 1;
  put32(f.data.data()+14,network); f.data[18]=uint8_t(5*count+3);
  if (wind) { put32(f.data.data()+19,wind); f.data[23]=payload; }
  f.length=14+5*count+3;
  f.data[f.length-1]=checksum(f.data.data()+14,f.length-15);
  f.data[12]=f.data[13]=f.length-2; return f;
}
inline uint32_t slot_us(uint8_t payload) {
  if (!payload) return 0;
  uint32_t raw=1771+1456+104*uint32_t(payload)+1893;
  return ((raw+243)/244)*244;
}
enum class Failure { NONE, TX_FAILED, NO_JOIN_REQUEST, NO_WIND_DATA, MISSING_SPEED, MISSING_ANGLE };
enum class Phase { IDLE, WAKE, WAIT, JOIN, CONFIRM, RUNNING, FAILED };
class Engine {
 public:
  void initialize(uint32_t network, Pairing saved, uint32_t now_us) {
    network_=network; saved_=saved.valid() && saved.network==network ? saved : Pairing{};
    phase_=saved_.valid() ? Phase::WAKE : Phase::IDLE;
    next_poll_=now_us+3000000; // Let the radio and web interface initialize.
  }
  bool start_pairing(uint32_t now_us) {
    if (pairing()) return true;
    if (awaiting_) return false;
    candidate_={}; wind_mask_=0; failure_=Failure::NONE; last_candidate_=0; pairing_requested_=true; changed_=false;
    phase_=Phase::WAKE; next_poll_=now_us; have_grid_=false; have_last_sync_=false; async_pending_=false; return true;
  }
  bool pairing() const { return pairing_requested_; }
  bool paired() const { return saved_.valid(); }
  bool online(uint32_t now_ms) const { return seen_saved_ && now_ms-last_saved_ms_<10000; }
  const Pairing &saved() const { return saved_; }
  Phase phase() const { return phase_; }
  uint32_t candidate_id() const { return candidate_.wind; }
  uint32_t last_candidate_id() const { return last_candidate_; }
  uint8_t candidate_payload() const { return candidate_.payload; }
  uint8_t confirmation_mask() const { return wind_mask_; }
  Failure failure() const { return failure_; }
  bool take_changed() { bool c=changed_; changed_=false; return c; }
  uint32_t missed_windows() const { return missed_; }
  uint32_t next_poll_deadline() const { return next_poll_; }
  uint32_t completed_polls() const { return completed_polls_; }
  uint32_t poll_interval_us() const { return poll_interval_us_; }
  int32_t poll_error_us() const { return poll_error_us_; }
  uint32_t skipped_polls() const { return skipped_polls_; }
  // One call emits at most one packet. Missed async slots are dropped, never
  // sent late into another device's response slot. Unsigned clocks may wrap.
  bool next(uint32_t now_us, uint32_t now_ms, Frame &frame) {
    if (awaiting_) return false;
    if (phase_==Phase::WAIT && now_ms-phase_ms_>=15000) { phase_=Phase::JOIN; phase_ms_=now_ms; }
    if (phase_==Phase::JOIN && now_ms-phase_ms_>=60000) {
      phase_=Phase::CONFIRM; phase_ms_=now_ms; wind_mask_=0; normal_poll_sent_=false;
    }
    if (phase_==Phase::CONFIRM && now_ms-phase_ms_>=30000) {
      Failure failure=Failure::NO_JOIN_REQUEST;
      if (candidate_.wind) {
        failure=Failure::NO_WIND_DATA;
        if (now_ms-last_candidate_ms_<=5000) {
          if (wind_mask_==1) failure=Failure::MISSING_ANGLE;
          else if (wind_mask_==2) failure=Failure::MISSING_SPEED;
        }
      }
      fail_pairing_(failure);
    }
    if (phase_==Phase::IDLE || phase_==Phase::FAILED) return false;
    if (async_pending_ && reached(now_us,async_at_)) {
      async_pending_=false;
      if (now_us-async_at_<=244) {
        frame=header(network_,async_type_,flags_()); awaiting_=true; pending_=async_type_; return true;
      }
      ++missed_;
    }
    if (!reached(now_us,next_poll_)) return false;
    Pairing target=target_();
    frame=poll_frame(network_,target.wind,target.payload,flags_());
    if (phase_==Phase::WAKE) frame.preamble_ms=6000;
    awaiting_=true; pending_=1; launched_us_=now_us; long_preamble_=frame.preamble_ms!=0;
    // Remember the actual advertised layout, even if an incoming request
    // changes the next cycle's allocation before the async window arrives.
    advertised_wind_=target.wind;
    advertised_payload_=target.wind ? target.payload : 0;
    advertised_master_payload_=frame.length-14;
    return true;
  }
  void completed(bool success, uint32_t sync_end_us, uint32_t end_us, uint32_t now_ms) {
    if (!awaiting_) return;
    awaiting_=false;
    if (!success) {
      if (pairing()) fail_pairing_(Failure::TX_FAILED);
      else { next_poll_=end_us+1000000; phase_=saved_.valid()?Phase::WAKE:Phase::FAILED; }
      return;
    }
    if (pending_!=1) return;
    ++completed_polls_;
    poll_interval_us_=have_last_sync_?sync_end_us-last_sync_us_:0;
    last_sync_us_=sync_end_us; have_last_sync_=true;
    poll_error_us_=have_grid_?int32_t(sync_end_us-next_sync_us_):0;
    if (!have_grid_) { next_sync_us_=sync_end_us; have_grid_=true; }
    next_sync_us_+=1000000;
    if (phase_==Phase::WAKE) {
      phase_=pairing_requested_?Phase::WAIT:Phase::RUNNING; phase_ms_=now_ms;
    }
    if (phase_==Phase::CONFIRM) normal_poll_sent_=true;
    if (!long_preamble_) {
      uint32_t observed=sync_end_us-launched_us_;
      if (observed>=1875 && observed<50000) turnaround_us_=(turnaround_us_+observed-1875)/2;
    }
    // Keep a fixed grid instead of adding every late launch to the next
    // period. If the loop was blocked for whole periods, skip rather than burst.
    next_poll_=next_sync_us_-1875-turnaround_us_;
    if (reached(end_us,next_poll_)) {
      const uint32_t skip=(end_us-next_poll_)/1000000+1;
      skipped_polls_+=skip; next_sync_us_+=skip*1000000; next_poll_+=skip*1000000;
    }
    // Anchor slots to the captured sync edge, not to a delayed ESPHome loop.
    async_at_=sync_end_us+slot_us(advertised_master_payload_)+slot_us(advertised_payload_)+7200-1771-turnaround_us_;
    ack_at_=async_at_+7200+(advertised_wind_?5328:0);
    cycle_valid_=true;
    if (!pairing() && saved_.valid() && now_ms-last_ping_ms_>=60000) {
      async_pending_=true; async_type_=0x0a; last_ping_ms_=now_ms;
    }
  }
  void receive(const uint8_t *p, size_t n, uint32_t now_ms) {
    micronet::Decoded decoded;
    if (!micronet::decode(p,n,decoded) || decoded.network_id!=network_ || uint8_t(decoded.device_id>>24)!=2) return;
    uint32_t wind=decoded.device_id;
    if (decoded.message_id==3) {
      if (n!=17 || checksum(p+14,2)!=p[16] || p[15]<6 || p[15]>82) return;
      if (phase_==Phase::JOIN && (!candidate_.wind || candidate_.wind==wind)) {
        candidate_={network_,wind,p[15]};
      } else if (saved_.valid() && saved_.wind==wind) saved_.payload=p[15];
    } else if (decoded.message_id==5) {
      if (n!=16 || p[14]!=p[15] || p[14]<6 || p[14]>82) return;
      if (candidate_.wind==wind) candidate_.payload=p[14];
      else if (saved_.valid() && saved_.wind==wind) saved_.payload=p[14];
    } else if (decoded.message_id==2) {
      bool speed=false,angle=false;
      for (size_t i=0;i<decoded.count;++i) {
        speed |= decoded.readings[i].kind==micronet::Measurement::WIND_SPEED;
        angle |= decoded.readings[i].kind==micronet::Measurement::WIND_ANGLE;
      }
      if (saved_.valid() && saved_.wind==wind && (speed||angle)) { last_saved_ms_=now_ms; seen_saved_=true; }
      if (candidate_.wind==wind && phase_==Phase::CONFIRM && normal_poll_sent_) {
        if (now_ms-last_candidate_ms_>5000) wind_mask_=0;
        wind_mask_|=(speed?1:0)|(angle?2:0); last_candidate_ms_=now_ms;
        if (wind_mask_==3) {
          saved_=candidate_; changed_=true; pairing_requested_=false; phase_=Phase::RUNNING;
          seen_saved_=true; last_saved_ms_=now_ms;
        }
      }
    } else if (decoded.message_id==6 && n>=18 && checksum(p+14,n-15)==p[n-1] && p[14]==0xe2) {
      // Acknowledge wind battery alarms, not unsupported setting changes.
      if (cycle_valid_ && wind==advertised_wind_) { async_type_=7; async_at_=ack_at_; async_pending_=true; }
    }
  }
 protected:
  uint8_t flags_() const {
    if (pairing_requested_ && (phase_==Phase::WAKE || phase_==Phase::WAIT)) return 0x0f;
    if (phase_==Phase::JOIN) return 0x0d;
    return 9;
  }
  Pairing target_() const { return candidate_.wind && pairing_requested_?candidate_:saved_; }
  void fail_pairing_(Failure failure) {
    failure_=failure; last_candidate_=candidate_.wind;
    pairing_requested_=false; candidate_={}; wind_mask_=0; async_pending_=false;
    phase_=saved_.valid()?Phase::RUNNING:Phase::FAILED;
  }
  Pairing saved_{},candidate_{};
  Phase phase_{Phase::IDLE};
  Failure failure_{Failure::NONE};
  uint32_t last_candidate_{0};
  uint32_t network_{0}, next_poll_{0}, phase_ms_{0}, async_at_{0},ack_at_{0};
  uint32_t next_sync_us_{0},last_sync_us_{0},poll_interval_us_{0},completed_polls_{0},skipped_polls_{0};
  int32_t poll_error_us_{0};
  bool have_grid_{false},have_last_sync_{false};
  uint32_t launched_us_{0},turnaround_us_{500};
  bool long_preamble_{false};
  uint32_t last_ping_ms_{0},last_saved_ms_{0},last_candidate_ms_{0},missed_{0},advertised_wind_{0};
  uint8_t pending_{0},async_type_{0},wind_mask_{0},advertised_payload_{0},advertised_master_payload_{0};
  bool awaiting_{false},pairing_requested_{false},changed_{false},seen_saved_{false};
  bool normal_poll_sent_{false},cycle_valid_{false},async_pending_{false};
};
}}
