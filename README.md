# SignalK MicroNet

An ESPHome project for receiving Raymarine/Tacktick Micronet wind data and
publishing it through [makusets/ESPHomeSignalK](https://github.com/makusets/ESPHomeSignalK).
The initial target is a **LOLIN D32 + CC1101 at 915.915 MHz**. Frequency and
pins are configurable. GPS and a tilt-compensated magnetic compass are optional.

**Status: initial implementation, not hardware validated.** The decoder is tested
against the documented MicroNav wind packet and malformed input. RF reception,
compass calibration, and a real Signal K connection still require bench testing.

## Wind sensor without a display

Micronet uses a master that polls slave instruments once per second. This project
currently **receives only**; it does not pair, wake a sensor, emit master requests,
or transmit local GPS/compass data onto Micronet. A paired, awake Micronet display
must already be operating the network for ordinary wind capture.

Your current hardware has only a wind sensor. Passive reception alone therefore
is **not a complete standalone solution**. [MicroNav](https://github.com/Rodemfr/MicroNav)
also requires a display: it implements a participating slave, not a replacement
master. Standalone polling/pairing needs further protocol research and hardware
captures; it is tracked in [docs/standalone.md](docs/standalone.md). No speculative
transmit sequence is enabled in this firmware.

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
| GDO0 / GDO2 | Unconnected (FIFO is polled) |

Use a CC1101 module and antenna matched to your band. Set `radio_frequency` in
`examples/lolin-d32.yaml` to `915.915` or `869.840` MHz. This is a firmware setting;
it does not change a module's physical RF matching. The driver assumes a 26 MHz
CC1101 crystal. GPIO16 is reserved by the optional GPS example; GPIO21/22 by I2C.

## Configure and build

Python 3.12 and a C++17 compiler are required for development. ESPHome is pinned
in `requirements-dev.txt`. Setup creates an ignored secrets file only if absent.

```sh
bash tools/setup.sh
# Edit examples/secrets.yaml: Wi-Fi SSID/password and Signal K server host.
bash tools/check.sh --compile
/workspace/.venvs/micronet/bin/esphome run examples/lolin-d32.yaml
```

Outside the cloud environment, set `MICRONET_ENV_DIR` to your preferred virtualenv
path, and set writable `XDG_CACHE_HOME` and `PLATFORMIO_CORE_DIR` paths when building.
The `run` command flashes hardware and should be run on a machine with access to
the ESP32 USB port. The example uses ESP-IDF and two build workers.

On Signal K, approve the device's access request under the server's access
requests page. The existing ESPHomeSignalK component handles discovery,
authentication, reconnection and publishing. This project pins it to commit
`3345231473cadd5184a3d4e741b86f3d7329c9c3`; it does not maintain a separate client.
Its connection settings entities are available through ESPHome if needed.
Secrets stay in `examples/secrets.yaml` and must not be committed.

## Data and filtering

The default package publishes wind speed in m/s to
`environment.wind.speedApparent` and signed wind angle in radians to
`environment.wind.angleApparent`. It accepts wind-transducer device type `0x02`.
Enable DEBUG logs to discover network and device IDs, then configure `network_id`
and optionally `device_id` to isolate your instrument. Filters combine with AND.
Unconfigured network filtering accepts all nearby networks.

`packages/micronet-sensors.yaml` provides additional decoded measurements: water
speed, depth below transducer, water temperature, magnetic heading, SOG/COG, and
trip/total log. **Replace** the default wind package with this package to use it,
and remove/change `device_type` as appropriate. Do not include both packages.
The decoder exposes transmitter voltage as an optional sensor; no generic Signal K
battery path is guessed for it. See [the protocol notes](docs/protocol.md).

Readings expire after `stale_after` (10 seconds by default), producing ESPHome NaN
and a JSON null through ESPHomeSignalK's ArduinoJson serializer. Outages are not
queued or replayed as fresh observations. Calibration messages are not applied;
calibrate with ESPHome sensor filters or Signal K as appropriate.

## Optional GPS and compass

Uncomment the relevant package in `examples/lolin-d32.yaml`:

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

The Micronet decoder has no CC1101 dependency. Another ESPHome radio adapter can
call `Micronet::receive(data, length)` with a complete packet after sync `0x99`.
It must implement Micronet's 2-FSK modulation, rate and framing; arbitrary LoRa or
OOK modules are not interchangeable. Only the CC1101 adapter is implemented here.
ESPHome's stock CC1101 packet mode cannot directly accommodate Micronet's header-
defined lengths up to 96 bytes (its packet schema limits lengths to 64), so this
adapter streams the FIFO and adjusts the hardware length after parsing the header.
See [docs/protocol.md](docs/protocol.md) for the contract and bench checks.

## Development

`pytest` compiles the actual protocol and compass math with AddressSanitizer and
UndefinedBehaviorSanitizer, exercises the documented wind fixture, SI conversions,
all single-byte corruptions/truncations, unknown fields and 41,500 malformed
payloads. CI also validates and compiles the base and optional-sensor ESPHome
configurations. Tests are not evidence of over-the-air or on-vessel operation.

Protocol interpretation and LSM303 register knowledge are based on MicroNav by
Ronan Demoment, with compass contributions by Dietmar Warning, at commit
`abaf6f20ae1af6967b30904ee8c062b63e569f2e`. This project is a new ESPHome
implementation, not the MicroNav firmware or its display/NMEA application.
GPL-3.0-or-later; see [LICENSE](LICENSE) and [NOTICE](NOTICE).
