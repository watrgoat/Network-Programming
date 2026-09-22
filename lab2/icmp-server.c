#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <netinet/ip_icmp.h>


int main() {
  int sd;
  struct sockaddr_in dest;
  char msg[] = "Hello, World!";

  dest.sin_family = AF_INET;
  if (inet_pton(AF_INET, "127.0.0.1", &(dest.sin_addr)) != 1) {
      printf("Bad Address!\n");
      return(1);
  }

  if ((sd = socket(AF_INET, SOCK_RAW, 1)) < 0) {
      printf("socket() failed!\n");
      return(1);
  }

  int one = 1;
  const int *val = &one;
  if (setsockopt(sd, IPPROTO_IP, IP_HDRINCL, val, sizeof(one)) < 0) {
      perror("Error setting IP_HDRINCL");
      exit(EXIT_FAILURE);
  }

  // ----
  struct icmphdr icmp_header;
  
  icmp_header.type = 8;
  




  if (sendto(sd, &msg, 14, 0, (struct sockaddr*) &dest, sizeof(struct sockaddr)) < 0)  {
    printf("sendto() failed!\n");
    return(1);
  }

  return(0);
}
