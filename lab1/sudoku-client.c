#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>

#define USERID_BYTES 11
#define MAXRESPONSE 128

void copy_rcsid(char *dest, const char *rcsid)
{
    memset(dest, ' ', USERID_BYTES);
    memcpy(dest, rcsid, strlen(rcsid));
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr,
                "Usage: %s <host> <port> <RCS-ID> START [puzzle-id]\n"
                "       %s <host> <port> <RCS-ID> PLACE <row> <col> <digit>\n",
                argv[0], argv[0]);
        return EXIT_FAILURE;
    }

    if (strlen(argv[3]) == 0 || strlen(argv[3]) > USERID_BYTES) {
        fprintf(stderr, "ERROR: invalid RCS-ID\n");
        return EXIT_FAILURE;
    }

    struct hostent *host = gethostbyname(argv[1]);

    if (host == NULL) {
        fprintf(stderr, "ERROR: could not resolve host %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[2]);

    if (port < 1 || port > 65535) {
        fprintf(stderr, "ERROR: invalid port\n");
        return EXIT_FAILURE;
    }

    int sd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct timeval timeout = {2, 0};

    setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO,
               &timeout, sizeof(timeout));

    struct sockaddr_in server = {0};

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    memcpy(&server.sin_addr, host->h_addr, host->h_length);

    char request[19];
    int request_length;

    if (strcmp(argv[4], "START") == 0) {
        if (argc != 5 && argc != 6) {
            fprintf(stderr,
                    "Usage: %s <host> <port> <RCS-ID> START [puzzle-id]\n",
                    argv[0]);
            close(sd);
            return EXIT_FAILURE;
        }

        memcpy(request, "START", 5);
        copy_rcsid(request + 5, argv[3]);

        if (argc == 5) {
            request_length = 16;
        }
        else {
            char *end;
            long puzzle_id = strtol(argv[5], &end, 10);

            if (*argv[5] == '\0' ||
                *end != '\0' ||
                puzzle_id < 1 ||
                puzzle_id > 64) {
                fprintf(stderr,
                        "ERROR: puzzle ID must be between 1 and 64\n");
                close(sd);
                return EXIT_FAILURE;
            }

            int id_len = strlen(argv[5]);

            if (id_len > 2) {
                close(sd);
                return EXIT_FAILURE;
            }

            memcpy(request + 16, argv[5], id_len);
            request_length = 16 + id_len;
        }
    }
    else if (strcmp(argv[4], "PLACE") == 0) {
        if (argc != 8) {
            fprintf(stderr,
                    "Usage: %s <host> <port> <RCS-ID> PLACE <row> <col> <digit>\n",
                    argv[0]);
            close(sd);
            return EXIT_FAILURE;
        }

        if (strlen(argv[5]) != 1 ||
            argv[5][0] < '1' || argv[5][0] > '9') {
            fprintf(stderr, "ERROR: invalid row\n");
            close(sd);
            return EXIT_FAILURE;
        }

        if (strlen(argv[6]) != 1 ||
            argv[6][0] < '1' || argv[6][0] > '9') {
            fprintf(stderr, "ERROR: invalid column\n");
            close(sd);
            return EXIT_FAILURE;
        }

        if (strlen(argv[7]) != 1 ||
            !((argv[7][0] >= '1' && argv[7][0] <= '9') ||
              argv[7][0] == '.')) {
            fprintf(stderr, "ERROR: invalid digit\n");
            close(sd);
            return EXIT_FAILURE;
        }

        memcpy(request, "PLACE", 5);
        copy_rcsid(request + 5, argv[3]);

        request[16] = argv[5][0];
        request[17] = argv[6][0];
        request[18] = argv[7][0];

        request_length = 19;
    }
    else {
        fprintf(stderr, "ERROR: command must be START or PLACE\n");
        close(sd);
        return EXIT_FAILURE;
    }

    printf("Sending to server: \"");
    fwrite(request, 1, request_length, stdout);
    printf("\"\n");

    if (sendto(sd, request, request_length, 0,
               (struct sockaddr *)&server,
               sizeof(server)) < 0) {
        perror("sendto");
        close(sd);
        return EXIT_FAILURE;
    }

    char response[MAXRESPONSE];

    ssize_t response_length =
        recvfrom(sd, response, sizeof(response), 0, NULL, NULL);

    if (response_length < 0) {
        perror("recvfrom");
        close(sd);
        return EXIT_FAILURE;
    }

    printf("Rcvd from server:\n");

    fwrite(response, 1, response_length, stdout);

    if (response_length == 0 ||
        response[response_length - 1] != '\n')
        printf("\n");

    printf("\n");

    close(sd);
    return EXIT_SUCCESS;
}
