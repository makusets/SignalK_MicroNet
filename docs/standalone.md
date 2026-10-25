# Standalone Micronet master and web pairing

The configured hardware is a LOLIN D32, 915 MHz CC1101 and a T120 wind sensor
previously paired with a display that is no longer available. The ESP32 now takes
the master role; the previous display and its network ID are not required for
initiating a new pairing.

## Evidence

MicroNav and MicronetToNMEA document the `0x01` master schedule, `0x03` slot request,
`0x05` resize, `0x06` commands, reverse acknowledgement slots and `0x0a/0x0b`
ping/pong. Those firmware applications are slaves, not complete master examples.
The related [mnsnoop/esp32Micronet](https://github.com/mnsnoop/esp32Micronet) project
hosts networks and documents pairing observations and captures. At reference
commit `92b53a6760ae31b0a419fa90e948f1630f0eb384`, see:

- `README.md`, Reverse Engineering Micronet / Pairing notes: a five-plus-second
  preamble and pairing status bytes `0x0f` (prepare) and `0x0d` (join).
- `micronet.h`, Full pairing capture: status `0x0f` from seconds 130–144,
  `0x0d` from 145–204, then normal `0x09` at 205. At 174, a sensor requests a
  slot; at 175 its ID is in the schedule and it begins sending data.
- The same notes describe old-network leave packets (`0x08`) and network ID
  changes at the end of pairing. No undocumented write-to-sensor command is used.

This is a new implementation based on protocol observations, not copied source
from that project. Those captures include a hull transmitter and do not prove
this implementation's compatibility with every T120 hardware/firmware revision.
Real T120 testing is still required.

## User flow

1. Wire GDO0 to GPIO25 as well as the SPI/power connections in the README.
2. Compile/install `examples/micronet-signalk.yaml` through ESPHome, join its
   Wi-Fi network and open the device's
   ESPHome web interface. All web assets are bundled locally.
3. Place the charged T120 near the ESP32 and press **Pair wind sensor** once.
4. Watch **Micronet status**: waking sensors, prepare (15 seconds), accepting a
   sensor (60 seconds), then confirming wind data (up to 30 seconds).
5. **Wind sensor paired** turns on when both valid apparent wind speed and angle
   have been received after the join window has finished and a normal-status poll
   has been sent. **Wind sensor ID** shows the confirmed ID. The association is
   saved to ESP32 preferences and normal polling continues once per second.

Pairing is an explicit, time-limited operation; it does not run automatically
when no sensor is paired. Repeated button presses during pairing do not restart
its timer. On reboot an existing association is restored, a six-second wake
preamble is sent, and polling resumes. A failed replacement attempt leaves the
previous association intact. A flash-write failure is reported in the status
entity; it is not silently presented as persisted.

**Paired** means an association exists, not that measurements are current.
**Online** means wind data was received within ten seconds. Wind readings expire
separately through the hub's `stale_after` setting. Optional local GPS and compass
continue to publish directly to Signal K; they are not transmitted onto Micronet.

## Implementation limits

One wind sensor is supported. During JOIN, the first valid wind-transducer slot
request for this network becomes the candidate; other devices are not admitted.
Slot sizes are bounded to 6–82 payload bytes. Acknowledging a slot request is done
by including the sensor in the next schedule. `0x05` resizes are applied on the
next schedule. Battery-alarm commands from the enrolled wind sensor are
acknowledged in the master's reverse ACK slot. Other setting commands and master
handover are not implemented. Ping packets use the asynchronous slot every sixty
seconds outside pairing; late asynchronous transmissions are dropped.

The master's own ID/network ID is stable for an ESP32 MAC. YAML `network_id` can
override it. Saved pairing records are versioned and scoped by network ID.
A changed network ID cannot silently restore an unrelated association.

The CC1101 extends its hardware-generated alternating preamble with an empty TX
FIFO, then sends `0x5599` synchronization and the packet. This keeps the ESPHome
loop and web UI responsive throughout the long wake burst. GDO0 ISR callbacks only
record timestamps/flags; SPI, parsing, flash writes and publishing happen in the
normal loop. RX is restored before TX-completion callbacks. Initial calibration
is reused, and measured TX turnaround adjusts the next poll's launch time.

Host tests cover the scheduling and state transitions, not RF power, antenna
matching, sensor sensitivity or exact on-air timing under Wi-Fi/web load. Bench
validation must verify pairing across reboot, old-network migration, reported
wind values, response timing, sleep/wake recovery and reception range. A passing
host test is not a claim that the physical T120 has been tested.
