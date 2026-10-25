// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/component.h"
#include "esphome/components/gps/gps.h"
#include "esphome/components/signalk/signalk.h"
namespace esphome { namespace signalk_gps {
class SignalKGPS : public PollingComponent {
 public:
  void set_gps(gps::GPS *gps) { gps_ = gps; }
  void set_signalk(signalk::SignalK *signalk) { signalk_ = signalk; }
  void set_stale_after(uint32_t ms) { stale_after_ = ms; }
  void update() override {
    if (!signalk_->is_connected()) return;
    auto &gps = gps_->get_tiny_gps();
    JsonDocument doc;
    auto update = doc["updates"].to<JsonArray>().add<JsonObject>();
    auto values = update["values"].to<JsonArray>();
    auto pos = values.add<JsonObject>(); pos["path"] = "navigation.position";
    if (gps.location.isValid() && gps.location.age() < stale_after_) {
      auto value = pos["value"].to<JsonObject>();
      value["latitude"] = gps.location.lat(); value["longitude"] = gps.location.lng();
      if (gps.altitude.isValid() && gps.altitude.age() < stale_after_) value["altitude"] = gps.altitude.meters();
    } else pos["value"] = nullptr;
    auto speed = values.add<JsonObject>(); speed["path"] = "navigation.speedOverGround";
    if (gps.location.isValid() && gps.location.age() < stale_after_ && gps.speed.isValid() && gps.speed.age() < stale_after_)
      speed["value"] = gps.speed.mps();
    else speed["value"] = nullptr;
    auto course = values.add<JsonObject>(); course["path"] = "navigation.courseOverGroundTrue";
    if (gps.location.isValid() && gps.location.age() < stale_after_ && gps.course.isValid() && gps.course.age() < stale_after_)
      course["value"] = gps.course.deg() * 0.017453292519943295;
    else course["value"] = nullptr;
    std::string payload; serializeJson(doc, payload); signalk_->send(payload);
  }
 protected:
  gps::GPS *gps_{nullptr};
  signalk::SignalK *signalk_{nullptr};
  uint32_t stale_after_{5000};
};
}}
