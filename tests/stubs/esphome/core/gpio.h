#pragma once
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
namespace esphome {
namespace gpio { constexpr int INTERRUPT_ANY_EDGE=3; }
struct PinState { bool high=false; void (*callback)(void*)=nullptr; void *arg=nullptr; };
struct ISRInternalGPIOPin {
  PinState *state=nullptr;
  bool digital_read() { return state->high; }
};
class InternalGPIOPin {
 public:
  PinState state;
  void setup() {}
  ISRInternalGPIOPin to_isr() { return {&state}; }
  template<class T> void attach_interrupt(void (*callback)(T*),T *arg,int) {
    state.callback=reinterpret_cast<void (*)(void*)>(callback); state.arg=arg;
  }
  void level(bool high) { state.high=high; if(state.callback) state.callback(state.arg); }
};
}
