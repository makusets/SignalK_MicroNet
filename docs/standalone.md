# Standalone Micronet master and web pairing

The configured hardware is a LOLIN D32, CC1101 and an 868 MHz T120 wind sensor
previously paired with a display that is no longer available. Its Micronet channel
is 869.840 MHz; the earlier 915.915 MHz configuration did not match it. The ESP32 now takes
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

## Pairing diagnostics

See the README's logging configuration. Stage transitions and join/confirmation
are logged at INFO, and actual queued TX / received RX packets at DEBUG. RX is
logged before hub filters, including traffic that is not published. A radio
summary appears every five seconds even in silence. Failed confirmation retains
the last candidate ID in the failure log and distinguishes no join request from
an admitted sensor that never supplied usable wind data. No log asserts remote
reception solely from local TX completion. The 76.8 kbaud correction is required;
older firmware using a data-rate exponent of 10 transmitted at half that rate.

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

The CC1101 transmits explicit `0x55` preamble bytes and `0x99` sync through its
FIFO, with automatic preamble/sync disabled. A dedicated ESP32 task feeds the
FIFO throughout the six-second wake burst, keeping the ESPHome loop and web UI
responsive. GDO0 ISR callbacks only record RF activity timestamps; no SPI or
logging runs in the ISR. The worker restores RX before notifying the main loop,
which performs parsing, flash writes, publishing and TX logging. Initial
calibration is reused. TX sync timing is inferred from the RF end edge and
packet airtime; see [protocol.md](protocol.md#master-transport) for timing limits.

Host tests cover the scheduling and state transitions, not RF power, antenna
matching, sensor sensitivity or exact on-air timing under Wi-Fi/web load. Bench
validation must verify pairing across reboot, old-network migration, reported
wind values, response timing, sleep/wake recovery and reception range. A passing
host test is not a claim that the physical T120 has been tested.


Poll scheduling now retains a one-second grid anchored at the first radio
sync timestamp; lateness from one launch is not added to every following period.
Whole overdue periods are skipped instead of sent as a catch-up burst. TX debug
messages are deferred until receive mode has been restored, with device timestamps
included. GUI status publication is also deferred while the radio is busy. These
changes address the uploaded log's 1.0104-second average polling interval; they do
not establish a received sensor response. The T120's subsequently confirmed 868 MHz
marking requires the independently necessary 869.840 MHz frequency correction.

## Preamble compatibility update

The two-CC1101 listener test received master control frames but no T120 response.
That established communication between our radios, not acceptance by the T120.
The TX framing now follows Rodemfr's explicit `0x55`/`0x99` sequence instead of
the CC1101 automatic `0xAA` preamble. The 868 MHz PA setting also now uses TI's documented nominal 0 dBm value
`0x50`, replacing the undocumented `0x8d` setting. The pairing phases and packet
payloads are unchanged. This corrects a reference mismatch; it does not establish the cause
of the T120's silence or demonstrate a successful physical pairing.

After rebuilding the main unit, its Wi-Fi config dump should include
`TX framing: explicit 0x55 preamble + 0x99 sync (FIFO worker)`. Deferred TX logs
include the actual preamble byte count and `sync=99`. The existing passive
listener firmware remains compatible because its receive sync is unchanged.
