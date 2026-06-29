# VITA 49.2 Testing

Validation of the VITA 49.2 subsystem consists of testing the 2 types of logic that are used:

- ***Static:*** Refers to public facing functions like the generators and parsers for V49.2 packtes.
- ***Runtime:*** Refers to the behavior of the subsystem (runtime logic) such as how the system responds to Control Packets or commands that result in warnings/errors.

The Static logic is tested using your typical unit tests.

The Runtime logic is tested using PyShark and a real hardware device (ADALM Pluto) to check that the V49.2 subsystem responds to different packets correctly and sends the appropriate responses (for example does it send an AckV/AckX Packet when requested?).