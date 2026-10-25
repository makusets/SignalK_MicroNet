// SPDX-License-Identifier: GPL-3.0-or-later
// Protocol interpretation based on MicroNav by Ronan Demoment (2021).
// New bounded, transactional decoder for ESPHome, 2026-10-25.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace esphome { namespace micronet {
constexpr size_t MAX_PACKET = 96;
enum class Measurement : uint8_t {
  WATER_SPEED, DEPTH, WATER_TEMPERATURE, WIND_SPEED, WIND_ANGLE, HEADING,
  SOG, COG, TRIP, LOG, VOLTAGE, COUNT
};
struct Reading { Measurement kind; float value; };
struct Decoded {
  uint32_t network_id{0}, device_id{0};
  uint8_t message_id{0};
  std::array<Reading, 20> readings{};
  size_t count{0};
  void add(Measurement kind, float value) { if (count < readings.size()) readings[count++] = {kind, value}; }
};
inline uint32_t be32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline uint16_t be16(const uint8_t *p) { return (uint16_t(p[0]) << 8) | p[1]; }
inline int signed16(const uint8_t *p) { unsigned v = be16(p); return v >= 32768 ? int(v) - 65536 : int(v); }
inline uint8_t checksum(const uint8_t *p, size_t n) {
  uint8_t sum = 0; for (size_t i = 0; i < n; ++i) sum += p[i]; return sum;
}
inline bool header_valid(const uint8_t *p, size_t n) {
  return n >= 14 && p[12] == p[13] && p[12] >= 12 && p[12] <= MAX_PACKET - 2 && checksum(p, 11) == p[11];
}
inline bool decode(const uint8_t *p, size_t n, Decoded &out) {
  if (!header_valid(p, n) || size_t(p[12]) + 2 != n) return false;
  Decoded d;
  d.network_id = be32(p); d.device_id = be32(p + 4); d.message_id = p[8];
  if (d.message_id != 2) { out = d; return true; }
  // Validate the entire packet before emitting any readings.
  for (size_t offset = 14; offset < n;) {
    const uint8_t *f = p + offset;
    size_t len = size_t(f[0]) + 2;
    if (len < 5 || len > n - offset || checksum(f, len - 1) != f[len - 1]) return false;
    constexpr float RAD = 0.017453292519943295f, KNOT = 1852.0f / 3600.0f;
    if (f[0] == 3 && f[1] == 3) {
      const int temp = f[3] < 128 ? f[3] : int(f[3]) - 256;
      d.add(Measurement::WATER_TEMPERATURE, temp * 0.5f + 273.15f);
    } else if (f[0] == 4 || f[0] == 5) {
      int v = signed16(f + 3);
      switch (f[1]) {
        case 1: if (v >= 0) d.add(Measurement::WATER_SPEED, v * 0.01f * KNOT); break;
        case 4: if (v >= 0 && v < 5000) d.add(Measurement::DEPTH, v * 0.03048f); break;
        case 5: if (v >= 0) d.add(Measurement::WIND_SPEED, v * 0.1f * KNOT); break;
        case 6: if (v >= -180 && v <= 180) d.add(Measurement::WIND_ANGLE, v * RAD); break;
        case 7: if (v >= 0 && v <= 360) d.add(Measurement::HEADING, (v % 360) * RAD); break;
        case 0x1b: if (v >= 0) d.add(Measurement::VOLTAGE, v * 0.1f); break;
      }
    } else if (f[0] == 6 && f[1] == 8) {
      d.add(Measurement::SOG, be16(f + 3) * 0.1f * KNOT);
      if (be16(f + 5) <= 360) d.add(Measurement::COG, (be16(f + 5) % 360) * RAD);
    } else if (f[0] == 10 && f[1] == 2) {
      d.add(Measurement::TRIP, be32(f + 3) * 18.52f);
      d.add(Measurement::LOG, be32(f + 7) * 185.2f);
    }
    offset += len;
  }
  out = d; return true;
}
}}  // namespace esphome::micronet
