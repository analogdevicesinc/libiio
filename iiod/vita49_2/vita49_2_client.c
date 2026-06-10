/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Praveen Perera <praveen.perera@analog.com>
 * 
 * Contributors:
 * 		- Travis Collins <travis.collins@analog.com>
*/

// Runs on the System-on-Module (SoM) and handles processing incoming VITA 49.2 packets,
// executing any necessary commands, querying data, generating VITA 49.2 packets, and sending those packets.

#include "vita49_2_client.h"
#include "vita49_2_packet_types.h"

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

#define UDP_PORT 4991 // By convention, VITA 49.2 uses 4991 for the UDP port

// I don't want to expose the function declarations listed below to other files, hence why I'm
// not putting them in the header file.

// ==============================================================
// FUNCTION DECLARATIONS
// ==============================================================

/**
 * @brief Add a CIF-to-hardware mapping node to the FRONT of the mappings linked list.
 * 
 * @param stream_id 
 * @param cif0_bit 
 * @param device_name 
 * @param attr_type 
 * @param channel_name 
 * @param is_output 
 * @param attr_name 
 * @return int 
 */
int command_add_mapping(uint32_t stream_id, uint32_t cif0_bit, 
			    const char *device_name, enum vita49_2_attr_type attr_type, const char *channel_name,
			    bool is_output, const char *attr_name);

/**
 * @brief Accepts a parsed Control Packet and executes the commands listed in it.
 * 
 * @param ctx 
 * @param pkt 
 * @return int 
 */
int execute_commands(struct iio_context *ctx, const struct vita49_2_control_packet* const pkt);

// ==============================================================
// GLOBAL VARIABLES
// ==============================================================

// Linked list containing CIF mapping structs which describe how a field in one of the CIFs 
// translates to a specific attribute on this device. This variable will be our head node.
static struct vita49_2_cif_mappings *vita49_2_cif_mappings_list = NULL;

// ==============================================================
// ENTRY POINT
// ==============================================================

int start_vita49_2_daemon(struct iio_context *ctx, struct thread_pool *pool)
{
	int ret;

	// This struct will contain all of the arguments that we want to pass to the main VITA 49.2 thread
	struct vita49_2_pdata *thread_arguments;
	thread_arguments = zalloc(sizeof(*thread_arguments));

	if (!thread_arguments)
		return -ENOMEM;

	thread_arguments->ctx = ctx;
	thread_arguments->pool = thread_pool_new();
	if (!thread_arguments->pool)
	{
		ret = -errno;
		free(thread_arguments);
		return ret;
	}

	ret = thread_pool_add_thread(pool, vita49_2_main, thread_arguments, "vita49_2_main_thd");

	if (ret == 0)
		return 0;
	
	// Free data on failure only
	free(thread_arguments);
	return ret;
}

static void vita49_2_main(struct thread_pool *pool, void *args)
{
	struct vita49_2_pdata *arguments = args;

	// ==============================================================
	// SOCKET CREATION
	// ==============================================================
	
	int socket_fd;
	struct sockaddr_in local_address;

	// Socket creation
	socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (socket_fd < 0) 
	{
		fprintf(stderr, "vita49_2_client: socket creation failed\n");
		return -1;
	}

	// Address info
	memset(&local_address, 0, sizeof(local_address));
	local_address.sin_family = AF_INET;
	local_address.sin_addr.s_addr = htonl(INADDR_ANY);
	local_address.sin_port = htons(UDP_PORT);

	// Binding socket
	if (bind(socket_fd, (struct sockaddr *)&local_address, sizeof(local_address)) < 0) 
	{
		fprintf(stderr, "vita49_2_client: UDP socket bind failed\n");
		close(socket_fd);
		return -1;
	}

	fprintf(stderr, "vita49_2_client terminating.\n");
	return;


	fprintf(stderr, "vita49_2_client: VITA 49.2 Packet Listener started.\n");


	// ==============================================================
	// EVENTS
	// ==============================================================
	
	// We'll wake up the thread whenever a packet is received or a STOP event is issued to the thread.
	// To accomplish that, we can use poll().

	struct pollfd wake_up_events[2];

	// The UDP socket
	wake_up_events[0].fd = socket_fd;
	wake_up_events[0].events = POLLIN;
	wake_up_events[0].revents = 0;

	// Thread pool wrapper issuing a STOP
	wake_up_events[1].fd = thread_pool_get_poll_fd(pool);
	wake_up_events[1].events = POLLIN;
	wake_up_events[1].revents = 0;


	// ==============================================================
	// RECEIVE LOOP
	// ==============================================================

	int ret;
	struct vita49_2_header header;
	uint32_t word;

	uint32_t buf[2048];
	ssize_t received;

	while (socket_fd >= 0) 
	{
		// ==============================================================
		// WAITING FOR EVENT TO WAKE UP THREAD
		// ==============================================================

		// Copied the poll code below from the poll_nointr() in iiod.c to ensure
		// that the VITA 49.2 backend isn't permanently interrupted by a signal.
			
			// The reason I want to "ignore" signals is because iiod.c already registers a signal
			// handler for specific signals, and that handler issues thread_pool_stop().

			// If I instead didn't use the poll-logic below, it could get interrupted by any signal
			// rather than just the ones other developers have decided to accept in iiod.c

		int ret;

		do {
			ret = poll(wake_up_events, sizeof(wake_up_events)/sizeof(wake_up_events[0]), -1);
		} while (ret == -1 && errno == EINTR);
		
		// Decode what event woke up the thread.
		// STOP takes priority.
		if (wake_up_events[1].revents & POLLIN)
			break;

		// ==============================================================
		// HANDLING RECEIVED DATA
		// ==============================================================

		// Otherwise data is available on the socket.
		received = recv(socket_fd, buf, sizeof(buf), 0);

		// Assume that a 0-length datagram implies a shutdown was called on the socket
		if (received <= 0)
		{
			fprintf(stderr, "vita49_2_client: Socket closed.\n");
			break;
		}

		// At a minimum any VITA 49.2 Packet requires a header which is 32 bits
		if (received < 4)
		{
			fprintf(stderr, "vita49_2_client: Received an invalid packet.\n");
			continue;
		}

		// Need to figure out what packet was received so that we can call the correct parser function.
		// We can determine that by looking at the header.
		word = ntohl(buf[0]);
		memcpy(&header, &word, sizeof(word));

		switch (header.packet_type)
		{
			// Signal Data Packet with no Stream ID
			case VITA49_2_PKT_TYPE_IF_DATA_NO_SID:
				// TODO: ADI doesn't support this packet as of yet, though we retain the right to implement it in the future.
				// Currently we require all Data Packets to have a Stream ID, though if that changes in the future we need to update this case-block.
				break;

			// Signal Data Packet with Stream ID
			case VITA49_2_PKT_TYPE_IF_DATA_WITH_SID:
				// Since the device is receiving this packet, that implies the host wants to the device to transmit this data.
				
				struct vita49_2_data_packet tx_data_packet;
				if (vita49_2_parse_data_packet(buf, received, &tx_data_packet) < 0)
				{
					fprintf(stderr, "vita49_2_client: Unable to parse Signal Data Packet with Stream ID.\n");
				}

				// TODO: Logic to parse the data in this packet and have the device transmit it

			// Extension Data Packet without Stream ID
			case VITA49_2_PKT_TYPE_EXT_DATA_NO_SID:
				// TODO: ADI doesn't support this packet as of yet, though we retain the right to implement it in the future.
				break;

			// Extension Data Packet with Stream ID
			case VITA49_2_PKT_TYPE_EXT_DATA_WITH_SID:
				// TODO: ADI doesn't support this packet as of yet, though we retain the right to implement it in the future.
				break;

			// Context Packet
			case VITA49_2_PKT_TYPE_IF_CONTEXT:
				// The device/client shouldn't be receiving Context Packets from host. It works the other way around,
				// as in the device/client should be generating and sending Context Packets to the host.
				fprintf(stderr, "vita49_2_client: Received a Context Packet. Skipping processing.\n");
				continue;

			// Extension Context Packet
			case VITA49_2_PKT_TYPE_EXT_CONTEXT:
				// TODO: ADI doesn't support this packet as of yet, though we retain the right to implement it in the future.
				break;

			// Command Packet
			case VITA49_2_PKT_TYPE_COMMAND:
				
				// There's multiple types of Command Packets:
					// Control
					// AckV
					// AckX
					// AckS

				// Indicator bit 26 can be used to determine if we have a Control Packet or an Acknowledge Packet.
				if (header.indicators & (1 << 2))
				{
					struct vita49_2_control_packet control_packet;
					if (vita49_2_parse_control_packet(buf, received, &control_packet) < 0)
					{
						fprintf(stderr, "vita49_2_client: Unable to parse Control Packet.\n");
					}

					// Executing the commands in the packet
					if (execute_commands(arguments->ctx, &control_packet) < 0)
					{
						fprintf(stderr, "vita49_2_client: Error while executing commands.\n");
						continue;
					}

					break;
				} 
				// Otherwise we have an Acknowledge Packet. 
				else
				{
					// The device/client shouldn't be receiving Ack Packets from host. It works the other way around,
					// as in the device/client should be generating and sending Ack Packets to the host.
					fprintf(stderr, "vita49_2_client: Received an Ack Packet. Skipping processing.\n");
					continue;
				}

				break;

			// Extension Command Packet
			case VITA49_2_PKT_TYPE_EXT_COMMAND:
				// TODO: ADI doesn't support this packet as of yet, though we retain the right to implement it in the future.
				// It's likely that we'll use this packet for executing commands that aren't well translated to CIF fields.
				break;

			// Unknown packet
			default:
				fprintf("vita49_2_client: Found a packet of unknown type: '%d'.\n", header.packet_type);
				continue;
			
		}
	}

	return NULL;
}

int vita49_2_command_init(struct iio_context *ctx)
{
	if (!ctx)
		return -1;
	
	fprintf(stderr, "vrt_command_init: Initialized VRT translation layer.\n");
	return 0;
}

int command_add_mapping(enum vita49_2_cif_types cif_type, uint32_t cif_bit, 
			    const char *device_name, enum vita49_2_attr_type attr_type, const char *channel_name,
			    bool is_output, const char *attr_name)
{
	struct vita49_2_cif_mapping *m = calloc(1, sizeof(*m));
	if (!m) return -1;

	m->cif_type = cif_type;
	m->cif_bit = cif_bit;
	snprintf(m->device_name, sizeof(m->device_name), "%s", device_name);
	m->attr_type = attr_type;
	snprintf(m->channel_name, sizeof(m->channel_name), "%s", channel_name ? channel_name : "");
	m->is_output = is_output;
	snprintf(m->attr_name, sizeof(m->attr_name), "%s", attr_name);

	m->next = vita49_2_cif_mappings_list;
	vita49_2_cif_mappings_list = m;
	
	const char *type_str = (attr_type == VITA49_2_ATTR_TYPE_DEVICE) ? "device" :
			       (attr_type == VITA49_2_ATTR_TYPE_DEBUG) ? "debug" : "channel";

	fprintf(stderr, "vrt_command: Added mapping CIF%d Bit %d -> %s/[%s]%s/%s\n",
		cif_type, cif_bit, device_name, type_str, channel_name ? channel_name : "", attr_name);
	return 0;
}

int vita49_2_command_load_mappings(const char *file_path)
{
	FILE *f;
	char line[256];
	int count = 0;

	f = fopen(file_path, "r");
	if (!f) {
		fprintf(stderr, "vrt_command: Failed to open mapping file %s\n", file_path);
		return -1;
	}

	while (fgets(line, sizeof(line), f)) {
		/* Ignore comments or empty lines */
		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
			continue;

		char *p = line;
		char *toks[7];
		int i;
		for (i = 0; i < 7; i++) {
			toks[i] = strsep(&p, ",\r\n");
			if (!toks[i]) break;
		}
		
		if (i == 7) {
			uint32_t stream_id = strtoul(toks[0], NULL, 16);
			uint32_t cif0_bit = strtoul(toks[1], NULL, 10);
			enum vita49_2_attr_type atype = VITA49_2_ATTR_TYPE_CHANNEL;
			if (strcmp(toks[3], "device") == 0) atype = VITA49_2_ATTR_TYPE_DEVICE;
			else if (strcmp(toks[3], "debug") == 0) atype = VITA49_2_ATTR_TYPE_DEBUG;

			bool is_out = (strcmp(toks[5], "true") == 0 || strcmp(toks[5], "1") == 0);
			vrt_command_add_mapping(stream_id, cif0_bit, toks[2], atype, toks[4], is_out, toks[6]);
			count++;
		} else {
			fprintf(stderr, "vrt_command: Ignoring malformed line (need 7 fields): %s\n", line);
		}
	}

	fclose(f);
	fprintf(stderr, "vrt_command: Loaded %d mappings from %s\n", count, file_path);
	return count;
}

void vita49_2_command_cleanup(void)
{
	struct vita49_2_cif_mapping *m = vita49_2_cif_mappings_list;
	struct vita49_2_cif_mapping *next;
	
	vrt_command_stop_listener();

	// Freeing the linked list memory
	while (m) 
	{
		next = m->next;
		free(m);
		m = next;
	}
	vita49_2_cif_mappings_list = NULL;

	fprintf(stderr, "vrt_command_cleanup: Cleaned up VRT translation layer.\n");
}

int execute_commands(struct iio_context *ctx, const struct vita49_2_control_packet* const pkt)
{
	struct iio_device *dev;
	struct iio_channel *chn;
	const struct iio_attr *attr;
	struct vita49_2_cif_mapping *m;
	int ret = 0;

	if (!ctx || !pkt)
		return -1;

	// TODO: Need logic that looks at the Controller ID/UUID in the Control Packet and determines if the commands
	// in this packet should be executed.

	// We're assuming the Control Packet has already been parsed and that the cif0 (and cif1-7) struct has already
	// been populated with the parsed data.

	// Iterate through all loaded mappings in the linked list. The mappings tell us what fields in the CIFs correspond to what attributes
	// on this device.
	for (m = vita49_2_cif_mappings_list; m != NULL; m = m->next) 
	{
		// Check if any of the commands in the Control Packet belong to the same CIF "family" and have the same CIF bit
		// as the current mapping. If none of the Control match with the current mapping, that means this Control Packet
		// is not targeting the attribute associated with this mapping, so we can move onto the next mapping.
		switch (m->cif_type)
		{
			case CIF0:
				uint32_t cif0_word;
				memcpy(&cif0_word, &pkt->cif0.cif0_word, sizeof(cif0_word));
			
				// If true, this means one of the bits in the CIF0 word is the same as the CIF0 bit for this mapping, thus we have a match
				if (cif0_word & (1 << m->cif_bit))
					break;

				continue;

			case CIF1:
				// CIF1 is not mandatory, so we need to check if this Control Packet uses it
				if (pkt->cif0.cif0_word.cif1_enable && pkt->cif1 != NULL)
				{
					// TODO: I haven't defined the CIF1 struct yet. Once it's been defined, we need logic to check if the CIF bit in the current
					// mapping corresponds to any bits in the CIF1 word
				}

				continue;

			case CIF2:
				// CIF2 is not mandatory, so we need to check if this Control Packet uses it
				if (pkt->cif0.cif0_word.cif2_enable && pkt->cif2 != NULL)
				{
					// TODO: I haven't defined the CIF2 struct yet. Once it's been defined, we need logic to check if the CIF bit in the current
					// mapping corresponds to any bits in the CIF2 word
				}

				continue;

			case CIF3:
				// CIF3 is not mandatory, so we need to check if this Control Packet uses it
				if (pkt->cif0.cif0_word.cif3_enable && pkt->cif3 != NULL)
				{
					// TODO: I haven't defined the CIF3 struct yet. Once it's been defined, we need logic to check if the CIF bit in the current
					// mapping corresponds to any bits in the CIF3 word
				}

				continue;

			case CIF7:
				// CIF7 is not mandatory, so we need to check if this Control Packet uses it
				if (pkt->cif0.cif0_word.cif7_enable && pkt->cif1 != NULL)
				{
					// TODO: I haven't defined the CIF7 struct yet. Once it's been defined, we need logic to check if the CIF bit in the current
					// mapping corresponds to any bits in the CIF7 word
				}

				continue;

			default:
				continue;
		}

		/* We have a match! Find device and channel */
		dev = iio_context_find_device(ctx, m->device_name);
		if (!dev) 
		{
			fprintf(stderr, "vita49_2_process: Device %s not found for mapping.\n", m->device_name);
			continue;
		}

		attr = NULL;

		// Attribute we're modifying is associated with a specific channel so we must find that channel first.
		if (m->attr_type == VITA49_2_ATTR_TYPE_CHANNEL)
		{
			chn = iio_device_find_channel(dev, m->channel_name, m->is_output);
			if (!chn) 
			{
				/* Fallback to opposite direction just in case */
				chn = iio_device_find_channel(dev, m->channel_name, !m->is_output);
				if (!chn) 
				{
					fprintf(stderr, "vita49_2_process: Channel %s not found.\n", m->channel_name);
					continue;
				}
			}

			attr = iio_channel_find_attr(chn, m->attr_name);
		} 
		// Attribute we're modifying is associated with the device as a whole
		else if (m->attr_type == VITA49_2_ATTR_TYPE_DEVICE) 
		{
			attr = iio_device_find_attr(dev, m->attr_name);
		} 
		// Attribute we're modifying is a debug attribute (advanced configuration)
		else if (m->attr_type == VITA49_2_ATTR_TYPE_DEBUG) 
		{
			attr = iio_device_find_debug_attr(dev, m->attr_name);
		}

		if (!attr) 
		{
			fprintf(stderr, "vita49_2_process: Attribute %s not found on %s.\n", m->attr_name, m->device_name);
			continue;
		}

		/* Extract value from parsed CIF structure */
		double val = 0.0;

		// Checking CIF0
		if (m->cif_type == CIF0)
		{
			// switch-statements are faster for large conditions :D
			switch (m->cif_bit)
			{
				// Context Field Change Indicator is N/A for Command Packets

				// Reference Point Identifier
				case (1 << 30):
					// TODO: Need logic to handle this. Travis and I discussed this, and conceivably we might use RPI to say that we're issuing
					// commands to a separate board/card like a daughter/personality card.
					break;

				// Bandwidth
				case (1 << 29):
					val = pkt->cif0.bandwidth;
					break;

				// IF Reference Frequency
				case (1 << 28):
					val = pkt->cif0.if_reference_frequency;
					break;

				// RF Reference Frequency
				case (1 << 27):
					val = pkt->cif0.rf_reference_frequency;
					break;

				// RF Reference Frequency Offset
				case (1 << 26):
					val = pkt->cif0.rf_reference_frequency_offset;
					break;

				// IF Band Offset
				case (1 << 25):
					val = pkt->cif0.if_band_offset;
					break;

				// Reference Level
				case (1 << 24):
					val = pkt->cif0.reference_level;
					break;

				// Gain
				case (1 << 23):
					// TODO: Since Gain consists of Stage 1 and Stage 2 gains, we need logic to figure out which one to use
					// or if both should be used
					break;

				// Over-Range Count is N/A for Command Packets

				// Sample Rate
				case (1 << 21):
					val = pkt->cif0.sample_rate;
					break;

				// Timestamp Adjustment, Timestamp Calibration Time, Temperature, Device ID, State/Event Indicators, Signal Data Packet Payload Format,
				// Formatted GPS, Formatted INS, ECEF Ephemeris, Relative Ephemeris, Ephemeris Reference ID, GPS ASCII, Context Association Lists
				// are N/A for Command Packets

				// Shouldn't get here since we already validated that the CIF of the mapping corresponds to one of the commands
				// in this Control Packet
				default:
					fprintf(stderr, "vita49_2_process: Unsupported CIF0 bit extraction %u for mapping %s.\n", 
					m->cif_bit, m->attr_name);
					continue;
			}
		}

		// Checking CIF1
		else if (m->cif_type == CIF1)
		{
			// TODO: Logic to extract the value from CIF1
			switch (m->cif_bit)
			{
				// Shouldn't get here since we already validated that the CIF of the mapping corresponds to one of the commands
				// in this Control Packet
				default:
					fprintf(stderr, "vita49_2_process: Unsupported CIF1 bit extraction %u for mapping %s.\n", 
					m->cif_bit, m->attr_name);
					continue;
			}
		}

		// Checking CIF2
		else if (m->cif_type == CIF2)
		{
			// TODO: Logic to extract the value from CIF2
			switch (m->cif_bit)
			{
				// Shouldn't get here since we already validated that the CIF of the mapping corresponds to one of the commands
				// in this Control Packet
				default:
					fprintf(stderr, "vita49_2_process: Unsupported CIF2 bit extraction %u for mapping %s.\n", 
					m->cif_bit, m->attr_name);
					continue;
			}
		}

		// Checking CIF3
		else if (m->cif_type == CIF3)
		{
			// TODO: Logic to extract the value from CIF3
			switch (m->cif_bit)
			{
				// Shouldn't get here since we already validated that the CIF of the mapping corresponds to one of the commands
				// in this Control Packet
				default:
					fprintf(stderr, "vita49_2_process: Unsupported CIF3 bit extraction %u for mapping %s.\n", 
					m->cif_bit, m->attr_name);
					continue;
			}
		}

		// Checking CIF7
		else if (m->cif_type == CIF7)
		{
			// TODO: Logic to extract the value from CIF7
			switch (m->cif_bit)
			{
				// Shouldn't get here since we already validated that the CIF of the mapping corresponds to one of the commands
				// in this Control Packet
				default:
					fprintf(stderr, "vita49_2_process: Unsupported CIF7 bit extraction %u for mapping %s.\n", 
					m->cif_bit, m->attr_name);
					continue;
			}
		}

		fprintf(stderr, "vita49_2_process: Translating mapped command %s -> %.0f\n", m->attr_name, val);
		ret = iio_attr_write_double(attr, val);
		if (ret < 0)
			fprintf(stderr, "Failed to write %s\n", m->attr_name);
	}

	return 0;
}
