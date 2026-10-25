# SignalK MicroNet

An ESPHome project for receiving Raymarine/Tacktick Micronet wind data and
publishing it through [makusets/ESPHomeSignalK](https://github.com/makusets/ESPHomeSignalK).
The target is a **LOLIN D32 + CC1101 at 869.840 MHz (868 MHz Micronet)**. Frequency and
pins are configurable. GPS and a tilt-compensated magnetic compass are optional.

**Status: ESPHome firmware builds and host tests pass; not hardware validated.** The decoder is tested
against the documented MicroNav wind packet and malformed input. RF transmission/reception, T120 pairing, compass calibration, and a real
Signal K connection still require bench testing.

## Standalone T120 operation

The ESP32 now runs the Micronet master, so **a master display is not required**.
It creates a stable network ID from its MAC address, wakes the network using a
six-second radio preamble and polls the paired wind sensor once per second.
The implementation follows the published Micronet master/pairing sequence;
**operation with a physical T120 still needs bench verification**.

In the ESPHome web interface, press **Pair wind sensor**. Pairing runs through
wake-up, a 15-second preparation phase and a 60-second join phase, followed by
confirmation with actual wind speed and angle. Keep the charged T120 near the
receiver during pairing. Allow up to about two minutes. The sensor's old display
is not needed; pairing invites it onto the ESP32's network.

- **Wind sensor paired**: binary sensor indicating an established association.
  It restores from flash after reboot and stays on when the sensor is asleep.
- **Wind sensor online**: binary sensor indicating wind data received within ten
  seconds. This is separate from pairing status.
- **Wind sensor pairing**: binary sensor indicating an active pairing attempt.
- **Micronet status**: pairing progress, waiting/receiving state or failure.
- **Wind sensor ID**: the associated sensor's ID.

A slot request alone does not set paired status. The new association is committed
to ESP32 preferences only after valid wind data arrives following normal polling
at the end of the join phase. Failed attempts preserve the previous association.
Only one wind sensor is supported; pair with your intended T120 nearby.
See [docs/standalone.md](docs/standalone.md) for protocol evidence and limitations.

## Hardware

Suggested wiring for an unwired LOLIN D32 (all signal levels 3.3 V):

| CC1101 | LOLIN D32 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SCK | GPIO18 |
| MOSI | GPIO23 |
| MISO | GPIO19 |
| CSN | GPIO27 |
| GDO0 | GPIO25 (required for master timing) |
| GDO2 | Unconnected |

The owner's T120 is the 868 MHz model, so the example defaults to **869.840 MHz**.
A previous 915.915 MHz setting did not match that sensor. Use a CC1101 module and antenna matched to your band. Set `radio_frequency` in
`examples/micronet-signalk.yaml` to `915.915` or `869.840` MHz. This is a firmware setting;
it does not change a module's physical RF matching. The driver assumes a 26 MHz
CC1101 crystal. GPIO16 is reserved by the optional GPS example; GPIO21/22 by I2C.

## Use with ESPHome

Use **ESPHome 2026.10.0 or newer**, either ESPHome Device Builder (including the
Home Assistant add-on) or the ESPHome CLI. The device configuration is a normal
ESPHome YAML file, like ESPHomeSignalK. ESPHome downloads both repositories'
external components and compiles the firmware itself.

1. Create a device in ESPHome Device Builder and open its YAML editor.
2. Copy [examples/micronet-signalk.yaml](examples/micronet-signalk.yaml) into it.
3. Set the device name, board, frequency and pins for your hardware. The defaults
   match a LOLIN D32 with the wiring above and 869.840 MHz.
4. Add `wifi_ssid`, `wifi_password` and `signalk_host` to your ESPHome
   `secrets.yaml`. See [the secret names](examples/secrets.yaml.example).
5. Choose **Install** in ESPHome. After flashing, open the device web interface
   and press **Pair wind sensor**.

The YAML is self-contained and uses GitHub `external_components` sources. There
are no local component directories or required local YAML includes to copy.
The optional GPS and compass packages are also fetched from GitHub when enabled.
`micronet_version` pins the Micronet components/packages to a tested source commit;
set it to `main` if you want to follow repository updates.

For the CLI, place the YAML and your `secrets.yaml` together in any directory:

```sh
esphome config micronet-signalk.yaml
esphome run micronet-signalk.yaml
```

The `run` command compiles, flashes and opens logs. ESPHome manages its ESP32
compiler and dependencies. Repository setup scripts and a separate PlatformIO
project are not required for installing this device.

On Signal K, approve the device's access request under the server's access
requests page. The existing ESPHomeSignalK component handles discovery,
authentication, reconnection and publishing. The example pins it to commit
`3345231473cadd5184a3d4e741b86f3d7329c9c3`; it does not maintain a separate client.
Its connection settings entities are available through ESPHome if needed.
Keep credentials in your local ESPHome `secrets.yaml`.

## Data and filtering

The default configuration publishes wind speed in m/s to
`environment.wind.speedApparent` and signed wind angle in radians to
`environment.wind.angleApparent`. It accepts wind-transducer device type `0x02`.
Master mode sets the receive filters to its own network and the confirmed wind
sensor. An optional `network_id` under `micronet_master` overrides the MAC-derived
ID; changing it starts a separate pairing namespace. For passive reception only,
use `examples/passive.yaml` and configure the hub filters explicitly if desired.

For passive/full-network listening, `packages/micronet-sensors.yaml` provides
additional decoded measurements: water speed, depth below transducer, water
temperature, magnetic heading, SOG/COG, and trip/total log. Replace the portable
YAML's wind `sensor:` block with a GitHub reference to that package and remove
`micronet_master` when listening to an existing network. Set the hub's network
and device filters for that deployment. The standalone master accepts one wind
sensor. In the local development fixture, replace the `wind` package instead.
Do not configure the same Micronet output sensors twice.
The decoder exposes transmitter voltage as an optional sensor; no generic Signal K
battery path is guessed for it. See [the protocol notes](docs/protocol.md).

Readings expire after `stale_after` (10 seconds by default), producing ESPHome NaN
and a JSON null through ESPHomeSignalK's ArduinoJson serializer. Outages are not
queued or replayed as fresh observations. Calibration messages are not applied;
calibrate with ESPHome sensor filters or Signal K as appropriate.

## Optional GPS and compass

Uncomment the relevant GitHub package at the end of
`examples/micronet-signalk.yaml`:

- `packages/gps.yaml`: any NMEA UART receiver supported by ESPHome `gps`; example
  baud rate 9600, receiver TX to GPIO16. Change pins/baud for your receiver.
  `signalk_gps` uses the existing Signal K connection to send a structured
  `navigation.position` object, SOG in m/s and true COG in radians. Fixes older than
  five seconds become null. It does not send JSON objects as strings.
- `packages/compass-lsm303dlhc.yaml`: an LSM303**DLHC** with magnetometer at `0x1e`
  and accelerometer at `0x19`, SDA GPIO21/SCL GPIO22. Other LSM303 variants are not
  register-compatible. Calibrate magnetic X/Y/Z using measured ESPHome filters,
  and align both sensors to X forward, Y starboard, Z down before relying on heading.
  Acceleration inputs must represent gravity down (negative specific force); the
  example inverts accelerometer outputs, and mounting-axis remapping is still required.
  `heading_offset` is a mounting correction in degrees, not declination.
- Other ESPHome magnetometers/accelerometers can feed the `micronet_compass`
  sensor's six input sensor IDs. Remap axes, signs and units in their filters.
  Supply a gravity-down vector rather than raw specific force. The calculation
  uses gravity to compensate tilt and publishes magnetic heading
  in radians. Bad or stale inputs become NaN. Dynamic acceleration affects heading.

`examples/all-sensors.yaml` validates/builds both optional packages. Local compass
and GPS do not interfere with the default wind paths. If you enable Micronet
heading or SOG/COG too, choose one publisher per Signal K path to avoid competing
sources. Optional compass/GPS are published to Signal K only.

## Radio extensibility

The Micronet decoder has no CC1101 dependency. Master operation additionally
requires a `RadioTransport` with asynchronous transmit completion timestamps. Another ESPHome radio adapter can
call `Micronet::receive(data, length)` with a complete packet after sync `0x99`.
It must implement Micronet's 2-FSK modulation, rate and framing; arbitrary LoRa or
OOK modules are not interchangeable. Only the CC1101 adapter is implemented here.
ESPHome's stock CC1101 packet mode cannot directly accommodate Micronet's header-
defined lengths up to 96 bytes (its packet schema limits lengths to 64), so this
adapter streams the FIFO and adjusts the hardware length after parsing the header.
See [docs/protocol.md](docs/protocol.md) for the contract and bench checks.

## Troubleshooting radio startup

`CC1101 not found: part=00 version=00` means the radio did not return a valid SPI
identity. Part `00` is normal for a CC1101; version `00` is not. Check the actual
module pin labels (not assumed connector order), its 3.3 V supply and common
ground, and the CSN/SCK/SI/SO connections against the wiring table. SI is MOSI;
SO is MISO. GDO0 is a separate signal used after identification for TX timing.
The configured frequency and the T120's pairing state do not explain an invalid
SPI identity response.

The component logs CS/GDO0 pins and the read identity. A successful start logs
`CC1101 initialized: part=00 version=...`. A failed radio now prevents pairing and
reports `Radio unavailable: CC1101 SPI identification failed` in the status entity.
Correct the wiring/supply or configuration and reboot before attempting pairing.
A saved association is retained when radio initialization fails; online remains off.

Frequent FIFO overflows with zero complete frames are a receiver problem to
investigate; they are not evidence that the wind sensor is transmitting. The
receiver uses bulk SPI register/FIFO transfers and bounded coherent-count retries
to keep draining while bytes arrive. Older per-byte transactions could continually
postpone draining until overflow. Occasional overflows can still occur when other
components block the ESPHome loop; use the summary and packet logs to distinguish
occasional losses from a receiver that never completes a packet.

## Following a pairing attempt in the logs

The example compiles DEBUG logging, keeps the general runtime level at INFO,
and enables DEBUG specifically for the Micronet components:

```yaml
logger:
  level: DEBUG
  initial_level: INFO
  logs:
    micronet: DEBUG
    micronet_master: DEBUG
    micronet_cc1101: DEBUG
```

Check the CC1101 configuration dump for `Modem readback: 76767 baud`, calculated
from its programmed registers. This appears when connecting to ESPHome API logs
over Wi-Fi, even after the startup messages have passed. Earlier firmware selected a half-rate modem (38383 baud);
update the pinned source version before investigating unsuccessful RF pairing.

Press **Pair wind sensor** and follow:

- INFO: wake, prepare, join and confirmation phases; selected wind sensor ID;
  confirmed pairing and saving the association.
- DEBUG: `TX frame (deferred log)` includes header fields and raw packet bytes;
  `TX start` gives the preamble duration and device strobe timestamp; `TX complete`
  gives hardware sync/end timestamps. These are printed after RX is restored so
  logger delays cannot postpone STX or returning to receive mode. `Poll timing`
  reports the hardware sync interval, target error and skipped whole periods.
  TX completion confirms the modem finished, not reception by the T120.
- DEBUG: `RX frame` includes network/device IDs, message type, flags and raw bytes.
  It is logged before measurement filtering so unpaired and foreign-network
  packets remain visible. Rejections and ignored networks are also logged.
- DEBUG: a radio summary every five seconds includes received-frame, rejected-
  header, overflow and partial-packet timeout counts, plus unstable FIFO samples
  and deferred reads, including when no data arrives.
- WARN: TX timeouts/underflow, received wind low-battery alerts, and a specific
  pairing failure reason (no join request, no confirmed wind data, missing speed
  or missing angle). A previous saved association is preserved after failure.

The T120 is solar charged. With no display and no received telemetry, this bridge
cannot establish its battery charge. Expose its solar panel to outdoor daylight
and retry after charging. A received low-battery alert provides evidence of low
charge; silence or the absence of an alert does not establish a charged battery.

## Development

For component contributors, `examples/lolin-d32.yaml` loads local sources and
`examples/all-sensors.yaml` enables the local GPS/compass packages. These are
checkout-based development fixtures; the user installation example is
`examples/micronet-signalk.yaml`.

```sh
bash tools/setup.sh
bash tools/check.sh --compile
```

The helper scripts install the pinned development dependencies and run tests.
Set `MICRONET_ENV_DIR` if you want a different virtualenv directory than the cloud
workspace default. For ordinary ESPHome installation, use the YAML workflow above.

`pytest` compiles the actual protocol and compass math with AddressSanitizer and
UndefinedBehaviorSanitizer, exercises the documented wind fixture, SI conversions,
all single-byte corruptions/truncations, unknown fields and 41,500 malformed
payloads. Additional tests exercise master framing, pairing phases, slot requests and
resizes, persistence, the web button, TX completion/timeout and long preambles.
Actual ESPHome C++ headers are checked in addition to hardware doubles. CI
validates the examples and compiles the local base, optional-sensor, and portable
GitHub-sourced ESPHome configurations. Portable YAML tests run outside the checkout
with only a YAML file and placeholder secrets. Tests are not evidence of over-the-air or on-vessel operation.

Protocol interpretation and LSM303 register knowledge are based on MicroNav by
Ronan Demoment, with compass contributions by Dietmar Warning, at commit
`abaf6f20ae1af6967b30904ee8c062b63e569f2e`. This project is a new ESPHome
implementation, not the MicroNav firmware or its display/NMEA application.
Master/pairing behavior is also informed by the published protocol observations
and captures in [mnsnoop/esp32Micronet](https://github.com/mnsnoop/esp32Micronet),
commit `92b53a6760ae31b0a419fa90e948f1630f0eb384`; no source from that project is copied.
GPL-3.0-or-later; see [LICENSE](LICENSE) and [NOTICE](NOTICE).
