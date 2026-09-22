#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/ip_icmp.h>


unsigned short checksum(void *data, int len)
{
    unsigned short *buf = data;
    unsigned int sum = 0;

    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }

    if (len == 1) {
        sum += *(unsigned char *)buf;
    }

    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    return (unsigned short)(~sum);
}


int main(void)
{
    int sd;
    struct sockaddr_in dest;

    char msg[] = "Hello, World!";

    /*
     * Packet contains:
     *
     * [ ICMP header ][ payload ]
     */
    char packet[sizeof(struct icmphdr) + sizeof(msg)];

    memset(&dest, 0, sizeof(dest));
    memset(packet, 0, sizeof(packet));

    /*
     * Destination address
     */
    dest.sin_family = AF_INET;

    if (inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr) != 1) {
        fprintf(stderr, "Bad Address!\n");
        return EXIT_FAILURE;
    }

    /*
     * Create raw ICMP socket.
     *
     * The kernel will create the IPv4 header for us.
     */
    sd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);

    if (sd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    /*
     * ICMP header lives at the beginning of packet.
     */
    struct icmphdr *icmp = (struct icmphdr *)packet;

    icmp->type = ICMP_ECHO;
    icmp->code = 0;

    /*
     * Arbitrary identifier and sequence number.
     */
    icmp->un.echo.id = htons(1234);
    icmp->un.echo.sequence = htons(1);

    /*
     * Copy payload immediately after ICMP header.
     */
    memcpy(
        packet + sizeof(struct icmphdr),
        msg,
        sizeof(msg)
    );

    /*
     * Checksum must initially be zero.
     */
    icmp->checksum = 0;

    /*
     * Calculate checksum across:
     *
     * ICMP header + payload
     */
    icmp->checksum = checksum(packet, sizeof(packet));

    /*
     * Send ICMP packet.
     */
    if (sendto(
            sd,
            packet,
            sizeof(packet),
            0,
            (struct sockaddr *)&dest,
            sizeof(dest)
        ) < 0) {

        perror("sendto");
        close(sd);
        return EXIT_FAILURE;
    }

    printf("ICMP Echo Request sent to 127.0.0.1\n");
    printf("Payload: %s\n", msg);

    close(sd);

    return EXIT_SUCCESS;
}