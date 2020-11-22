
#define _GNU_SOURCE

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/sha.h>
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
#define CONTENT_LEN 100
#define LINE_LEN 256

typedef struct Request {
    char reqtype[TYPELEN];
    char webaddr[ADDR_LEN];
    int port;
    struct hostent *host;
    struct sockaddr_in addr;
} Request;

typedef struct Response {
    char header[MAXBUF / 2];
    size_t content_length;
} Response;

typedef struct Cache {
    struct Cache *next;
    struct CachePage *page;
} Cache;

typedef struct CachePage {
    unsigned char hash[SHA_DIGEST_LENGTH];
    char header[MAXBUF];
    size_t length;
    char * page;
} CachePage;
