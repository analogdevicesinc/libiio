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

static struct vrt_mapping *vrt_mappings = NULL;

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

int vrt_process_command_packet(struct iio_context *ctx, const struct vrt_packet *pkt)
{
	struct iio_device *dev;
	struct iio_channel *chn;
	const struct iio_attr *attr;
	struct vrt_mapping *m;
	int ret = 0;

	if (!ctx || !pkt)
		return -1;

	/* Only handle Context packets for configuration/command updates */
	if (pkt->header.packet_type != VRT_PKT_TYPE_IF_CONTEXT)
		return 0;

	if (!pkt->has_stream_id)
		return 0;

	struct vrt_cif_fields cif;
	if (vrt_parse_cif_payload(pkt, &cif) < 0)
		return 0;

	/* Iterate through all loaded mappings */
	for (m = vrt_mappings; m != NULL; m = m->next) {
		/* Match Stream ID */
		if (pkt->stream_id != m->stream_id)
			continue;

		/* Match CIF0 bit flag */
		if (!(cif.cif0 & (1 << m->cif0_bit)))
			continue;

		/* We have a match! Find device and channel */
		dev = iio_context_find_device(ctx, m->device_name);
		if (!dev) {
			fprintf(stderr, "vrt_process: Device %s not found for mapping.\n", m->device_name);
			continue;
		}

		attr = NULL;
		if (m->attr_type == VRT_ATTR_TYPE_CHANNEL) {
			chn = iio_device_find_channel(dev, m->channel_name, m->is_output);
			if (!chn) {
				/* Fallback to opposite direction just in case */
				chn = iio_device_find_channel(dev, m->channel_name, !m->is_output);
				if (!chn) {
					fprintf(stderr, "vrt_process: Channel %s not found.\n", m->channel_name);
					continue;
				}
			}
			attr = iio_channel_find_attr(chn, m->attr_name);
		} else if (m->attr_type == VRT_ATTR_TYPE_DEVICE) {
			attr = iio_device_find_attr(dev, m->attr_name);
		} else if (m->attr_type == VRT_ATTR_TYPE_DEBUG) {
			attr = iio_device_find_debug_attr(dev, m->attr_name);
		}

		if (!attr) {
			fprintf(stderr, "vrt_process: Attribute %s not found on %s.\n", m->attr_name, m->device_name);
			continue;
		}

		/* Extract value from parsed CIF structure */
		double val = 0.0;
		if (m->cif0_bit == 29 && cif.has_bandwidth) {
			val = cif.bandwidth;
		} else if (m->cif0_bit == 28 && cif.has_if_reference_frequency) {
			val = cif.if_reference_frequency;
		} else if (m->cif0_bit == 27 && cif.has_rf_reference_frequency) {
			val = cif.rf_reference_frequency;
		} else if (m->cif0_bit == 26 && cif.has_rf_reference_frequency_offset) {
			val = cif.rf_reference_frequency_offset;
		} else if (m->cif0_bit == 25 && cif.has_if_band_offset) {
			val = cif.if_band_offset;
		} else if (m->cif0_bit == 21 && cif.has_sample_rate) {
			val = cif.sample_rate;
		} else {
			fprintf(stderr, "vrt_process: Unsupported CIF0 bit extraction %u for mapping %s.\n", 
			        m->cif0_bit, m->attr_name);
			continue;
		}

		fprintf(stderr, "vrt_process: Translating mapped command %s -> %.0f\n", m->attr_name, val);
		ret = iio_attr_write_double(attr, val);
		if (ret < 0)
			fprintf(stderr, "Failed to write %s\n", m->attr_name);
	}

	return 0;
}
