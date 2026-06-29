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
#include <vita49_2/vita49_2_packet_types.h>
#include <vita49_2/vita49_2_packet_elements.h>
#include <iio/iio.h>

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <math.h>
#include <sys/timerfd.h>
#include <float.h>

#define UDP_PORT 4991 // By convention, VITA 49.2 uses 4991 for the UDP port

// Streaming related parameters for the RX and TX I/Q buffers

// A VITA 49.2 packet has a max size of 65535 words. It's more efficient to try and pack as many samples as possible into a packet.
// However UDP has a max packet size of 65535 bytes (not words!), so we actually have to divide by 4.
#define NUM_RX_SAMPLES 65440/4 
#define NUM_RX_BLOCKS 4

#define NUM_TX_SAMPLES 128
#define NUM_RX_BLOCKS 4

#define NUM_CHANNELS 2 // One for the I-component and one for the Q-component
#define BYTES_PER_SAMPLE 4 // I and Q-components are both 16-bit signed integers

#define STREAM_ID_TABLE_SIZE 50 // How big our array of Stream IDs structs should be. Expand this number in the future if we have a lot of concurrent VITA 49.2 connections/packet streams.

#define CONTEXT_PACKET_INTERVAL_S 3 // How many seconds to wait before sending the next Context Packet (ignoring Context Packets that are triggered by metadata changes)

#define EPSILON

// I don't want to expose the function declarations listed below to other files, hence why I'm
// not putting them in the header file.

// ==============================================================
// FUNCTION DECLARATIONS (INTERNAL ONLY, NOT OUTWARDLY EXPOSED)
// ==============================================================

/**
 * @brief Linear search of an array of vita49_2_stream_entry structs to see if the insertion_item already exists.
 * If not, the element is copied onto the next available element in the array and the index is returned.
 * If the insertion_item already exists in the array, the index to that element is returned.
 * Otherwise a negative error code is returned.
 * 
 * @param array_start Pointer to the start of the array
 * @param array_size Size of the array
 * @param insertion_item Pointer to a struct containing the parameter values of the associated struct that we want to insert into the array.
 * @return ssize_t 
 */
ssize_t insert_stream_id(struct vita49_2_stream_entry* array_start, size_t array_size, const struct vita49_2_stream_entry* const insertion_item);

/**
 * @brief Add a CIF-to-hardware mapping node to the FRONT of the mappings linked list.
 * 
 * @param cif_type 
 * @param cif_bit 
 * @param device_name 
 * @param attr_type 
 * @param channel_name 
 * @param is_output 
 * @param attr_name 
 * @return int 
 */
int command_add_mapping(enum vita49_2_cif_types cif_type, uint32_t cif_bit, 
			    const char *device_name, enum vita49_2_attr_type attr_type, const char *channel_name,
			    bool is_output, const char *attr_name);

/**
 * @brief Accepts a parsed Control Packet and executes the commands listed in it.
 * 
 * @param ctx 
 * @param pkt 
 * @param ackX_packet Can be NULL (optional). Pass an AckX Packet to populate its error fields if necessary. 
 * @return int 
 */
int execute_commands(struct iio_context *ctx, const struct vita49_2_control_packet* const pkt, struct vita49_2_ackX_packet* const ackX_packet);

/**
 * @brief Validates the commands in a Control Packet by verifying that provided new value for an attribute is within the acceptable range.
 * 
 * @param ctx
 * @param control_packet 
 * @param warnings 
 * @return int 
 */
int validate_commands(struct iio_context *ctx, const struct vita49_2_control_packet* const control_packet, struct vita49_2_warnings* const warnings);

/**
 * @brief Validates a specific command that takes a uint32_t attribute value.
 * 
 * Returns a negative value if there's an error, otherwise 0 is returned.
 * 
 * @param ctx 
 * @param cif_type Which CIF group (0, 1, 3, 7).
 * @param cif_bit 0 to 31.
 * @param new_value The new value that you want to validate before assigning to the attribute.
 * @return enum vita49_2_warnings_error_codes 
 */
enum vita49_2_warnings_error_codes validate_command_u32(struct iio_context *ctx, uint8_t cif_type, uint8_t cif_bit, uint32_t new_value);

/**
 * @brief Validates a specific command that takes a uint64_t attribute value.
 * 
 * Returns a negative value if there's an error, otherwise 0 is returned.
 * 
 * @param ctx 
 * @param cif_type Which CIF group (0, 1, 3, 7).
 * @param cif_bit 0 to 31.
 * @param new_value The new value that you want to validate before assigning to the attribute.
 * @return enum vita49_2_warnings_error_codes 
 */
enum vita49_2_warnings_error_codes validate_command_u64(struct iio_context *ctx, uint8_t cif_type, uint8_t cif_bit, uint64_t new_value);

/**
 * @brief Validates a specific command that takes a int64_t attribute value.
 * 
 * Returns a negative value if there's an error, otherwise 0 is returned.
 * 
 * @param ctx 
 * @param cif_type Which CIF group (0, 1, 3, 7).
 * @param cif_bit 0 to 31.
 * @param new_value The new value that you want to validate before assigning to the attribute.
 * @return enum vita49_2_warnings_error_codes 
 */
enum vita49_2_warnings_error_codes validate_command_i64(struct iio_context *ctx, uint8_t cif_type, uint8_t cif_bit, int64_t new_value);

/**
 * @brief Validates a specific command that takes a float attribute value.
 * 
 * Returns a negative value if there's an error, otherwise 0 is returned.
 * 
 * @param ctx 
 * @param cif_type Which CIF group (0, 1, 3, 7).
 * @param cif_bit 0 to 31.
 * @param new_value The new value that you want to validate before assigning to the attribute.
 * @return enum vita49_2_warnings_error_codes 
 */
enum vita49_2_warnings_error_codes validate_command_float(struct iio_context *ctx, uint8_t cif_type, uint8_t cif_bit, float new_value);

/**
 * @brief Validates a specific command that takes a double attribute value.
 * 
 * Returns a negative value if there's an error, otherwise 0 is returned.
 * 
 * @param ctx 
 * @param cif_type Which CIF group (0, 1, 3, 7).
 * @param cif_bit 0 to 31.
 * @param new_value The new value that you want to validate before assigning to the attribute.
 * @return enum vita49_2_warnings_error_codes 
 */
enum vita49_2_warnings_error_codes validate_command_double(struct iio_context *ctx, uint8_t cif_type, uint8_t cif_bit, double new_value);

/**
 * @brief Looks at the CIF mappings and queries libiio for the current value of the attributes in the CIF mappings and writes them to a Context Packet struct.
 * 
 * Returns 0 for success and a negative value for errors.
 * 
 * @param ctx 
 * @param cif0 
 * @param cif1 Can be NULL (optional)
 * @param cif2 Can be NULL (optional)
 * @param cif3 Can be NULL (optional)
 * @param cif7 Can be NULL (optional)
 * @param associated_control_packet Can be NULL (optional). Used for populating CIF data for AckS Packets since AckS Packets can only contain the CIF fields that were present in the associated Control Packet.
 * @return int 
 */
int acquire_context_data(struct iio_context *ctx, struct vita49_2_cif0_fields* cif0, struct vita49_2_cif1_fields* cif1, struct vita49_2_cif2_fields* cif2,
						struct vita49_2_cif3_fields* cif3, struct vita49_2_cif7_fields* cif7, struct vita49_2_control_packet* associated_control_packet);
/**
 * @brief More precise way of comparing doubles. Takes into account scaling.
 * 
 * Returns true if the 2 doubles are equal.
 * 
 * @param a 
 * @param b 
 * @return true 
 * @return false 
 */
static inline bool double_compare(double a, double b)
{
	return fabs(a - b) <= DBL_EPSILON * fmax(fabs(a), fabs(b));
}

/**
 * @brief More precise way of comparing floats. Takes into account scaling.
 * 
 * Returns true if the 2 floats are equal.
 * 
 * @param a 
 * @param b 
 * @return true 
 * @return false 
 */
static inline bool float_compare(float a, float b)
{
	return fabs(a - b) <= FLT_EPSILON * fmax(fabs(a), fabs(b));
}

// ==============================================================
// GLOBAL VARIABLES
// ==============================================================

// Linked list containing CIF mapping structs which describe how a field in one of the CIFs 
// translates to a specific attribute on this device. This variable will be our head node.
static struct vita49_2_cif_mapping *vita49_2_cif_mappings_list = NULL;

// ==============================================================
// ENTRY POINT
// ==============================================================

int start_vita49_2_daemon(struct iio_context *ctx, struct thread_pool *pool)
{
	int ret;

	// This struct will contain all of the arguments that we want to pass to the main VITA 49.2 thread
	struct vita49_2_pdata *thread_arguments;
	thread_arguments = calloc(1, sizeof(*thread_arguments));

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
		return;
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
		return;
	}

	fprintf(stderr, "vita49_2_client: VITA 49.2 Packet Listener started.\n");


	// ==================================================================================
	// CREATING BUFFER + STREAM FOR I/Q DATA (High level iio_stream API from Libiio v1.x)
	// ==================================================================================

	// See https://events.gnuradio.org/event/18/contributions/242/attachments/101/207/grcon2022.pdf
	// for more information, specifically Slide 10.

	// To retrieve I/Q data from the device via libiio, we need to create a buffer as well as a streaming object
	// to leverage DMA to continuously provide us with new data.

	// My logic below is based on Travis' example code (test_ad9364.c) as I saw it captured pretty much
	// exactly what I wanted to achieve, however I'm attempting to make this code generic to support
	// different RF transceivers rather than just hte AD9364 or anything in just that family.


	// Question from leaving off point from last night, how do I dynamically determine the RX device? Do I use a config
	// file and have the user specify it or is there a way for me to discover it or a common pattern (like iio:device4 is always RX)?
	
	// Until I figure this (^^) out, I'll work under the assumption that the RX and TX device names are known.

	// I need some booleans to keep track of whether initialization of the buffer+stream for RX and TX were successful
	// which will let me know if I can handle I/Q RX or TX requests from VITA
	bool rx_ready = false, tx_ready = false;

	char rx_device_name[] = "cf-ad9361-lpc";
	char tx_device_name[] = "cf-ad9361-dds-core-lpc";

	struct iio_channel *channel, *i_channel;

	// ==============================================================
	// CONFIGURING RX DEVICE
	// ==============================================================

	// First we need handles to the devices
	struct iio_device *rx_device = iio_context_find_device(arguments->ctx, rx_device_name);

	if (rx_device == NULL)
	{
		fprintf(stderr, "vita49_2_client: Could not find RX device. Device will be unable to retrieve I/Q data.\n");

		// Skip the rest of the setup for RX and proceed with TX
		goto tx_configuration;
	}
	
	// Now to enable the channels. We only care about I/Q data, so we don't need to capture data from all of the channels on a device.
	// To accomplish that, I'll use a channel mask.
	struct iio_channels_mask *rx_channel_mask = iio_create_channels_mask(iio_device_get_channels_count(rx_device));
	
	if (rx_channel_mask == NULL)
	{
		fprintf(stderr, "vita49_2_client: Failed to create RX channel mask.");
		rx_device = NULL;
	
		goto tx_configuration;
	}

	// Adding I (voltage0) and Q (voltage1) to the mask
	channel = iio_device_find_channel(rx_device, "voltage0", false);
	if (channel == NULL)
	{
		fprintf(stderr, "vita49_2_client: Unable to find I-channel (voltage0) for RX device.\n");
		rx_device = NULL;
		rx_channel_mask = NULL;

		goto tx_configuration;
	}

	iio_channel_enable(channel, rx_channel_mask);

	i_channel = iio_device_find_channel(rx_device, "voltage1", false);
	if (i_channel == NULL)
	{
		fprintf(stderr, "vita49_2_client: Unable to find Q-channel (voltage1) for RX device.\n");
		rx_device = NULL;
		rx_channel_mask = NULL;

		goto tx_configuration;
	}

	iio_channel_enable(i_channel, rx_channel_mask);

	// Creating a buffer
	struct iio_buffer *rx_buffer = iio_device_create_buffer(rx_device, 0, rx_channel_mask);
	if (iio_err(rx_buffer) != 0)
	{
		fprintf(stderr, "vita49_2_client: Unable to create RX buffer.\n");
		rx_device = NULL;
		rx_channel_mask = NULL;
		
		goto tx_configuration;
	}

	// Creating a stream
	struct iio_stream *rx_stream = iio_buffer_create_stream(rx_buffer, NUM_RX_BLOCKS, NUM_RX_SAMPLES);
	if (iio_err(rx_stream) != 0)
	{
		fprintf(stderr, "vita49_2_client: Unable to create RX stream.\n");
		rx_device = NULL;
		rx_channel_mask = NULL;
		iio_buffer_destroy(rx_buffer);

		goto tx_configuration;
	}
	// Since RX setup was successful, we can enable the flag
	rx_ready = true;
	const struct iio_block *rx_block; // Will be used when we want to grab data from the RX stream

	// ==============================================================
	// CONFIGURING TX DEVICE
	// ==============================================================
	tx_configuration: ;

	// Handle to the TX device
	struct iio_device *tx_device;
	tx_device = iio_context_find_device(arguments->ctx, tx_device_name);
	if (tx_device == NULL)
	{
		fprintf(stderr, "vita49_2_client: Could not find TX device. Device will be unable to handle requests to transmit data.\n");

		// Skip the rest of the setup for TX
		goto context_packet_timerfd_configuration;
	}

	// Channel Mask
	struct iio_channels_mask *tx_channel_mask = iio_create_channels_mask(iio_device_get_channels_count(tx_device));
	
	if (tx_channel_mask == NULL)
	{
		fprintf(stderr, "vita49_2_client: Failed to create TX channel mask.");
		tx_device = NULL;
	
		goto context_packet_timerfd_configuration;
	}

	// Adding I (voltage0) and Q (voltage1) to the mask
	channel = iio_device_find_channel(tx_device, "voltage0", true);
	if (channel == NULL)
	{
		fprintf(stderr, "vita49_2_client: Unable to find I-channel (voltage0) for TX device.\n");
		tx_device = NULL;
		tx_channel_mask = NULL;

		goto context_packet_timerfd_configuration;
	}

	iio_channel_enable(channel, tx_channel_mask);

	channel = iio_device_find_channel(tx_device, "voltage1", true);
	if (channel == NULL)
	{
		fprintf(stderr, "vita49_2_client: Unable to find Q-channel (voltage1) for RX device.\n");
		tx_device = NULL;
		tx_channel_mask = NULL;

		goto context_packet_timerfd_configuration;
	}

	iio_channel_enable(channel, tx_channel_mask);

	// Creating a buffer
	struct iio_buffer *tx_buffer = iio_device_create_buffer(tx_device, 0, tx_channel_mask);
	if (iio_err(tx_buffer) != 0)
	{
		fprintf(stderr, "vita49_2_client: Unable to create TX buffer.\n");
		tx_device = NULL;
		tx_channel_mask = NULL;
		
		goto context_packet_timerfd_configuration;
	}

	// Up until this point the TX config has been pretty identical to the RX config.
	// The stream component is where we'll diverge. TX doesn't need a stream because the device
	// doesn't need to constantly transmit data. The most likely usecase that we'd target is the host
	// sends us several Signal Data Packets and we combine all of that data into a single block and transmit that.

	// In the future we might want to consider a stream if ADI decides to support functionality
	// where a host can constantly sent Signal Data Packets with I/Q data to transmit.

	// Creating a block
	struct iio_block *tx_block = iio_buffer_create_block(tx_buffer, NUM_TX_SAMPLES * BYTES_PER_SAMPLE * NUM_CHANNELS);
	if (iio_err(tx_block) != 0)
	{
		fprintf(stderr, "vita49_2_client: Unable to create TX block.\n");
		tx_device = NULL;
		tx_channel_mask = NULL;
		iio_buffer_destroy(tx_buffer);
	}
	else
		tx_ready = true;


	// ==============================================================
	// CONTEXT PACKET INTERVAL TIMER
	// ==============================================================

	context_packet_timerfd_configuration: ;

	// VITA 49.2 recommends sending Context Packets on a regular interval as well as when certain
	// metadata changes (like temperature).

	// To handle generation and sending of the Context Packets on an interval, we can use a timerfd
	// that triggers an event in the call to poll().

	int context_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
	if (context_timer_fd < 0)
	{
		fprintf(stderr, "vita49_2_client: Failed to create timerfd for Context Packet intervals.\n");
		return;
	}

	struct itimerspec timer_specs;
	
	// Fire for the first time in 5 seconds
	timer_specs.it_value.tv_nsec = 0;
	timer_specs.it_value.tv_sec = 5;

	// Fire the timer on an interval defined by CONTEXT_PACKET_INTERVALS_S
	timer_specs.it_interval.tv_nsec = 0;
	timer_specs.it_interval.tv_sec = CONTEXT_PACKET_INTERVAL_S;

	if (timerfd_settime(context_timer_fd, 0, &timer_specs, NULL) < 0)
	{
		fprintf(stderr, "vita49_2_client: Failed to initialize timerfd for Context Packet intervals.\n");
		return;
	}

	uint64_t num_timer_expirations;

	// ==============================================================
	// EVENTS
	// ==============================================================

	// We'll wake up the thread whenever a packet is received, STOP event is issued to the thread,
	// or the Context Packet interval timer fires.
	// To accomplish that, we can use poll().

	struct pollfd wake_up_events[3];

	// Thread pool wrapper issuing a STOP
	wake_up_events[0].fd = thread_pool_get_poll_fd(pool);
	wake_up_events[0].events = POLLIN;
	wake_up_events[0].revents = 0;

	// The UDP socket
	wake_up_events[1].fd = socket_fd;
	wake_up_events[1].events = POLLIN;
	wake_up_events[1].revents = 0;

	// Context Packet timerfd
	wake_up_events[2].fd = context_timer_fd;
	wake_up_events[2].events = POLLIN;
	wake_up_events[2].revents = 0;


	// ==============================================================
	// TIME DATA PACKET SETUP
	// ==============================================================

	// Setting up the majority of a time data packet now to streamline future processing
	struct vita49_2_data_packet time_data_packet = {0};

	time_data_packet.prologue.header.indicators = (1 << 1); // Indicate that we're NOT generating a VITA 49.0 packet. See Table 5.1.1.1-1 for more info on indicator bits.
	time_data_packet.prologue.header.ts_integer_format = VITA49_2_TSI_UTC;
	time_data_packet.prologue.header.ts_fractional_format = VITA49_2_TSF_NONE; // We have no way of getting picosecond time precision in Linux
	time_data_packet.prologue.header.has_class_id = 1;
	time_data_packet.prologue.header.packet_type = VITA49_2_PKT_TYPE_IF_DATA_WITH_SID;

	time_data_packet.prologue.class_id.lower_word.oui = OUI;
	time_data_packet.prologue.class_id.upper_word.packet_class_code = VITA49_2_PKT_CLASS_TIME_DATA;
	time_data_packet.prologue.class_id.upper_word.information_class_code = VITA49_2_INFO_CLASS_MODULE_TIME_DATA;

	time_data_packet.prologue.has_stream_id = 1;
	time_data_packet.prologue.has_class_id = 1;
	time_data_packet.prologue.has_timestamp_int = 1;
	// time_data_packet.prologue.has_timestamp_frac = 1; // No way of getting picosecond time precision in Linux

	// Here are the remaining fields that have to be set whenever one of these messages have to be generated and sent:
	// Data Packet:
		// Prologue:
			// Header:
				// packet_size_words (this gets handled automatically when the vita49_2_generate_<packet_name>() function gets called) *
				// packet_count *
			// stream_id *
			// timestamp_int *
			// TODO: timestamp_frac, Linux doesn't provide a way to get picosecond accuracy natively
		// Payload:
			// payload *
			// payload_num_words *

		// TODO: Trailer support

	ssize_t time_data_packet_size = 0;

	// ==============================================================
	// ACKV PACKET SETUP
	// ==============================================================

	struct vita49_2_ackV_packet ackV_packet = {0};

	ackV_packet.command_prologue.common_prologue.header.indicators = (1 << 2);		// Indicate that this specific Command Packet is an Acknowledge type
	ackV_packet.command_prologue.common_prologue.header.ts_integer_format = VITA49_2_TSI_UTC;
	ackV_packet.command_prologue.common_prologue.header.ts_fractional_format = VITA49_2_TSF_NONE;
	ackV_packet.command_prologue.common_prologue.header.has_class_id = 1;
	ackV_packet.command_prologue.common_prologue.header.packet_type = VITA49_2_PKT_TYPE_COMMAND;

	ackV_packet.command_prologue.common_prologue.class_id.lower_word.oui = OUI;
	ackV_packet.command_prologue.common_prologue.class_id.upper_word.packet_class_code = VITA49_2_PKT_CLASS_ACKV_ACKX;
	ackV_packet.command_prologue.common_prologue.class_id.upper_word.information_class_code = VITA49_2_INFO_CLASS_MODULE_TIME_DATA;
	
	ackV_packet.command_prologue.common_prologue.has_stream_id = 1;
	ackV_packet.command_prologue.common_prologue.has_class_id = 1;
	ackV_packet.command_prologue.common_prologue.has_timestamp_int = 1;
	// time_data_packet.prologue.has_timestamp_frac = 1; // No way of getting picosecond time precision in Linux

	ackV_packet.command_prologue.ack_cam = calloc(1, sizeof(*ackV_packet.command_prologue.ack_cam));
	if (ackV_packet.command_prologue.ack_cam == NULL)
	{
		fprintf(stderr, "vita49_2_client: Failed to allocate memory for AckV CAM field.\n");
		return;
	}

	ackV_packet.command_prologue.ack_cam->ackV_request = 1;

	// Remaining Fields that need to be set:
	// AckV Packet:
		// Command Prologue:
			// Common Prologue
				// Header:
					// packet_size_words (this gets handled automatically when the vita49_2_generate_<packet_name>() function gets called) *
					// packet_count *
				// stream_id *
				// timestamp_int *
				// TODO: timestamp_frac, Linux doesn't provide a way to get picosecond accuracy natively
			// CAM
				// Warnings Present *
				// Control Fields *
			// Message ID *
			// Controllee ID/UUID *
			// Controller ID/UUID *
		// CIF0 Warnings *
		// CIF1 Warnings (if applicable) *
		// CIF2 Warnings (if applicable) *
		// CIF3 Warnings (if applicable) *
		// CIF7 Warnings (if applicable) *
		// Warnings Payload *
		// Warnings Payload Size *

	ssize_t ackV_packet_size = 0;

	// ========================================================================================================
	// ACKX PACKET SETUP
	// ========================================================================================================

	struct vita49_2_ackX_packet ackX_packet = {0};

	ackX_packet.command_prologue.common_prologue.header.indicators = (1 << 2);		// Indicate that this specific Command Packet is an Acknowledge type
	ackX_packet.command_prologue.common_prologue.header.ts_integer_format = VITA49_2_TSI_UTC;
	ackX_packet.command_prologue.common_prologue.header.ts_fractional_format = VITA49_2_TSF_NONE;
	ackX_packet.command_prologue.common_prologue.header.has_class_id = 1;
	ackX_packet.command_prologue.common_prologue.header.packet_type = VITA49_2_PKT_TYPE_COMMAND;

	ackX_packet.command_prologue.common_prologue.class_id.lower_word.oui = OUI;
	ackX_packet.command_prologue.common_prologue.class_id.upper_word.packet_class_code = VITA49_2_PKT_CLASS_ACKV_ACKX;
	ackX_packet.command_prologue.common_prologue.class_id.upper_word.information_class_code = VITA49_2_INFO_CLASS_MODULE_TIME_DATA;
	
	ackX_packet.command_prologue.common_prologue.has_stream_id = 1;
	ackX_packet.command_prologue.common_prologue.has_class_id = 1;
	ackX_packet.command_prologue.common_prologue.has_timestamp_int = 1;
	// time_data_packet.prologue.has_timestamp_frac = 1; // No way of getting picosecond time precision in Linux

	ackX_packet.command_prologue.ack_cam = calloc(1, sizeof(*ackX_packet.command_prologue.ack_cam));
	if (ackV_packet.command_prologue.ack_cam == NULL)
	{
		fprintf(stderr, "vita49_2_client: Failed to allocate memory for AckV CAM field.\n");
		return;
	}

	ackX_packet.command_prologue.ack_cam->ackX_request = 1;

	// Remaining Fields that need to be set:
	// AckV Packet:
		// Command Prologue:
			// Common Prologue
				// Header:
					// packet_size_words (this gets handled automatically when the vita49_2_generate_<packet_name>() function gets called) *
					// packet_count *
				// stream_id *
				// timestamp_int *
				// TODO: timestamp_frac, Linux doesn't provide a way to get picosecond accuracy natively
			// CAM
				// Warnings Present *
				// Errors Present *
				// Control Fields *
			// Message ID *
			// Controllee ID/UUID *
			// Controller ID/UUID *
		// CIF0 Warnings *
		// CIF1 Warnings (if applicable) *
		// CIF2 Warnings (if applicable) *
		// CIF3 Warnings (if applicable) *
		// CIF7 Warnings (if applicable) *
		// Warnings Payload *
		// Warnings Payload Size *
		// Errors Payload *
		// Errors Payload Size *

	ssize_t ackX_packet_size = 0;

	// ==============================================================
	// ACKS PACKET SETUP
	// ==============================================================

	struct vita49_2_ackS_packet ackS_packet = {0};

	ackS_packet.command_prologue.common_prologue.header.indicators = (1 << 2);		// Indicate that this specific Command Packet is an Acknowledge type
	ackS_packet.command_prologue.common_prologue.header.ts_integer_format = VITA49_2_TSI_UTC;
	ackS_packet.command_prologue.common_prologue.header.ts_fractional_format = VITA49_2_TSF_NONE;
	ackS_packet.command_prologue.common_prologue.header.has_class_id = 1;
	ackS_packet.command_prologue.common_prologue.header.packet_type = VITA49_2_PKT_TYPE_COMMAND;

	ackS_packet.command_prologue.common_prologue.class_id.lower_word.oui = OUI;
	ackS_packet.command_prologue.common_prologue.class_id.upper_word.packet_class_code = VITA49_2_PKT_CLASS_ACKS;
	ackS_packet.command_prologue.common_prologue.class_id.upper_word.information_class_code = VITA49_2_INFO_CLASS_MODULE_TIME_DATA;
	
	ackS_packet.command_prologue.common_prologue.has_stream_id = 1;
	ackS_packet.command_prologue.common_prologue.has_class_id = 1;
	ackS_packet.command_prologue.common_prologue.has_timestamp_int = 1;
	// time_data_packet.prologue.has_timestamp_frac = 1; // No way of getting picosecond time precision in Linux

	ackS_packet.command_prologue.ack_cam = calloc(1, sizeof(*ackS_packet.command_prologue.ack_cam));
	if (ackS_packet.command_prologue.ack_cam == NULL)
	{
		fprintf(stderr, "vita49_2_client: Failed to allocate memory for AckV CAM field.\n");
		return;
	}

	ackS_packet.command_prologue.ack_cam->ackS_request = 1;
	ackS_packet.command_prologue.ack_cam->action_scheduled = 1; // ADI doesn't currently support scheduling Commands, so this will be asserted to indicate that the Query was executed

	// Remaining Fields that need to be set:
	// AckV Packet:
		// Command Prologue:
			// Common Prologue
				// Header:
					// packet_size_words (this gets handled automatically when the vita49_2_generate_<packet_name>() function gets called) *
					// packet_count *
				// stream_id *
				// timestamp_int *
				// TODO: timestamp_frac, Linux doesn't provide a way to get picosecond accuracy natively
			// Message ID *
			// Controllee ID/UUID *
			// Controller ID/UUID *
		// CIF0 *
		// CIF1 (if applicable) *
		// CIF2 (if applicable) *
		// CIF3 (if applicable) *
		// CIF7 (if applicable) *
		// CIF Payload

	ssize_t ackS_packet_size = 0;

	// ==============================================================
	// CONTEXT PACKET SETUP
	// ==============================================================

	struct vita49_2_context_packet context_packet = {0}, previous_context_packet = {0};
	
	context_packet.prologue.header.indicators = (1 << 0);					// See Rule 7.1.1-4 in the VITA 49.2 2017 document. Setting bit 24 to 1 indicates Context Data is reflecting general timing of events.
	context_packet.prologue.header.ts_integer_format = VITA49_2_TSI_UTC;
	context_packet.prologue.header.ts_fractional_format = VITA49_2_TSF_NONE;
	context_packet.prologue.header.has_class_id = 1;
	context_packet.prologue.header.packet_type = VITA49_2_PKT_TYPE_IF_CONTEXT;

	context_packet.prologue.class_id.lower_word.oui = OUI;
	context_packet.prologue.class_id.upper_word.packet_class_code = VITA49_2_PKT_CLASS_GENERIC_CONTEXT;
	context_packet.prologue.class_id.upper_word.information_class_code = VITA49_2_INFO_CLASS_MODULE_TIME_DATA;
	
	context_packet.prologue.has_stream_id = 1;
	context_packet.prologue.has_class_id = 1;
	context_packet.prologue.has_timestamp_int = 1;

	// Remaining fields that need to be set:
	// Context Packet:
		// Prologue
			// Header
				// Packet Size (gets set automatically when the vita49_2_generate<packet_name>() function is called)
				// Packet Count *
			// Stream ID *
			// Timestamp Int *
		// CIF0 Fields *
		// CIF1 Fields (if applicable) *
		// CIF2 Fields (if applicable) *
		// CIF3 Fields (if applicable) *
		// CIF7 Fields (if applicable) *

	ssize_t context_packet_size;

	// ==============================================================
	// RECEIVE LOOP
	// ==============================================================

	int ret;
	struct vita49_2_header received_header;
	uint32_t word;

	uint32_t receive_buffer[2048];
	uint8_t send_buffer[65500];
	ssize_t received;

	// For extracting the sender information
	struct sockaddr_in sender_address;
	socklen_t address_length = sizeof(sender_address);
	char sender_ip[INET_ADDRSTRLEN];
	uint16_t sender_port;

	// For keeping track of the Stream IDs created by the device
	struct vita49_2_stream_entry device_stream_id_table[STREAM_ID_TABLE_SIZE] = {0};
	struct vita49_2_stream_entry stream_entry = {0};
	ssize_t last_insertion_index = -1;

	// Dumb way of keep track of what Stream ID we're currently on for the packet types
	// that the device can send.
	uint32_t next_time_data_stream_id = VITA49_2_PKT_TIME_DATA_DEVICE_START;
	uint32_t next_spectral_data_stream_id = VITA49_2_PKT_SPECTRAL_DATA_DEVICE_START;
	uint32_t next_context_stream_id = 0;
	uint32_t next_ackx_ackv_stream_id = 0;
	uint32_t next_acks_stream_id = 0;

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

		do {
			ret = poll(wake_up_events, sizeof(wake_up_events)/sizeof(wake_up_events[0]), -1);
		} while (ret == -1 && errno == EINTR);
		
		// Decode what event woke up the thread.
		// STOP takes priority.
		if (wake_up_events[0].revents & POLLIN)
			break;

		// ==============================================================
		// HANDLING RECEIVED DATA
		// ==============================================================

		// Data is available on the socket.
		else if (wake_up_events[1].revents & POLLIN)
		{
			received = recvfrom(socket_fd, receive_buffer, sizeof(receive_buffer), 0, (struct sockaddr*)&sender_address, &address_length);

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

			// Extracting the sender information (optional for debugging)
			// inet_ntop(AF_INET, &(sender_address.sin_addr), sender_ip, INET_ADDRSTRLEN);
			// sender_port = ntohs(sender_address.sin_port);

			// Need to figure out what packet was received so that we can call the correct parser function.
			// We can determine that by looking at the header.
			word = ntohl(receive_buffer[0]);
			memcpy(&received_header, &word, sizeof(word));

			switch (received_header.packet_type)
			{
				// Signal Data Packet with no Stream ID
				case VITA49_2_PKT_TYPE_IF_DATA_NO_SID:
					// TODO: ADI doesn't support this packet as of yet, though we retain the right to implement it in the future.
					// Currently we require all Data Packets to have a Stream ID, though if that changes in the future we need to update this case-block.
					break;

				// Signal Data Packet with Stream ID
				case VITA49_2_PKT_TYPE_IF_DATA_WITH_SID:
				{
					// Since the device is receiving this packet, that implies the host wants to the device to transmit this data.
					
					struct vita49_2_data_packet tx_data_packet;
					if (vita49_2_parse_data_packet(receive_buffer, received, &tx_data_packet) < 0)
					{
						fprintf(stderr, "vita49_2_client: Unable to parse Signal Data Packet with Stream ID.\n");
					}

					// Checking that this is a Signal Time Data Packet and not a Signal Spectrum Data Packet.
					if (tx_data_packet.prologue.header.indicators & 1)
					{
						fprintf(stderr, "vita49_2_client: Received a Signal Spectrum Data Packet. Skipping processing.\n");
						continue;
					}


					// TODO: Logic to parse the data in this packet and have the device transmit it. Make sure to check if a trailer
					// was included and what bits in the trailer were asserted. See Table 5.1.6-1 in the VITA 49.2 2017 document.
				}

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
					// Same issue as with the regular Context Packet.
					fprintf(stderr, "vita49_2_client: Received an Extension Context Packet. Skipping processing.\n");
					continue;

				// Command Packet
				case VITA49_2_PKT_TYPE_COMMAND:
					
					// There's multiple types of Command Packets:
						// Control
						// AckV
						// AckX
						// AckS

					// If bit 24 is asserted (implies Cancellation Packet), then we need to skip processing as we currently don't support it (yet).
					if (received_header.indicators & 1)
					{
						fprintf(stderr, "vita49_2_client: Cancellation Packets are unsupported at this time. Skipping processing.\n");
						continue;
					}

					// Indicator bit 26 can be used to determine if we have a Control Packet or an Acknowledge Packet.
					if (received_header.indicators & (1 << 2))
					{
						// The device/client shouldn't be receiving Ack Packets from host. It works the other way around,
						// as in the device/client should be generating and sending Ack Packets to the host.
						fprintf(stderr, "vita49_2_client: Received an Acknowledge Packet. Skipping processing.\n");
						continue;
					} 
					// Otherwise we have a Control Packet. 
					else
					{
						ssize_t ackV_ret_value;
						bool ackX_warning_init_error = false;		// Indicates if there was an issue while setting the warning fields in the AckX Packet

						// If no CIF mappings were loaded, then we're unable to process any of the commands in the Control Packet, nor can we generate any Ack Packets.
						if (vita49_2_cif_mappings_list == NULL)
						{
							fprintf(stderr, "vita49_2_client: No CIF mappings file has been provided. Received Command Packet is ignored.\n");
							continue;
						}

						struct vita49_2_control_packet control_packet;
						int ret_value;
						if ((ret_value = vita49_2_parse_control_packet(receive_buffer, received, &control_packet)) < 0)
						{
							fprintf(stderr, "vita49_2_client: Unable to parse Control Packet. Error: %d\n", ret_value);
							continue;
						}

						// I defined a custom Control Packet Class specifically for requesting I/Q data refills.
						// Instead of incorporating this logic into the execute_commands() function and returning some specific value
						// or passing IIO streaming/block arguments to that function, it's easier to do it here.
						if ((uint16_t)(control_packet.command_prologue.common_prologue.class_id.upper_word.packet_class_code) == VITA49_2_PKT_CLASS_REFILL_TIME_REQUEST)
						{
							// First we have to check if setup of the RX stream was successful.
							if (!rx_ready)
							{
								fprintf(stderr, "vita49_2_client: Unable to respond to Time Data Refill Request because RX configuration failed previously.\n");
								
								// TODO: If the Control Packet requested Ack messages, we need to indicate this error to the host
								continue;
							}

							// Timestamp for a packet that has multiple samples should be when the first sample was recorded.
							// I have no good way of determining latency of a call to libiio to grab data, so I'll just record
							// the timestamp now.
							time_data_packet.prologue.timestamp_int = (uint32_t)(time(NULL));

							// Fetching a block from the stream
							rx_block = iio_stream_get_next_block(rx_stream);
							if (iio_err(rx_block) != 0)
							{
								fprintf(stderr, "vita49_2_client: Encountered an error while trying to query I/Q data.\n");

								// TODO: If the Control Packet requested Ack messages, we need to indicate this error to the host
								continue;
							}

							// Now we have to write the data in this block to the Signal Data Packet's payload buffer.
							// Instead of copying memory (wasteful), I'll just map the packet's buffer pointer to the
							// block that we're reading from.

							// I don't need to check that this payload pushes over the 65535 word limit because I specified
							// that NUM_RX_SAMPLES is below that while also giving some room for the other fields in the packet.
							time_data_packet.payload = (struct vita49_2_iq_item *)(iio_block_first(rx_block, i_channel));

							ssize_t sample_size_bytes = iio_device_get_sample_size(rx_device, rx_channel_mask);

							// Means there was an error with capturing the data if the sample size is less than 0 bytes or more than 4 bytes.
							// It should either be 4 bytes (meaning the I and Q components are both 16 bits) or 2 bytes (meaning the I and Q components
							// are both 8 bits)
							if (sample_size_bytes <= 0 || sample_size_bytes > 4)
							{
								fprintf(stderr, "vita49_2_client: I/Q sample size is invalid. Sample Size (bytes) = %zd\n", sample_size_bytes);
								continue;
							}

							ssize_t payload_size_words = (((uint32_t *)(iio_block_end(rx_block) + sample_size_bytes) - (uint32_t *)(iio_block_first(rx_block, i_channel))) * sample_size_bytes)/4;

							// If this failed as well, then we shouldn't send the packet at all
							if (payload_size_words <= 0)
							{
								fprintf(stderr, "vita49_2_client: Encountered an error while reading data from the RX block. Number of words read is invalid (%zd)\n", payload_size_words);
								continue;
							}

							time_data_packet.payload_num_words = payload_size_words;

							// Next we have to set the Stream ID. We can check if it exists in the existing Stream ID table
							// and insert it if it doesn't.
							stream_entry.host_ip_addr = sender_address.sin_addr.s_addr;
							stream_entry.host_port = sender_address.sin_port;
							stream_entry.packet_class_code = VITA49_2_PKT_CLASS_TIME_DATA;
							stream_entry.stream_id = next_time_data_stream_id;

							ssize_t ret_value;

							// Error occurred
							if ((ret_value = insert_stream_id(device_stream_id_table, sizeof(device_stream_id_table)/sizeof(device_stream_id_table[0]), &stream_entry)) < 0)
							{
								fprintf(stderr, "vita49_2_client: Encountered an error while trying to retrieve Stream ID for the Signal Time Data Packet that was to be sent.\n");
								continue;
							}

							// If the return value is greater than the last_insertion_index, that means a new element was inserted.
							if (ret_value > last_insertion_index)
							{
								time_data_packet.prologue.stream_id = stream_entry.stream_id;
								time_data_packet.prologue.header.packet_count = 0;
							
								device_stream_id_table[ret_value].packet_count = 0;

								last_insertion_index++;
								next_time_data_stream_id++;
							}
							// Otherwise that means the element already existed in the array.
							else
							{
								time_data_packet.prologue.stream_id = device_stream_id_table[ret_value].stream_id;
								time_data_packet.prologue.header.packet_count = ++device_stream_id_table[ret_value].packet_count;
							}

							// Now to write that data to a buffer and send it
							if ((time_data_packet_size = vita49_2_generate_data_packet(&time_data_packet, &send_buffer, sizeof(send_buffer)/4)) <= 0)
							{
								fprintf(stderr, "vita49_2_client: Failed to serialize Signal Time Data Packet.\n");
								continue;							
							}

							if (sendto(socket_fd, send_buffer, time_data_packet_size*4, 0, (struct sockaddr*)&sender_address, sizeof(sender_address)) <= 0)
								fprintf(stderr, "vita49_2_client: Failed to send Signal Time Data Packet over UDP.\n");

							break;
						}


						// If an AckV packet was requested, we need to validate the commands in the Control Packet and send
						// back the AckV packet
						if (control_packet.command_prologue.control_cam->request_ack_v)
						{

							if ((ackV_ret_value = (ssize_t)(validate_commands(arguments->ctx, &control_packet, &ackV_packet.warnings))) < 0)
							{
								fprintf(stderr, "vita49_2_client: Failed to generate AckV Packet.\n");
								goto cleanup_ackv;
							}
							
							// Populating the rest of the AckV Packet
							ackV_packet.command_prologue.common_prologue.timestamp_int = (uint32_t)(time(NULL));

							stream_entry.host_ip_addr = sender_address.sin_addr.s_addr;
							stream_entry.host_port = sender_address.sin_port;
							stream_entry.packet_class_code = VITA49_2_PKT_CLASS_ACKV_ACKX;
							stream_entry.stream_id = next_ackx_ackv_stream_id;


							// Error occurred
							if ((ackV_ret_value = insert_stream_id(device_stream_id_table, sizeof(device_stream_id_table)/sizeof(device_stream_id_table[0]), &stream_entry)) < 0)
							{
								fprintf(stderr, "vita49_2_client: Encountered an error while trying to retrieve Stream ID for the AckV Packet that was to be sent.\n");
								goto cleanup_ackv;
							}

							// If the return value is greater than the last_insertion_index, that means a new element was inserted.
							if (ackV_ret_value > last_insertion_index)
							{
								ackV_packet.command_prologue.common_prologue.stream_id = stream_entry.stream_id;
								ackV_packet.command_prologue.common_prologue.header.packet_count = 0;
							
								device_stream_id_table[ret_value].packet_count = 0;

								last_insertion_index++;
								next_ackx_ackv_stream_id++;
							}
							// Otherwise that means the element already existed in the array.
							else
							{
								ackV_packet.command_prologue.common_prologue.stream_id = device_stream_id_table[ret_value].stream_id;
								ackV_packet.command_prologue.common_prologue.header.packet_count = ++device_stream_id_table[ret_value].packet_count;
							}

							ackV_packet.command_prologue.message_id = control_packet.command_prologue.message_id;

							// Determining if we should assert the Warnings Present bit in the CAM
							if (ackV_packet.warnings.warnings_payload_num_words > 0)
								ackV_packet.command_prologue.ack_cam->warnings_present = 1;

							// Copying the 11 bits from the Control Packet CAM. To avoid any complications with byte ordering I'll manually do the copies:
							ackV_packet.command_prologue.ack_cam->reserved_21 = 0;
							ackV_packet.command_prologue.ack_cam->nack 					= control_packet.command_prologue.control_cam->nack;
							ackV_packet.command_prologue.ack_cam->action_bits 			= control_packet.command_prologue.control_cam->action_bits;
							ackV_packet.command_prologue.ack_cam->errors				= control_packet.command_prologue.control_cam->errors;
							ackV_packet.command_prologue.ack_cam->warnings				= control_packet.command_prologue.control_cam->warnings;
							ackV_packet.command_prologue.ack_cam->partial_execution		= control_packet.command_prologue.control_cam->partial_execution;
							ackV_packet.command_prologue.ack_cam->controller_id_format	= control_packet.command_prologue.control_cam->controller_id_format;
							ackV_packet.command_prologue.ack_cam->has_controller_id		= control_packet.command_prologue.control_cam->has_controller_id;
							ackV_packet.command_prologue.ack_cam->controllee_id_format	= control_packet.command_prologue.control_cam->controllee_id_format;
							ackV_packet.command_prologue.ack_cam->has_controllee_id		= control_packet.command_prologue.control_cam->has_controllee_id;

							// Copying the Controller and Controllee ID information from the Control Packet
							memcpy(&ackV_packet.command_prologue.controller_id, &control_packet.command_prologue.controller_id, sizeof(ackV_packet.command_prologue.controller_id));
							memcpy(&ackV_packet.command_prologue.controllee_id, &control_packet.command_prologue.controllee_id, sizeof(ackV_packet.command_prologue.controllee_id));

							// Now to write that data to a buffer and send it
							if ((ackV_packet_size = vita49_2_generate_ackV_packet(&ackV_packet, &send_buffer, sizeof(send_buffer)/4)) <= 0)
								fprintf(stderr, "vita49_2_client: Failed to serialize AckV Packet.\n");
							else if (sendto(socket_fd, send_buffer, ackV_packet_size*4, 0, (struct sockaddr*)&sender_address, sizeof(sender_address)) <= 0)
								fprintf(stderr, "vita49_2_client: Failed to send AckV Packet over UDP.\n");

							cleanup_ackv:
							free(ackV_packet.warnings.cif1_warnings);
							free(ackV_packet.warnings.cif2_warnings);
							free(ackV_packet.warnings.cif3_warnings);
							free(ackV_packet.warnings.cif7_warnings);

							ackV_packet.warnings.cif1_warnings = NULL;
							ackV_packet.warnings.cif2_warnings = NULL;
							ackV_packet.warnings.cif3_warnings = NULL;
							ackV_packet.warnings.cif7_warnings = NULL;

							free(ackV_packet.warnings.warnings_payload);
							ackV_packet.warnings.warnings_payload = NULL;
							ackV_packet.warnings.warnings_payload_num_words = 0;

							memset(&ackV_packet.warnings.cif0_warnings, 0, sizeof(ackV_packet.warnings.cif0_warnings));
						}

						// If AckX was requested, I need to do some initial setup to see what fields might generate warnings.
						if (control_packet.command_prologue.control_cam->request_ack_x && control_packet.command_prologue.control_cam->action_bits == VITA49_2_CTRL_EXECUTE)
						{
							if (validate_commands(arguments->ctx, &control_packet, &ackX_packet.warnings) < 0)
							{
								fprintf(stderr, "vita49_2_client: Failed to acquire Warnings for AckX Packet.\n");
								ackX_warning_init_error = true;
							}
						}


						// TODO: Add logic that looks at the timestamp and schedules execution of the controls in the future.
						// An idea might be to spawn a detached pthread that checks a timer_fd and executes the controls once the timer
						// has been met. Additional complexity has to be considered if we support cancellation packets which should kill that pthread.

						// Now to execute the commands in the packet. Currently ADI only supports Execute Mode and No-Action Mode for Control Packets,
						// however we retain the right to implement Dry Run Mode in the future.
						if (control_packet.command_prologue.control_cam->action_bits == VITA49_2_CTRL_EXECUTE)
						{
							if (control_packet.command_prologue.control_cam->request_ack_x && !ackX_warning_init_error)
							{
								if (execute_commands(arguments->ctx, &control_packet, &ackX_packet) < 0)
								{
									fprintf(stderr, "vita49_2_client: Error while executing commands.\n");	
									goto cleanup_ackX;
								}

								if (ackX_packet.errors.errors_payload_num_words > 0)
									ackX_packet.command_prologue.ack_cam->errors_present = 1;

								// Setting the "warnings_present" field isn't as easy as errors since warnings were removed if an error was generated for the corresponding CIF field in execute_commands().
								// Warnings were "removed" by setting the corresponding struct in the payload to 0, however the payload size wasn't touched (to avoid issues with copying that payload buffer).
								// Hence we must iterate over that payload manually and scan for non-zero elements.
								uint32_t warning_word;
								ackX_packet.command_prologue.ack_cam->warnings_present = 0;
								for (uint8_t current_warning = 0; current_warning < ackX_packet.warnings.warnings_payload_num_words; current_warning++)
								{
									memcpy(&warning_word, &ackX_packet.warnings.warnings_payload[current_warning], sizeof(warning_word));
									if (warning_word != 0)
									{
										ackX_packet.command_prologue.ack_cam->warnings_present = 1;
										break;
									}
								}

								// Writing the remaining information before sending the packet
								ackX_packet.command_prologue.common_prologue.timestamp_int = (uint32_t)(time(NULL));

								stream_entry.host_ip_addr = sender_address.sin_addr.s_addr;
								stream_entry.host_port = sender_address.sin_port;
								stream_entry.packet_class_code = VITA49_2_PKT_CLASS_ACKV_ACKX;
								stream_entry.stream_id = next_ackx_ackv_stream_id;

								ssize_t ret_value;

								// Error occurred
								if ((ret_value = insert_stream_id(device_stream_id_table, sizeof(device_stream_id_table)/sizeof(device_stream_id_table[0]), &stream_entry)) < 0)
								{
									fprintf(stderr, "vita49_2_client: Encountered an error while trying to retrieve Stream ID for the AckX Packet that was to be sent.\n");
									goto cleanup_ackX;
								}

								// If the return value is greater than the last_insertion_index, that means a new element was inserted.
								if (ret_value > last_insertion_index)
								{
									ackX_packet.command_prologue.common_prologue.stream_id = stream_entry.stream_id;
									ackX_packet.command_prologue.common_prologue.header.packet_count = 0;
								
									device_stream_id_table[ret_value].packet_count = 0;

									last_insertion_index++;
									next_acks_stream_id++;
								}
								// Otherwise that means the element already existed in the array.
								else
								{
									ackX_packet.command_prologue.common_prologue.stream_id = device_stream_id_table[ret_value].stream_id;
									ackX_packet.command_prologue.common_prologue.header.packet_count = ++device_stream_id_table[ret_value].packet_count;
								}

								// Copying the 11 bits from the Control Packet CAM. To avoid any complications with byte ordering I'll manually do the copies:
								ackX_packet.command_prologue.ack_cam->reserved_21 			= 0;
								ackX_packet.command_prologue.ack_cam->nack 					= control_packet.command_prologue.control_cam->nack;
								ackX_packet.command_prologue.ack_cam->action_bits 			= control_packet.command_prologue.control_cam->action_bits;
								ackX_packet.command_prologue.ack_cam->errors				= control_packet.command_prologue.control_cam->errors;
								ackX_packet.command_prologue.ack_cam->warnings				= control_packet.command_prologue.control_cam->warnings;
								ackX_packet.command_prologue.ack_cam->partial_execution		= control_packet.command_prologue.control_cam->partial_execution;
								ackX_packet.command_prologue.ack_cam->controller_id_format	= control_packet.command_prologue.control_cam->controller_id_format;
								ackX_packet.command_prologue.ack_cam->has_controller_id		= control_packet.command_prologue.control_cam->has_controller_id;
								ackX_packet.command_prologue.ack_cam->controllee_id_format	= control_packet.command_prologue.control_cam->controllee_id_format;
								ackX_packet.command_prologue.ack_cam->has_controllee_id		= control_packet.command_prologue.control_cam->has_controllee_id;

								// Copying the Controller and Controllee ID information from the Control Packet
								memcpy(&ackX_packet.command_prologue.controller_id, &control_packet.command_prologue.controller_id, sizeof(ackX_packet.command_prologue.controller_id));
								memcpy(&ackX_packet.command_prologue.controllee_id, &control_packet.command_prologue.controllee_id, sizeof(ackX_packet.command_prologue.controllee_id));


								// Now to write that data to a buffer and send it
								if ((ackX_packet_size = vita49_2_generate_ackX_packet(&ackX_packet, &send_buffer, sizeof(send_buffer)/4)) <= 0)
									fprintf(stderr, "vita49_2_client: Failed to serialize AckX Packet.\n");
								else if (sendto(socket_fd, send_buffer, ackX_packet_size*4, 0, (struct sockaddr*)&sender_address, sizeof(sender_address)) <= 0)
									fprintf(stderr, "vita49_2_client: Failed to send AckX Packet over UDP.\n");
							}
							else
							{
								if (execute_commands(arguments->ctx, &control_packet, NULL) < 0)
								{
									fprintf(stderr, "vita49_2_client: Error while executing commands.\n");
									continue;
								}
							}
						}

						
						cleanup_ackX:
						free(ackX_packet.warnings.cif1_warnings);
						free(ackX_packet.warnings.cif2_warnings);
						free(ackX_packet.warnings.cif3_warnings);
						free(ackX_packet.warnings.cif7_warnings);

						ackX_packet.warnings.cif1_warnings = NULL;
						ackX_packet.warnings.cif2_warnings = NULL;
						ackX_packet.warnings.cif3_warnings = NULL;
						ackX_packet.warnings.cif7_warnings = NULL;

						free(ackX_packet.warnings.warnings_payload);
						ackX_packet.warnings.warnings_payload = NULL;
						ackX_packet.warnings.warnings_payload_num_words = 0;

						memset(&ackX_packet.warnings.cif0_warnings, 0, sizeof(ackX_packet.warnings.cif0_warnings));


						// Checking if AckS was requested
						if (control_packet.command_prologue.control_cam->request_ack_s)
						{
							// Acquiring the attribute data to populate the Context Packet
							if (acquire_context_data(arguments->ctx, &ackS_packet.cif0, ackS_packet.cif1, ackS_packet.cif2, ackS_packet.cif3, ackS_packet.cif7, &control_packet) < 0)
							{
								fprintf(stderr, "vita49_2_client: Error while acquiring data for AckS Packet.\n");
								goto cleanup_ackS;
							}

							// Writing the remaining information before sending the packet
							ackS_packet.command_prologue.common_prologue.timestamp_int = (uint32_t)(time(NULL));

							stream_entry.host_ip_addr = sender_address.sin_addr.s_addr;
							stream_entry.host_port = sender_address.sin_port;
							stream_entry.packet_class_code = VITA49_2_PKT_CLASS_ACKS;
							stream_entry.stream_id = next_acks_stream_id;

							ssize_t ret_value;

							// Error occurred
							if ((ret_value = insert_stream_id(device_stream_id_table, sizeof(device_stream_id_table)/sizeof(device_stream_id_table[0]), &stream_entry)) < 0)
							{
								fprintf(stderr, "vita49_2_client: Encountered an error while trying to retrieve Stream ID for the AckS Packet that was to be sent.\n");
								goto cleanup_ackS;
							}

							// If the return value is greater than the last_insertion_index, that means a new element was inserted.
							if (ret_value > last_insertion_index)
							{
								ackS_packet.command_prologue.common_prologue.stream_id = stream_entry.stream_id;
								ackS_packet.command_prologue.common_prologue.header.packet_count = 0;
							
								device_stream_id_table[ret_value].packet_count = 0;

								last_insertion_index++;
								next_acks_stream_id++;
							}
							// Otherwise that means the element already existed in the array.
							else
							{
								ackS_packet.command_prologue.common_prologue.stream_id = device_stream_id_table[ret_value].stream_id;
								ackS_packet.command_prologue.common_prologue.header.packet_count = ++device_stream_id_table[ret_value].packet_count;
							}

							// Copying the 11 bits from the Control Packet CAM. To avoid any complications with byte ordering I'll manually do the copies:
							ackS_packet.command_prologue.ack_cam->reserved_21 			= 0;
							ackS_packet.command_prologue.ack_cam->nack 					= control_packet.command_prologue.control_cam->nack;
							ackS_packet.command_prologue.ack_cam->action_bits 			= control_packet.command_prologue.control_cam->action_bits;
							ackS_packet.command_prologue.ack_cam->errors				= control_packet.command_prologue.control_cam->errors;
							ackS_packet.command_prologue.ack_cam->warnings				= control_packet.command_prologue.control_cam->warnings;
							ackS_packet.command_prologue.ack_cam->partial_execution		= control_packet.command_prologue.control_cam->partial_execution;
							ackS_packet.command_prologue.ack_cam->controller_id_format	= control_packet.command_prologue.control_cam->controller_id_format;
							ackS_packet.command_prologue.ack_cam->has_controller_id		= control_packet.command_prologue.control_cam->has_controller_id;
							ackS_packet.command_prologue.ack_cam->controllee_id_format	= control_packet.command_prologue.control_cam->controllee_id_format;
							ackS_packet.command_prologue.ack_cam->has_controllee_id		= control_packet.command_prologue.control_cam->has_controllee_id;

							// Copying the Controller and Controllee ID information from the Control Packet
							memcpy(&ackS_packet.command_prologue.controller_id, &control_packet.command_prologue.controller_id, sizeof(ackS_packet.command_prologue.controller_id));
							memcpy(&ackS_packet.command_prologue.controllee_id, &control_packet.command_prologue.controllee_id, sizeof(ackS_packet.command_prologue.controllee_id));
						
							// Now to write that data to a buffer and send it
							if ((ackS_packet_size = vita49_2_generate_ackS_packet(&ackS_packet, &send_buffer, sizeof(send_buffer)/4)) <= 0)
								fprintf(stderr, "vita49_2_client: Failed to serialize AckS Packet.\n");
							else if (sendto(socket_fd, send_buffer, ackS_packet_size*4, 0, (struct sockaddr*)&sender_address, sizeof(sender_address)) <= 0)
								fprintf(stderr, "vita49_2_client: Failed to send AckS Packet over UDP.\n");

							cleanup_ackS:
							free(ackS_packet.cif1);
							free(ackS_packet.cif2);
							free(ackS_packet.cif3);
							free(ackS_packet.cif7);
							
							ackS_packet.cif1 = NULL;
							ackS_packet.cif2 = NULL;
							ackS_packet.cif3 = NULL;
							ackS_packet.cif7 = NULL;

							memset(&ackS_packet.cif0, 0, sizeof(ackS_packet.cif0));
						}

						break;
					}

					break;

				// Extension Command Packet
				case VITA49_2_PKT_TYPE_EXT_COMMAND:
					// TODO: ADI doesn't support this packet as of yet, though we retain the right to implement it in the future.
					// It's likely that we'll use this packet for executing commands that aren't well translated to CIF fields.
					break;

				// Unknown packet
				default:
					fprintf(stderr, "vita49_2_client: Found a packet of unknown type: '%u'.\n", (uint32_t)(received_header.packet_type));
					continue;
				
			}		
		}

		// We're ready to send another Context Packet
		else
		{
			if (read(context_timer_fd, &num_timer_expirations, sizeof(num_timer_expirations)) != sizeof(num_timer_expirations))
			{
				fprintf(stderr, "vita49_2_client: Error while reading timerfd value.\n");
				continue;
			}

			// First let's check that the sender address has been populated with information. This happens after we've received at least VITA 49.2
			// packet from a host. Otherwise if we haven't received any packets, then we don't know who to send this Context Packet to.
			if (sender_address.sin_addr.s_addr == 0)
			{
				fprintf(stderr, "vita49_2_client: Cannot send Context Packet because host address hasn't been resolved.\n");
				continue;
			}

			// Acquiring the attribute data to populate the Context Packet
			if (acquire_context_data(arguments->ctx, &context_packet.cif0, context_packet.cif1, context_packet.cif2, context_packet.cif3, context_packet.cif7, NULL) < 0)
			{
				fprintf(stderr, "vita49_2_client: Error while acquiring data for Context Packet.\n");
				goto cleanup_context;
			}

			// Writing the remaining information before sending the packet
			context_packet.prologue.timestamp_int = (uint32_t)(time(NULL));

			stream_entry.host_ip_addr = sender_address.sin_addr.s_addr;
			stream_entry.host_port = sender_address.sin_port;
			stream_entry.packet_class_code = VITA49_2_PKT_CLASS_GENERIC_CONTEXT;
			stream_entry.stream_id = next_context_stream_id;

			ssize_t ret_value;

			// Error occurred
			if ((ret_value = insert_stream_id(device_stream_id_table, sizeof(device_stream_id_table)/sizeof(device_stream_id_table[0]), &stream_entry)) < 0)
			{
				fprintf(stderr, "vita49_2_client: Encountered an error while trying to retrieve Stream ID for the Context Packet that was to be sent.\n");
				goto cleanup_context;
			}

			// If the return value is greater than the last_insertion_index, that means a new element was inserted.
			if (ret_value > last_insertion_index)
			{
				context_packet.prologue.stream_id = stream_entry.stream_id;
				context_packet.prologue.header.packet_count = 0;
			
				device_stream_id_table[ret_value].packet_count = 0;

				last_insertion_index++;
				next_context_stream_id++;
			}
			// Otherwise that means the element already existed in the array.
			else
			{
				context_packet.prologue.stream_id = device_stream_id_table[ret_value].stream_id;
				context_packet.prologue.header.packet_count = ++device_stream_id_table[ret_value].packet_count;
			}

			// Checking if this Context Packet has changed. There's a CIF0 field called "Context Field Change Indicator" which when asserted
			// indicates that the attribute data in this packet has changed since the previous Context Packet.

			// Pointers to make addressing easier (less typing)
			struct vita49_2_cif0_fields *current_cif0_ptr = &context_packet.cif0;
			struct vita49_2_cif0_fields *previous_cif0_ptr = &previous_context_packet.cif0;
			struct cif0_word *current_cif0_word_ptr = &context_packet.cif0.cif0_word;

			current_cif0_word_ptr->context_field_change = 0;
			previous_cif0_ptr->cif0_word.context_field_change = 0;

			// First let's check if the previous packet was ever initialized to anything besides 0. If it was only initialized to 0,
			// then this represents the first time a Context Packet is being sent, thus "Context Field Change Indicator" should be asserted.
			if (previous_context_packet.prologue.header.packet_type != 0)
			{
				// Now to do an attribute-by-attribute comparison of the present CIF0 fields. I can't just use memcmp
				// because structs have padding bytes which can differ between 2 structs with the same attribute values.

				uint32_t current_cif0_word, previous_cif0_word;
				memcpy(&current_cif0_word, &context_packet.cif0.cif0_word, sizeof(current_cif0_word));
				memcpy(&previous_cif0_word, &previous_context_packet.cif0.cif0_word, sizeof(previous_cif0_word));
				
				// Checking for a difference in what fields are present/enabled
				if (current_cif0_word != previous_cif0_word)
					current_cif0_word_ptr->context_field_change = 1;

				// If the same fields are present, we have to compare each field individually to check for differing values
				else
				{
					if (current_cif0_ptr->reference_point_id != previous_cif0_ptr->reference_point_id)
						current_cif0_word_ptr->context_field_change = 1;

					else if (!double_compare(current_cif0_ptr->bandwidth, previous_cif0_ptr->bandwidth))
						current_cif0_word_ptr->context_field_change = 1;

					else if (!double_compare(current_cif0_ptr->if_reference_frequency, previous_cif0_ptr->if_reference_frequency))
						current_cif0_word_ptr->context_field_change = 1;

					else if (!double_compare(current_cif0_ptr->rf_reference_frequency, previous_cif0_ptr->rf_reference_frequency))
						current_cif0_word_ptr->context_field_change = 1;

					else if (!double_compare(current_cif0_ptr->rf_reference_frequency_offset, previous_cif0_ptr->rf_reference_frequency_offset))
						current_cif0_word_ptr->context_field_change = 1;

					else if (!double_compare(current_cif0_ptr->if_band_offset, previous_cif0_ptr->if_band_offset))
						current_cif0_word_ptr->context_field_change = 1;

					else if (!float_compare(current_cif0_ptr->reference_level, previous_cif0_ptr->reference_level))
						current_cif0_word_ptr->context_field_change = 1;

					else if (!float_compare(current_cif0_ptr->gain_stage_1, previous_cif0_ptr->gain_stage_1) || !float_compare(current_cif0_ptr->gain_stage_2, previous_cif0_ptr->gain_stage_2))
						current_cif0_word_ptr->context_field_change = 1;

					else if (current_cif0_ptr->over_range_count != previous_cif0_ptr->over_range_count)
						current_cif0_word_ptr->context_field_change = 1;

					else if (!double_compare(current_cif0_ptr->sample_rate, previous_cif0_ptr->sample_rate))
						current_cif0_word_ptr->context_field_change = 1;

					else if (current_cif0_ptr->timestamp_adjustment != previous_cif0_ptr->timestamp_adjustment)
						current_cif0_word_ptr->context_field_change = 1;

					else if (current_cif0_ptr->timestamp_calibration_time_int != previous_cif0_ptr->timestamp_calibration_time_int)
						current_cif0_word_ptr->context_field_change = 1;

					else if (!float_compare(current_cif0_ptr->temperature, previous_cif0_ptr->if_band_offset))
						current_cif0_word_ptr->context_field_change = 1;

					// Skipping the device identifier struct (that shouldn't change between Context Packets)

					else if (current_cif0_ptr->state_and_event_indicators != previous_cif0_ptr->state_and_event_indicators)
						current_cif0_word_ptr->context_field_change = 1;

					// Only checking the Data Packet Payload Format struct if it's enabled
					else if (current_cif0_word_ptr->has_data_packet_payload_format)
					{
						uint32_t current_packet_word, previous_packet_word;

						memcpy(&current_packet_word, &current_cif0_ptr->data_packet_payload_format.lower_word, sizeof(current_packet_word));
						memcpy(&previous_packet_word, &previous_cif0_ptr->data_packet_payload_format.lower_word, sizeof(previous_packet_word));

						if (current_packet_word != previous_packet_word)
							current_cif0_word_ptr->context_field_change = 1;
						else
						{
							memcpy(&current_packet_word, &current_cif0_ptr->data_packet_payload_format.upper_word, sizeof(current_packet_word));
							memcpy(&previous_packet_word, &previous_cif0_ptr->data_packet_payload_format.upper_word, sizeof(previous_packet_word));

							if (current_packet_word != previous_packet_word)
								current_cif0_word_ptr->context_field_change = 1;
						}
					}

					// TODO: Currently the vita49_2_formatted_gps_ins, vita49_2_gps_ascii, vita49_2_ecef_relative_ephemeris, and vita49_2_context_association_lists
					// structs haven't been fully defined, so I'm unable to implement the comparison logic for that.
					// Once they've been defined, that logic needs to be added here.
					else if (current_cif0_word_ptr->has_formatted_gps)
					{
						// TODO: See the above TODO
					}

					else if (current_cif0_word_ptr->has_formatted_ins)
					{
						// TODO: See the above TODO
					}

					else if (current_cif0_word_ptr->has_ecef_ephemeris)
					{
						// TODO: See the above TODO
					}

					else if (current_cif0_word_ptr->has_relative_ephemeris)
					{
						// TODO: See the above TODO
					}

					else if (current_cif0_ptr->ephemeris_ref_id != previous_cif0_ptr->ephemeris_ref_id)
							current_cif0_word_ptr->context_field_change = 1;

					else if (current_cif0_word_ptr->has_gps_ascii)
					{
						// TODO: See the above TODO
					}

					else if (current_cif0_word_ptr->has_context_association_lists)
					{
						// TODO: See the above TODO
					}
				}

				// I haven't fully defined the CIF1/2/3/7 structs yet, so the logic for that needs to be implemented

				// TODO: Logic for checking CIF1 fields
				
				// TODO: Logic for checking CIF2 fields
				
				// TODO: Logic for checking CIF3 fields
				
				// TODO: Logic for checking CIF7 fields

			}

			if ((context_packet_size = vita49_2_generate_context_packet(&context_packet, &send_buffer, sizeof(send_buffer)/4)) <= 0)
			{
				fprintf(stderr, "vita49_2_client: Failed to serialize Context Packet.\n");
				goto cleanup_context;
			}

			if (sendto(socket_fd, send_buffer, context_packet_size*4, 0, (const struct sockaddr*)&sender_address, address_length) <= 0)
				fprintf(stderr, "vita49_2_client: Failed to send Context Packet over UDP. Error %d: %s\n", errno, strerror(errno));
			else
				previous_context_packet = context_packet;

			cleanup_context:
			free(context_packet.cif1);
			free(context_packet.cif2);
			free(context_packet.cif3);
			free(context_packet.cif7);

			context_packet.cif1 = NULL;
			context_packet.cif2 = NULL;
			context_packet.cif3 = NULL;
			context_packet.cif7 = NULL;

			memset(&context_packet.cif0, 0, sizeof(context_packet.cif0));
		}
	}

	return;
}

ssize_t insert_stream_id(struct vita49_2_stream_entry* array_start, size_t array_size, const struct vita49_2_stream_entry* const insertion_item)
{
	if (array_start == NULL || array_size == 0 || insertion_item == NULL)
		return -EINVAL;
	
	ssize_t i;
	for (i = 0; i < array_size; i++)
	{
		// If host_ip_addr is uninitialized, that means this element and downstream elements haven't been populated,
		// so we can terminate the search.
			// NOTE: Can be unsafe if the array wasn't initialized to 0 before creation. Thankfully I've already done that.

		if (array_start[i].host_ip_addr == 0)
			break;

		// Can't do a simple memcmp between the 2 structs because of the risk of padding bytes, hence why I'm
		// doing direct attribute comparisons
		if (array_start[i].host_ip_addr 		== insertion_item->host_ip_addr && 
			array_start[i].host_port 			== insertion_item->host_port && 
			array_start[i].packet_class_code 	== insertion_item->packet_class_code)
			return i;
	}

	// If we get here, that means the element isn't in the array.
	// We need to check if we have the capacity to add it by checking if the iterator got to the last index.
	if (i == array_size)
		return -ENOMEM;

	// Space is available
	array_start[i] = *insertion_item;
	// array_start[i].host_ip_addr 		= insertion_item->host_ip_addr;
	// array_start[i].host_port 			= insertion_item->host_port;
	// array_start[i].packet_class_code 	= insertion_item->packet_class_code;
	// array_start[i].stream_id 			= insertion_item->stream_id;

	return i;
}

int vita49_2_command_init(struct iio_context *ctx)
{
	if (!ctx)
		return -1;
	
	fprintf(stderr, "vita49_2_command_init: Initialized VITA 49.2 translation layer.\n");
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

	fprintf(stderr, "vita49_2_process: Added mapping CIF%d Bit %d -> %s/[%s]%s/%s\n",
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
		fprintf(stderr, "vita49_2_client: Failed to open mapping file %s\n", file_path);
		return -1;
	}

	while (fgets(line, sizeof(line), f)) 
	{
	
		/* Ignore comments or empty lines */
		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
			continue;

		char *p = line;
		char *toks[6];
		int i;
		for (i = 0; i < 6; i++) 
		{
			toks[i] = strsep(&p, ",\r\n");
			if (!toks[i]) break;
		}
		
		// Each line should have 6 fields/attributes
		if (i == 6) 
		{
			uint32_t cif_type = strtoul(toks[0], NULL, 16);
			uint32_t cif_bit = strtoul(toks[1], NULL, 10);

			enum vita49_2_attr_type atype = VITA49_2_ATTR_TYPE_CHANNEL;
			if (strcmp(toks[3], "device") == 0) 
				atype = VITA49_2_ATTR_TYPE_DEVICE;
			else if (strcmp(toks[3], "debug") == 0) 
				atype = VITA49_2_ATTR_TYPE_DEBUG;

			bool is_out = (strcmp(toks[4], "true") == 0 || strcmp(toks[4], "1") == 0);
			command_add_mapping(cif_type, cif_bit, toks[2], atype, toks[3], is_out, toks[5]);
			count++;
		} 
		else 
		{
			fprintf(stderr, "vita49_2: Ignoring malformed line (need 6 fields): %s\n", line);
		}
	}

	fclose(f);
	fprintf(stderr, "vita49_2: Loaded %d mappings from %s\n", count, file_path);
	return count;
}

void vita49_2_command_cleanup(void)
{
	struct vita49_2_cif_mapping *m = vita49_2_cif_mappings_list;
	struct vita49_2_cif_mapping *next;
	
	// Freeing the linked list memory
	while (m) 
	{
		next = m->next;
		free(m);
		m = next;
	}
	vita49_2_cif_mappings_list = NULL;

	fprintf(stderr, "vita49_2_process: Cleaned up VITA 49.2 translation layer.\n");
}

int execute_commands(struct iio_context *ctx, const struct vita49_2_control_packet* const pkt, struct vita49_2_ackX_packet* const ackX_packet)
{
	struct iio_device *dev;
	struct iio_channel *chn;
	const struct iio_attr *attr;
	struct vita49_2_cif_mapping *m;
	int ret = 0;

	// Don't need to check the AckX Packet pointer since it's optional
	if (!ctx || !pkt)
		return -EINVAL;

	if (pkt->command_prologue.control_cam == NULL)
		return -EINVAL;

	// Check the Action Mode bits in the Packet to see if the Controls should be executed
	if (pkt->command_prologue.control_cam->action_bits != VITA49_2_CTRL_EXECUTE)
		return -EINVAL;


	// For keeping track of which CIF0 fields have generated errors
	struct vita49_2_warning_error_indicators cif0_errors_buffer[32] = {0};
	uint8_t cif0_errors_index = 0;


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
			{
				uint32_t cif0_word;
				memcpy(&cif0_word, &pkt->cif0.cif0_word, sizeof(cif0_word));
			
				// If true, this means one of the bits in the CIF0 word is the same as the CIF0 bit for this mapping, thus we have a match
				if (cif0_word & (1 << m->cif_bit))
					break;

				continue;
			}

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
				fprintf(stderr, "vita49_2_process: Channel %s not found.\n", m->channel_name);
				continue;
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
			switch (m->cif_bit)
			{
				// Context Field Change Indicator is N/A for Command Packets

				// Reference Point Identifier
				case (30):
					// TODO: Need logic to handle this. Travis and I discussed this, and conceivably we might use RPI to say that we're issuing
					// commands to a separate board/card like a daughter/personality card.
					break;

				// Bandwidth
				case (29):
					val = pkt->cif0.bandwidth;
					break;

				// IF Reference Frequency
				case (28):
					val = pkt->cif0.if_reference_frequency;
					break;

				// RF Reference Frequency
				case (27):
					val = pkt->cif0.rf_reference_frequency;
					break;

				// RF Reference Frequency Offset
				case (26):
					val = pkt->cif0.rf_reference_frequency_offset;
					break;

				// IF Band Offset
				case (25):
					val = pkt->cif0.if_band_offset;
					break;

				// Reference Level
				case (24):
					val = pkt->cif0.reference_level;
					break;

				// Gain
				case (23):
					// TODO: Since Gain consists of Stage 1 and Stage 2 gains, we need logic to figure out which one to use
					// or if both should be used
					break;

				// Over-Range Count is N/A for Command Packets

				// Sample Rate
				case (21):
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

		printf("vita49_2_process: Executing attribute update: %s %s (%s) -> %.5f\n", m->device_name, m->attr_name, m->is_output ? "out" : "in", val);
		ret = iio_attr_write_double(attr, val);
		if (ret < 0)
		{
			fprintf(stderr, "vita49_2_process: Failed to write %.5f to %s\n", val, m->attr_name);
		
			ret *= -1;

			// If an AckX Packet was passed, then we write the error value.
			// **IMPORTANT: Every error indicator should have bit 31 ("Field not EXECUTED") asserted since libiio failed to issue the command.
			if (ackX_packet != NULL)
			{
				uint32_t existing_cif_errors;

				switch (m->cif_type)
				{
					case CIF0:

						// Because the CIF words fields in the vita49_2_errors struct are defined as structs rather than uint32_ts,
						// it's slightly cumbersome to set the bit-field values. Instead of referencing attributes within the CIF structs,
						// I'll instead treat it as a uint32_t by copying it, modifying that value, then storing it again.
						memcpy(&existing_cif_errors, &ackX_packet->errors.cif0_errors, sizeof(existing_cif_errors));
						existing_cif_errors |= (1 << m->cif_bit);
						memcpy(&ackX_packet->errors.cif0_errors, &existing_cif_errors, sizeof(existing_cif_errors));

						// Now to write the actual error value
						if (ret > sizeof(VITA49_2_ERRNO_MAP)/sizeof(VITA49_2_ERRNO_MAP[0]))
						{
							cif0_errors_buffer[cif0_errors_index].field_not_executed = 1;
							cif0_errors_buffer[cif0_errors_index++].device_failure = 1;
						}
						else
						{
							existing_cif_errors = ((1 << ENOEXECUTE) | (1 << VITA49_2_ERRNO_MAP[ret]));
							memcpy(&cif0_errors_buffer[cif0_errors_index++], &existing_cif_errors, sizeof(existing_cif_errors));
						}

						// The corresponding CIF bit in the warnings field must now also be disabled since a CIF field can't simultaneously
						// have a warning and an error.
						memcpy(&existing_cif_errors, &ackX_packet->warnings.cif0_warnings, sizeof(existing_cif_errors));
						existing_cif_errors &= ~(1 << m->cif_bit);
						memcpy(&ackX_packet->warnings.cif0_warnings, &existing_cif_errors, sizeof(existing_cif_errors));

						// Setting that warning indicator struct to 0 to indicate that it shouldn't be copied during the serialization of the packet
						// To do that, I need to determine its index which can be figured out by counting how many fields to the left of this field were
						// also asserted/present in the warnings CIF0 word.
						uint8_t warning_index = 0;
						memcpy(&existing_cif_errors, &ackX_packet->warnings.cif0_warnings, sizeof(existing_cif_errors));
						for (int8_t i = 31; i > m->cif_bit; i--)
						{
							if (existing_cif_errors & (1 << i))
								warning_index++;
						}

						memset(&ackX_packet->warnings.warnings_payload[warning_index], 0, sizeof(ackX_packet->warnings.warnings_payload[0]));

						break;

					case CIF1:
						// TODO: Logic for CIF1 error fields
						break;

					case CIF2:
						// TODO: Logic for CIF2 error fields
						break;

					case CIF3:
						// TODO: Logic for CIF3 error fields
						break;

					case CIF7:
						// TODO: Logic for CIF7 error fields
						break;
				}

				// Sometimes there's consecutive mappings because an attribute applies to RX and TX devices.
				// For example sample rate is an attribute for RX and TX. VITA doesn't allow us to create separate fields
				// for each, so if I encounter consecutive sets, I'll treat them as one collective error which requires me decrementing
				// the error index as soon as the first mapping is seen.
				if (m->next != NULL)
				{
					if (m->cif_type == m->next->cif_type && m->cif_bit == m->next->cif_bit)
						cif0_errors_index--;
				}
			}
		}
	}

	// Copying the errors buffer
	if (cif0_errors_index > 0)
	{
		if (ackX_packet->errors.errors_payload == NULL) 
		{
			ackX_packet->errors.errors_payload = calloc(cif0_errors_index, sizeof(struct vita49_2_warning_error_indicators));
		
			if (ackX_packet->errors.errors_payload == NULL)
				return -ENOMEM;
		}
		// We may need to resize the buffer if we have more errors compared to the last AckX Packet that was sent
		else
		{
			void* errors_tmp = realloc(ackX_packet->errors.errors_payload, cif0_errors_index * sizeof(struct vita49_2_warning_error_indicators));
			
			if (errors_tmp == NULL)
			{
				fprintf(stderr, "vita49_2_process: Failed to allocate memory for the error indicators for an AckX Packet.\n");
				return -ENOMEM;
			}

			ackX_packet->errors.errors_payload = (struct vita49_2_warning_error_indicators*)(errors_tmp);
		}

		memcpy(ackX_packet->errors.errors_payload, &cif0_errors_buffer, cif0_errors_index * sizeof(struct vita49_2_warning_error_indicators));

		ackX_packet->errors.errors_payload_num_words = cif0_errors_index;
	}

	return 0;
}

enum vita49_2_warnings_error_codes find_available_attribute(struct iio_context *ctx, uint8_t cif_type, uint8_t cif_bit, char* available_range, size_t buffer_size)
{
	// We need to iterate through the CIF mappings linked list until we find a hardware attribute
	// associated with this CIF bit
	struct vita49_2_cif_mapping * cif_mappings;

	for (cif_mappings = vita49_2_cif_mappings_list; cif_mappings != NULL; cif_mappings = cif_mappings->next) 
	{
		// Checking that the current mapping and the attribute we're looking for belong to the same
		// CIF group (0/1/2/3/7)
		if (cif_mappings->cif_type != cif_type)
			continue;

		// Checking that the specific CIF bit we're targeting exists in this mapping
		if (cif_mappings->cif_bit != cif_bit)
			continue;

		// Since neither of the 2 conditions above were true, we have a match!
		goto match;
	}

	// If the loop exited naturally after going through all of the mappings without hitting the goto statement,
	// then there was no viable mapping
	return -ENOFIELD;

	match: ;
	// Find device and channel
	struct iio_device *device;
	struct iio_channel *channel;
	struct iio_attr *attribute;

	device = iio_context_find_device(ctx, cif_mappings->device_name);
	if (!device) 
	{
		fprintf(stderr, "vita49_2_process: Device %s not found for mapping.\n", cif_mappings->device_name);
		return -ENOFIELD;
	}

	int ret_value;

	// Appending "_available" to access the fd containing the valid range of values for this attribute
	char available_options_fd_name[74];
	snprintf(available_options_fd_name, sizeof(available_options_fd_name), "%s_available", cif_mappings->attr_name);

	// Attribute we're modifying is associated with a specific channel so we must find that channel first.
	if (cif_mappings->attr_type == VITA49_2_ATTR_TYPE_CHANNEL)
	{
		channel = iio_device_find_channel(device, cif_mappings->channel_name, cif_mappings->is_output);
		if (!channel)
		{
			fprintf(stderr, "vita49_2_process: Channel %s not found.\n", available_options_fd_name);
			return -ENOFIELD;
		}

		// Now to find the channel attribute
		attribute = iio_channel_find_attr(channel, available_options_fd_name);
		if (attribute == NULL)
		{
			fprintf(stderr, "vita49_2_process: Could not find channel attribute: %s\n", available_options_fd_name);
			return -ENOFIELD;
		}

		if (ret_value = iio_attr_read_raw(attribute, available_range, buffer_size) < 0)
		{
			fprintf(stderr, "vita49_2_process: Reading from channel attribute '%s' failed.\n", available_options_fd_name);
		}
	} 
	// Attribute we're modifying is associated with the device as a whole
	else if (cif_mappings->attr_type == VITA49_2_ATTR_TYPE_DEVICE) 
	{
		attribute = iio_device_find_attr(device, available_options_fd_name);
		if (attribute == NULL)
		{
			fprintf(stderr, "vita49_2_process: Could not find device attribute: %s\n", available_options_fd_name);
			return -ENOFIELD;
		}

		if (ret_value = iio_attr_read_raw(attribute, available_range, buffer_size) < 0)
		{
			fprintf(stderr, "vita49_2_process: Reading from device attribute '%s' failed.\n", available_options_fd_name);
		}
	} 
	// Attribute we're modifying is a debug attribute (advanced configuration)
	else if (cif_mappings->attr_type == VITA49_2_ATTR_TYPE_DEBUG) 
	{
		attribute = iio_device_find_debug_attr(device, available_options_fd_name);
		if (attribute == NULL)
		{
			fprintf(stderr, "vita49_2_process: Could not find debug attribute: %s\n", available_options_fd_name);
			return -ENOFIELD;
		}

		if (ret_value = iio_attr_read_raw(attribute, available_range, buffer_size) < 0)
		{
			fprintf(stderr, "vita49_2_process: Reading from debug attribute '%s' failed.\n", available_options_fd_name);
		}
	}
	// Otherwise the attribute doesn't have a proper mapping
	else
	{
		return -ENOFIELD;
	}

	if (ret_value == -ENOSYS || ret_value == -ENOENT)	
		return -ENOFIELD;
	else
		return ret_value;
}

enum vita49_2_warnings_error_codes validate_command_u32(struct iio_context *ctx, uint8_t cif_type, uint8_t cif_bit, uint32_t new_value)
{
	if (ctx == NULL || (cif_type < 7 && cif_type > 3) || cif_bit > 32)
		return -EBADARGS;

	// For storing the output of the attribute/fd
	char available_range[256];
	
	int ret_value = find_available_attribute(ctx, cif_type, cif_bit, available_range, sizeof(available_range));
	if (ret_value < 0)
		return -ENOFIELD;

	// Typically the contents of the "<attr>_available" attribute is a range of values formatted like this:
		// "[min step max]"
	// The alternative is a set of discrete values in which case the format usually involves more than 3 values
	// in the brackets:
		// "500 600 700 750 900"

	// Means we're dealing with the first case
	if (available_range[0] == '[')
	{
		uint32_t values[3];
		int successes = sscanf(available_range, "[%" PRIu32 "%" PRIu32 "%" PRIu32 "]", &values[0], &values[1], &values[2]);

		// Checking bounds
		if (values[0] > new_value || new_value > values[2])
			return -EOUTRANGE;

		// Checking if new value lands on a proper increment/step.
		// Simple way to check is to do (new_value - min) and see if that's divisible by the step size.
		if (((new_value - values[0]) % values[1]) != 0)
			return -EPRECISION;

		// Otherwise our new value is valid
		return 0;
	}
	// We're dealing with a fixed sequence of discrete values so we have to iterate over the values and
	// see if the provided new value is one of those values
	else
	{
		uint32_t values[10];
		int successes = sscanf(available_range, "%" PRIu32 "%" PRIu32 "%" PRIu32 "%" PRIu32 "%" PRIu32 "%" PRIu32 "%" PRIu32 "%" PRIu32 "%" PRIu32 "%" PRIu32,
		&values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6], &values[7], &values[8], &values[9]);

		for (uint8_t i = 0; i < sizeof(values)/sizeof(values[0]); i++)
		{
			if (new_value == values[i])
				return 0;
		}

		return -EOUTRANGE;
	}

	// Otherwise there was some kind of error
	return -EGENERIC;
}

enum vita49_2_warnings_error_codes validate_command_u64(struct iio_context *ctx, uint8_t cif_type, uint8_t cif_bit, uint64_t new_value)
{
	if (ctx == NULL || (cif_type < 7 && cif_type > 3) || cif_bit > 32)
		return -EBADARGS;

	// For storing the output of the attribute/fd
	char available_range[1024];
	
	int ret_value = find_available_attribute(ctx, cif_type, cif_bit, available_range, sizeof(available_range));
	if (ret_value < 0)
		return ENOFIELD;

	// Typically the contents of the "<attr>_available" attribute is a range of values formatted like this:
		// "[min step max]"
	// The alternative is a set of discrete values in which case the format usually involves more than 3 values
	// in the brackets:
		// "500 600 700 750 900"

	// Means we're dealing with the first case
	if (available_range[0] == '[')
	{
		uint64_t values[3];
		int successes = sscanf(available_range, "[%" SCNu64 " %" SCNu64 " %" SCNu64 "]", &values[0], &values[1], &values[2]);

		// Checking bounds
		if (values[0] > new_value || new_value > values[2])
			return -EOUTRANGE;

		// Checking if new value lands on a proper increment/step.
		// Simple way to check is to do (new_value - min) and see if that's divisible by the step size.
		if (((new_value - values[0]) % values[1]) != 0)
			return -EPRECISION;

		// Otherwise our new value is valid
		return 0;
	}
	// We're dealing with a fixed sequence of discrete values so we have to iterate over the values and
	// see if the provided new value is one of those values
	else
	{
		uint64_t values[10];
		int successes = sscanf(available_range, "%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
		&values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6], &values[7], &values[8], &values[9]);

		for (uint8_t i = 0; i < successes; i++)
		{
			if (new_value == values[i])
				return 0;
		}

		return -EOUTRANGE;
	}

	// Otherwise there was some error
	return -EGENERIC;
}

enum vita49_2_warnings_error_codes validate_command_i64(struct iio_context *ctx, uint8_t cif_type, uint8_t cif_bit, int64_t new_value)
{
	if (ctx == NULL || (cif_type < 7 && cif_type > 3) || cif_bit > 32)
		return -EBADARGS;

	// For storing the output of the attribute/fd
	char available_range[1024];
	
	int ret_value = find_available_attribute(ctx, cif_type, cif_bit, available_range, sizeof(available_range));
	if (ret_value < 0)
		return ENOFIELD;

	// Typically the contents of the "<attr>_available" attribute is a range of values formatted like this:
		// "[min step max]"
	// The alternative is a set of discrete values in which case the format usually involves more than 3 values
	// in the brackets:
		// "500 600 700 750 900"

	// Means we're dealing with the first case
	if (available_range[0] == '[')
	{
		int64_t values[3];
		int successes = sscanf(available_range, "[%" SCNd64 " %" SCNd64 " %" SCNd64 "]", &values[0], &values[1], &values[2]);

		// Checking bounds
		if (values[0] > new_value || new_value > values[2])
			return -EOUTRANGE;

		// Checking if new value lands on a proper increment/step.
		// Simple way to check is to do (new_value - min) and see if that's divisible by the step size.
		if (((new_value - values[0]) % values[1]) != 0)
			return -EPRECISION;

		// Otherwise our new value is valid
		return 0;
	}
	// We're dealing with a fixed sequence of discrete values so we have to iterate over the values and
	// see if the provided new value is one of those values
	else
	{
		int64_t values[10];
		int successes = sscanf(available_range, "%" SCNd64 " %" SCNd64 " %" SCNd64 " %" SCNd64 " %" SCNd64 " %" SCNd64 " %" SCNd64 " %" SCNd64 " %" SCNd64 " %" SCNd64,
		&values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6], &values[7], &values[8], &values[9]);

		for (uint8_t i = 0; i < successes; i++)
		{
			if (new_value == values[i])
				return 0;
		}

		return -EOUTRANGE;
	}

	// Otherwise there was some error
	return -EGENERIC;
}

enum vita49_2_warnings_error_codes validate_command_double(struct iio_context *ctx, uint8_t cif_type, uint8_t cif_bit, double new_value)
{
	if (ctx == NULL || (cif_type < 7 && cif_type > 3) || cif_bit > 32)
		return -EBADARGS;

	// For storing the output of the attribute/fd
	char available_range[1024];
	
	int ret_value = find_available_attribute(ctx, cif_type, cif_bit, available_range, sizeof(available_range));
	if (ret_value < 0)
		return -ENOFIELD;

	// Typically the contents of the "<attr>_available" attribute is a range of values formatted like this:
		// "[min step max]"
	// The alternative is a set of discrete values in which case the format usually involves more than 3 values
	// in the brackets:
		// "500 600 700 750 900"

	// Means we're dealing with the first case
	if (available_range[0] == '[')
	{
		double values[3];
		int successes = sscanf(available_range, "[%lf %lf %lf]", &values[0], &values[1], &values[2]);
		
		// Checking bounds
		if (values[0] > new_value || new_value > values[2])
			return -EOUTRANGE;

		// Checking if new value lands on a proper increment/step.
		// Simple way to check is to do (new_value - min) and see if that's divisible by the step size.
		// With double/fps, one extra step is to check if it's below some threshold to qualify as 0.
		double remainder = fmod((new_value - values[0]), values[1]);
		if (fabs(remainder) > 1e-9)
			return -EPRECISION;

		// Otherwise our new value is valid
		return 0;
	}
	// We're dealing with a fixed sequence of discrete values so we have to iterate over the values and
	// see if the provided new value is one of those values
	else
	{
		double values[10];
		int successes = sscanf(available_range, "%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
		&values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6], &values[7], &values[8], &values[9]);

		for (uint8_t i = 0; i < successes; i++)
		{
			if (new_value == values[i])
				return 0;
		}

		return -EOUTRANGE;
	}

	// Otherwise there was some error
	return -EGENERIC;
}

enum vita49_2_warnings_error_codes validate_command_float(struct iio_context *ctx, uint8_t cif_type, uint8_t cif_bit, float new_value)
{
	if (ctx == NULL || (cif_type < 7 && cif_type > 3) || cif_bit > 32)
		return -EBADARGS;

	// For storing the output of the attribute/fd
	char available_range[1024];
	
	int ret_value = find_available_attribute(ctx, cif_type, cif_bit, available_range, sizeof(available_range));
	if (ret_value < 0)
		return -ENOFIELD;

	// Typically the contents of the "<attr>_available" attribute is a range of values formatted like this:
		// "[min step max]"
	// The alternative is a set of discrete values in which case the format usually involves more than 3 values
	// in the brackets:
		// "[500 600 700 750 900]"

	// Means we're dealing with the first case
	if (available_range[0] == '[')
	{
		float values[3];
		int successes = sscanf(available_range, "[%f %f %f]", &values[0], &values[1], &values[2]);

		// Checking bounds
		if (values[0] > new_value || new_value > values[2])
			return -EOUTRANGE;

		// Checking if new value lands on a proper increment/step.
		// Simple way to check is to do (new_value - min) and see if that's divisible by the step size.
		// With double/fps, one extra step is to check if it's below some threshold to qualify as 0.
		double remainder = fmodf((new_value - values[0]), values[1]);
		if (fabs(remainder) > 1e-9)
			return -EPRECISION;

		// Otherwise our new value is valid
		return 0;
	}
	// We're dealing with a fixed sequence of discrete values so we have to iterate over the values and
	// see if the provided new value is one of those values
	else
	{
		float values[10];
		int successes = sscanf(available_range, "%f %f %f %f %f %f %f %f %f %f",
		&values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6], &values[7], &values[8], &values[9]);

		for (uint8_t i = 0; i < successes; i++)
		{
			if (new_value == values[i])
				return 0;
		}

		return -EOUTRANGE;
	}

	// Otherwise there was some error
	return -EGENERIC;
}

int validate_commands(struct iio_context *ctx, const struct vita49_2_control_packet* const control_packet, struct vita49_2_warnings* warnings)
{	
	if (!ctx || !control_packet || !warnings)
		return -EINVAL;

	struct vita49_2_cif_mapping *m;
	uint32_t cif0_word;

	// For keeping track of which CIF0 fields have generated warnings
	struct vita49_2_warning_error_indicators cif0_warnings_buffer[32] = {0};
	uint8_t cif0_warnings_index = 0;

	// We're assuming the Control Packet has already been parsed and that the cif0 (and cif1-7) struct has already
	// been populated with the parsed data.
	memset(cif0_warnings_buffer, 0, sizeof(cif0_warnings_buffer));

	// Iterate over each bit of CIF 0 (that corresponds to an actual command, not the reserved or CIF enable bits)
	// TODO: Logic to support CIF1/2/3/7 as well
	for (int cif_bit = 30; cif_bit >= 0; cif_bit--)
	{
		// Bits that don't correspond to an actual command:
			// 31, 22, 20-0
		if (cif_bit == 22 || cif_bit <= 20)
			continue;

		// Checking that the bit is asserted/field is enabled
		memcpy(&cif0_word, &control_packet->cif0.cif0_word, sizeof(cif0_word));
		if (((1 << cif_bit) & cif0_word) == 0)
			continue;

		// Iterating over each mapping
		for (m = vita49_2_cif_mappings_list; m != NULL; m = m->next) 
		{
			if (m->cif_bit != cif_bit)
				continue;
			
			bool warning_generated = false;
			
			// If we get here, that means we have a match. Now we have to extract the new attribute value
			// from the packet and feed it to the appropriate command validation function based on the type.
			int ret_value;

			// Checking CIF0
			if (m->cif_type == CIF0)
			{

				switch (m->cif_bit)
				{
					// Context Field Change Indicator is N/A for Command Packets

					// Reference Point Identifier
					case (30):
						// TODO: Need logic to handle this. Travis and I discussed this, and conceivably we might use RPI to say that we're issuing
						// commands to a separate board/card like a daughter/personality card.

						// For now we'll return a warning. Errors aren't allowed for AckV.
						warnings->cif0_warnings.has_reference_point_id = 1;
						cif0_warnings_buffer[cif0_warnings_index++].erroneous_field = 1;
						warning_generated = true;
						break;

					// Bandwidth (double)
					case (29):
						
						if ((ret_value = validate_command_double(ctx, 0, cif_bit, control_packet->cif0.bandwidth)) != ENONE)
						{
							warnings->cif0_warnings.has_bandwidth = 1;
							uint32_t warning_encoding = (1 << (-1 * ret_value));
							memcpy(&cif0_warnings_buffer[cif0_warnings_index++], &warning_encoding, sizeof(warning_encoding));		
							warning_generated = true;
						}

						break;

					// IF Reference Frequency (double)
					case (28):
						
						if ((ret_value = validate_command_double(ctx, 0, cif_bit, control_packet->cif0.if_reference_frequency)) != ENONE)
						{
							warnings->cif0_warnings.has_if_reference_frequency = 1;
							uint32_t warning_encoding = (1 << (-1 * ret_value));
							memcpy(&cif0_warnings_buffer[cif0_warnings_index++], &warning_encoding, sizeof(warning_encoding));
							warning_generated = true;
						}
						
						break;

					// RF Reference Frequency (double)
					case (27):

						if ((ret_value = validate_command_double(ctx, 0, cif_bit, control_packet->cif0.rf_reference_frequency)) != ENONE)
						{
							warnings->cif0_warnings.has_rf_reference_frequency = 1;
							uint32_t warning_encoding = (1 << (-1 * ret_value));
							memcpy(&cif0_warnings_buffer[cif0_warnings_index++], &warning_encoding, sizeof(warning_encoding));
							warning_generated = true;
						}						
						
						break;

					// RF Reference Frequency Offset (double)
					case (26):

						if ((ret_value = validate_command_double(ctx, 0, cif_bit, control_packet->cif0.rf_reference_frequency_offset)) != ENONE)
						{
							warnings->cif0_warnings.has_rf_reference_frequency_offset = 1;
							uint32_t warning_encoding = (1 << (-1 * ret_value));
							memcpy(&cif0_warnings_buffer[cif0_warnings_index++], &warning_encoding, sizeof(warning_encoding));
							warning_generated = true;
						}	

						break;

					// IF Band Offset (double)
					case (25):
						
						if ((ret_value = validate_command_double(ctx, 0, cif_bit, control_packet->cif0.if_band_offset)) != ENONE)
						{
							warnings->cif0_warnings.has_if_band_offset = 1;
							uint32_t warning_encoding = (1 << (-1 * ret_value));
							memcpy(&cif0_warnings_buffer[cif0_warnings_index++], &warning_encoding, sizeof(warning_encoding));
							warning_generated = true;
						}	
					
						break;

					// Reference Level (16-bit float)
					case (24):
						
						if ((ret_value = validate_command_float(ctx, 0, cif_bit, control_packet->cif0.reference_level)) != ENONE)
						{
							warnings->cif0_warnings.has_reference_level = 1;
							uint32_t warning_encoding = (1 << (-1 * ret_value));
							memcpy(&cif0_warnings_buffer[cif0_warnings_index++], &warning_encoding, sizeof(warning_encoding));
							warning_generated = true;
						}	
					
						break;

					// Gain
					case (23):
						// TODO: Since Gain consists of Stage 1 and Stage 2 gains, we need logic to figure out which one to use
						// or if both should be used

						// For now we'll return a warning.
						warnings->cif0_warnings.has_reference_point_id = 1;
						cif0_warnings_buffer[cif0_warnings_index++].erroneous_field = 1;
						warning_generated = true;

						break;

					// Sample Rate (double)
					case (21):

						if ((ret_value = validate_command_double(ctx, 0, cif_bit, control_packet->cif0.sample_rate)) != ENONE)
						{
							warnings->cif0_warnings.has_sample_rate = 1;
							uint32_t warning_encoding = (1 << (-1 * ret_value));
							memcpy(&cif0_warnings_buffer[cif0_warnings_index++], &warning_encoding, sizeof(warning_encoding));
							warning_generated = true;
						}	

						break;

					// Timestamp Adjustment, Timestamp Calibration Time, Temperature, Device ID, State/Event Indicators, Signal Data Packet Payload Format,
					// Formatted GPS, Formatted INS, ECEF Ephemeris, Relative Ephemeris, Ephemeris Reference ID, GPS ASCII, Context Association Lists
					// are N/A for Command Packets

					default:
						fprintf(stderr, "vita49_2_client: Unsupported CIF0 bit extraction %u for mapping %s.\n", m->cif_bit, m->attr_name);
						continue;
				}
			}

			// Don't increment the index if first pair got it
			// Increment 

			// Sometimes there's consecutive mappings because an attribute applies to RX and TX devices.
			// For example sample rate is an attribute for RX and TX. VITA doesn't allow us to create separate fields
			// for each, so if I encounter consecutive sets, I'll treat them as one collective warning. Since the current mapping
			// encountered an error (if warning_generated is true), then we can skip the other mappings until we reach one with a different CIF type and bit.
			if (warning_generated)
			{
				while (m->next != NULL)
				{
					if (m->cif_type == m->next->cif_type && m->cif_bit == m->next->cif_bit)
						m = m->next;
					else
						break;
				}
			}
		}
	}

	// TODO: Logic to support CIF1/2/3/7 as well

	// Now to write the warnings (if any) to the AckV Packet's payload
	if (cif0_warnings_index > 0)
	{
		warnings->warnings_payload = calloc(cif0_warnings_index, sizeof(struct vita49_2_warning_error_indicators));
		if (warnings->warnings_payload == NULL)
			return -ENOMEM;

		memcpy(warnings->warnings_payload, &cif0_warnings_buffer, cif0_warnings_index * sizeof(struct vita49_2_warning_error_indicators));

		warnings->warnings_payload_num_words = cif0_warnings_index;
	}

	return 0;
}

int acquire_context_data(struct iio_context *ctx, struct vita49_2_cif0_fields* cif0, struct vita49_2_cif1_fields* cif1, struct vita49_2_cif2_fields* cif2,
						struct vita49_2_cif3_fields* cif3, struct vita49_2_cif7_fields* cif7, struct vita49_2_control_packet* associated_control_packet)
{
	// The CIF1-7 pointers don't need to be checked since those fields are optional.
	// Later in this function there's some checks to see if memory needs to be allocated.
	// The same is true for the Control Packet pointer.
	if (ctx == NULL || cif0 == NULL)
		return -EINVAL;

	memset(cif0, 0, sizeof(*cif0));

	// For each attribute in the CIF mappings linked list, we'll acquire the current value of that
	// attribute and write it into the Context Packet

	struct iio_device *device;
	struct iio_channel *channel;
	struct iio_attr *attribute;
	struct vita49_2_cif_mapping *cif_mappings;

	int ret_value;

	char attr_value_s[256];
	double attr_value_d;
	long long attr_value_ll;

	// Denotes which CIF0 bits correspond to what data types. This table is necessary because the correct attribute function call needs
	// to be made based on the data type:
		// iio_attr_read_double()
		// iio_attr_read_longlong()
		// iio_attr_read_bool()
		// iio_attr_read_raw()

	// 0 = Unsupported (fields with this value may change in the future as ADI's implementation of the VITA 49.2 protocol expands)
	// 1 = Unnecessary, means that these fields aren't associated with any libiio attributes and thus we don't need to know their data type
	// 2 = Double/Float
	// 3 = uint64
	// 4 = int64
	// 5 = uint32

	const uint32_t cif0_type_table[] = {
		1,		// CIF0 0 - Reserved
		1,		// CIF0 1 - CIF 1 Enable
		1,		// CIF0 2 - CIF 2 Enable
		1,		// 3 - CIF 3 Enable
		1,		// 4 - Reserved
		1,		// 5 - Reserved
		1,		// 6 - Reserved
		1,		// 7 - CIF 7 Enable
		1,		// 8 - Context Association Lists
		1,		// 9 - GPS ASCII
		5,		// 10 - Ephemeris Ref ID
		1,		// 11 - Relative Ephemeris
		1,		// 12 - ECEF Ephemeris
		1,		// 13 - Formatted INS
		1,		// 14 - Formatted GPS
		1,		// 15 - Signal Data Packet Payload Format (see the )
		0,		// 16 - State/Event Indicators
		1,		// 17 - Device Identifier (4 uint32_t, see the vita49_2_device_identifier struct)
		0,		// 18 - Temperature: TODO: Would require reading the raw, scale, and offset file descriptors
		5,		// 19 - Timestamp Calibration Time
		4,		// 20 - Timestamp Adjustment
		2,		// 21 - Sample Rate
		5,		// 22 - Over-Range Count
		0,		// 23 - Gain TODO: Needs to be looked at more since Stage 1 and Stage 2 gain are encoded into the same 32-bit word
		2,		// 24 - Reference Level
		2,		// 25 - IF Band Offset
		2,		// 26 - RF Reference Frequency Offset
		2, 		// 27 - RF Reference Frequency
		2,	 	// 28 - IF Reference Frequency
		2,	 	// 29 - Bandwidth
		0,	 	// 30 - Reference Point Identifier
		1,		// 31 - Context Field Change Indicator. This doesn't correspond to an attribute, it just indicates whether the Context Packet has changed since the last one.
	};
	
	for (cif_mappings = vita49_2_cif_mappings_list; cif_mappings != NULL; cif_mappings = cif_mappings->next)
	{
		// Checking that the mapping has a proper CIF type and bit value
		if (cif_mappings->cif_bit > 32)
		{
			fprintf(stderr, "vita49_2_proces: Encountered an invalid CIF bit (%d)\n", cif_mappings->cif_bit);
			continue;
		}

		switch (cif_mappings->cif_type)
		{
			case CIF0:
			case CIF1:
			case CIF2:
			case CIF3:
			case CIF7:
				break;
			default:
				fprintf(stderr, "vita49_2_process: Encountered an unknown CIF type (%d)\n", cif_mappings->cif_type);
				continue;
		}

		device = iio_context_find_device(ctx, cif_mappings->device_name);
		if (!device) 
		{
			fprintf(stderr, "vita49_2_process: Device %s not found for mapping.\n", cif_mappings->device_name);
			continue;
		}

		// Attribute we're modifying is associated with a specific channel so we must find that channel first.
		if (cif_mappings->attr_type == VITA49_2_ATTR_TYPE_CHANNEL)
		{
			channel = iio_device_find_channel(device, cif_mappings->channel_name, cif_mappings->is_output);
			if (!channel)
			{
				fprintf(stderr, "vita49_2_process: Channel %s not found.\n", cif_mappings->channel_name);
				continue;
			}

			// Now to find the channel attribute
			attribute = iio_channel_find_attr(channel, cif_mappings->attr_name);
			if (attribute == NULL)
			{
				fprintf(stderr, "vita49_2_process: Could not find channel attribute: %s\n", cif_mappings->attr_name);
				continue;
			}
		} 
		// Attribute we're modifying is associated with the device as a whole
		else if (cif_mappings->attr_type == VITA49_2_ATTR_TYPE_DEVICE) 
		{
			attribute = iio_device_find_attr(device, cif_mappings->attr_name);
			if (attribute == NULL)
			{
				fprintf(stderr, "vita49_2_process: Could not find device attribute: %s\n", cif_mappings->attr_name);
				continue;
			}
		} 
		// Attribute we're modifying is a debug attribute (advanced configuration)
		else if (cif_mappings->attr_type == VITA49_2_ATTR_TYPE_DEBUG) 
		{
			attribute = iio_device_find_debug_attr(device, cif_mappings->attr_name);
			if (attribute == NULL)
			{
				fprintf(stderr, "vita49_2_process: Could not find debug attribute: %s\n", cif_mappings->attr_name);
				continue;
			}
		}
		// Otherwise the attribute doesn't have a proper mapping
		else
		{
			fprintf(stderr, "vita49_2_process: CIF mapping has an unrecognized attribute type (%d)", cif_mappings->attr_type);
			continue;
		}

		switch (cif_mappings->cif_type)
		{
			case CIF0:

				// If a valid Control Packet pointer was passed as an argument, that means we're trying to populate data for a AckS Packet.
				// AckS Packets only populate CIF fields that were also present in the associated Control Packet, thus we must check the CIF0
				// word in the Control Packet.
				if (associated_control_packet != NULL)
				{
					// Checking if the current CIF mapping exists amongst the fields that were modified by the Control Packet.
					uint32_t cif0_word;
					memcpy(&cif0_word, &associated_control_packet->cif0.cif0_word, sizeof(cif0_word));

					if (!(cif0_word & (1 << cif_mappings->cif_bit)))
						continue;
				}

				// We need to parse the data based on the data type, and we can determine that by looking at the CIF bit.
				switch (cif0_type_table[cif_mappings->cif_bit])
				{
					// Unsupported
					case 0:
						break;

					// Unnecessary
					case 1:
						break;

					// Double/Float
					case 2:

						if (iio_attr_read_double(attribute, &attr_value_d) < 0)
						{
							fprintf(stderr, "vita49_2_process: ERror while reading attribute value (%s)\n", cif_mappings->attr_name);
							break;
						}

						switch (cif_mappings->cif_bit)
						{
							// Bandwidth
							case 29:
								cif0->bandwidth = attr_value_d;
								cif0->cif0_word.has_bandwidth = 1;
								break;

							// IF Reference Frequency
							case 28:
								cif0->if_reference_frequency = attr_value_d;
								cif0->cif0_word.has_if_reference_frequency = 1;
								break;

							// RF Reference Frequency
							case 27:
								cif0->rf_reference_frequency = attr_value_d;
								cif0->cif0_word.has_rf_reference_frequency = 1;
								break;

							// RF Reference Frequency Offset
							case 26:
								cif0->rf_reference_frequency_offset = attr_value_d;
								cif0->cif0_word.has_rf_reference_frequency_offset = 1;
								break;

							// IF Band Offset
							case 25:
								cif0->if_band_offset = attr_value_d;
								cif0->cif0_word.has_if_band_offset = 1;
								break;

							// Reference Level
							case 24:
								cif0->reference_level = attr_value_d;
								cif0->cif0_word.has_reference_level = 1;
								break;

							// Sample Rate
							case 21:
								cif0->sample_rate = attr_value_d;
								cif0->cif0_word.has_sample_rate = 1;
								break;

							// Temperature
							case 20:
								// TODO: Requires additional logic since you'd need to read raw, scale, and offset attributes to compute the actual temperature from the XADC
								break;
						}

						break;


					// uint64_t
					case 3:
					// int64_t
					case 4:
					// uint32_t
					case 5:

						if (iio_attr_read_longlong(attribute, &attr_value_ll) < 0)
						{
							fprintf(stderr, "vita49_2_process: Error while reading attribute value (%s)\n", cif_mappings->attr_name);
							break;
						}

						switch (cif_mappings->cif_bit)
						{
							// Over-Range Count
							case 22:
								cif0->over_range_count = (uint32_t)(attr_value_ll);
								cif0->cif0_word.has_over_range_count = 1;
								break;

							// Timestamp Adjustment
							case 20:
								cif0->timestamp_adjustment = (int64_t)(attr_value_ll);
								cif0->cif0_word.has_timestamp_adjustment = 1;
								break;

							// Timestamp Calibration Time
							case 19:
								cif0->timestamp_calibration_time_int = (uint32_t)(attr_value_ll);
								cif0->cif0_word.has_timestamp_calibration_time = 1;
								break;

							// Ephemeris Ref ID
							case 10:
								cif0->ephemeris_ref_id = (uint32_t)(attr_value_ll);
								cif0->cif0_word.has_ephemeris_ref_id = 1;
								break;
						}

						break;

					// Shouldn't be anything besides the options above. If it is, that indicates an issue
					// with the table definition.
					default:
						fprintf(stderr, "vita49_2_process: Undefined attribute data type (%d)\n", cif0_type_table[cif_mappings->cif_bit]);
						break;
				}

				break;

			// IMPORTANT: When handling CIF1/2/3/7, make sure to check if memory has been allocated for them!

			// TODO: Logic for CIF1 field encoding
			case CIF1:

				if (associated_control_packet != NULL)
				{
					if (associated_control_packet->cif1 == NULL)
						continue;

					// Checking if the current CIF mapping exists amongst the fields that were modified by the Control Packet.
					uint32_t cif1_word;
					memcpy(&cif1_word, &associated_control_packet->cif1->cif1_word, sizeof(cif1_word));

					if (!(cif1_word & (1 << cif_mappings->cif_bit)))
						continue;
				}

				if (cif1 == NULL)
				{
					cif1 = calloc(1, sizeof(*cif1));

					if (cif1 == NULL)
					{
						fprintf(stderr, "vita49_2_process: Failed to allocate memory for CIF1.\n");
						continue;
					}
				}

				break;

			// TODO: Logic for CIF2 field encoding
			case CIF2:

				if (associated_control_packet != NULL)
				{
					if (associated_control_packet->cif2 == NULL)
						continue;

					// Checking if the current CIF mapping exists amongst the fields that were modified by the Control Packet.
					uint32_t cif2_word;
					memcpy(&cif2_word, &associated_control_packet->cif2->cif2_word, sizeof(cif2_word));

					if (!(cif2_word & (1 << cif_mappings->cif_bit)))
						continue;
				}

				if (cif2 == NULL)
				{
					cif2 = calloc(1, sizeof(*cif2));

					if (cif2 == NULL)
					{
						fprintf(stderr, "vita49_2_process: Failed to allocate memory for CIF2.\n");
						continue;
					}
				}
				
				break;

			// TODO: Logic for CIF3 field encoding
			case CIF3:

				if (associated_control_packet != NULL)
				{
					if (associated_control_packet->cif3 == NULL)
						continue;

					// Checking if the current CIF mapping exists amongst the fields that were modified by the Control Packet.
					uint32_t cif3_word;
					memcpy(&cif3_word, &associated_control_packet->cif3->cif3_word, sizeof(cif3_word));

					if (!(cif3_word & (1 << cif_mappings->cif_bit)))
						continue;
				}

				if (cif3 == NULL)
				{
					cif3 = calloc(1, sizeof(*cif3));

					if (cif3 == NULL)
					{
						fprintf(stderr, "vita49_2_process: Failed to allocate memory for CIF3.\n");
						continue;
					}
				}

				break;

			// TODO: Logic for CIF7 field encoding
			case CIF7:

				if (associated_control_packet != NULL)
				{
					if (associated_control_packet->cif7 == NULL)
						continue;

					// Checking if the current CIF mapping exists amongst the fields that were modified by the Control Packet.
					uint32_t cif7_word;
					memcpy(&cif7_word, &associated_control_packet->cif7->cif7_word, sizeof(cif7_word));

					if (!(cif7_word & (1 << cif_mappings->cif_bit)))
						continue;
				}

				if (cif7 == NULL)
				{
					cif7 = calloc(1, sizeof(*cif7));

					if (cif7 == NULL)
					{
						fprintf(stderr, "vita49_2_process: Failed to allocate memory for CIF7.\n");
						continue;
					}
				}

				break;

			default:
				fprintf(stderr, "vita49_2_process: Encountered a CIF mapping with an unknown CIF type (%d)", cif_mappings->cif_type);
				break;
		}
	}

	return 0;
}