/* 
 * udpserver.c - A simple UDP echo server 
 * usage: udpserver <port>
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>

#define BUFSIZE 1024

/*
 * error - wrapper for perror
 */
void error(char *msg) {
  perror(msg);
  exit(1);
}

/*
 * sendAllTo - wraps the sendto function and ensures that all data is transmitted (or results in an error)
 */
int sendAllTo(int sockfd, char *buf, int buf_len, int flags, struct sockaddr_in * sockAddr, socklen_t addrlen) {
    int n;
    int total = 0;
    while (total < buf_len) {
        n = sendto(sockfd, buf + total, buf_len - total, 0, (struct sockaddr *) sockAddr, addrlen);
        if (n < 0) { break; }
        total += n;
    }

    buf_len = total;

    return (n==-1) ? -1 : 0;
}

int main(int argc, char **argv) {
  int sockfd; /* socket */
  int portno; /* port to listen on */
  int clientlen; /* byte size of client's address */
  struct sockaddr_in serveraddr; /* server's addr */
  struct sockaddr_in clientaddr; /* client addr */
  struct hostent *hostp; /* client host info */
  char buf[BUFSIZE]; /* message buf */
  char *hostaddrp; /* dotted decimal host addr string */
  int optval; /* flag value for setsockopt */
  int n; /* message byte size */
  char type[10];

  FILE *file;
  DIR *dir;
  /* 
   * check command line arguments 
   */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  portno = atoi(argv[1]);

  /* 
   * socket: create the parent socket 
   */
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) 
    error("ERROR opening socket");

  /* setsockopt: Handy debugging trick that lets 
   * us rerun the server immediately after we kill it; 
   * otherwise we have to wait about 20 secs. 
   * Eliminates "ERROR on binding: Address already in use" error. 
   */
  optval = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
	     (const void *)&optval , sizeof(int));

  /*
   * build the server's Internet address
   */
  bzero((char *) &serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serveraddr.sin_port = htons((unsigned short)portno);

  /* 
   * bind: associate the parent socket with a port 
   */
  if (bind(sockfd, (struct sockaddr *) &serveraddr, 
	   sizeof(serveraddr)) < 0) 
    error("ERROR on binding");

  /* 
   * main loop: wait for a datagram, then echo it
   */
  clientlen = sizeof(clientaddr);
  while (1) {

    /*
     * recvfrom: receive a UDP datagram from a client
     */
    bzero(buf, BUFSIZE);
    n = recvfrom(sockfd, buf, BUFSIZE, 0,
		 (struct sockaddr *) &clientaddr, &clientlen);
    if (n < 0)
      error("ERROR in recvfrom");

    /* 
     * gethostbyaddr: determine who sent the datagram
     */
    hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr, 
			  sizeof(clientaddr.sin_addr.s_addr), AF_INET);
    if (hostp == NULL)
      error("ERROR on gethostbyaddr");
    hostaddrp = inet_ntoa(clientaddr.sin_addr);
    if (hostaddrp == NULL)
      error("ERROR on inet_ntoa\n");
    printf("server received datagram from %s (%s)\n", 
	   hostp->h_name, hostaddrp);
    printf("server received %d/%d bytes: %s\n", strlen(buf), n, buf);

    sscanf(buf, "%9s ", type);

    if (!strcmp("get", type)) {
        printf("get\n");
    } else if (!strcmp("put", type)) {
        printf("put\n");
    } else if (!strcmp("delte", type)) {
        printf("delete\n");
    } else if (!strcmp("ls", type)) {
        char * lsBody;
        int curLen = BUFSIZE, pos = 0;
        lsBody = (char *) malloc(BUFSIZE * sizeof(char));
        dir = opendir(".");
        struct dirent *entry;
        if (dir) {
          while ((entry = readdir(dir))) {
            pos += snprintf(lsBody + pos, 255, "%s\n", entry->d_name);
            if (BUFSIZE - pos < 256) {
              char * temp = lsBody;
              lsBody = (char *) malloc(2*curLen * sizeof(char));
              strncpy(lsBody, temp, curLen);
              curLen <<= 1;
              free(temp);
            }
          }
          n = sendAllTo(sockfd, lsBody, strlen(lsBody), 0, &clientaddr, clientlen);
          if (n < 0) {
            error("ERROR in sendto");
          }
        } else {
          printf("Could not execute ls\n");
        }

        free(lsBody);
    } else if (!strcmp("exit", type)) {
        char * msg = "QUIT SERVER";
        printf("Exiting from server...\n");
        close(sockfd);
        break;
    } else {
        char msg[BUFSIZE];
        snprintf(msg, BUFSIZE, "Unknown command: %s", buf);
        n = sendAllTo(sockfd, msg, strlen(msg), 0, &clientaddr, clientlen);
        if (n < 0) error("ERROR in sendto");

        printf("Unknown command %s\n",msg);
    }
  }
  return 0;
}
