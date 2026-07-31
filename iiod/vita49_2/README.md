# VITA 49.2 IIOD Backend Integration

## Intro
VITA 49.2 is a packetization protocol to standardize how network packets carry information (I/Q data from radios) from RF receivers to processors (DSP, MCUs, etc.).

Within the larger libiio 'ecosystem', the intent behind incorporating VITA 49.2 is to advance ADI's vision of ***DataX*** (https://developer.analog.com/solutions/adi-datax).

Essentially the VITA 49.2 system will exist as a backend parser within IIOD, allowing certain properly formatted VITA 49.2 packets to be interpreted into commands/queries that can be issued to devices via libiio. Furthermore, the device will be able to construct and send VITA 49.2 packets to a host to exchange information such as I/Q samples, spectral data, or metadata regarding the device's operation.

## This Directory
This directory contains the backend logic for the thread(s) that handle receiving VITA 49.2 packets over UDP, processing those packets, executing any relevant commands, query signal data/metdata from the device, and generating VITA 49.2 packets such as for Signal Data or Context.

***vita49_2_client.h/.c*** is the backend logic that integrates with IIOD. It does all of the work mentioned above.

***vita49_2_host.h/.c*** is meant to run on a host and communicate with a device via VITA 49.2. Conceivably you might use the host code to generate VITA 49.2 packets (such as Control Packets) to control the device and query I/Q data, then interpret the Signal Data Packets that the device sends to you. That also makes the host code good for testing.

***vita49_2_iiod_helpers.h/.c*** is a set of common logic used by the VITA 49.2 client code, as well as the device plugins (described below).

## Plugins Directory
This directory contains code specific to each RF transceiver/device which we'll call *"plugins"*.

Each plugin contains logic to execute/validate commands, as well as retrieve VITA context information.

The reason plugins are needed is because VITA 49.2 context fields don't have a 1-to-1 mapping to libiio attributes, hence some logic is needed to handle that translation. Each device will need its own plugin because libiio attributes vary widely across ADI's SoMs depending on complexity, so there's not really a "1 size fits all" here.

This design philosophy using "plugins" allows for a scaleable architecture that also allows users to modify device specific behavior without having to touch much of the client code. This philosophy is also what's employed with IIO-Oscilloscope.


<br>

Praveen Perera 
praveen.perera@analog.com