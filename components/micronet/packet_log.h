// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "protocol.h"
#include "esphome/core/log.h"
namespace esphome { namespace micronet {
inline void log_packet_debug(const char *tag,const char *event,const uint8_t *data,size_t length) {
  char raw[MAX_PACKET*3]{};
  constexpr char HEX[]="0123456789ABCDEF";
  size_t shown=length<MAX_PACKET?length:MAX_PACKET;
  for (size_t i=0;i<shown;++i) {
    raw[3*i]=HEX[data[i]>>4]; raw[3*i+1]=HEX[data[i]&15];
    raw[3*i+2]=(i+1<shown)?' ':'\0';
  }
  if (length>=14) {
    ESP_LOGD(tag,"%s network=%08X device=%08X type=%02X flags=%02X len=%u raw=%s",
      event,unsigned(be32(data)),unsigned(be32(data+4)),data[8],data[9],unsigned(length),raw);
  } else ESP_LOGD(tag,"%s len=%u raw=%s",event,unsigned(length),raw);
}
}}
