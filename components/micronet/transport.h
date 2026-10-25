// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
namespace esphome { namespace micronet {
class PacketListener {
 public:
  virtual void on_packet(const uint8_t *data, size_t length) = 0;
  virtual void on_transmitted(bool success, uint32_t sync_end_us, uint32_t end_us) = 0;
  virtual ~PacketListener() = default;
};
class RadioTransport {
 public:
  // Copies the packet; completion is asynchronous. Long preambles are generated
  // by the radio, not by blocking ESPHome's loop or streaming from an ISR.
  virtual bool transmit(const uint8_t *data, size_t length, uint32_t preamble_ms) = 0;
  virtual bool busy() const = 0;
  // Adapters may fail during setup even when a transport object is registered.
  virtual bool ready() const { return true; }
  virtual const char *failure_reason() const { return nullptr; }
  virtual ~RadioTransport() = default;
};
}}
