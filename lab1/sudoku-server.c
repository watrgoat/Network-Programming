#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "map.h"

#define MAXBUFFER 128
#define MAXIDS 64

typedef struct {
    char board[90];
    char key[3];
} Puzzle;

typedef struct {
    Puzzle puzzle;
    int puzzle_id;
    char user[12];
} User_Puzzle;

HashMap puzzles = {0};
HashMap user_puzzles = {0};

int puzzle_count = 0;

Puzzle *get_puzzle(int id)
{
    char key[3];
    snprintf(key, sizeof(key), "%d", id);
    return map_get(&puzzles, key);
}

void read_puzzles(int fd)
{
    char c;

    while (read(fd, &c, 1) > 0) {
        if (c != '<')
            continue;

        int id = 0;

        while (read(fd, &c, 1) > 0 && c != '>') {
            id = id * 10 + (c - '0');
        }

        read(fd, &c, 1);

        Puzzle *p = malloc(sizeof(Puzzle));
        if (p == NULL)
            exit(EXIT_FAILURE);

        if (read(fd, p->board, 90) != 90) {
            free(p);
            exit(EXIT_FAILURE);
        }

        snprintf(p->key, sizeof(p->key), "%d", id);
        map_put(&puzzles, p->key, p);

        puzzle_count++;
    }

    printf("SERVER: read \"puzzles.txt\"; number of Sudoku puzzles: %d\n",
           puzzle_count);
}

char get_cell(const char *board, int row, int col)
{
    return board[row * 10 + col];
}

void set_cell(char *board, int row, int col, char digit)
{
    board[row * 10 + col] = digit;
}

int duplicate_in_row(const char *board, int row, int col, char digit)
{
    for (int c = 0; c < 9; c++) {
        if (c != col && get_cell(board, row, c) == digit)
            return 1;
    }

    return 0;
}

int duplicate_in_column(const char *board, int row, int col, char digit)
{
    for (int r = 0; r < 9; r++) {
        if (r != row && get_cell(board, r, col) == digit)
            return 1;
    }

    return 0;
}

int duplicate_in_block(const char *board, int row, int col, char digit)
{
    int start_row = (row / 3) * 3;
    int start_col = (col / 3) * 3;

    for (int r = start_row; r < start_row + 3; r++) {
        for (int c = start_col; c < start_col + 3; c++) {
            if ((r != row || c != col) &&
                get_cell(board, r, c) == digit)
                return 1;
        }
    }

    return 0;
}

void send_fail(int sd, const char *error,
               struct sockaddr_in *client, socklen_t addrlen)
{
    char response[64];

    int len = snprintf(response, sizeof(response),
                       "FAIL\n%s", error);

    sendto(sd, response, len, 0,
           (struct sockaddr *)client, addrlen);
}

void get_user(char *user, const char *buffer)
{
    memcpy(user, buffer + 5, 11);
    user[11] = '\0';

    for (int i = 10; i >= 0 && user[i] == ' '; i--)
        user[i] = '\0';
}

void handle_start(int sd, char *buffer, int n,
                  struct sockaddr_in *client, socklen_t addrlen)
{
    if (n < 16 || n > 18)
        return;

    char user[12];
    get_user(user, buffer);

    printf("SERVER: rcvd START request for %s\n", user);

    int puzzle_id;

    if (n == 16) {
        if (puzzle_count == 0)
            return;

        puzzle_id = (rand() % puzzle_count) + 1;
    }
    else {
        char id[3] = {0};
        int id_len = n - 16;

        memcpy(id, buffer + 16, id_len);

        for (int i = 0; i < id_len; i++) {
            if (id[i] < '0' || id[i] > '9')
                return;
        }

        puzzle_id = atoi(id);

        if (puzzle_id < 1 || puzzle_id > puzzle_count)
            return;
    }

    Puzzle *p = get_puzzle(puzzle_id);

    if (p == NULL)
        return;

    User_Puzzle *up = map_get(&user_puzzles, user);

    if (up == NULL) {
        up = malloc(sizeof(User_Puzzle));

        if (up == NULL)
            return;

        strcpy(up->user, user);
        map_put(&user_puzzles, up->user, up);
    }

    up->puzzle = *p;
    up->puzzle_id = puzzle_id;

    sendto(sd, up->puzzle.board, 90, 0,
           (struct sockaddr *)client, addrlen);
}

void handle_place(int sd, char *buffer, int n,
                  struct sockaddr_in *client, socklen_t addrlen)
{
    if (n != 19)
        return;

    char user[12];
    get_user(user, buffer);

    char row_char = buffer[16];
    char col_char = buffer[17];
    char digit = buffer[18];

    if (row_char < '1' || row_char > '9')
        return;

    if (col_char < '1' || col_char > '9')
        return;

    if ((digit < '1' || digit > '9') && digit != '.')
        return;

    printf("SERVER: rcvd PLACE request for %s\n", user);

    User_Puzzle *up = map_get(&user_puzzles, user);

    if (up == NULL)
        return;

    Puzzle *original = get_puzzle(up->puzzle_id);

    if (original == NULL)
        return;

    int row = row_char - '1';
    int col = col_char - '1';

    if (get_cell(original->board, row, col) != '.') {
        send_fail(sd, "CANNOT CHANGE ORIGINAL PUZZLE\n",
                  client, addrlen);
        return;
    }

    if (digit != '.') {
        if (duplicate_in_row(up->puzzle.board, row, col, digit)) {
            send_fail(sd, "DUPLICATE DIGIT IN ROW\n",
                      client, addrlen);
            return;
        }

        if (duplicate_in_column(up->puzzle.board, row, col, digit)) {
            send_fail(sd, "DUPLICATE DIGIT IN COLUMN\n",
                      client, addrlen);
            return;
        }

        if (duplicate_in_block(up->puzzle.board, row, col, digit)) {
            send_fail(sd, "DUPLICATE DIGIT IN BLOCK\n",
                      client, addrlen);
            return;
        }
    }

    set_cell(up->puzzle.board, row, col, digit);

    char response[95];

    memcpy(response, "PASS\n", 5);
    memcpy(response + 5, up->puzzle.board, 90);

    sendto(sd, response, 95, 0,
           (struct sockaddr *)client, addrlen);
}

void handle_request(int sd, char *buffer, int n,
                    struct sockaddr_in *client, socklen_t addrlen)
{
    if (n >= 5 && memcmp(buffer, "START", 5) == 0)
        handle_start(sd, buffer, n, client, addrlen);

    else if (n >= 5 && memcmp(buffer, "PLACE", 5) == 0)
        handle_place(sd, buffer, n, client, addrlen);
}

void listen_loop(int sd)
{
    while (1) {
        char buffer[MAXBUFFER];
        struct sockaddr_in client;
        socklen_t addrlen = sizeof(client);

        int n = recvfrom(sd, buffer, sizeof(buffer), 0,
                         (struct sockaddr *)&client, &addrlen);

        if (n < 0)
            continue;

        handle_request(sd, buffer, n, &client, addrlen);
    }
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);
    srand(128);

    int fd = open("puzzles.txt", O_RDONLY);

    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }

    read_puzzles(fd);
    close(fd);

    int sd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    printf("SERVER: created socket endpoint on descriptor %d\n", sd);

    struct sockaddr_in server = {0};

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);

    if (argc > 1)
        server.sin_port = htons(atoi(argv[1]));
    else
        server.sin_port = htons(0);

    if (bind(sd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("bind");
        close(sd);
        return EXIT_FAILURE;
    }

    socklen_t length = sizeof(server);

    if (getsockname(sd, (struct sockaddr *)&server, &length) < 0) {
        perror("getsockname");
        close(sd);
        return EXIT_FAILURE;
    }

    if (argc > 1) {
        printf("SERVER: UDP server bound to specified port number\n");
    }
    else {
        printf("SERVER: UDP server bound to port %d\n",
               ntohs(server.sin_port));
    }

    listen_loop(sd);

    close(sd);
    return EXIT_SUCCESS;
}
