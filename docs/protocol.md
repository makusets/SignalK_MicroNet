# Protocol and adapter contract

Reference: Rodemfr/MicroNav `doc/Micronet.txt`, `Micronet.h`, `MicronetCodec.cpp`
and `SX1276MnetDriver.cpp` at abaf6f20ae1af6967b30904ee8c062b63e569f2e.
The upstream document describes reverse-engineered observations, including
uncertainties. Real instrument captures must validate any additional behavior.

RF: 2-FSK, 76,800 baud, approximately ±38 kHz deviation; repeating `0x55`
preamble followed by sync `0x99`. Europe 869.840 MHz; other systems 915.915 MHz
(the MicroNav constant rounds the latter to 915.914). The adapter uses a 16-bit
`0x5599` sync so the first FIFO byte is the first header byte. No whitening,
Manchester coding, appended radio status, hardware CRC or address byte.

Header: network ID (4 bytes), source device ID (4), message ID, flags, signal
strength, additive checksum of bytes 0..10, and duplicated total-length-minus-two.
Values are big endian. The implementation bounds packets to 14..96 bytes.
Message `0x02` contains fields. A field occupies its first byte + 2 bytes; its last
byte is the additive checksum. Unknown field IDs/formats with valid lengths and
checksums are skipped. Malformed fields invalidate the entire packet before
anything is published. Control packets are logged but do not update sensors.

Implemented measurements match MicroNav/documented scaling: hundredths of knots
(water speed), tenths of feet (depth), signed half degrees Celsius (water temp),
tenths of knots (wind and SOG), signed degrees (wind angle), degrees (magnetic
heading and COG), hundredths of nautical miles (trip), tenths of nautical miles
(log), tenths of volts (supply). Sensors expose SI: m/s, m, kelvin, radians, volts.
Depth 500.0 ft and above is omitted to match MicroNav's cutoff. No true-wind
calculation, calibration command handling or position/time field decoding is
claimed. Local NMEA GPS is supported separately.

`micronet::Micronet::receive(const uint8_t*, size_t)` is the radio-neutral entry
point. The adapter owns packet assembly and recovery; the hub owns validation,
filters, ESPHome sensor publishing and expiry. It never accesses the radio.

The CC1101 driver uses a 26 MHz crystal, 76.77 kbaud (nearest register rate),
±38.09 kHz deviation and 203 kHz bandwidth. It polls RXBYTES with ESPHome's high
frequency loop requester, drains the FIFO while receiving, leaves one byte until
packet end for the CC1101 FIFO erratum, and changes PKTLEN from 255 to the header's
specified packet length. Overflow/invalid headers/truncated packets trigger an
RX restart. Wi-Fi or other blocking components can still cause FIFO overflow;
bench-test under load before selecting this transport for continuous use.

Required bench validation: verify SPI identity, actual center frequency and
crystal; capture the wind sensor with an awake paired master; confirm complete
packets against header and field checksums; compare wind speed/angle to a known
reference; test 96-byte traffic, corrupt sync, partial packets and FIFO overflow;
confirm null updates after RF loss and fresh updates after recovery. Verify
Signal K access approval, reconnect, unit conventions and optional GPS fix loss.
No hardware validation has been performed in the cloud environment.
