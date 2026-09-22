#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <netdb.h>

#define DEFAULT_INTERVAL 1
#define DATA_SIZE 56
#define RECV_BUFFER_SIZE 65536

unsigned short calculate_internet_checksum(void *data, int len) {
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

int resolve_host(const char *host, struct sockaddr_in *dest) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;
    // first check whether the argument is already a numeric IPv4 address
    if (inet_pton(AF_INET, host, &dest->sin_addr) == 1) {
        dest->sin_family = AF_INET;
        return 0;
    }
    // otherwise, resolve the domain name
    if (getaddrinfo(host, NULL, &hints, &result) != 0) {
        return -1;
    }
    memcpy(
        &dest->sin_addr,
        &((struct sockaddr_in *)result->ai_addr)->sin_addr,
        sizeof(dest->sin_addr)
    );
    dest->sin_family = AF_INET;
    freeaddrinfo(result);
    return 0;
}

void construct_icmp_packet(unsigned char *packet, size_t packet_size, unsigned short identifier, unsigned short sequence) {
    struct icmphdr *icmp;
    unsigned char *data;
    memset(packet, 0, packet_size);
    // ICMP header is at the beginning of the packet
    icmp = (struct icmphdr *)packet;
    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->checksum = 0;
    // Identifier is the process ID
    icmp->un.echo.id = htons(identifier);
    // Sequence number increments for each request
    icmp->un.echo.sequence = htons(sequence);
    // Optional ICMP data begins immediately after the ICMP header
    data = packet + sizeof(struct icmphdr);

    for (int i = 0; i < DATA_SIZE; i++) {
        data[i] = (unsigned char)('A' + ((sequence * 4 + i) % 26));
    }

    // Calculate the Internet checksum over the complete ICMP message
    icmp->checksum = calculate_internet_checksum(
        packet,
        (int)packet_size
    );
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);

    int sd;
    int interval = DEFAULT_INTERVAL;
    unsigned short identifier;
    unsigned short sequence = 0;

    struct sockaddr_in dest;

    size_t packet_size;
    unsigned char *packet;
    unsigned char *recv_buffer;

    FILE *rcvd_file;

    packet_size = sizeof(struct icmphdr) + DATA_SIZE;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s host [interval]\n", argv[0]);
        return EXIT_FAILURE;
    }

    // optional interval argument
    if (argc == 3) {
        char *endptr;
        long value;

        value = strtol(argv[2], &endptr, 10);

        if (*endptr != '\0' || value <= 0) {
            fprintf(stderr, "Invalid interval\n");
            return EXIT_FAILURE;
        }

        interval = (int)value;
    }

    // resolve the destination host
    memset(&dest, 0, sizeof(dest));

    if (resolve_host(argv[1], &dest) != 0) {
        fprintf(stderr, "Could not resolve host %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    // display the initial ping information
    printf(
        "ICMPv4 ping (interval %d second%s); host %s (%s)\n",
        interval,
        interval == 1 ? "" : "s",
        argv[1],
        inet_ntoa(dest.sin_addr)
    );

    // create a raw ICMP socket
    sd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);

    if (sd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    // dynamically allocate the outgoing ICMP packet
    packet = malloc(packet_size);

    if (packet == NULL) {
        perror("malloc");
        close(sd);
        return EXIT_FAILURE;
    }

    recv_buffer = calloc(RECV_BUFFER_SIZE, sizeof(char));
    if (recv_buffer == NULL) {
        perror("malloc");
        free(packet);
        close(sd);
        return EXIT_FAILURE;
    }

    // open the output file for raw received data
    rcvd_file = fopen("rcvd.dat", "wb");

    if (rcvd_file == NULL) {
        perror("rcvd.dat");
        free(recv_buffer);
        free(packet);
        close(sd);
        return EXIT_FAILURE;
    }

    // use the process ID as the ICMP identifier
    identifier = (unsigned short)getpid();

    // continuously send echo requests
    while (1) {
        struct sockaddr_in sender;
        socklen_t sender_len;
        ssize_t received;

        struct ip *ip_header;
        struct icmphdr *icmp_header;

        int ip_header_length;

        unsigned char *data;

        // construct echo request 
        construct_icmp_packet(
            packet,
            packet_size,
            identifier,
            sequence
        );

        // send the request 
        if (sendto(
                sd,
                packet,
                packet_size,
                0,
                (struct sockaddr *)&dest,
                sizeof(dest)
            ) < 0) {
            perror("sendto");
            break;
        }

        // recieve packets 
        sender_len = sizeof(sender);

        received = recvfrom(
            sd,
            recv_buffer,
            RECV_BUFFER_SIZE,
            0,
            (struct sockaddr *)&sender,
            &sender_len
        );

        if (received < 0) {
            perror("recvfrom");
            break;
        }

        if (received < (ssize_t)sizeof(struct ip)) {
            continue;
        }

        ip_header = (struct ip *)recv_buffer;

        ip_header_length = ip_header->ip_hl * 4;

        if (ip_header_length < (int)sizeof(struct ip)) {
            continue;
        }

        if (received <
            ip_header_length + (ssize_t)sizeof(struct icmphdr)) {
            continue;
        }

        // locate the ICMP header after the IPv4 header
        icmp_header =
            (struct icmphdr *)(recv_buffer + ip_header_length);

        // make sure this is an echo reply
        if (icmp_header->type != ICMP_ECHOREPLY) {
            continue;
        }

        // make sure the response belongs to this ping process 
        if (ntohs(icmp_header->un.echo.id) != identifier) {
            continue;
        }

        // make sure this is the expected sequence number
        if (ntohs(icmp_header->un.echo.sequence) != sequence) {
            continue;
        }

        // save the complete raw IPv4 packet, including the IPv4 header, to rcvd.dat
        if (fwrite(
                recv_buffer,
                1,
                (size_t)received,
                rcvd_file
            ) != (size_t)received) {

            perror("fwrite");
            break;
        }

        fflush(rcvd_file);

        data =
            recv_buffer +
            ip_header_length +
            sizeof(struct icmphdr);

        // display output
        printf(
            "Rcvd ICMP response (%zd bytes) from %s; "
            "seq no %u; data \"%.4s\"\n",
            received,
            inet_ntoa(sender.sin_addr),
            ntohs(icmp_header->un.echo.sequence),
            data
        );

        // move to the next sequence number
        sequence++;

        // wait before sending the next request
        sleep((unsigned int)interval);
    }
    fclose(rcvd_file);
    free(recv_buffer);
    free(packet);
    close(sd);

    return EXIT_SUCCESS;
}