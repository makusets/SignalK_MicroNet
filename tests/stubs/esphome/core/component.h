#pragma once
#include <cstdint>
namespace esphome {
namespace setup_priority { constexpr float HARDWARE = 800, DATA = 600; }
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
  void mark_failed() { failed = true; }
  bool is_failed() const { return failed; }
  bool failed{false};
};
}
