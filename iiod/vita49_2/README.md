# VITA 49.2 IIOD Backend Integration

## Intro:
VITA 49.2 is a transport-layer protocol to standardize how digitized packets carry information (IQ data from the ADCs) from the RF receivers to processors (DSP, MCUs, etc.).

Within the larger libiio 'ecosystem', the intent behind incorporating VITA 49.2 is to advance ADI's vision of ***DataX*** (https://developer.analog.com/solutions/adi-datax).

Essentially the VITA 49.2 system will exist as a backend parser within IIOD, allowing certain properly formatted VITA 49.2 packets to be interpreted into commands/queries that can be issued to devices via libiio. Furthermore, the device will be able to construct and send VITA 49.2 packets to a host to exchange information such as I/Q samples, spectral data, or metadata regarding the device's operation.

## This Directory
This directory contains the backend logic for the thread(s) that handle receiving VITA 49.2 packets over UDP, processing those packets, executing any relevant commands, query signal data/metdata from the device, and generating VITA 49.2 packets such as for Signal Data or Context.

***vita49_2_client.h/.c*** is the backend logic that integrates with IIOD. It does all of the work mentioned above.

***vita49_2_host.h/.c*** is meant to run on a host and communicate with a device via VITA 49.2. Conceivably you might use the host code to generate VITA 49.2 packets (such as Control Packets) to control the device and query I/Q data, then interpret the Signal Data Packets that the device sends to you. That also makes the host code good for testing.


<br>

Praveen Perera 
praveen.perera@analog.com