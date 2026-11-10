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


## Master transport

`micronet_master` observes decoded packets independently of measurement filters
and owns the network schedule and pairing state. The hub's `RadioTransport`
interface adds nonblocking `transmit(data, length, preamble_ms)` and completion
notifications containing sync/end timestamps. The implemented CC1101 master TX
packets are bounded to 48 bytes (a schedule for one wind sensor fits in 27).
Normal reception remains bounded to 96 bytes.

TX disables automatic preamble/sync (`MDMCFG2.SYNC_MODE=0`) and puts
sixteen literal `0x55` bytes, `0x99`, and the Micronet packet into the FIFO.
The wake burst uses enough literal `0x55` bytes for at least six seconds at the
read-back baud rate. This matches MicroNav's selected `0x55` polarity and
MicronetToNMEA's explicit FIFO framing; the former CC1101 automatic `0xAA`
preamble introduced a different bit transition before `0x5599`.

A dedicated ESP32 FreeRTOS task (priority 20, 4096-byte stack) services both RX
and TX every 1 ms using ESPHome's 1 kHz tick. After setup only that task accesses
this radio's SPI. TX requests wake it immediately. The 64-byte FIFO holds about
6.7 ms at this baud rate. Long bursts
start in infinite-length mode, then switch to fixed length with fewer than 256
bytes remaining, using the CC1101 datasheet section 15.2.2 procedure. FIFO
underflow and completion timeout abort TX and restore RX; a stalled worker is
not reported as a successful packet. Other devices sharing SPI must not hold
the bus long enough to starve this FIFO.

During TX, GDO0 uses inverted `PA_PD` (`0x5b`) so its edges mark RF activity even
though hardware sync is disabled. The ISR records the end edge; the sync-end
timestamp supplied to the scheduler is inferred by subtracting packet airtime
at the read-back baud. PA switching delay and ISR latency remain bench timing
uncertainties. RX restores GDO0 FIFO-threshold signalling, fixed length, and `0x5599` sync detection
before completion is published to the main loop. All TX logs and application
callbacks run there after the worker restores reception. RX continues while
those callbacks run. The worker checks headers, adjusts PKTLEN from the first
length byte, and copies complete packets into a four-slot queue. The main loop
copies/releases each slot before calling the hub or master. A full queue drops
new packets and increments `queue_drops`; it never overwrites pending packets
or blocks radio service. No logger, hub callback, sensor publishing or flash
write runs in the radio task. Header-only handling and the RX FIFO erratum
workaround are preserved.

The schedule's first entry reserves the master's own payload length; a wind
entry reserves the requested data payload. Each entry and the header have
additive checksums. Polling runs at one-second intervals relative to the radio
sync timestamps. The measured radio turnaround is included when choosing the
next launch deadline. Asynchronous pings and alarm acknowledgements use scheduled
slots; slots missed by more than 244 microseconds are dropped, not sent late.
Pairing status/timings and persistence are described in [standalone.md](standalone.md).


The modem data-rate settings are `MDMCFG4=0x8b` and `MDMCFG3=0x83`. The CC1101
formula is `(256 + DRATE_M) * 2^DRATE_E * f_XOSC / 2^28`, giving 76,766.968 baud
with a 26 MHz crystal. The earlier `0x8a` setting selected exponent 10 instead of
11 and incorrectly ran at 38,383.484 baud. TX completion alone could not reveal
that error. The regression test now derives baud from the programmed registers,
and the startup log reports baud calculated from register readback.


## FIFO service timing

Register command+data bytes and FIFO bursts are sent as individual bulk SPI
transactions. Per-byte ESP-IDF transactions can cost more than a 104 us radio
byte, and waiting for identical FIFO counts while reading each byte separately
can starve a live stream. Coherent RXBYTES sampling now retries up to three times
per loop with the faster transfers. Unstable/deferred counters are logged; the
last-byte FIFO workaround remains in force. Partial reception timeouts are
checked before sampling so changing counts cannot bypass timeout recovery.
A timed emulator regression streams a 96-byte packet through the 64-byte FIFO
while charging transfer overhead and fails with the old receiver path. It is not
a measurement of a particular board's SPI timing. Baud readback is retained and
included in `dump_config()` so API/Wi-Fi log captures can confirm it after boot.


Reference comparison: MicroNav's `src/Radio/SX1276MnetDriver.cpp` services FIFO
interrupts, bursts the header, then collects the payload and pushes it through a
message FIFO. Its radio is SX1276, not CC1101. MicronetToNMEA's CC1101
`src/RfDriver.cpp::RfIsr_Rx` uses burst FIFO reads and changes PKTLEN after reading
the header. It calls `RestartReception()` before publishing the completed message.
The ESPHome adapter follows the burst-read and receive-restart ordering, then
copies the packet into a queue before reusing its RX buffer. GDO0 interrupts
capture TX timestamps; an ESP32 task services RX instead of the main loop.
It is not a port of the references' interrupt-driven SPI implementation. Long
main-loop stalls can fill the delivery queue; task scheduling delays or shared
SPI contention can still starve the hardware FIFO.

MicronetToNMEA also sets `PKTCTRL1=0x80` (PQT=4). This preamble-quality qualifier
is now enabled in the ESPHome CC1101 adapter to reduce false sync detections in
noise. The low five bits remain zero: no appended status bytes, hardware CRC
flush, or address filtering changes the data presented to the Micronet decoder.


TX logging is deferred until RX is restored. On-air sync timestamps remain the
source of timing evidence; client Wi-Fi log receipt times are not timing anchors.
The master schedules on an absolute one-second grid instead of repeatedly anchoring
to a late previous poll. Diagnostics report interval and signed target error;
regressions cover repeated 10 ms lateness, microsecond timer wrap and skipping
blocked periods without catch-up bursts. This is still a main-loop radio service,
so timing under actual ESPHome/network load must be verified on hardware.

## RX while the main loop is busy

The listener capture identified 21 checksum-valid `0x03` slot requests from wind
sensor `020387F2`, addressed to network `81875510`. The main device recorded 21
RX overflows at the corresponding times and no complete RX packet. This makes
receive servicing a concrete problem to address; it does not by itself prove
which scheduling delay or register condition caused every overflow.

Before the RX worker change, TX ran independently but RX waited for the main
loop to finish TX logs and other application work. Restoring SRX before logging
was insufficient: initial fixed PKTLEN is 255 until the header is processed.
After a short transmitter burst ends, the modem can continue filling the FIFO
with demodulated noise. A host model reproduces an overflow when a captured
17-byte join plus subsequent noise goes unserviced for 20 ms. Servicing RX every
1 ms captures that request during a simulated 40 ms main-loop logging delay.

The config dump now reports `RX servicing: dedicated radio worker; delivery
queue=4 packets`. Runtime summaries add `RX service: queue_drops=...` and
`max_service_gap_us=...`. The maximum excludes deliberate TX time by resetting
the sampling timestamp when RX restarts. Overflows capture raw RXBYTES and
MARCSTATE before restarting, and log them later on the main loop. These fields
help diagnose remaining hardware, SPI bus contention, or scheduling faults.

## FIFO interrupts and per-packet RSSI

The radio task now also wakes on GDO0 RX FIFO threshold interrupts: FIFOTHR=0
(four bytes), IOCFG0=0 (assert at threshold, deassert below it). The ISR only
notifies the task. SPI and packet handling remain outside interrupt context;
the one-millisecond poll remains a fallback. This lets short headers reach the
PKTLEN update promptly instead of waiting for repeated periodic polls. It uses
the same GDO0 wire; TX still switches GDO0 to inverted PA_PD. Config dumps report
`RX FIFO wake: GDO0 threshold=4 bytes; fallback=1 ms; packet RSSI enabled`.
`fifo_irq_wakes` in RX service summaries confirms RX interrupts are occurring.
This does not prevent gaps caused by disabled interrupts/flash operations or
long SPI bus contention; hardware timing still needs measurement.

The worker reads RSSI (status register 0x34) before restarting reception. With
sync detection enabled the RSSI estimate is frozen at sync, so it describes the
received frame rather than noise after a later restart. Signed raw units are
converted as `(raw >= 128 ? raw - 256 : raw) / 2 - 74` dBm. The float travels with
the queued packet; the main loop validates the message and filters the source
before updating the optional hub signal-strength sensor. No APPEND_STATUS bytes,
CRC mode, or over-the-air framing change is needed.
