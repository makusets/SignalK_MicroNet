#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
namespace testlog {
inline std::vector<std::string> messages;
inline void (*on_append)(const char *,const char *)=nullptr;
inline void append(const char *tag,const char *format,...) __attribute__((format(printf,2,3)));
inline void append(const char *tag,const char *format,...) {
  char buffer[1024];
  va_list args; va_start(args,format); std::vsnprintf(buffer,sizeof(buffer),format,args); va_end(args);
  if(on_append) on_append(tag,buffer);
  messages.push_back(std::string(tag)+": "+buffer);
}
inline bool contains(const std::string &text) {
  for (const auto &line:messages) if (line.find(text)!=std::string::npos) return true;
  return false;
}
}
#define ESP_LOGD(tag, ...) testlog::append(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) ESP_LOGD(tag, __VA_ARGS__)
#define ESP_LOGCONFIG(tag, ...) ESP_LOGD(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) ESP_LOGD(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) ESP_LOGD(tag, __VA_ARGS__)
#define LOG_PIN(prefix, pin) do { (void)(prefix); (void)(pin); } while(0)
