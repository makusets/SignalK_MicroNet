#pragma once
#include <cstdint>
using BaseType_t=int;
using UBaseType_t=unsigned;
using TickType_t=uint32_t;
constexpr BaseType_t pdFALSE=0,pdTRUE=1,pdPASS=1;
constexpr TickType_t portMAX_DELAY=0xffffffff;
constexpr unsigned portTICK_PERIOD_MS=1;
namespace rtos_test { inline unsigned isr_notifications=0,yields=0; }
#define portYIELD_FROM_ISR() (++rtos_test::yields)
