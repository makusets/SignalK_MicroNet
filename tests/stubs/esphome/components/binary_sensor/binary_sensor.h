#pragma once
#include <vector>
namespace esphome { namespace binary_sensor {
struct BinarySensor { bool state=false; std::vector<bool> values; void publish_state(bool v) { state=v; values.push_back(v); } };
}}
