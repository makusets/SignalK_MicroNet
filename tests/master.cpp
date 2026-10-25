#include "components/micronet_master/engine.h"
#include "components/micronet_master/master.h"
#include "esphome/core/hal.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace esphome;
using namespace esphome::micronet_master;
constexpr uint32_t NETWORK=0x83037737, WIND=0x02039087;
static std::vector<uint8_t> control(uint32_t network,uint32_t device,uint8_t type,std::initializer_list<uint8_t> payload) {
  auto h=header(network,type,1);
  put32(h.data.data()+4,device); h.data[10]=9; h.data[11]=micronet::checksum(h.data.data(),11);
  std::vector<uint8_t> p(h.data.begin(),h.data.begin()+14);
  p.insert(p.end(),payload); if(payload.size()) p.push_back(micronet::checksum(p.data()+14,payload.size()));
  p[12]=p[13]=p.size()-2; return p;
}
static std::vector<uint8_t> wind(uint32_t device=WIND) {
  std::vector<uint8_t> p{0x83,3,0x77,0x37,2,3,0x90,0x87,2,1,9,0x5c,0x18,0x18,
                       4,5,5,0,0x2c,0x3a,4,6,5,0xff,0xfb,9};
  put32(p.data()+4,device); p[11]=micronet::checksum(p.data(),11); return p;
}
static void step(Engine &e,uint32_t ms,uint32_t us=0) {
  Frame f;
  if(e.next(us?us:ms*1000,ms,f)) e.completed(true,(us?us:ms*1000)+2375,(us?us:ms*1000)+5000,ms);
}
int main() {
  // Exact ping fixture from MicronetToNMEA/doc/Micronet.txt.
  auto ping=header(NETWORK,0x0a,9);
  const uint8_t expected[]{0x83,3,0x77,0x37,0x83,3,0x77,0x37,0x0a,9,0,0x7b,12,12};
  assert(std::equal(ping.data.begin(),ping.data.begin()+14,expected));
  auto poll=poll_frame(NETWORK,WIND,12,0x0d);
  assert(poll.length==27 && poll.data[9]==0x0d && poll.data[18]==13 && poll.data[23]==12);
  assert(micronet::be32(poll.data.data()+14)==NETWORK && micronet::be32(poll.data.data()+19)==WIND);
  assert(poll.data[26]==micronet::checksum(poll.data.data()+14,12));
  assert(slot_us(0)==0 && slot_us(12)==6588 && slot_us(13)==6588);
  Engine e; e.initialize(NETWORK,{},0); Frame f;
  assert(!e.paired() && !e.next(0,0,f));
  e.start_pairing(0); assert(e.pairing());
  assert(e.next(0,0,f) && f.preamble_ms==6000 && f.data[9]==0x0f);
  assert(e.start_pairing(1000)); // Re-press does not restart an active attempt.
  assert(!e.next(1000,1,f)); // No reentrant transmission while the wake burst is active.
  e.completed(true,6000000,6003000,6003);
  assert(e.phase()==Phase::WAIT);
  auto req=control(NETWORK,WIND,3,{1,12});
  e.receive(req.data(),req.size(),7000); assert(e.candidate_id()==0); // No admission in prepare phase.
  step(e,21003); assert(e.phase()==Phase::JOIN);
  auto bad=req; bad.back()^=1; e.receive(bad.data(),bad.size(),22000); assert(e.candidate_id()==0);
  auto foreign=control(0x12345678,WIND,3,{0,12}); e.receive(foreign.data(),foreign.size(),22000);
  auto hull=control(NETWORK,0x01000001,3,{0,12}); e.receive(hull.data(),hull.size(),22000);
  assert(e.candidate_id()==0);
  e.receive(req.data(),req.size(),22000); assert(e.candidate_id()==WIND);
  auto other=control(NETWORK,0x02000001,3,{0,12}); e.receive(other.data(),other.size(),22000);
  assert(e.candidate_id()==WIND); // A second sensor cannot replace a candidate mid-pairing.
  auto oversize=control(NETWORK,WIND,5,{83}); e.receive(oversize.data(),oversize.size(),23000);
  assert(e.next(23000000,23000,f) && f.data[23]==12);
  e.completed(true,23002375,23005000,23005);
  auto resize=control(NETWORK,WIND,5,{18}); e.receive(resize.data(),resize.size(),24000);
  assert(e.next(24000000,24000,f) && f.data[23]==18);
  e.completed(true,24002375,24005000,24005);
  auto data=wind(); e.receive(data.data(),data.size(),25000); assert(!e.paired());
  // Keep the JOIN status for the full sixty seconds, then confirm only after
  // a normal-status poll. Data during the JOIN phase never commits a pairing.
  assert(e.next(81003000,81003,f) && f.data[9]==9);
  e.receive(data.data(),data.size(),81003); assert(!e.paired());
  e.completed(true,81005375,81008000,81008);
  e.receive(data.data(),data.size(),81009);
  assert(e.paired() && !e.pairing() && e.take_changed() && !e.take_changed());
  assert(e.saved().wind==WIND && e.saved().payload==18 && e.online(81010) && !e.online(92000));
  Pairing saved=e.saved();
  // Reboot restores the pair and emits a wake burst, without re-pairing.
  Engine reboot; reboot.initialize(NETWORK,saved,0);
  assert(reboot.paired() && !reboot.online(0));
  assert(!reboot.next(0,0,f)); assert(reboot.next(3000000,3000,f) && f.preamble_ms==6000 && f.data[9]==9);
  assert(!reboot.start_pairing(4000000)); // A wake transmission cannot be interrupted.
  reboot.completed(true,9000000,9003000,9003);
  assert(reboot.phase()==Phase::RUNNING);
  // Failed replacement preserves the old pair, including across a wrap in micros().
  e.start_pairing(0xfffffff0); assert(e.next(0xfffffff0,100000,f));
  e.completed(true,1000,3000,106003);
  step(e,121003); step(e,181003); step(e,211003);
  assert(e.paired() && !e.pairing() && e.saved().wind==WIND && !e.take_changed());
  Engine empty; empty.initialize(NETWORK,{},0); empty.start_pairing(0);
  assert(empty.next(0,0,f)); empty.completed(false,0,1,0);
  assert(!empty.paired() && !empty.pairing() && empty.phase()==Phase::FAILED);
  // Ping is scheduled, but a late loop must drop it instead of transmitting
  // inside a sensor's acknowledgement slot.
  Engine timed; timed.initialize(NETWORK,saved,0); assert(timed.next(3000000,3000,f));
  timed.completed(true,9000000,9003000,70000);
  assert(!timed.next(9050000,70050,f)); assert(timed.missed_windows()==1);
  // Check the ESPHome web button, binary sensor, and flash restore integration.
  struct Radio : micronet::RadioTransport {
    Frame frame;
    bool transmit(const uint8_t *p,size_t n,uint32_t preamble) override {
      std::copy(p,p+n,frame.data.begin()); frame.length=n; frame.preamble_ms=preamble; return true;
    }
    bool busy() const override { return false; }
  } radio;
  micronet::Micronet hub; hub.set_transport(&radio);
  Master master; master.set_hub(&hub); master.set_network_id(NETWORK);
  binary_sensor::BinarySensor paired, pairing,online; text_sensor::TextSensor status,id;
  master.set_paired(&paired); master.set_pairing(&pairing); master.set_online(&online);
  master.set_status(&status); master.set_wind_id(&id);
  fake_time=0; fake_microseconds=0; master.setup();
  assert(!paired.state && !pairing.state);
  PairButton button; button.set_parent(&master); button.press(); assert(pairing.state);
  master.loop(); assert(radio.frame.preamble_ms==6000);
  fake_time=6003; fake_microseconds=6003000; master.on_transmitted(true,6000000,6003000);
  fake_time=21003; fake_microseconds=21003000; master.loop();
  master.on_transmitted(true,21005375,21008000);
  master.on_packet(req.data(),req.size());
  fake_time=81003; fake_microseconds=81003000; master.loop();
  master.on_transmitted(true,81005375,81008000);
  master.on_packet(data.data(),data.size());
  assert(paired.state && !pairing.state && online.state && id.state=="02039087");
  assert(fake_flash.size()==1);
  micronet::Micronet hub2; hub2.set_transport(&radio);
  Master restored; restored.set_hub(&hub2); restored.set_network_id(NETWORK);
  binary_sensor::BinarySensor restored_paired; restored.set_paired(&restored_paired); restored.setup();
  assert(restored_paired.state);
  std::cout<<"Master framing, wake/join/confirm, malformed controls, slot resize, timer wrap, timeout rollback, web button and flash restore passed\n";
}
