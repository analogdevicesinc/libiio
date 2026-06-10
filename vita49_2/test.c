#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>

#include "vita49_2_packet_types.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 4991

int main(void) 
{
    int sock_fd;
    struct sockaddr_in server_addr;
    const uint32_t message[2048];
    struct vita49_2_control_packet pkt;
    pkt.command_prologue.common_prologue.has_stream_id = 1;
    pkt.command_prologue.common_prologue.header.packet_type = VITA49_2_PKT_TYPE_COMMAND;
    int value;
    if ((value = vita49_2_generate_control_packet(&pkt, message, sizeof(message)/sizeof(message[0]))) < 0)
    {
        printf("Error: %s", strerror(value));
        return 1;
    }

    // 1. Create the UDP socket
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        perror("Socket creation failed");
        return EXIT_FAILURE;
    }

    // 2. Clear and configure the destination address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    
    // Convert IP text to binary form
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid address or address family not supported");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    // 3. Send the datagram packet
    while (1)
    {
        ssize_t bytes_sent = sendto(sock_fd, message, value*4, 0,
                                    (struct sockaddr *)&server_addr, sizeof(server_addr));
        
        if (bytes_sent < 0) {
            perror("Data transmission failed");
        } else {
            printf("Successfully sent %zd bytes to %s:%d\n", bytes_sent, SERVER_IP, SERVER_PORT);
        }

        usleep(5e6);
    }

    // 4. Free the socket file descriptor
    close(sock_fd);
    return EXIT_SUCCESS;
}