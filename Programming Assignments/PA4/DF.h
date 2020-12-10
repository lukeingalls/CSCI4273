/**
 * \brief contains imports, macros, and definitions that are useful for both client and server
 **/
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/md5.h>
#include <pthread.h>    /* pthread_create, pthread_detach */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#define true 1
#define false 0
#define USERNAME_LEN 50
#define PASSWORD_LEN 50
#define MAXBUF  8192    /* max I/O buffer size */

typedef struct CREDS {
    char username[USERNAME_LEN];
    char password[PASSWORD_LEN];
} CREDS;