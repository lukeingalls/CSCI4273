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
  unsigned int clientlen; /* byte size of client's address */
  struct sockaddr_in serveraddr; /* server's addr */
  struct sockaddr_in clientaddr; /* client addr */
  struct hostent *hostp; /* client host info */
  char buf[BUFSIZE]; /* message buf */
  char *hostaddrp; /* dotted decimal host addr string */
  int optval; /* flag value for setsockopt */
  int n; /* message byte size */
  char type[10];
  char filename[256];
  long filesize, bytes_written = 0, bytes_sent = 0, bytes_read;
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
    printf("server received %d/%d bytes: %s\n", (int) strlen(buf), n, buf);

    sscanf(buf, "%9s ", type);

    if (!strcmp("get", type)) {
      sscanf(buf, "%*s %s ", type);
      file = fopen(type, "rb");
      if (file) {
        // open the file and retrieve its size
        if (fseek(file, 0, SEEK_END)) {
          error("Failed to SEEK_END");
        } else {   
          filesize = ftell(file);
          rewind(file);

          // Inform the client of the size of file to accept
          sendto(sockfd, &filesize, sizeof(filesize), 0, (struct sockaddr *) &clientaddr, clientlen);
          printf("Sending file %s (%ld bytes)\n", type, filesize);
          bytes_sent = 0;
          while (bytes_sent < filesize) {
              // Read and then send the packet containing part of the file
              bzero(buf, BUFSIZE);
              bytes_read = fread(buf, 1, BUFSIZE, file);
              n = sendto(sockfd, buf, bytes_read, 0, (struct sockaddr *) &clientaddr, clientlen);
              bytes_written = n;
              
              // Send any bytes that had failed to be sent
              while (bytes_written < bytes_read) {
                if (n < 0) {
                    error("sendto (line 153) FAILED");
                }
                n = sendto(sockfd, buf + bytes_written, bytes_read - bytes_written, 0, (struct sockaddr *) &clientaddr, clientlen);
                bytes_written += n;
              }

              bytes_sent += n;
              printf("sent %d bytes\n", n);
          }
        }
        fclose(file);
        bzero(buf, BUFSIZE);
      } else {
          fprintf(stderr, "File failed to open\n");
          filesize = -1;
          sendto(sockfd, &filesize, sizeof(filesize), 0, (struct sockaddr *) &clientaddr, clientlen);
      }
    } else if (!strcmp("put", type)) {
      // Recieve file size from client
      n = recvfrom(sockfd, &filesize, sizeof(filesize), 0,
        (struct sockaddr *) &clientaddr, &clientlen);  
      sscanf(buf, "%*s %s", filename);
      file = fopen(filename, "wb");
      if (file) {
        // REcieve packets from the client up until the file size
        bytes_written = 0;
        while (bytes_written < filesize) {
          n = recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) &clientaddr, &clientlen);
          if (n < 0) {
            error("RECVFROM failed");
          }
          printf("Received %d bytes\n", n);
          bytes_written += fwrite(buf, 1, n, file);
        }
        // Close file and notify user of success
        fclose(file);
        snprintf(buf, BUFSIZE, "File %s sent successfully", filename);
        n = sendAllTo(sockfd, buf, strlen(buf), 0, &clientaddr, clientlen);
        if (n < 0) {
          error("ERROR in sendto");
        }
      } else {
        fprintf(stderr, "File failed to open\n");
      }
    } else if (!strcmp("delete", type)) {
      // Store the file name in type
        sscanf(buf, "%*s %s ", type);
        
        // Record what happens on attempted removal
        if (remove(type)) {
          snprintf(buf, BUFSIZE, "The file %s failed to delete\n", type);
        } else {
          snprintf(buf, BUFSIZE, "The file %s was deleted\n", type);
        }
        // Inform the client
        n = sendto(sockfd, buf, strlen(buf), 0, (struct sockaddr *) &clientaddr, clientlen);
        if (n < 0) {
          error("ERROR in sendto");
        }
    } else if (!strcmp("ls", type)) {
        char * lsBody;
        int curLen = BUFSIZE, pos = 0;
        // Allow the string holding ls to grow as necessary
        lsBody = (char *) malloc(BUFSIZE * sizeof(char));
        dir = opendir(".");
        struct dirent *entry;
        if (dir) {
          // Prepare string
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
          // Forward it to user
          n = sendAllTo(sockfd, lsBody, strlen(lsBody), 0, &clientaddr, clientlen);
          if (n < 0) {
            error("ERROR in sendto");
          }
        } else {
          printf("Could not execute ls\n");
        }

        free(lsBody);
    } else if (!strcmp("exit", type)) {
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
