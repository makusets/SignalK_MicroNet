#pragma once
#include <cstdint>
namespace esphome { inline void get_mac_address_raw(uint8_t *mac) { for(int i=0;i<6;++i) mac[i]=i; } struct HighFrequencyLoopRequester { void start() {} }; }
