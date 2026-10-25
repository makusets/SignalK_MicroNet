#pragma once
#include <array>
#include <deque>
#include <cstdint>
namespace esphome { namespace spi {
constexpr int BIT_ORDER_MSB_FIRST=0, CLOCK_POLARITY_LOW=0, CLOCK_PHASE_LEADING=0, DATA_RATE_1MHZ=1;
struct Chip {
  std::array<uint8_t,64> regs{};
  std::deque<uint8_t> fifo;
  bool overflow{false}, selected{false}, first{true};
  uint8_t address{0};
  unsigned flushes{0};
  Chip() { regs[0x31]=0x14; }
  uint8_t transfer(uint8_t byte) {
    if (first) {
      first=false; address=byte;
      if (byte==0x3a) { fifo.clear(); overflow=false; ++flushes; }
      return 0;
    }
    auto reg=address & 0x3f;
    if (!(address & 0x80)) { regs[reg]=byte; return 0; }
    if (reg==0x3b) return uint8_t(fifo.size()) | (overflow ? 0x80 : 0);
    if (reg==0x3f) { auto b=fifo.front(); fifo.pop_front(); return b; }
    return regs[reg];
  }
};
inline Chip chip;
template<int, int, int, int> class SPIDevice {
 public:
  void spi_setup() {}
  void enable() { chip.selected=true; chip.first=true; }
  void disable() { chip.selected=false; }
  uint8_t transfer_byte(uint8_t b) { return chip.transfer(b); }
};
}}
