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
    dest->sin_family = AF_INET;

    if (inet_pton(AF_INET, host, &dest->sin_addr) == 1)
        return 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;

    if (getaddrinfo(host, NULL, &hints, &result) != 0)
        return -1;

    dest->sin_addr =
        ((struct sockaddr_in *)result->ai_addr)->sin_addr;

    freeaddrinfo(result);

    return 0;
}

void construct_icmp_packet(unsigned char *packet,
                           unsigned short identifier,
                           unsigned short sequence,
                           const char *first_four_bytes)
{
    struct icmphdr *icmp;
    unsigned char *data;
    int icmp_size;

    icmp_size = sizeof(struct icmphdr) + DATA_SIZE;

    memset(packet, 0, icmp_size);

    icmp = (struct icmphdr *)packet;

    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->un.echo.id = htons(identifier);
    icmp->un.echo.sequence = htons(sequence);

    data = packet + sizeof(struct icmphdr);

    memcpy(data, first_four_bytes, 4);

    icmp->checksum =
        calculate_internet_checksum(packet, icmp_size);
}

void construct_ip_packet(unsigned char *packet,
                         size_t packet_size,
                         struct in_addr source,
                         struct in_addr destination,
                         unsigned short identifier,
                         unsigned short sequence,
                         const char *first_four_bytes)
{
    struct iphdr *ip_header;
    unsigned char *icmp_packet;
    int icmp_size;

    memset(packet, 0, packet_size);

    ip_header = (struct iphdr *)packet;

    ip_header->version = 4;
    ip_header->ihl = 5;
    ip_header->tos = 0;
    ip_header->tot_len = htons((unsigned short)packet_size);
    ip_header->id = htons(identifier);
    ip_header->frag_off = htons(0);
    ip_header->ttl = 64;
    ip_header->protocol = IPPROTO_ICMP;
    ip_header->check = 0;
    ip_header->saddr = source.s_addr;
    ip_header->daddr = destination.s_addr;

    ip_header->check =
        calculate_internet_checksum(ip_header,
                                    sizeof(struct iphdr));

    icmp_size = sizeof(struct icmphdr) + DATA_SIZE;

    icmp_packet = packet + sizeof(struct iphdr);

    construct_icmp_packet(icmp_packet,
                          identifier,
                          sequence,
                          first_four_bytes);

    (void)icmp_size;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    int sd;
    int interval = DEFAULT_INTERVAL;
    int responses = 0;
    int one = 1;

    unsigned short identifier;
    unsigned short sequence = 0;

    struct sockaddr_in dest;
    struct in_addr source;

    size_t packet_size;

    unsigned char *packet;
    unsigned char *recv_buffer;

    FILE *rcvd_file;

    if (argc != 3 && argc != 4)
    {
        fprintf(stderr, "Usage: %s host data [interval]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (strlen(argv[2]) != 4)
    {
        fprintf(stderr, "Data must be exactly four bytes\n");
        return EXIT_FAILURE;
    }

    if (argc == 4)
    {
        char *end;
        long value;

        value = strtol(argv[3], &end, 10);

        if (*end != '\0' || value <= 0)
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

    if (inet_pton(AF_INET, "128.113.28.122", &source) != 1)
        return EXIT_FAILURE;

    printf("ICMPv4 ping (interval %d second%s); host %s",
           interval,
           interval == 1 ? "" : "s",
           argv[1]);

    {
        struct in_addr numeric;

        if (inet_pton(AF_INET, argv[1], &numeric) != 1)
            printf(" (%s)", inet_ntoa(dest.sin_addr));
    }

    printf("\n");

    sd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);

    if (sd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    if (setsockopt(sd,
                   IPPROTO_IP,
                   IP_HDRINCL,
                   &one,
                   sizeof(one)) < 0)
    {
        perror("setsockopt");
        close(sd);
        return EXIT_FAILURE;
    }

    packet_size =
        sizeof(struct iphdr) +
        sizeof(struct icmphdr) +
        DATA_SIZE;

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
        perror("fopen");
        free(recv_buffer);
        free(packet);
        close(sd);
        return EXIT_FAILURE;
    }

    identifier = (unsigned short)getpid();

    while (responses < NUM_RESPONSES)
    {
        int received_reply = 0;

        construct_ip_packet(packet,
                            packet_size,
                            source,
                            dest.sin_addr,
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
            goto failure;
        }

        while (!received_reply)
        {
            struct sockaddr_in sender;
            socklen_t sender_len;
            ssize_t bytes_received;

            struct iphdr *ip_header;
            struct icmphdr *icmp_header;

            int ip_header_length;
            int icmp_length;

            unsigned char *data;

            memset(&sender, 0, sizeof(sender));
            sender_len = sizeof(sender);

            bytes_received =
                recvfrom(sd,
                         recv_buffer,
                         RECV_BUFFER_SIZE,
                         0,
                         (struct sockaddr *)&sender,
                         &sender_len);

            if (bytes_received < 0)
            {
                perror("recvfrom");
                goto failure;
            }

            if (bytes_received < (ssize_t)sizeof(struct iphdr))
                continue;

            ip_header = (struct iphdr *)recv_buffer;

            if (ip_header->version != 4)
                continue;

            ip_header_length = ip_header->ihl * 4;

            if (ip_header_length < (int)sizeof(struct iphdr))
                continue;

            if (bytes_received <
                ip_header_length + (ssize_t)sizeof(struct icmphdr))
                continue;

            if (ip_header->protocol != IPPROTO_ICMP)
                continue;

            icmp_header =
                (struct icmphdr *)(recv_buffer + ip_header_length);

            icmp_length =
                (int)bytes_received - ip_header_length;

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

            data =
                recv_buffer +
                ip_header_length +
                sizeof(struct icmphdr);

            if (memcmp(data, argv[2], 4) != 0)
                continue;

            if (fwrite(recv_buffer,
                       1,
                       (size_t)bytes_received,
                       rcvd_file) != (size_t)bytes_received)
            {
                perror("fwrite");
                goto failure;
            }

            fflush(rcvd_file);

            printf("Rcvd ICMP response (%d bytes) from %s; "
                   "seq no %u; data \"%.4s\"\n",
                   icmp_length,
                   inet_ntoa(sender.sin_addr),
                   ntohs(icmp_header->un.echo.sequence),
                   (char *)data);

            received_reply = 1;
            responses++;
        }

        sequence++;

        if (responses < NUM_RESPONSES)
            sleep((unsigned int)interval);
    }

    fclose(rcvd_file);
    free(recv_buffer);
    free(packet);
    close(sd);

    return EXIT_SUCCESS;

failure:
    fclose(rcvd_file);
    free(recv_buffer);
    free(packet);
    close(sd);

    return EXIT_FAILURE;
}
