#pragma once
#include <array>
#include <deque>
#include <cstdint>
#include <vector>
#include "esphome/core/hal.h"
namespace esphome { namespace spi {
constexpr int BIT_ORDER_MSB_FIRST=0, CLOCK_POLARITY_LOW=0, CLOCK_PHASE_LEADING=0, DATA_RATE_1MHZ=1;
struct Chip {
  std::array<uint8_t,64> regs{};
  std::deque<uint8_t> fifo,tx_fifo;
  bool overflow{false}, selected{false}, first{true};
  uint8_t address{0};
  unsigned flushes{0},rx_starts{0};
  bool timed=false;
  std::deque<uint8_t> incoming;
  uint32_t next_byte_at=0;
  void advance(unsigned us) {
    if(!timed) return;
    fake_microseconds+=us; fake_time=fake_microseconds/1000;
    while(!incoming.empty() && int32_t(fake_microseconds-next_byte_at)>=0) {
      if(fifo.size()>=64) overflow=true;
      else fifo.push_back(incoming.front());
      incoming.pop_front(); next_byte_at+=104;
    }
  }
  void stream(const std::vector<uint8_t> &packet) {
    incoming.assign(packet.begin(),packet.end()); timed=true; next_byte_at=fake_microseconds+104;
  }
  Chip() { regs[0x31]=0x14; }
  uint8_t transfer(uint8_t byte) {
    if (first) {
      first=false; address=byte;
      if (byte==0x34) ++rx_starts;
      if (byte==0x3b) tx_fifo.clear();
      if (byte==0x3a) { fifo.clear(); overflow=false; ++flushes; }
      return 0;
    }
    auto reg=address & 0x3f;
    if (!(address & 0x80)) { if(reg==0x3f) tx_fifo.push_back(byte); else regs[reg]=byte; return 0; }
    if (reg==0x3a) return uint8_t(tx_fifo.size());
    if (reg==0x3b) return uint8_t(fifo.size()) | (overflow ? 0x80 : 0);
    if (reg==0x3f) { auto b=fifo.front(); fifo.pop_front(); return b; }
    return regs[reg];
  }
};
inline Chip chip;
template<int, int, int, int> class SPIDevice {
 public:
  esphome::InternalGPIOPin *cs_{nullptr};
  void spi_setup() {}
  void enable() { chip.selected=true; chip.first=true; }
  void disable() { chip.selected=false; }
  uint8_t transfer_byte(uint8_t b) { chip.advance(80+8); return chip.transfer(b); }
  void transfer_array(uint8_t *data,size_t length) {
    chip.advance(20);
    for(size_t i=0;i<length;++i) { chip.advance(8); data[i]=chip.transfer(data[i]); }
  }
};
}}
