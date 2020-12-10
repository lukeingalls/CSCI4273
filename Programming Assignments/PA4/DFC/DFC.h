#include <sys/time.h>

#define DFS_LEN 4
#define ADDR_LEN 50
#define SERV_IDENT_LEN 20

typedef struct DFS {
    struct hostent *host;
    struct sockaddr_in addr;
    char active;
    int port;
    int connfd;
    char server_ident[SERV_IDENT_LEN];
    char url[ADDR_LEN];
} DFS;