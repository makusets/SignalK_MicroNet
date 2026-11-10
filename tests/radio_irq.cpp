#include "components/micronet_cc1101/micronet_cc1101.h"
#include "esphome/core/hal.h"
#include <cassert>
#include <vector>
using namespace esphome;
class Radio : public micronet_cc1101::MicronetCC1101 {
 public:
  void step_worker() { receive_once_(true); }
};
int main() {
  micronet::Micronet hub;
  sensor::Sensor speed,signal;
  hub.set_sensor(micronet::Measurement::WIND_SPEED,&speed);
  hub.set_signal_strength_sensor(&signal);
  InternalGPIOPin pin;
  Radio radio; radio.set_parent(&hub); radio.set_gdo0_pin(&pin); radio.setup();
  assert(radio.ready() && spi::chip.regs[2]==0 && spi::chip.regs[3]==0);
  const std::vector<uint8_t> wind{0x83,3,0x77,0x37,2,3,0x90,0x87,2,1,9,0x5c,0x18,0x18,
                                4,5,5,0,0x2c,0x3a,4,6,5,0xff,0xfb,9};
  spi::chip.regs[0x34]=0xe0;
  for(auto byte:wind) {
    spi::chip.fifo.push_back(byte);
    if(spi::chip.fifo.size()>=4) {
      const size_t queued=spi::chip.fifo.size();
      pin.level(true);
      assert(spi::chip.fifo.size()==queued); // ISR notifies; it does not read SPI.
      pin.level(false);
      radio.step_worker();
    }
  }
  radio.step_worker(); radio.step_worker(); // One-ms fallback collects the last bytes.
  assert(rtos_test::isr_notifications>0 && rtos_test::yields==rtos_test::isr_notifications);
  assert(speed.values.empty() && signal.values.empty()); // No worker-side callbacks.
  // A fake FIFO byte must remain untouched: ESP32's main loop only delivers
  // queued frames, and never competes with the worker for this SPI device.
  spi::chip.fifo.push_back(0x42);
  radio.loop();
  assert(spi::chip.fifo.size()==1 && spi::chip.fifo.front()==0x42);
  assert(speed.values.size()==1 && signal.values.size()==1 && signal.values.back()==-90);
}
