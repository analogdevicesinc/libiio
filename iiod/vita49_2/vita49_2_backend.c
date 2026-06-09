/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Travis Collins <travis.collins@analog.com>
 */

#include "iio-config.h"
#include "iio-private.h"
#include "vita49_2_packet_types.h"

#include <iio/iio.h>
#include <iio/iio-backend.h>
#include <iio/iio-debug.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define VITA49_TIMEOUT_MS 5000

struct iio_context_pdata {
	int fd;
	struct sockaddr_storage addr;
	socklen_t addrlen;
};


static struct iio_context *
vita49_2_create_context(const struct iio_context_params *params, const char *hostname)
{
	struct addrinfo hints, *res;
	struct iio_context_pdata *pdata;
	struct iio_context *ctx;
	char *host, *port_str, *tmp_host;
	int ret, fd;
	uint32_t buf[1024];
	struct timeval tv;

	fprintf(stderr, "vrt_create_context: hostname=%s\n", hostname);

	pdata = zalloc(sizeof(*pdata));
	if (!pdata)
		return iio_ptr(-ENOMEM);

	tmp_host = iio_strdup(hostname);
	if (!tmp_host) {
		free(pdata);
		return iio_ptr(-ENOMEM);
	}

	host = tmp_host;
	port_str = strchr(host, ':');
	if (port_str) {
		*port_str = '\0';
		port_str++;
	} else {
		port_str = "1234"; /* Default VRT port */
	}

	fprintf(stderr, "vrt_create_context: host=%s port=%s\n", host, port_str);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	ret = getaddrinfo(host, port_str, &hints, &res);
	free(tmp_host);
	if (ret != 0) {
		fprintf(stderr, "vrt_create_context: getaddrinfo failed: %s\n", gai_strerror(ret));
		free(pdata);
		return iio_ptr(-EINVAL);
	}

	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) {
		fprintf(stderr, "vrt_create_context: socket failed: %s\n", strerror(errno));
		freeaddrinfo(res);
		free(pdata);
		return iio_ptr(-errno);
	}

	pdata->fd = fd;
	memcpy(&pdata->addr, res->ai_addr, res->ai_addrlen);
	pdata->addrlen = res->ai_addrlen;
	freeaddrinfo(res);

	/* Bind to local port if we want to receive? Actually VRT is usually broadcast/multicast.
	 * For simplicity, let's assume we are receiving on the same port we target.
	 * Or just use the socket to receive if it was bound.
	 */
	struct sockaddr_in local_addr;
	memset(&local_addr, 0, sizeof(local_addr));
	local_addr.sin_family = AF_INET;
	local_addr.sin_port = htons(atoi(port_str));
	local_addr.sin_addr.s_addr = INADDR_ANY;
	bind(fd, (struct sockaddr *)&local_addr, sizeof(local_addr));

	/* Set timeout for discovery */
	tv.tv_sec = 2;
	tv.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	ctx = iio_context_create_from_backend(params, &iio_vrt_backend,
					      "VITA 49.2 VRT Backend", 0, 1, "");
	if (!ctx) {
		fprintf(stderr, "vrt_create_context: iio_context_create_from_backend failed\n");
#ifdef _WIN32
		closesocket(fd);
#else
		close(fd);
#endif
		free(pdata);
		return NULL;
	}

	iio_context_set_pdata(ctx, pdata);

	/* Discovery loop */
	fprintf(stderr, "vrt_create_context: starting discovery loop\n");
	struct timeval start_time, current_time;
	gettimeofday(&start_time, NULL);

	while (1) {
		ssize_t received = recv(fd, buf, sizeof(buf), 0);
		if (received < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				fprintf(stderr, "vrt_create_context: discovery timeout\n");
				break;
			}
			fprintf(stderr, "vrt_create_context: recv failed: %s\n", strerror(errno));
			break;
		}

		struct vrt_packet pkt;
		if (vrt_parse_packet(buf, received / 4, &pkt) < 0) {
			fprintf(stderr, "vrt_create_context: Failed to parse VRT packet\n");
			continue;
		}

		fprintf(stderr, "vrt_create_context: received packet type %u\n", pkt.header.packet_type);

		if (pkt.header.packet_type == VRT_PKT_TYPE_IF_CONTEXT && pkt.has_stream_id) {
			uint32_t sid = pkt.stream_id;
			char sid_str[32];
			snprintf(sid_str, sizeof(sid_str), "vrt_device_%08x", sid);
			
			if (!iio_context_find_device(ctx, sid_str)) {
				fprintf(stderr, "vrt_create_context: discovered device %s\n", sid_str);
				struct iio_device *dev = iio_context_add_device(ctx, sid_str, sid_str, NULL);
				if (dev) {
					struct iio_data_format fmt;
					memset(&fmt, 0, sizeof(fmt));
					fmt.length = 16;
					fmt.bits = 16;
					fmt.is_signed = true;
					fmt.is_fully_defined = true;
					iio_device_add_channel(dev, 0, "voltage0", "voltage0_i", NULL, false, true, &fmt);
					iio_device_add_channel(dev, 1, "voltage1", "voltage0_q", NULL, false, true, &fmt);
				}
			}
		}

		gettimeofday(&current_time, NULL);
		long elapsed_ms = (current_time.tv_sec - start_time.tv_sec) * 1000 + 
						  (current_time.tv_usec - start_time.tv_usec) / 1000;
		if (elapsed_ms >= 2000) {
			fprintf(stderr, "vrt_create_context: absolute discovery timeout\n");
			break;
		}
	}

	return ctx;
}

static void vita49_2_shutdown(struct iio_context *ctx)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	if (pdata->fd >= 0) {
#ifdef _WIN32
		closesocket(pdata->fd);
#else
		close(pdata->fd);
#endif
	}
}

static int vita49_2_get_version(const struct iio_context *ctx, unsigned int *major,
			   unsigned int *minor, char git_tag[8])
{
	*major = 0;
	*minor = 1;
	strncpy(git_tag, "v0.1", 8);
	return 0;
}

static const struct iio_backend_ops vita49_2_ops = {
	.create = vrt_create_context,
	.shutdown = vrt_shutdown,
	.get_version = vrt_get_version,
	/* TODO: Implement other ops */
};

const struct iio_backend iio_vita49_2_backend = {
	.api_version = IIO_BACKEND_API_V1,
	.name = "vrt",
	.uri_prefix = "vrt:",
	.ops = &vrt_ops,
	.default_timeout_ms = VITA49_TIMEOUT_MS,
};



#include "vrt_command.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>

/* Proof of concept mappings for ad9361-phy */
#define POC_DEVICE_NAME "ad9361-phy"
#define POC_CHANNEL_NAME "voltage0"

static pthread_t listener_thd;
static int listener_fd = -1;
static struct iio_context *listener_ctx = NULL;

static void *vrt_listener_thread(void *arg)
{
	uint32_t buf[2048];
	ssize_t received;
	struct vrt_packet pkt;

	fprintf(stderr, "vrt_listener_thread: VITA 49.2 Command Listener started.\n");

	while (listener_fd >= 0) {
		received = recv(listener_fd, buf, sizeof(buf), 0);
		if (received <= 0)
			break;

		if (vrt_parse_packet(buf, received / 4, &pkt) == 0) {
			vrt_process_command_packet(listener_ctx, &pkt);
		}
	}

	return NULL;
}

#include "vrt_command.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

// Linked list containing CIF mapping structs which describe how a field in one of the CIFs 
// translates to a specific attribute on this device. This variable will be our head node.
static struct vita49_2_cif_mapping *vita49_2_cif_mappings_list = NULL;

int vrt_command_init(struct iio_context *ctx)
{
	if (!ctx)
		return -1;
	
	fprintf(stderr, "vrt_command_init: Initialized VRT translation layer.\n");
	return 0;
}

int vrt_command_add_mapping(uint32_t stream_id, uint32_t cif0_bit, 
			    const char *device_name, enum vrt_attr_type attr_type, const char *channel_name,
			    bool is_output, const char *attr_name)
{
	struct vrt_mapping *m = calloc(1, sizeof(*m));
	if (!m) return -1;

	m->stream_id = stream_id;
	m->cif0_bit = cif0_bit;
	snprintf(m->device_name, sizeof(m->device_name), "%s", device_name);
	m->attr_type = attr_type;
	snprintf(m->channel_name, sizeof(m->channel_name), "%s", channel_name ? channel_name : "");
	m->is_output = is_output;
	snprintf(m->attr_name, sizeof(m->attr_name), "%s", attr_name);

	m->next = vrt_mappings;
	vrt_mappings = m;
	
	const char *type_str = (attr_type == VRT_ATTR_TYPE_DEVICE) ? "device" :
			       (attr_type == VRT_ATTR_TYPE_DEBUG) ? "debug" : "channel";

	fprintf(stderr, "vrt_command: Added mapping Stream 0x%X Bit %u -> %s/[%s]%s/%s\n",
		stream_id, cif0_bit, device_name, type_str, channel_name ? channel_name : "", attr_name);
	return 0;
}

int vrt_command_load_mappings(const char *file_path)
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
			enum vrt_attr_type atype = VRT_ATTR_TYPE_CHANNEL;
			if (strcmp(toks[3], "device") == 0) atype = VRT_ATTR_TYPE_DEVICE;
			else if (strcmp(toks[3], "debug") == 0) atype = VRT_ATTR_TYPE_DEBUG;

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

void vrt_command_cleanup(void)
{
	struct vrt_mapping *m = vrt_mappings;
	struct vrt_mapping *next;
	
	vrt_command_stop_listener();

	while (m) {
		next = m->next;
		free(m);
		m = next;
	}
	vrt_mappings = NULL;

	fprintf(stderr, "vrt_command_cleanup: Cleaned up VRT translation layer.\n");
}

int vrt_command_start_listener(struct iio_context *ctx, uint16_t port)
{
	struct sockaddr_in saddr;

	listener_ctx = ctx;

	listener_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (listener_fd < 0) {
		fprintf(stderr, "vrt_command_start_listener: socket creation failed\n");
		return -1;
	}

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	saddr.sin_port = htons(port);

	if (bind(listener_fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
		fprintf(stderr, "vrt_command_start_listener: bind failed\n");
		close(listener_fd);
		listener_fd = -1;
		return -1;
	}

	if (pthread_create(&listener_thd, NULL, vrt_listener_thread, NULL) != 0) {
		fprintf(stderr, "vrt_command_start_listener: thread creation failed\n");
		close(listener_fd);
		listener_fd = -1;
		return -1;
	}

	return 0;
}

void vrt_command_stop_listener(void)
{
	if (listener_fd >= 0) {
		int fd = listener_fd;
		listener_fd = -1;
		close(fd);
		pthread_join(listener_thd, NULL);
	}
}

int vita49_2_execute_commands(struct iio_context *ctx, const struct vita49_2_control_packet *pkt)
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
		switch (m->cif_marker)
		{
			case CIF0:
				// If true, this means one of the bits in the CIF0 word is the same as the CIF0 bit for this mapping, thus we have a match
				if (pkt->cif0.cif0_word & (1 << m->cif_bit))
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
		if (m->cif_marker == CIF0)
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
		else if (m->cif_marker == CIF1)
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
		else if (m->cif_marker == CIF2)
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
		else if (m->cif_marker == CIF3)
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
		else if (m->cif_marker == CIF7)
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
