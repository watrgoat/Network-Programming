// udp-client.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#define PUZZLE_BYTES 90
#define USERID_BYTES 11

// function for printing the sodoko puzzle 
void print_puzzle(const char *puzzle) {
    int row;
    int col;
    printf("\n");
    for (row = 0; row < 9; row++) {
        for (col = 0; col < 9; col++) {
            putchar(puzzle[row * 10 + col]);
            if (col == 2 || col == 5)
                printf(" | ");
            else if (col != 8)
                printf(" ");
        }
        putchar('\n');
        if (row == 2 || row == 5)
            printf("------+-------+------\n");
    }
    printf("\n");
}

void copy_rcsid(char *destination, const char *rcsid) {
    int i;
    for (i = 0; i < USERID_BYTES; i++)
        destination[i] = ' ';
    for (i = 0; i < USERID_BYTES && rcsid[i] != '\0'; i++)
        destination[i] = rcsid[i];
}

// main function for the client 
int main(int argc, char **argv) {
    int sd;
    struct sockaddr_in server;
    struct hostent *host;
    char request[1024];
    char response[256];
    int request_length;
    ssize_t response_length;

    if (argc < 5) {
        fprintf(stderr, "Usage Example:  %s <host> <port> <RCS-ID> START [puzzle-id]\n", argv[0]);
        return EXIT_FAILURE;
    }
    // resolve hostname
    host = gethostbyname(argv[1]);
    if (host == NULL) {
        fprintf(stderr, "ERROR: could not resolve host %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    // create UDP socket
    sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }
    // set up server address
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(argv[2]));
    memcpy(&server.sin_addr, host->h_addr, host->h_length);
    memset(request, 0, sizeof(request));

    // START function
    if (strcmp(argv[4], "START") == 0) {
        if (argc == 5) {
            memcpy(request, "START", 5);
            copy_rcsid(request + 5, argv[3]);
            request_length = 18;
            request[16] = ' ';
            request[17] = ' '; 
        }
        // START w/ puzzle ID 
        else if (argc == 6) {
            int puzzle_id;
            puzzle_id = atoi(argv[5]);
            if (puzzle_id < 1 || puzzle_id > 64) {
                fprintf(stderr, "ERROR: puzzle ID must be between 1 and 64\n");
                close(sd);
                return EXIT_FAILURE;
            }
            memcpy(request, "START", 5);
            copy_rcsid(request + 5, argv[3]);
            
            if (puzzle_id < 10) {
                request[16] = ' ';
                request[17] = '0' + puzzle_id;
            }
            else {
                request[16] = '0' + (puzzle_id / 10);
                request[17] = '0' + (puzzle_id % 10);
            }
            request_length = 18;
        }
        else {
            fprintf(stderr, "Usage:  %s <host> <port> <RCS-ID> START [puzzle-id]\n", argv[0]);
            close(sd);
            return EXIT_FAILURE;
        }
    }
    // PLACE function
    else if (strcmp(argv[4], "PLACE") == 0) {
        int row;
        int col;
        char digit;
        if (argc != 8) {
            fprintf(stderr, "Usage:  %s <host> <port> <RCS-ID> PLACE <row> <col> <digit>\n", argv[0]);
            close(sd);
            return EXIT_FAILURE;
        }
        // convert row and column to integers for validation
        row = atoi(argv[5]);
        col = atoi(argv[6]);
        if (row < 1 || row > 9 || col < 1 || col > 9) {
            fprintf(stderr, "ERROR: row or column must be between 1 and 9\n");
            close(sd);
            return EXIT_FAILURE;
        }
        if (strlen(argv[7]) != 1) {
            fprintf(stderr, "ERROR: digit must be one character\n");
            close(sd);
            return EXIT_FAILURE;
        }
        digit = argv[7][0];
        if (!((digit >= '1' && digit <= '9') ||
              digit == '.')) {
            fprintf(stderr, "ERROR: digit must be 1-9 or .\n");
            close(sd);
            return EXIT_FAILURE;
        }
        // build PLACE function
        memcpy(request, "PLACE", 5);
        copy_rcsid(request + 5, argv[3]);
        request[16] = argv[5][0];
        request[17] = argv[6][0];
        request[18] = digit;
        request_length = 19;
    }
    else {
        fprintf(stderr, "ERROR: command must be START or PLACE\n");
        close(sd);
        return EXIT_FAILURE;
    }
    // SEND request to server 
    if (sendto(sd, request, request_length, 0, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("sendto");
        close(sd);
        return EXIT_FAILURE;
    }
    // RECIEVE response from server
    response_length = recvfrom(sd, response, sizeof(response) - 1, 0, NULL, NULL);
    if (response_length < 0) {
        perror("recvfrom");
        close(sd);
        return EXIT_FAILURE;
    }
    response[response_length] = '\0';
    // START response (exactly 90 bytes)
    if (strcmp(argv[4], "START") == 0) {
        if (response_length == PUZZLE_BYTES) {
            printf("SERVER RESPONSE:\n");
            print_puzzle(response);
        } else {
            printf("SERVER RESPONSE:\n%s", response);
        }
    }
    // PLACE response (PASS or FAIL)
    else {
        if (response_length >= 5 &&
            strncmp(response, "PASS\n", 5) == 0) {
            printf("PASS\n");
            if (response_length >= 95) {
                print_puzzle(response + 5);
            }
        } else {
            printf("%s", response);
        }
    }
    close(sd);
    return EXIT_SUCCESS;
}