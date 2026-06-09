# VITA 49.2

## Intro:
VITA 49.2 is a transport-layer protocol to standardize how digitized packets carry information (IQ data from the ADCs) from the RF receivers to processors (DSP, MCUs, etc.).

Within the larger libiio 'ecosystem', the intent behind incorporating VITA 49.2 is to advance ADI's vision of ***DataX*** (https://developer.analog.com/solutions/adi-datax).

Essentially the VITA 49.2 system will exist as a backend parser within IIOD, allowing certain properly formatted VITA 49.2 packets to be interpreted into commands/queries that can be issued to devices via libiio. Furthermore, the device will be able to construct and send VITA 49.2 packets to a host to exchange information such as I/Q samples, spectral data, or metadata regarding the device's operation.

## This Directory
This directory contains definitions for VITA 49.2 packets and elements/fields of those packets, as well as logic for generating and parsing those messages.

***vita49_2_packet_elements.h/.c*** contain definitions for elements/fields of the packets, as well as methods of extracting or encoding that information.

***vita49_2_packet_types.h/.c*** contain definitions for the actual VITA 49.2 packets (encompassing the aforementioned elements/fields) as well as methods of generating and parsing those packets.

This directory isn't meant to handle any of the backend/runtime logic for executing actions based on VITA 49.2 packets. Rather it's meant to contain helpful VITA 49.2 related functionality, mainly for packet generation/parsing.

## VITA 49.2 Backend
The logic for the VITA 49.2 Backend system and its integration with IIOD exist in the *iiod* directory.

<br>

Praveen Perera 
praveen.perera@analog.com