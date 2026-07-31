# VITA 49.2 Plugins

## Plugins Directory
This directory contains code specific to each RF transceiver/device which we'll call *"plugins"*.

Each plugin contains logic to execute/validate commands, as well as retrieve VITA context information.

The reason plugins are needed is because VITA 49.2 context fields don't have a 1-to-1 mapping to libiio attributes, hence some logic is needed to handle that translation. Each device will need its own plugin because libiio attributes vary widely across ADI's SoMs depending on complexity, so there's not really a "1 size fits all" here.

This design philosophy using "plugins" allows for a scaleable architecture that also allows users to modify device specific behavior without having to touch much of the client code. This philosophy is also what's employed with IIO-Oscilloscope.

## Getting Started
***ad9361.c*** should be used as a template when generating plugins for other devices.

Plugins have a simple API of mandatory functions that are exposed to the VITA 49.2 client code:
- ***bool identify*** *(const struct iio_context\* const ctx)*
Determines whether this plugin is compatible with the current device.

- ***const char\**** *get_rx_device_name()*
Returns a pointer to the RX device name. Avoids having to hard code this name in the client code or in a config file.


- ***const char\**** *get_tx_device_name()*
Returns a pointer to the TX device name. Avoids having to hard code this name in the client code or in a config file.


- ***int validate_control_packet*** *(const struct iio_context\* const ctx, const struct vita49_2_control_packet\* const control_packet, struct vita49_2_warnings\* const warnings)*
Validates the commands in a Control Packet and writes any warnings to the provided warnings struct.


- ***int execute_control_packet*** *(const struct iio_context\* const ctx, const struct vita49_2_control_packet\* const control_packet, struct vita49_2_ackX_packet\* const ackX_packet)*
Executes the commands in a Control Packet and writes any warnings/errors if a proper handle to a AckX packet is provided.


- ***int acquire_context_data*** *(const struct iio_context\* const ctx, struct vita49_2_cif0_fields\* const cif0, struct vita49_2_cif1_fields\* const cif1, struct vita49_2_cif2_fields\* const cif2, struct vita49_2_cif3_fields\* const cif3, struct vita49_2_cif7_fields\* const cif7, const struct vita49_2_control_packet\* const associated_control_packet)*
Queries libiio attributes to populate the appropriate CIF fields in a VITA 49.2 Context Packet.

Again much of this code can be derived from ad9361.c with modifications to the particular device that is being used.


<br>

Praveen Perera 
praveen.perera@analog.com