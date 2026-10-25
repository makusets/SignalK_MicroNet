#include "components/micronet/micronet.h"
#include "components/micronet_cc1101/micronet_cc1101.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <cstring>
static unsigned logging_requires_rx_start=0;
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
  assert(!radio.failed && radio.ready());
  testlog::messages.clear(); radio.dump_config();
  assert(testlog::contains("Modem readback: 76767 baud (DRATE_E=11 DRATE_M=131"));
  assert(spi::chip.regs[0x12]==2);
  // Enable the reference's preamble qualifier without adding CRC/address/status
  // bytes that would change the Micronet framing exposed to the decoder.
  assert((spi::chip.regs[0x07]>>5)==4 && (spi::chip.regs[0x07]&0x1f)==0);
  // Decode the hardware register settings independently using the datasheet
  // equation. A register-value assertion previously blessed a half-rate modem.
  const double programmed_baud=std::ldexp(26000000.0*(256+spi::chip.regs[0x11]),
                                        spi::chip.regs[0x10]&0x0f)/268435456.0;
  assert(std::fabs(programmed_baud-76800.0)<50.0);
  const double half_rate=std::ldexp(26000000.0*(256+0x83),0x0a)/268435456.0;
  assert(std::fabs(half_rate-76800.0)>38000.0);
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
  testlog::messages.clear();
  hub.set_network_id(0x12345678); assert(!hub.receive(wind.data(),wind.size()));
  assert(testlog::contains("RX frame network=83037737 device=02039087 type=02"));
  assert(testlog::contains("raw=83 03 77 37 02 03 90 87"));
  assert(testlog::contains("RX not published: measurement filter"));
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
  // Model incoming bytes every 104 us while ESP-IDF SPI transfers consume
  // real time. Per-byte transactions + the identical-count gate used to defer
  // all draining until overflow. The FIFO is smaller than this 96-byte packet.
  const size_t streamed_before=speed.values.size();
  fake_microseconds=100000; fake_time=100;
  spi::chip.stream(large);
  for(int i=0;i<2000 && speed.values.size()==streamed_before;++i) radio.loop();
  assert(speed.values.size()==streamed_before+1);
  assert(std::isfinite(speed.values.back()));
  assert(spi::chip.incoming.empty() && spi::chip.fifo.empty() && !spi::chip.overflow);
  spi::chip.timed=false;
  // GDO0 timestamps complete TX without blocking while hardware stretches
  // its preamble. The ESPHome loop can run throughout all six seconds.
  struct Listener : micronet::PacketListener {
    int received=0,completed=0; bool success=false; uint32_t sync=0,end=0;
    unsigned expect_rx_restart=0;
    void on_packet(const uint8_t*,size_t) override {
      assert(spi::chip.rx_starts>=expect_rx_restart); ++received;
    }
    void on_transmitted(bool ok,uint32_t s,uint32_t e) override { ++completed; success=ok; sync=s; end=e; }
  } listener;
  hub.set_listener(&listener);
  InternalGPIOPin pin; radio.set_gdo0_pin(&pin); radio.setup();
  auto ping=wind; ping.resize(14); ping[8]=0x0a; ping[12]=ping[13]=12;
  ping[11]=micronet::checksum(ping.data(),11);
  listener.expect_rx_restart=spi::chip.rx_starts+1;
  for (auto b : ping) { spi::chip.fifo.push_back(b); radio.loop(); }
  radio.loop(); assert(listener.received==1); // Header-only RX used to strand its last byte.
  testlog::messages.clear();
  testlog::on_append=[](const char *,const char *line) {
    if(std::strncmp(line,"TX",2)==0) {
      assert(spi::chip.rx_starts>=logging_requires_rx_start);
      // Even a slow UART logger must not delay STX or the RX restart.
      fake_microseconds+=10000; fake_time=fake_microseconds/1000;
    }
  };
  logging_requires_rx_start=spi::chip.rx_starts+1;
  fake_microseconds=1000;
  assert(radio.transmit(ping.data(),ping.size(),6000)); assert(radio.busy());
  assert(!radio.transmit(ping.data(),ping.size(),0));
  assert(spi::chip.tx_fifo.empty());
  assert(!testlog::contains("TX frame")); // Printing is deferred until RX is active.
  fake_microseconds=5000000; radio.loop(); assert(spi::chip.tx_fifo.empty());
  fake_microseconds=6001000; radio.loop(); assert(spi::chip.tx_fifo.size()==14);
  assert(std::equal(spi::chip.tx_fifo.begin(),spi::chip.tx_fifo.end(),ping.begin()));
  fake_microseconds=6001200; pin.level(true);
  fake_microseconds=6003000; pin.level(false); radio.loop();
  assert(testlog::contains("TX frame (deferred log)"));
  assert(testlog::contains("TX start: preamble=6000 ms"));
  assert(testlog::contains("TX complete: sync_us=6001200 end_us=6003000"));
  assert(listener.completed==1 && listener.success && listener.sync==6001200 && listener.end==6003000 && !radio.busy());
  logging_requires_rx_start=spi::chip.rx_starts+1;
  assert(radio.transmit(ping.data(),ping.size(),0));
  fake_microseconds+=50001; radio.loop();
  assert(listener.completed==2 && !listener.success && !radio.busy());
  assert(testlog::contains("TX failed: completion timeout"));
  testlog::on_append=nullptr;
  fake_time+=5000; radio.loop(); assert(testlog::contains("Radio summary: RX frames="));
  // Reproduce the user's startup log: a low/absent SPI response must never
  // be advertised as a usable radio, nor accepted for transmission.
  micronet_cc1101::MicronetCC1101 european_radio;
  european_radio.set_parent(&hub); european_radio.set_frequency(869.840f); european_radio.setup();
  assert(european_radio.ready());
  const unsigned eu_word=(unsigned(spi::chip.regs[0xd])<<16)|(unsigned(spi::chip.regs[0xe])<<8)|spi::chip.regs[0xf];
  assert(std::fabs(eu_word*26.0/65536-869.840)<0.001);
  assert((spi::chip.regs[0x10]&15)==11 && spi::chip.regs[0x11]==0x83);
  spi::chip.regs[0x31]=0;
  micronet_cc1101::MicronetCC1101 missing_radio;
  missing_radio.set_parent(&hub); missing_radio.set_gdo0_pin(&pin); missing_radio.setup();
  assert(missing_radio.failed && !missing_radio.ready());
  assert(std::string(missing_radio.failure_reason())=="CC1101 SPI identification failed");
  assert(!missing_radio.transmit(ping.data(),ping.size(),6000));
  std::cout << "Streaming radio, filters, expiry, timer wrap and recovery checks passed\n";
}
