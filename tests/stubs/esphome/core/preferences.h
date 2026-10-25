#pragma once
#include <map>
#include <vector>
#include <cstdint>
#include <cstring>
namespace esphome {
inline std::map<uint32_t,std::vector<uint8_t>> fake_flash;
class ESPPreferenceObject {
 public:
  uint32_t key=0;
  template<class T> bool load(T *value) {
    auto it=fake_flash.find(key); if(it==fake_flash.end() || it->second.size()!=sizeof(T)) return false;
    std::memcpy(value,it->second.data(),sizeof(T)); return true;
  }
  template<class T> bool save(const T *value) {
    auto p=reinterpret_cast<const uint8_t*>(value); fake_flash[key]=std::vector<uint8_t>(p,p+sizeof(T)); return true;
  }
};
struct ESPPreferences {
  template<class T> ESPPreferenceObject make_preference(uint32_t key,bool) { ESPPreferenceObject p; p.key=key; return p; }
  bool sync() { return true; }
};
inline ESPPreferences fake_preferences;
inline ESPPreferences *global_preferences=&fake_preferences;
}
