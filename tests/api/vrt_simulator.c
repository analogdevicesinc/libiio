#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

/* Minimal VRT Header */
struct vrt_header {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint32_t packet_size_words:16;
    uint32_t packet_count:4;
    uint32_t ts_fractional_format:2;
    uint32_t ts_integer_format:2;
    uint32_t reserved:2;
    uint32_t has_trailer:1;
    uint32_t has_class_id:1;
    uint32_t packet_type:4;
#else
    uint32_t packet_type:4;
    uint32_t has_class_id:1;
    uint32_t has_trailer:1;
    uint32_t reserved:2;
    uint32_t ts_integer_format:2;
    uint32_t ts_fractional_format:2;
    uint32_t packet_count:4;
    uint32_t packet_size_words:16;
#endif
};

// Commonly used convention for sending VITA 49.2 packets over UDP
#define VITA49_UDP_PORT 4991

int main() {
    int fd;
    struct sockaddr_in addr;
    uint32_t packet[1024];
    struct vrt_header hdr_s;
    struct vrt_header *hdr = &hdr_s;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(VITA49_UDP_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    /* Create a Context Packet */
    memset(packet, 0, sizeof(packet));
    memset(&hdr_s, 0, sizeof(hdr_s));
    
    /* VITA 49.2 Header: Type=4 (Context), Size=10 words */
    hdr->packet_type = 4;
    hdr->has_class_id = 1; /* Class ID present */
    hdr->packet_size_words = 9;
    packet[0] = htonl(*(uint32_t *)hdr);

    /* Stream ID */
    packet[1] = htonl(0x12345678);

    /* Class ID (2 words) */
    packet[2] = htonl(0x0012A200); /* OUI = VITA */
    packet[3] = htonl(0x00000001); /* Class Code */

    /* Context Indicator Field 0 (CIF0) */
    /* Bit 29: Bandwidth, Bit 21: Sample Rate */
    /* VITA 49 fields must be formatted sequentially from MSB (31) to LSB (0) */
    packet[4] = htonl((1 << 21) | (1 << 29));

    /* Bandwidth (2 words - float64) evaluates first */
    /* Let's say 80 MHz */
    double bandwidth = 80e6;
    int64_t bw_int;

    // VITA 49.2 specifies that the Bandwidth field be expressed in Hertz using
    // 64-bit, 2's complement format with the radix point to the right of bit 20
    // meaning we have 20 fractional bits
    bw_int = (int64_t)(bandwidth * (1 << 20));

    packet[5] = htonl(bw_int >> 32);
    packet[6] = htonl(bw_int & 0xFFFFFFFF);

    /* Sample Rate (2 words - float64) evaluates next */
    /* Let's say 100 MSPS */
    double sample_rate = 100e6;
    int64_t sr_int;

    // Same format as Bandwidth
    sr_int = (int64_t)(sample_rate * (1 << 20));

    packet[7] = htonl(sr_int >> 32);
    packet[8] = htonl(sr_int & 0xFFFFFFFF);

    printf("Sending VRT Context Packet to 127.0.0.1:%d\n", VITA49_UDP_PORT);
    while (1) {
        if (sendto(fd, packet, hdr->packet_size_words * 4, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("sendto");
        }
        usleep(100000);
    }

    close(fd);
    return 0;
}
