# Standalone wind sensor: unresolved requirement

The owner's hardware is a LOLIN D32, a 915 MHz CC1101 and a T120 wind sensor, with no
Micronet display. The T120 was previously paired with a display that is no longer
available. Its stored network/device IDs are not yet known.

MicroNav's README explicitly requires at least one display. Its MicronetDevice
processes incoming `0x01` master requests, requests transmission slots and sends
slave responses. It is not a demonstrated standalone master implementation.

For the existing receive-only firmware, borrow/use a paired display to activate
the network for initial capture. Without a master, silence does not prove a radio
wiring, frequency or decoder fault.

A standalone implementation needs the sensor model, device/network IDs, pairing
state, and validated observations of startup, pairing, wake/sleep, master requests
and ping/ack timing. The MicroNav protocol document gives a scheduling layout but
does not establish a complete sensor wake/pairing sequence. Implementing an
arbitrary periodic master packet would not prove that an unpaired or sleeping
sensor will respond.

Future work: capture reference traffic, reproduce sensor discovery/pairing, then
implement a separately enabled master with accurate one-second scheduling and
CC1101 transmit support. Verify actual wind responses and clean recovery after
power loss before claiming standalone support. Sending GPS/heading to displays
is a different feature and is not needed for this wind-only goal.
