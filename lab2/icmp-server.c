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
#include "lab2-em.h"

#define DEFAULT_INTERVAL 1
#define DATA_SIZE 56
#define RECV_BUFFER_SIZE 65536
#define NUM_RESPONSES 8

unsigned short calculate_internet_checksum(void *data, int len)
{
    unsigned short *buf = data;
    unsigned int sum = 0;

    while (len > 1)
    {
        sum += *buf++;
        len -= 2;
    }

    if (len == 1)
        sum += *(unsigned char *)buf;

    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);

    return (unsigned short)(~sum);
}

int resolve_host(const char *host, struct sockaddr_in *dest)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;

    memset(dest, 0, sizeof(*dest));

    if (inet_pton(AF_INET, host, &dest->sin_addr) == 1)
    {
        dest->sin_family = AF_INET;
        dest->sin_port = htons(IPPROTO_ICMP);
        return 0;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;
    hints.ai_protocol = IPPROTO_ICMP;

    if (getaddrinfo(host, NULL, &hints, &result) != 0)
        return -1;

    memcpy(&dest->sin_addr,
           &((struct sockaddr_in *)result->ai_addr)->sin_addr,
           sizeof(dest->sin_addr));

    dest->sin_family = AF_INET;
    dest->sin_port = htons(IPPROTO_ICMP);

    freeaddrinfo(result);

    return 0;
}

void construct_icmp_packet(unsigned char *packet,
                           size_t packet_size,
                           unsigned short identifier,
                           unsigned short sequence,
                           const char *first_four_bytes)
{
    struct icmphdr *icmp;
    unsigned char *data;

    memset(packet, 0, packet_size);

    icmp = (struct icmphdr *)packet;

    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->un.echo.id = htons(identifier);
    icmp->un.echo.sequence = htons(sequence);

    data = packet + sizeof(struct icmphdr);

    memset(data, 0, DATA_SIZE);
    memcpy(data, first_four_bytes, 4);

    icmp->checksum =
        calculate_internet_checksum(packet, (int)packet_size);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    int sd;
    int interval = DEFAULT_INTERVAL;
    int valid_responses = 0;
    unsigned short identifier;
    unsigned short sequence = 0;

    struct sockaddr_in dest;

    size_t packet_size;
    unsigned char *packet;
    unsigned char *recv_buffer;
    FILE *rcvd_file;

    if (argc < 3 || argc > 4)
    {
        fprintf(stderr, "Usage: %s host data [interval]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (strlen(argv[2]) != 4)
    {
        fprintf(stderr, "Data argument must contain exactly four bytes\n");
        return EXIT_FAILURE;
    }

    if (argc == 4)
    {
        char *endptr;
        long value = strtol(argv[3], &endptr, 10);

        if (*endptr != '\0' || value <= 0)
        {
            fprintf(stderr, "Invalid interval\n");
            return EXIT_FAILURE;
        }

        interval = (int)value;
    }

    if (resolve_host(argv[1], &dest) != 0)
    {
        fprintf(stderr, "Could not resolve host %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    printf("ICMPv4 ping (interval %d second%s); host %s",
           interval,
           interval == 1 ? "" : "s",
           argv[1]);

    {
        struct in_addr numeric_addr;

        if (inet_pton(AF_INET, argv[1], &numeric_addr) != 1)
            printf(" (%s)", inet_ntoa(dest.sin_addr));
    }

    printf("\n");

    sd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);

    if (sd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    packet_size = sizeof(struct icmphdr) + DATA_SIZE;

    packet = malloc(packet_size);

    if (packet == NULL)
    {
        perror("malloc");
        close(sd);
        return EXIT_FAILURE;
    }

    recv_buffer = malloc(RECV_BUFFER_SIZE);

    if (recv_buffer == NULL)
    {
        perror("malloc");
        free(packet);
        close(sd);
        return EXIT_FAILURE;
    }

    rcvd_file = fopen("rcvd.dat", "wb");

    if (rcvd_file == NULL)
    {
        perror("rcvd.dat");
        free(recv_buffer);
        free(packet);
        close(sd);
        return EXIT_FAILURE;
    }

    identifier = (unsigned short)getpid();

    while (valid_responses < NUM_RESPONSES)
    {
        int got_valid_response = 0;

        construct_icmp_packet(packet,
                              packet_size,
                              identifier,
                              sequence,
                              argv[2]);

        if (sendto(sd,
                   packet,
                   packet_size,
                   0,
                   (struct sockaddr *)&dest,
                   sizeof(dest)) < 0)
        {
            perror("sendto");
            break;
        }

        while (!got_valid_response)
        {
            struct sockaddr_in sender;
            socklen_t sender_len = sizeof(sender);
            ssize_t received;
            struct ip *ip_header;
            struct icmphdr *icmp_header;
            int ip_header_length;
            int icmp_length;
            unsigned char *data;

            memset(&sender, 0, sizeof(sender));

            received = recvfrom(sd,
                                recv_buffer,
                                RECV_BUFFER_SIZE,
                                0,
                                (struct sockaddr *)&sender,
                                &sender_len);

            if (received < 0)
            {
                perror("recvfrom");
                goto cleanup_failure;
            }

            if (received < (ssize_t)sizeof(struct ip))
                continue;

            ip_header = (struct ip *)recv_buffer;
            ip_header_length = ip_header->ip_hl * 4;

            if (ip_header_length < (int)sizeof(struct ip))
                continue;

            if (received < ip_header_length)
                continue;

            if (ip_header->ip_p != IPPROTO_ICMP)
                continue;

            if (received <
                (ssize_t)(ip_header_length + sizeof(struct icmphdr)))
                continue;

            icmp_header =
                (struct icmphdr *)(recv_buffer + ip_header_length);

            icmp_length = (int)received - ip_header_length;

            if (icmp_length <
                (int)(sizeof(struct icmphdr) + DATA_SIZE))
                continue;

            if (icmp_header->type != ICMP_ECHOREPLY)
                continue;

            if (icmp_header->code != 0)
                continue;

            if (ntohs(icmp_header->un.echo.id) != identifier)
                continue;

            if (ntohs(icmp_header->un.echo.sequence) != sequence)
                continue;

            data = recv_buffer +
                   ip_header_length +
                   sizeof(struct icmphdr);

            if (memcmp(data, argv[2], 4) != 0)
                continue;

            if (fwrite(recv_buffer,
                       1,
                       (size_t)received,
                       rcvd_file) != (size_t)received)
            {
                perror("fwrite");
                goto cleanup_failure;
            }

            fflush(rcvd_file);

            printf("Rcvd ICMP response (%d bytes) from %s; "
                   "seq no %u; data \"%.4s\"\n",
                   icmp_length,
                   inet_ntoa(sender.sin_addr),
                   ntohs(icmp_header->un.echo.sequence),
                   (char *)data);

            got_valid_response = 1;
            valid_responses++;
        }

        sequence++;

        if (valid_responses < NUM_RESPONSES)
            sleep((unsigned int)interval);
    }

    fclose(rcvd_file);
    free(recv_buffer);
    free(packet);
    close(sd);

    return EXIT_SUCCESS;

cleanup_failure:
    fclose(rcvd_file);
    free(recv_buffer);
    free(packet);
    close(sd);

    return EXIT_FAILURE;
}
