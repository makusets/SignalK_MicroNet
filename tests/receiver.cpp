#include "components/micronet/micronet.h"
#include "components/micronet_cc1101/micronet_cc1101.h"
#include "esphome/core/hal.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using namespace esphome;
int main() {
  micronet::Micronet hub;
  sensor::Sensor speed, angle;
  hub.set_sensor(micronet::Measurement::WIND_SPEED,&speed);
  hub.set_sensor(micronet::Measurement::WIND_ANGLE,&angle);
  hub.set_network_id(0x83037737); hub.set_device_type(2); hub.set_stale_after(10000);
  std::vector<uint8_t> wind{0x83,3,0x77,0x37,2,3,0x90,0x87,2,1,9,0x5c,0x18,0x18,
                          4,5,5,0,0x2c,0x3a,4,6,5,0xff,0xfb,9};
  micronet_cc1101::MicronetCC1101 radio;
  radio.set_parent(&hub); radio.set_frequency(915.915f); radio.setup();
  assert(!radio.failed);
  assert(spi::chip.regs[0x10]==0x8a && spi::chip.regs[0x12]==2);
  assert(spi::chip.regs[4]==0x55 && spi::chip.regs[5]==0x99);
  unsigned freq = (unsigned(spi::chip.regs[0xd])<<16) | (unsigned(spi::chip.regs[0xe])<<8) | spi::chip.regs[0xf];
  assert(std::fabs(freq*26.0/65536-915.915)<0.001);
  fake_time=100;
  // Deliver a realistic stream across several loop iterations, not an atomic packet.
  for (auto b : wind) { spi::chip.fifo.push_back(b); radio.loop(); }
  radio.loop();
  assert(speed.values.size()==1 && angle.values.size()==1);
  assert(std::fabs(speed.values.back()-2.2635556)<0.0001);
  assert(spi::chip.fifo.empty());
  // A maximum-size packet streams through a FIFO smaller than the packet.
  auto large = wind;
  large.resize(96, 0); large[12]=large[13]=94;
  large[26]=68; large[27]=0x77; large[28]=5;
  large[95]=micronet::checksum(large.data()+26,69);
  for (auto b : large) { spi::chip.fifo.push_back(b); radio.loop(); }
  radio.loop(); assert(speed.values.size()==2 && angle.values.size()==2);
  speed.values.clear(); angle.values.clear();
  assert(hub.receive(wind.data(),wind.size()));
  // Corrupt payload is rejected transactionally.
  auto bad=wind; bad.back()^=1;
  for (auto b : bad) { spi::chip.fifo.push_back(b); radio.loop(); }
  radio.loop(); assert(speed.values.size()==1);
  // Network and device filters are independent of decoding validity.
  hub.set_network_id(0x12345678); assert(!hub.receive(wind.data(),wind.size()));
  hub.set_network_id(0x83037737); hub.set_device_type(1); assert(!hub.receive(wind.data(),wind.size()));
  hub.set_device_type(2); hub.set_device_id(123); assert(!hub.receive(wind.data(),wind.size()));
  hub.set_device_id(0x02039087);
  // Overflow and a single trailing byte both recover.
  unsigned before=spi::chip.flushes;
  spi::chip.overflow=true; radio.loop(); assert(spi::chip.flushes==before+1);
  spi::chip.fifo.push_back(0x83); radio.loop(); fake_time+=31; radio.loop();
  assert(spi::chip.fifo.empty());
  fake_time=10100; hub.loop();
  assert(speed.values.size()==2 && std::isnan(speed.values.back()));
  hub.loop(); assert(speed.values.size()==2); // Expiry notification happens once.
  // Unsigned timer wrap is valid.
  fake_time=0xfffffff0; assert(hub.receive(wind.data(),wind.size()));
  fake_time=100; hub.loop(); assert(std::isfinite(speed.values.back()));
  fake_time=10000; hub.loop(); assert(std::isnan(speed.values.back()));
  for (auto b : wind) { spi::chip.fifo.push_back(b); radio.loop(); }
  radio.loop(); assert(std::isfinite(speed.values.back()));
  std::cout << "Streaming radio, filters, expiry, timer wrap and recovery checks passed\n";
}
