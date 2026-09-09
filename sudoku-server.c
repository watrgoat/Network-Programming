#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define MAXBUFFER 128

// puzzle struct 
typedef struct {
    char board[90];
} Puzzle;

// function to read puzzles
Puzzle *read_puzzles(int fd, int max_id) {
    Puzzle *puzzles = malloc((max_id + 1) * sizeof(Puzzle));
    if (puzzles == NULL)
        return NULL;
    char c;
    int num_read = 0;
    while (read(fd, &c, 1) > 0) {
        if (c == '<') {
            int id = 0;
            while (read(fd, &c, 1) > 0 && c != '>') {
                if (c >= '0' && c <= '9')
                    id = id * 10 + (c - '0');
            }
            read(fd, &c, 1);
            ssize_t n = read(fd, puzzles[id].board, 90);
            if (n != 90) {
                free(puzzles);
                return NULL;
            }
            num_read++;
        }
    }
    printf("SERVER: read \"puzzles.txt\"; number of Sudoku puzzles: %d\n", num_read);
    return puzzles;
}

// fetch the digit/"." at a specific puzzle position 
char get_cell(const char *puzzle, int row, int col) {
    return puzzle[row * 10 + col];
}

// set sudoku cell to a digit 
void set_cell(char *puzzle, int row, int col, char digit) {
    puzzle[row * 10 + col] = digit;
}

// check whether a digit already appears in the specified row
int duplicate_in_row(const char *puzzle, int target_row, int target_col, char digit) {
    int col;
    for (col = 0; col < 9; col++) {
        if (col == target_col)
            continue;
        if (get_cell(puzzle, target_row, col) == digit)
            return 1;
    }
    return 0;
}

// check whether a digit already appears in the specified column
int duplicate_in_column(const char *puzzle, int target_row, int target_col, char digit) {
    int row;
    for (row = 0; row < 9; row++) {
        if (row == target_row)
            continue;
        if (get_cell(puzzle, row, target_col) == digit)
            return 1;
    }
    return 0;
}

// check whether a digit already appears in the specified 3x3 block
int duplicate_in_block(const char *puzzle, int target_row, int target_col, char digit) {
    int start_row; int start_col;
    int row; int col;

    start_row = (target_row / 3) * 3;
    start_col = (target_col / 3) * 3;

    for (row = start_row; row < start_row + 3; row++) {
        for (col = start_col; col < start_col + 3; col++) {
            if (row == target_row && col == target_col)
                continue;
            if (get_cell(puzzle, row, col) == digit)
                return 1;
        }
    }
    return 0;
}

void loop_n_listen(int sd) {
    while ( 1 )
    {
    char buffer[MAXBUFFER+1];

    struct sockaddr_in remote_client;
    int addrlen = sizeof( remote_client );

    /* read a datagram from the remote client side (BLOCKING) */
    int n = recvfrom( sd, buffer, MAXBUFFER, 0,
                      (struct sockaddr *)&remote_client,
                      (socklen_t *)&addrlen );

    if ( n == -1 ) { perror( "recvfrom() failed" ); continue; }
    printf( "SERVER: received %d bytes from %s port %d\n",
            n, inet_ntoa( remote_client.sin_addr ),
            ntohs( remote_client.sin_port ) );
    handle_request(sd, remote_client, addrlen, n, buffer);
  }
}

void handle_request(int sd, struct sockaddr_in remote_client, int addrlen, int n, char buffer[MAXBUFFER+1]) {
    if (n == 18) {
        // start
        printf("rcvd start request prolly");
    } else if (n == 19) {
        // place
        printf("rcvd start request prolly");
    } else {
        // bad request
        printf("rcvd bad request prolly");
    }

    printf( "Rcvd datagram from %s port %d\n",
            inet_ntoa( remote_client.sin_addr ),
            ntohs( remote_client.sin_port ) );

    printf( "Rcvd %d bytes\n", n );
    buffer[n] = '\0';  /* assume this is printable char[] data */
    printf( "Rcvd: \"%s\"\n", buffer );

    /* echo the first 3 bytes (at most) plus '\n' back to the client */
    if ( n > 3 ) { n = 4; buffer[3] = '\n'; }
    sendto( sd, buffer, n, 0, (struct sockaddr *)&remote_client, addrlen );

    /* TO DO: check the return value from sendto() */
}

void start() {} // handle start request
// no clue what errors could respond with

void place() {}
// err on oob access

// main function 
int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    int fd = open("puzzles.txt", O_RDONLY);

    if (fd < 0) {
        perror("open");
        return 1;
    }
    int max_id = 64;
    Puzzle *puzzles = read_puzzles(fd, max_id);
    close(fd);
    if (puzzles == NULL) {
        return EXIT_FAILURE;
    }
    int sd;
    sd = socket( AF_INET, SOCK_DGRAM, 0 );
    if ( sd == -1 ) { perror( "socket() failed" ); return EXIT_FAILURE; }
    printf("SERVER: created socket endpoint on descriptor %d\n", sd);

    struct sockaddr_in udp_server;
    int length = sizeof( udp_server );

    udp_server.sin_family = AF_INET;  /* IPv4 */

    udp_server.sin_addr.s_addr = htonl( INADDR_ANY ); /* any remote IP can send us a datagram */
    int port = -1;

    if (argc != 2) {
        udp_server.sin_port = htons( 0 );  /* htons( 12345 ); for bind(), the 0 here means let the OS assign a port number */
    } else {
        port = atoi(argv[1]);
        udp_server.sin_port = htons(port); 
    }
    
    if ( bind( sd, (struct sockaddr *)&udp_server, length ) == -1 ) {
        perror( "bind() failed" );
        return EXIT_FAILURE;
    }
    if ( getsockname( sd, (struct sockaddr *)&udp_server, (socklen_t *)&length ) == -1 ) {
        perror( "getsockname() failed" );
        return EXIT_FAILURE;
    }
    if (ntohs( udp_server.sin_port ) == port) {
        printf("SERVER: UDP server bound to specified port\n");
    } else {
        printf("SERVER: UDP server bound to port %d\n", ntohs( udp_server.sin_port ) );
    }

    loop_n_listen(sd);

    free(puzzles);
    close (sd);

    return 0;
}