/* udp-server.c */


/* UDP SERVER example:
 *
 * -- socket()             create a socket (endpoint) for communication
 * -- bind()               bind to (assign) a specific port number
 *
 * -- getsockname()        get socket "name" -- IP addr, port number, etc.
 *
 * -- recvfrom()/sendto()  receive/send datagrams (similar to read()/write())
 *
 */


/* To test this server, you can use the following
   command-line netcat tool:
                                        port number
                                        v
   bash$ netcat -u linux-new.cs.rpi.edu 12345
                   ^^^^^^^^^
                   replace with your hostname

   Note that netcat will act as a client to this UDP server...

   The hostname (e.g., linux-new.cs.rpi.edu) can also be
   localhost (127.0.0.1); and the port number must match what
   the server reports.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAXBUFFER 128

int main()
{
  int sd;  /* socket descriptor -- this is actually in the fd table */

  /* create the socket (endpoint) on the server side */
  sd = socket( AF_INET, SOCK_DGRAM, 0 );
  if ( sd == -1 ) { perror( "socket() failed" ); return EXIT_FAILURE; }

  printf( "Server-side UDP socket created on file descriptor %d\n", sd );


  struct sockaddr_in udp_server;
  int length = sizeof( udp_server );

  udp_server.sin_family = AF_INET;  /* IPv4 */

  udp_server.sin_addr.s_addr = htonl( INADDR_ANY );
            /* any remote IP can send us a datagram */

  udp_server.sin_port = htons( 0 );  /* htons( 12345 ); */
            /* for bind(), the 0 here means let the OS assign a port number */

  if ( bind( sd, (struct sockaddr *)&udp_server, length ) == -1 )
  {
    perror( "bind() failed" );
    return EXIT_FAILURE;
  }


  /* call getsockname() to obtain the port number that was just assigned */
  if ( getsockname( sd, (struct sockaddr *)&udp_server, (socklen_t *)&length ) == -1 )
  {
    perror( "getsockname() failed" );
    return EXIT_FAILURE;
  }

  printf( "UDP server bound to port %d\n", ntohs( udp_server.sin_port ) );



  /* the code below implements the APPLICATION PROTOCOL */
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




  close ( sd );

  return EXIT_SUCCESS;
}
