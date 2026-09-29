#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define USERID_BYTES 11
#define PUZZLE_BYTES 90
#define START_BYTES 17
#define START_RESPONSE_BYTES 101
#define PLACE_BYTES 19
#define PLACE_RESPONSE_BYTES 92

void die(const char *s)
{
    perror(s);
    exit(EXIT_FAILURE);
}

void make_userid(char *dest, const char *src)
{
    memset(dest, ' ', USERID_BYTES);
    memcpy(dest, src, strlen(src));
}

int send_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        sent += (size_t)n;
    }

    return 0;
}

int recv_all(int fd, void *buf, size_t len)
{
    char *p = buf;
    size_t total = 0;

    while (total < len) {
        ssize_t n = recv(fd, p + total, len - total, 0);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        total += (size_t)n;
    }

    return 0;
}

int start_error_length(const unsigned char *buf, size_t n)
{
    const char *errors[] = {
        "INVALID VERSION\n",
        "USER NOT ALLOWED\n",
        "NO SUCH PUZZLE ID\n"
    };

    int i;

    if (n <= USERID_BYTES)
        return 0;

    for (i = 0; i < 3; i++) {
        size_t len = strlen(errors[i]);
        size_t have = n - USERID_BYTES;

        if (have <= len &&
            memcmp(buf + USERID_BYTES, errors[i], have) == 0) {
            if (have == len)
                return USERID_BYTES + (int)len;
            return -1;
        }
    }

    return 0;
}

int recv_start_tcp(int fd, unsigned char *buf)
{
    size_t total = 0;

    while (total < START_RESPONSE_BYTES) {
        ssize_t n = recv(fd, buf + total,
                         START_RESPONSE_BYTES - total, 0);

        if (n <= 0)
            return -1;

        total += (size_t)n;

        if (total >= USERID_BYTES) {
            int error_len = start_error_length(buf, total);

            if (error_len > 0)
                return error_len;

            if (error_len == 0 && total == START_RESPONSE_BYTES)
                return START_RESPONSE_BYTES;
        }
    }

    return (int)total;
}

void print_puzzle(const unsigned char *puzzle)
{
    int r;

    printf("CLIENT: +---+---+---+\n");

    for (r = 0; r < 9; r++) {
        int i = r * 10;

        printf("CLIENT: |%c%c%c|%c%c%c|%c%c%c|\n",
               puzzle[i], puzzle[i + 1], puzzle[i + 2],
               puzzle[i + 3], puzzle[i + 4], puzzle[i + 5],
               puzzle[i + 6], puzzle[i + 7], puzzle[i + 8]);

        if (r == 2 || r == 5 || r == 8)
            printf("CLIENT: +---+---+---+\n");
    }
}

const char *reply_text(int reply)
{
    if (reply == 0) return "SUCCESS";
    if (reply == 1) return "INVALID PROTOCOL VERSION";
    if (reply == 2) return "INVALID REQUEST";
    if (reply == 3) return "CANNOT CHANGE ORIGINAL PUZZLE";
    if (reply == 4) return "DUPLICATE DIGIT IN ROW";
    if (reply == 5) return "DUPLICATE DIGIT IN COLUMN";
    if (reply == 6) return "DUPLICATE DIGIT IN BLOCK";
    return "UNKNOWN REPLY";
}

int connect_tcp(struct addrinfo *result)
{
    struct addrinfo *p;
    int fd;

    for (p = result; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

        if (fd == -1)
            continue;

        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0)
            return fd;

        close(fd);
    }

    return -1;
}

int create_udp(struct addrinfo *result,
               struct sockaddr_storage *server,
               socklen_t *server_len)
{
    struct addrinfo *p;
    int fd;

    for (p = result; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

        if (fd == -1)
            continue;

        memcpy(server, p->ai_addr, p->ai_addrlen);
        *server_len = p->ai_addrlen;

        return fd;
    }

    return -1;
}

int main(int argc, char **argv)
{
    struct addrinfo hints;
    struct addrinfo *result;
    struct sockaddr_storage server;
    socklen_t server_len = 0;

    unsigned char start[START_BYTES];
    unsigned char start_response[START_RESPONSE_BYTES];
    unsigned char place[PLACE_BYTES];
    unsigned char place_response[PLACE_RESPONSE_BYTES];

    char userid[USERID_BYTES];
    char digit;

    int fd;
    int tcp;
    int row, col;
    int n;
    int status;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc != 5) {
        fprintf(stderr,
                "ERROR: usage: %s <TCP|UDP> <RCS-ID> <host> <port>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "TCP") == 0)
        tcp = 1;
    else if (strcmp(argv[1], "UDP") == 0)
        tcp = 0;
    else {
        fprintf(stderr, "ERROR: transport must be TCP or UDP\n");
        return EXIT_FAILURE;
    }

    if (strlen(argv[2]) == 0 || strlen(argv[2]) > USERID_BYTES) {
        fprintf(stderr, "ERROR: invalid RCS-ID\n");
        return EXIT_FAILURE;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = tcp ? SOCK_STREAM : SOCK_DGRAM;

    status = getaddrinfo(argv[3], argv[4], &hints, &result);

    if (status != 0) {
        fprintf(stderr, "ERROR: %s\n", gai_strerror(status));
        return EXIT_FAILURE;
    }

    if (tcp) {
        printf("CLIENT: connecting to server (TCP)...\n");

        fd = connect_tcp(result);

        if (fd == -1) {
            freeaddrinfo(result);
            fprintf(stderr, "ERROR: unable to connect\n");
            return EXIT_FAILURE;
        }
    } else {
        printf("CLIENT: connecting to server (UDP)...\n");

        fd = create_udp(result, &server, &server_len);

        if (fd == -1) {
            freeaddrinfo(result);
            fprintf(stderr, "ERROR: unable to create UDP socket\n");
            return EXIT_FAILURE;
        }
    }

    freeaddrinfo(result);

    make_userid(userid, argv[2]);

    start[0] = 0x20;
    memcpy(start + 1, "START", 5);
    memcpy(start + 6, userid, USERID_BYTES);

    printf("CLIENT: sending START request to server for \"%s\"\n", argv[2]);

    if (tcp) {
        if (send_all(fd, start, sizeof(start)) == -1)
            die("ERROR: send");

        n = recv_start_tcp(fd, start_response);

        if (n == -1)
            die("ERROR: recv");
    } else {
        if (sendto(fd, start, sizeof(start), 0,
                   (struct sockaddr *)&server, server_len) == -1)
            die("ERROR: sendto");

        n = (int)recvfrom(fd, start_response,
                          sizeof(start_response), 0, NULL, NULL);

        if (n == -1)
            die("ERROR: recvfrom");
    }

    if (n != START_RESPONSE_BYTES) {
        if (n > USERID_BYTES)
            printf("CLIENT: %.*s",
                   n - USERID_BYTES,
                   start_response + USERID_BYTES);

        close(fd);
        return EXIT_FAILURE;
    }

    printf("CLIENT: rcvd puzzle from server for \"%s\":\n", argv[2]);

    print_puzzle(start_response + USERID_BYTES);

    while (1) {
        unsigned short header;
        int reply;
        int moves;

        printf("CLIENT: enter row (0..8), column (0..8), and digit:\n");
        printf(">");

        if (scanf("%d %d %c", &row, &col, &digit) != 3)
            break;

        printf("CLIENT: placing '%c' at (%d,%d)...\n",
               digit, row, col);

        place[0] = 0x20;
        memcpy(place + 1, "PLACE", 5);
        memcpy(place + 6, userid, USERID_BYTES);
        place[17] = (unsigned char)((row << 4) | col);
        place[18] = (unsigned char)digit;

        if (tcp) {
            if (send_all(fd, place, sizeof(place)) == -1)
                die("ERROR: send");

            if (recv_all(fd, place_response,
                         sizeof(place_response)) == -1)
                die("ERROR: recv");
        } else {
            if (sendto(fd, place, sizeof(place), 0,
                       (struct sockaddr *)&server,
                       server_len) == -1)
                die("ERROR: sendto");

            n = (int)recvfrom(fd, place_response,
                              sizeof(place_response),
                              0, NULL, NULL);

            if (n == -1)
                die("ERROR: recvfrom");

            if (n != PLACE_RESPONSE_BYTES) {
                fprintf(stderr, "ERROR: invalid response\n");
                close(fd);
                return EXIT_FAILURE;
            }
        }

        memcpy(&header, place_response, 2);
        header = ntohs(header);

        reply = header >> 13;
        moves = header & 0x1fff;

        printf("CLIENT: %s; after %d move%s:\n",
               reply_text(reply),
               moves,
               moves == 1 ? "" : "s");

        print_puzzle(place_response + 2);
    }

    close(fd);

    return EXIT_SUCCESS;
}