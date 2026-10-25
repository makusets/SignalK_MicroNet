#pragma once
#include <cstdint>
inline uint32_t fake_time = 0;
inline uint32_t fake_microseconds = 0;
namespace esphome {
inline uint32_t micros() { return fake_microseconds; }
inline uint32_t millis() { return fake_time; }
inline void delay(unsigned) {}
inline void delayMicroseconds(unsigned) {}
}
