#pragma once
#include <vector>
namespace esphome { namespace sensor {
struct Sensor { std::vector<float> values; void publish_state(float value) { values.push_back(value); } };
}}
