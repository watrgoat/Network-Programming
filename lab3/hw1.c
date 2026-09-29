#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "map.h"

#define USERID_BYTES 11
#define PUZZLE_BYTES 90
#define MAX_PUZZLES 65536
#define MAX_MOVES 8191
#define START_BYTES 17
#define START_ID_BYTES 19
#define PLACE_BYTES 19

typedef struct {
    unsigned char board[PUZZLE_BYTES];
} Puzzle;

typedef struct {
    int puzzle;
    unsigned char board[PUZZLE_BYTES];
    unsigned int moves;
    int banned;
} User;

Puzzle *puzzles = NULL;
int puzzle_count = 0;
off_t loaded_bytes = 0;
HashMap users = {0};

void die(const char *s)
{
    perror(s);
    exit(EXIT_FAILURE);
}

void get_userid(const unsigned char *src, char *dst)
{
    memcpy(dst, src, USERID_BYTES);
    dst[USERID_BYTES] = '\0';
}

int valid_userid(const unsigned char *id)
{
    int i;
    int seen = 0;
    int spaces = 0;

    for (i = 0; i < USERID_BYTES; i++) {
        if (id[i] == ' ') {
            if (seen) spaces = 1;
        } else {
            if (id[i] < 33 || id[i] > 126 || spaces)
                return 0;
            seen = 1;
        }
    }

    return seen;
}

int cell(int row, int col)
{
    return row * 10 + col;
}

int load_puzzles(void)
{
    struct stat st;
    FILE *fp;
    char line[128];
    int added = 0;

    if (lstat("puzzles.txt", &st) == -1)
        die("ERROR: lstat");

    if (st.st_size <= loaded_bytes)
        return 0;

    fp = fopen("puzzles.txt", "r");
    if (!fp)
        die("ERROR: fopen");

    while (fgets(line, sizeof(line), fp)) {
        int r;

        if (line[0] != '<')
            continue;

        if (puzzle_count >= MAX_PUZZLES)
            break;

        Puzzle *tmp = realloc(
            puzzles,
            (puzzle_count + 1) * sizeof(Puzzle)
        );

        if (!tmp)
            die("ERROR: realloc");

        puzzles = tmp;

        for (r = 0; r < 9; r++) {
            if (!fgets(line, sizeof(line), fp)) {
                fprintf(stderr, "ERROR: invalid puzzles.txt\n");
                exit(EXIT_FAILURE);
            }

            if (strlen(line) < 9) {
                fprintf(stderr, "ERROR: invalid puzzles.txt\n");
                exit(EXIT_FAILURE);
            }

            memcpy(
                puzzles[puzzle_count].board + r * 10,
                line,
                9
            );

            puzzles[puzzle_count].board[r * 10 + 9] = '\n';
        }

        puzzle_count++;
        added++;
    }

    fclose(fp);
    loaded_bytes = st.st_size;

    return added;
}

User *get_user(const unsigned char *userid)
{
    char key[USERID_BYTES + 1];
    get_userid(userid, key);
    return map_get(&users, key);
}

User *new_user(const unsigned char *userid)
{
    char key[USERID_BYTES + 1];
    User *u = calloc(1, sizeof(User));

    if (!u)
        die("ERROR: calloc");

    get_userid(userid, key);
    map_put(&users, key, u);

    return u;
}

int send_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = buf;
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);

        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            return -1;
        }

        sent += (size_t)n;
    }

    return 0;
}

void start_error(int fd, const unsigned char *userid, const char *msg)
{
    unsigned char out[43];
    size_t n = strlen(msg);

    memcpy(out, userid, USERID_BYTES);
    memcpy(out + USERID_BYTES, msg, n);

    send_all(fd, out, USERID_BYTES + n);
}

int duplicate_row(User *u, int row, int col, unsigned char digit)
{
    int i;

    for (i = 0; i < 9; i++)
        if (i != col && u->board[cell(row, i)] == digit)
            return 1;

    return 0;
}

int duplicate_col(User *u, int row, int col, unsigned char digit)
{
    int i;

    for (i = 0; i < 9; i++)
        if (i != row && u->board[cell(i, col)] == digit)
            return 1;

    return 0;
}

int duplicate_block(User *u, int row, int col, unsigned char digit)
{
    int r, c;
    int sr = row / 3 * 3;
    int sc = col / 3 * 3;

    for (r = sr; r < sr + 3; r++)
        for (c = sc; c < sc + 3; c++)
            if ((r != row || c != col) &&
                u->board[cell(r, c)] == digit)
                return 1;

    return 0;
}

void handle_start(int fd, unsigned char *req, int len)
{
    unsigned char *userid = req + 6;
    unsigned char out[USERID_BYTES + PUZZLE_BYTES];
    char name[USERID_BYTES + 1];
    User *u;
    int id;

    get_userid(userid, name);
    printf("SERVER: TCP rcvd START request for %s\n", name);

    if ((req[0] >> 4) != 2 || (req[0] & 15) != 0) {
        start_error(fd, userid, "INVALID VERSION\n");
        return;
    }

    if (!valid_userid(userid)) {
        start_error(fd, userid, "USER NOT ALLOWED\n");
        return;
    }

    u = get_user(userid);

    if (u && u->banned) {
        start_error(fd, userid, "USER NOT ALLOWED\n");
        return;
    }

    if (load_puzzles())
        printf("SERVER: read new data from \"puzzles.txt\"; number of Sudoku puzzles: %d\n",
               puzzle_count);

    if (len == START_ID_BYTES) {
        unsigned short n;
        memcpy(&n, req + 17, 2);
        id = ntohs(n);

        if (id >= puzzle_count) {
            start_error(fd, userid, "NO SUCH PUZZLE ID\n");
            return;
        }
    } else {
        id = rand() % puzzle_count;
    }

    if (!u)
        u = new_user(userid);

    u->puzzle = id;
    u->moves = 0;
    memcpy(u->board, puzzles[id].board, PUZZLE_BYTES);

    memcpy(out, userid, USERID_BYTES);
    memcpy(out + USERID_BYTES, u->board, PUZZLE_BYTES);

    send_all(fd, out, sizeof(out));
}

void place_response(int fd, int reply, User *u)
{
    unsigned char out[92];
    unsigned short header;
    unsigned char blank[PUZZLE_BYTES];
    int r, c;

    header = htons((unsigned short)((reply << 13) |
                                   (u ? u->moves : 0)));

    memcpy(out, &header, 2);

    if (u) {
        memcpy(out + 2, u->board, PUZZLE_BYTES);
    } else {
        for (r = 0; r < 9; r++) {
            for (c = 0; c < 9; c++)
                blank[cell(r, c)] = '.';
            blank[r * 10 + 9] = '\n';
        }

        memcpy(out + 2, blank, PUZZLE_BYTES);
    }

    send_all(fd, out, sizeof(out));
}

void handle_place(int fd, unsigned char *req)
{
    unsigned char *userid = req + 6;
    unsigned char digit = req[18];
    int row = req[17] >> 4;
    int col = req[17] & 15;
    int reply = 0;
    char name[USERID_BYTES + 1];
    User *u = get_user(userid);

    get_userid(userid, name);
    printf("SERVER: TCP rcvd PLACE request for %s\n", name);

    if ((req[0] >> 4) != 2 || (req[0] & 15))
        reply = 1;
    else if (!valid_userid(userid) || !u || u->banned ||
             row > 8 || col > 8 ||
             !((digit >= '1' && digit <= '9') || digit == '.'))
        reply = 2;
    else if (puzzles[u->puzzle].board[cell(row, col)] != '.')
        reply = 3;
    else if (digit != '.' && duplicate_row(u, row, col, digit))
        reply = 4;
    else if (digit != '.' && duplicate_col(u, row, col, digit))
        reply = 5;
    else if (digit != '.' && duplicate_block(u, row, col, digit))
        reply = 6;
    else {
        u->board[cell(row, col)] = digit;

        if (digit != '.') {
            u->moves++;

            if (u->moves == MAX_MOVES)
                u->banned = 1;
        }
    }

    place_response(fd, reply, u);
}

int create_listener(int port)
{
    int fd;
    int yes = 1;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
        die("ERROR: socket");

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        die("ERROR: bind");

    if (listen(fd, 16) == -1)
        die("ERROR: listen");

    return fd;
}

void handle_client(int fd, fd_set *master)
{
    unsigned char req[PLACE_BYTES];
    ssize_t n = recv(fd, req, sizeof(req), 0);

    if (n <= 0) {
        close(fd);
        FD_CLR(fd, master);
        printf("SERVER: TCP closed connection\n");
        return;
    }

    if (n < 6)
        return;

    if (!memcmp(req + 1, "START", 5) &&
        (n == START_BYTES || n == START_ID_BYTES))
        handle_start(fd, req, (int)n);
    else if (!memcmp(req + 1, "PLACE", 5) && n == PLACE_BYTES)
        handle_place(fd, req);
}

int main(int argc, char **argv)
{
    int listener, maxfd, fd, ready, port;
    fd_set master, readfds;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc != 2) {
        fprintf(stderr, "ERROR: usage: %s <TCP-port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    port = atoi(argv[1]);

    if (port < 1 || port > 65535) {
        fprintf(stderr, "ERROR: invalid TCP port\n");
        return EXIT_FAILURE;
    }

    srand(128);

    if (!load_puzzles()) {
        fprintf(stderr, "ERROR: no puzzles\n");
        return EXIT_FAILURE;
    }

    printf("SERVER: read \"puzzles.txt\"; number of Sudoku puzzles: %d\n",
           puzzle_count);

    listener = create_listener(port);

    printf("SERVER: TCP listener port %d\n", port);

    FD_ZERO(&master);
    FD_SET(listener, &master);
    maxfd = listener;

    while (1) {
        readfds = master;

        ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ready == -1) {
            if (errno == EINTR)
                continue;
            die("ERROR: select");
        }

        if (FD_ISSET(listener, &readfds)) {
            int client = accept(listener, NULL, NULL);

            if (client == -1)
                die("ERROR: accept");

            FD_SET(client, &master);

            if (client > maxfd)
                maxfd = client;

            ready--;
        }

        for (fd = 0; fd <= maxfd && ready > 0; fd++) {
            if (fd != listener && FD_ISSET(fd, &readfds)) {
                handle_client(fd, &master);
                ready--;
            }
        }

        while (maxfd > listener && !FD_ISSET(maxfd, &master))
            maxfd--;
    }
}