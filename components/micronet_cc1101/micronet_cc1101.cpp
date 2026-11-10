// SPDX-License-Identifier: GPL-3.0-or-later
#include "micronet_cc1101.h"
#include "esphome/components/micronet/packet_log.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cstring>
#include <algorithm>
#include <cmath>
namespace esphome { namespace micronet_cc1101 {
static const char *const TAG = "micronet_cc1101";
void MicronetCC1101::write_reg_(uint8_t address, uint8_t value) {
  uint8_t transfer[2]{address,value};
  enable(); delayMicroseconds(10); transfer_array(transfer,sizeof(transfer)); disable();
}
uint8_t MicronetCC1101::read_reg_(uint8_t address) {
  enable(); delayMicroseconds(10);
  // Status registers require the burst bit even for a single read.
  uint8_t transfer[2]{uint8_t(address | (address >= 0x30 ? 0xc0 : 0x80)),0};
  transfer_array(transfer,sizeof(transfer)); disable(); return transfer[1];
}
void MicronetCC1101::strobe_(uint8_t command) {
  enable(); delayMicroseconds(10); transfer_byte(command); disable();
}
void MicronetCC1101::restart_() {
  strobe_(0x36); delayMicroseconds(100); // SIDLE
  strobe_(0x3a); // SFRX; never flush in RX
  write_reg_(0x08, 0x00); // Restore fixed RX length after raw/infinite TX.
  write_reg_(0x06, 255); // Adjusted after receiving the Micronet header.
  write_reg_(0x03,0x00); // RX FIFO threshold: 4 bytes, prompt header service.
  if (gdo0_pin_) write_reg_(0x02,0x00); // FIFO threshold, deasserts below 4.
  write_reg_(0x12, 0x02); // RX: 16/16 sync detection.
  used_ = expected_ = 0; reception_started_ = false;
  rx_busy_.store(false,std::memory_order_release);
  last_rx_service_us_=micros();
  strobe_(0x34); // SRX
}
void MicronetCC1101::setup() {
  initialized_=false;
  spi_setup();
  strobe_(0x30); delay(2); // SRES
  uint8_t part = read_reg_(0x30), version = read_reg_(0x31);
  chip_part_=part; chip_version_=version;
  if (part != 0 || version == 0 || version == 0xff) {
    ESP_LOGE(TAG, "CC1101 not found: part=%02X version=%02X", part, version);
    ESP_LOGE(TAG,"Check CC1101 3.3 V power, common ground and SPI wiring (CSN/SCK/SI/SO)");
    mark_failed(); return;
  }
  // 26 MHz crystal. 2-FSK, 76.77 kbaud, +/-38.09 kHz deviation,
  // 203 kHz RX bandwidth, 16/16 sync 0x5599, no whitening/CRC/address bytes.
  // PQT=4 matches MicronetToNMEA: qualify the alternating preamble before sync,
  // rather than accepting random sync matches from an ungated noise stream.
  const uint8_t config[][2] = {
    {0x00, 0x2e}, {0x01, 0x2e}, {0x02, 0x2e}, // GDOs high impedance
    {0x03, 0x07}, {0x04, 0x55}, {0x05, 0x99},
    {0x07, 0x80}, {0x08, 0x00}, {0x0a, 0x00}, {0x0b, 0x06},
    {0x10, 0x8b}, {0x11, 0x83}, {0x12, 0x02}, {0x13, 0x22}, {0x14, 0xf8},
    {0x15, 0x44}, {0x17, 0x00}, {0x18, 0x18},
    {0x19, 0x16}, {0x1a, 0x6c}, {0x1b, 0x43}, {0x1c, 0x40}, {0x1d, 0x91},
    {0x21, 0x56}, {0x22, 0x10}, {0x23, 0xe9}, {0x24, 0x2a}, {0x25, 0x00}, {0x26, 0x1f},
    {0x2c, 0x81}, {0x2d, 0x35}, {0x2e, 0x09}
  };
  for (auto &reg : config) write_reg_(reg[0], reg[1]);
  uint32_t frequency_word = uint32_t(frequency_ * 65536.0 / 26.0 + 0.5);
  write_reg_(0x0d, frequency_word >> 16); write_reg_(0x0e, frequency_word >> 8); write_reg_(0x0f, frequency_word);
  // TX bypasses the automatic 0xAA preamble: the FIFO carries explicit
  // 0x55 preamble bytes, 0x99 sync, then the unchanged Micronet packet.
  write_reg_(0x3e, frequency_ > 900 ? 0x8e : 0x50); // PATABLE: 0 dBm.
  if (gdo0_pin_) {
    write_reg_(0x02, 0x06); // High after sync, low at end of packet.
    gdo0_pin_->setup(); isr_gdo0_=gdo0_pin_->to_isr();
    gdo0_pin_->attach_interrupt(edge_,this,gpio::INTERRUPT_ANY_EDGE);
  }
  strobe_(0x33); delay(1); // Calibrate once, then avoid 0.8 ms recalibration jitter on every poll.
  write_reg_(0x18,0x08);
  restart_();
  fast_loop_.start(); initialized_=true; failure_reason_=nullptr;
  // CC1101 data-rate equation (26 MHz crystal). Log the register readback so
  // a startup log confirms the actual programmed rate, rather than a label.
  rate_exponent_=read_reg_(0x10)&0x0f;
  rate_mantissa_=read_reg_(0x11);
  baud_=std::ldexp(26000000.0*(256.0+rate_mantissa_),rate_exponent_)/268435456.0;
  ESP_LOGI(TAG,"CC1101 initialized: part=%02X version=%02X, %.3f MHz, %.0f baud",part,version,frequency_,baud_);
#ifdef USE_ESP32
  if (portTICK_PERIOD_MS>1) {
    failure_reason_="CC1101 radio worker requires a 1 ms FreeRTOS tick";
    ESP_LOGE(TAG,"%s",failure_reason_); mark_failed(); return;
  }
  if (xTaskCreate(radio_task_, "micronet_radio", 4096, this, 20, &radio_task_handle_) != pdPASS) {
    failure_reason_="CC1101 radio worker allocation failed";
    ESP_LOGE(TAG,"%s",failure_reason_); mark_failed(); return;
  }
#endif
}
void IRAM_ATTR MicronetCC1101::edge_(MicronetCC1101 *self) {
  if (!self->capture_tx_.load(std::memory_order_relaxed)) {
#ifdef USE_ESP32
    if (self->radio_task_handle_ && self->isr_gdo0_.digital_read()) {
      ++self->rx_irq_wakes_;
      BaseType_t woken=pdFALSE;
      vTaskNotifyGiveFromISR(self->radio_task_handle_,&woken);
      if (woken==pdTRUE) portYIELD_FROM_ISR();
    }
#endif
    return;
  }
  uint32_t now=micros();
  if (self->isr_gdo0_.digital_read()) {
    self->sync_end_us_.store(now,std::memory_order_relaxed);
    self->sync_seen_.store(true,std::memory_order_release);
  } else if (self->sync_seen_.load(std::memory_order_acquire)) {
    self->tx_end_us_.store(now,std::memory_order_relaxed);
    self->end_seen_.store(true,std::memory_order_release);
  }
}
void MicronetCC1101::write_fifo_(const uint8_t *data,size_t length) {
  uint8_t transfer[65]{}; transfer[0]=0x7f;
  std::memcpy(transfer+1,data,length);
  enable(); delayMicroseconds(10); transfer_array(transfer,length+1); disable();
}
void MicronetCC1101::read_fifo_(uint8_t *data,size_t length) {
  uint8_t transfer[65]{}; transfer[0]=0xff;
  enable(); delayMicroseconds(10); transfer_array(transfer,length+1); disable();
  std::memcpy(data,transfer+1,length);
}
bool MicronetCC1101::transmit(const uint8_t *data,size_t length,uint32_t preamble_ms) {
  if (busy()) return false;
  if (!ready() || !gdo0_pin_ || length<14 || length>sizeof(tx_data_) || preamble_ms>6000) {
    ESP_LOGW(TAG,"TX refused: ready=%s gdo0=%s len=%u",
      ready()?"YES":"NO",gdo0_pin_?"YES":"NO",unsigned(length)); return false;
  }
  std::memcpy(tx_data_,data,length); tx_length_=length;
  tx_preamble_ms_=preamble_ms;
  // Round up so a requested six-second wake never becomes shorter. Ordinary
  // frames have exactly the reference's sixteen 0x55 bytes and one 0x99 byte.
  tx_preamble_bytes_=std::max(uint32_t(16),uint32_t(std::ceil(baud_*preamble_ms/8000.0)));
  tx_total_=tx_preamble_bytes_+1+length;
  // Avoid PKTLEN=0 when changing from infinite to fixed packet length.
  if ((tx_total_&255)==0) { ++tx_preamble_bytes_; ++tx_total_; }
  tx_written_=0; tx_error_bytes_=tx_error_state_=0;
  tx_fixed_=tx_total_<=255; tx_done_.store(false,std::memory_order_relaxed);
  transmitting_=true;
#ifdef USE_ESP32
  tx_requested_.store(true,std::memory_order_release);
  xTaskNotifyGive(radio_task_handle_);
#else
  start_tx_(); // Host behavioral tests drive the same pump explicitly.
#endif
  return true;
}
void MicronetCC1101::fill_tx_(size_t count) {
  uint8_t bytes[64];
  for (size_t i=0;i<count;++i,++tx_written_) {
    if (tx_written_<tx_preamble_bytes_) bytes[i]=0x55;
    else if (tx_written_==tx_preamble_bytes_) bytes[i]=0x99;
    else bytes[i]=tx_data_[tx_written_-tx_preamble_bytes_-1];
  }
  write_fifo_(bytes,count);
}
void MicronetCC1101::start_tx_() {
  strobe_(0x36); delayMicroseconds(100);
  strobe_(0x3b);
  write_reg_(0x12,0x00); // No automatic preamble/sync, 2-FSK unchanged.
  write_reg_(0x08,tx_fixed_?0x00:0x02); // No whitening, CRC or length byte.
  write_reg_(0x06,tx_total_&255);
  // With sync disabled use inverted PA_PD for actual RF start/end edges.
  // Recover the Micronet sync timestamp from the end edge and payload airtime.
  write_reg_(0x02,0x5b);
  sync_seen_.store(false); end_seen_.store(false);
  capture_tx_.store(true,std::memory_order_release);
  fill_tx_(std::min(uint32_t(64),tx_total_));
  tx_strobe_us_=micros();
  tx_timeout_=tx_strobe_us_+uint32_t(std::ceil(tx_total_*8000000.0/baud_))+50000;
  strobe_(0x35);
}
void MicronetCC1101::stop_tx_(bool success) {
  capture_tx_.store(false,std::memory_order_release);
  tx_success_=success;
  if (!success) tx_end_us_.store(micros());
  // SPI and RX state are handed back only after all worker activity ends.
  restart_();
  tx_done_.store(true,std::memory_order_release);
}
bool MicronetCC1101::pump_tx_() {
  const uint32_t now=micros();
  // Use the larger of two TXBYTES samples: a conservative free-space bound
  // across the asynchronous status-register clock boundary (CC1101 errata).
  const uint8_t first=read_reg_(0x3a),second=read_reg_(0x3a);
  tx_error_bytes_=first|second;
  if ((tx_error_bytes_&0x80) || int32_t(now-tx_timeout_)>=0) {
    tx_error_state_=read_reg_(0x35)&0x1f; stop_tx_(false); return false;
  }
  if (end_seen_.load(std::memory_order_acquire)) {
    stop_tx_(tx_written_==tx_total_); return false;
  }
  const uint32_t queued=std::max(first,second)&0x7f;
  if (queued>64) return true; // Incoherent sample; never overflow the FIFO.
  // TXBYTES excludes the byte in the shift register. Subtract one more to
  // obtain a safe lower bound on completed bytes before switching length mode.
  const uint32_t drained=tx_written_>queued ? tx_written_-queued-1 : 0;
  if (!tx_fixed_ && tx_total_-drained<=255) {
    write_reg_(0x08,0x00); tx_fixed_=true;
  }
  const uint32_t count=std::min(uint32_t(64)-queued,tx_total_-tx_written_);
  if (count) fill_tx_(count);
  return true;
}
#ifdef USE_ESP32
void MicronetCC1101::radio_task_(void *arg) {
  auto *self=static_cast<MicronetCC1101 *>(arg);
  for (;;) {
    // No logging or application callbacks here. A 17-byte join request must
    // be length-limited and drained before the 64-byte FIFO fills with noise
    // after the remote transmitter stops. The main loop may be blocked.
    self->receive_once_(true);
    if (self->tx_requested_.load(std::memory_order_acquire) && !self->reception_started_) {
      self->tx_requested_.store(false,std::memory_order_relaxed);
      self->start_tx_();
      while (self->pump_tx_()) vTaskDelay(1);
    }
    // Wake immediately for scheduled TX, otherwise service RX every 1 ms.
    // ESPHome uses a 1 kHz FreeRTOS tick, checked during setup.
    ulTaskNotifyTake(pdTRUE,1);
  }
}
#endif
void MicronetCC1101::finish_tx_(bool success) {
  const uint32_t end=tx_end_us_.load();
  const uint32_t payload_us=uint32_t(std::lround(tx_length_*8000000.0/baud_));
  const uint32_t sync=success?end-payload_us:0;
  transmitting_=false;
  parent_->transmitted(success,sync,end);
  micronet::log_packet_debug(TAG,"TX frame (deferred log)",tx_data_,tx_length_);
  ESP_LOGD(TAG,"TX start: preamble=%u ms bytes=%u x 55, sync=99 strobe_us=%u (deferred until RX restored)",
    unsigned(tx_preamble_ms_),unsigned(tx_preamble_bytes_),unsigned(tx_strobe_us_));
  if (success) ESP_LOGD(TAG,"TX complete: sync_us=%u end_us=%u payload_duration_us=%u (sync inferred from PA end)",
    unsigned(sync),unsigned(end),unsigned(payload_us));
  else ESP_LOGW(TAG,"TX failed: %s TXBYTES=%02X MARCSTATE=%02X queued=%u/%u",
    (tx_error_bytes_&0x80)?"FIFO underflow":"completion timeout or early RF end",
    tx_error_bytes_,tx_error_state_,unsigned(tx_written_),unsigned(tx_total_));
}
void MicronetCC1101::service_tx_() {
#ifndef USE_ESP32
  if (!tx_done_.load(std::memory_order_acquire)) pump_tx_();
#endif
  if (tx_done_.load(std::memory_order_acquire)) finish_tx_(tx_success_);
}
void MicronetCC1101::log_diagnostics_() {
  if (millis()-last_diagnostics_<5000) return;
  last_diagnostics_=millis();
  ESP_LOGD(TAG,"Radio summary: RX frames=%u header_rejects=%u overflow=%u timeout=%u unstable_samples=%u deferred_reads=%u transmitting=%s",
    unsigned(rx_frames_),unsigned(header_rejects_),unsigned(overflows_),unsigned(timeouts_),
    unsigned(unstable_samples_),unsigned(deferred_reads_),transmitting_?"YES":"NO");
  ESP_LOGD(TAG,"RX service: queue_drops=%u max_service_gap_us=%u fifo_irq_wakes=%u",
    unsigned(queue_drops_.load()),unsigned(max_rx_service_gap_us_.load()),unsigned(rx_irq_wakes_.load()));
}
void MicronetCC1101::queue_received_(const uint8_t *data,size_t length,float rssi) {
  const uint32_t written=rx_written_.load(std::memory_order_relaxed);
  if (written-rx_read_.load(std::memory_order_acquire)>=RX_QUEUE_SIZE) {
    ++queue_drops_; return;
  }
  auto &slot=rx_queue_[written%RX_QUEUE_SIZE];
  std::memcpy(slot.data,data,length); slot.length=length; slot.rssi=rssi;
  rx_written_.store(written+1,std::memory_order_release);
}
void MicronetCC1101::dispatch_received_() {
  // Bound one main-loop pass even if the radio keeps producing frames while
  // application callbacks run. Never retain a queue slot during a callback.
  for (uint32_t i=0;i<RX_QUEUE_SIZE;++i) {
    const uint32_t read=rx_read_.load(std::memory_order_relaxed);
    if (read==rx_written_.load(std::memory_order_acquire)) break;
    const ReceivedPacket packet=rx_queue_[read%RX_QUEUE_SIZE];
    rx_read_.store(read+1,std::memory_order_release);
    parent_->receive(packet.data,packet.length,packet.rssi);
  }
  const uint32_t overflows=overflows_.load(),timeouts=timeouts_.load(),rejects=header_rejects_.load();
  if (overflows!=reported_overflows_) {
    const uint32_t fault=last_rx_fault_.load();
    ESP_LOGD(TAG,"RX FIFO overflow/invalid count: total=%u RXBYTES=%02X MARCSTATE=%02X (deferred log)",
      unsigned(overflows),unsigned(fault&255),unsigned((fault>>8)&255));
    reported_overflows_=overflows;
  }
  if (timeouts!=reported_timeouts_ || rejects!=reported_header_rejects_) {
    ESP_LOGD(TAG,"RX recovery: timeouts=%u header_rejects=%u (deferred log)",unsigned(timeouts),unsigned(rejects));
    reported_timeouts_=timeouts; reported_header_rejects_=rejects;
  }
}
void MicronetCC1101::loop() {
  if (transmitting_) service_tx_();
#ifdef USE_ESP32
  dispatch_received_();
  if (!transmitting_) log_diagnostics_();
#else
  if (!transmitting_) receive_once_();
#endif
}
void MicronetCC1101::receive_once_(bool background) {
  const uint32_t now=micros(),gap=now-last_rx_service_us_;
  last_rx_service_us_=now;
  if (gap>max_rx_service_gap_us_.load(std::memory_order_relaxed))
    max_rx_service_gap_us_.store(gap,std::memory_order_relaxed);
  // Poll fast enough to drain the 64-byte FIFO during packets up to 96 bytes.
  // Leave one byte while receiving (CC1101 RX FIFO erratum); drain the final
  // byte after the hardware fixed-length packet counter has reached its end.
  // Expire partial packets even when no coherent FIFO count can be obtained.
  if (reception_started_ && millis()-started_>30) {
    ++timeouts_; restart_(); return;
  }
  uint8_t count=read_reg_(0x3b);
  bool stable=false;
  for (unsigned attempt=0;attempt<3;++attempt) {
    const uint8_t confirm=read_reg_(0x3b);
    if ((count|confirm)&0x80) {
      last_rx_fault_.store(uint32_t(count|confirm)|(uint32_t(read_reg_(0x35)&0x1f)<<8));
      ++overflows_; restart_(); return;
    }
    if (count==confirm) { stable=true; break; }
    ++unstable_samples_; count=confirm;
  }
  if (!stable) { ++deferred_reads_; return; }
  count &= 0x7f;
  if (!background && !count && !reception_started_) log_diagnostics_();
  if (count > 64) {
    last_rx_fault_.store(uint32_t(count)|(uint32_t(read_reg_(0x35)&0x1f)<<8));
    ++overflows_; restart_(); return;
  }
  if (count && !reception_started_) {
    started_=millis(); reception_started_=true; rx_busy_.store(true,std::memory_order_release);
  }
  bool complete = expected_ && used_ + count >= expected_;
  size_t available = complete ? count : (count ? count - 1 : 0);
  while (available && used_ < sizeof(packet_)) {
    size_t take=available;
    // Stop at each header boundary to program/validate length before payload.
    const size_t boundary=used_<13?13:(used_<14?14:expected_);
    if (take>boundary-used_) take=boundary-used_;
    read_fifo_(packet_+used_,take); used_+=take; available-=take;
    if (used_==13) {
      // Program length before reading the duplicated length byte. Otherwise a
      // header-only ping/ACK leaves its last byte stranded by the FIFO erratum.
      if (packet_[12]<12 || packet_[12]>micronet::MAX_PACKET-2 ||
          micronet::checksum(packet_,11)!=packet_[11]) {
        ++header_rejects_; restart_(); return;
      }
      expected_=size_t(packet_[12])+2; write_reg_(0x06,expected_);
    }
    if (used_==14 && !micronet::header_valid(packet_,used_)) {
      ++header_rejects_; restart_(); return;
    }
    if (expected_ && used_ == expected_) {
      const size_t length=used_;
      // Match the reference drivers: resume RX before parsing, logging, flash
      // writes or Signal K callbacks. restart_ resets indices, not packet bytes;
      // the worker copies the frame into the delivery queue before reusing it.
      // RSSI is frozen at sync detection until RX is restarted. Snapshot it
      // before SRX; the 74 dB offset is nominal for this CC1101 configuration.
      const uint8_t raw=read_reg_(0x34);
      const float rssi=(raw>=128?int(raw)-256:int(raw))*0.5f-74.0f;
      ++rx_frames_; restart_();
      if (background) queue_received_(packet_,length,rssi);
      else parent_->receive(packet_,length,rssi);
      return;
    }
  }
}
void MicronetCC1101::dump_config() {
  LOG_PIN("  CS Pin: ",this->cs_);
  LOG_PIN("  GDO0 Pin: ",gdo0_pin_);
  ESP_LOGCONFIG(TAG,"  SPI identity: part=%02X version=%02X; initialized=%s",chip_part_,chip_version_,ready()?"YES":"NO");
  ESP_LOGCONFIG(TAG,"  Modem readback: %.0f baud (DRATE_E=%u DRATE_M=%u, 26 MHz crystal)",baud_,rate_exponent_,rate_mantissa_);
  ESP_LOGCONFIG(TAG,"  TX framing: explicit 0x55 preamble + 0x99 sync (FIFO worker)");
  ESP_LOGCONFIG(TAG,"  RX FIFO wake: GDO0 threshold=4 bytes; fallback=1 ms; packet RSSI enabled");
  ESP_LOGCONFIG(TAG,"  RX servicing: dedicated radio worker; delivery queue=%u packets",unsigned(RX_QUEUE_SIZE));
  ESP_LOGCONFIG(TAG, "Micronet CC1101 at %.3f MHz; overflow=%u timeout=%u", frequency_, unsigned(overflows_), unsigned(timeouts_));
}
}}
