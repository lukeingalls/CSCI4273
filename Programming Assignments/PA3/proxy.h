
#define _GNU_SOURCE

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>    /* pthread_create, pthread_detach */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAXBUF  8192    /* max I/O buffer size */
#define LISTENQ 1024    /* second argument to listen() */
#define true 1
#define false 0
#define ADDR_LEN 256
#define TYPELEN 4
#define VERINFO 10
#define CONTENT_LEN 100

typedef struct Request {
    char reqtype[TYPELEN];
    char webaddr[ADDR_LEN];
    int port;
    char httpver[VERINFO];
    char reqcontent[MAXBUF];
    struct hostent *host;
    struct sockaddr_in addr;
} Request;
