#pragma once
#include <string>
namespace esphome { namespace text_sensor {
struct TextSensor { std::string state; void publish_state(const std::string &v) { state=v; } };
}}
