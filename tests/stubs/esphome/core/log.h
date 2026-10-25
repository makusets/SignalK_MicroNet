#pragma once
// Consume arguments to keep production format/log calls compiled and type-checked.
#include <cstdio>
#define ESP_LOGD(tag, ...) do { (void)(tag); if (false) std::printf(__VA_ARGS__); } while(0)
#define ESP_LOGE(tag, ...) ESP_LOGD(tag, __VA_ARGS__)
#define ESP_LOGCONFIG(tag, ...) ESP_LOGD(tag, __VA_ARGS__)
