#pragma once
#include "FreeRTOS.h"
using TaskHandle_t=void*;
inline BaseType_t xTaskCreate(void (*)(void*),const char*,unsigned,void *arg,unsigned,TaskHandle_t *handle) {
  *handle=arg; return pdPASS; // Tests explicitly step the worker, never run its infinite loop.
}
inline void xTaskNotifyGive(TaskHandle_t) {}
inline void vTaskNotifyGiveFromISR(TaskHandle_t,BaseType_t *woken) {
  ++rtos_test::isr_notifications; *woken=pdTRUE;
}
inline uint32_t ulTaskNotifyTake(BaseType_t,TickType_t) { return 0; }
inline void vTaskDelay(TickType_t) {}
