
#define _GNU_SOURCE

#include <dirent.h>
#include <sys/stat.h>

#define LISTENQ 1024    /* second argument to listen() */

typedef struct CREDNODE {
    CREDS *user;
    struct CREDNODE * next;
} CREDNODE;