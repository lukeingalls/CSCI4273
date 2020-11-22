/*
 * server.c - The main driver file for PA2
 */
#include "proxy.h"

// Socket listening for connections
int listenfd, dns_items = 0, blacklist_items = 0;
Cache * cache = 0;
int ttl;
pthread_mutex_t cache_lock;
pthread_mutex_t dns_lock;

DNS dns[100];
Blacklist blacklist[100];

void catch_sigint(int signo);
void catch_sigpipe(int signo);
int open_listenfd(int port);
void handle_request(int connfd);
void *thread(void *vargp);
Request *parse_request(char * buf, const int buf_lim);
Cache *new_cache();
CachePage *init_page(Response *response);
Cache *check_cache(unsigned char hash[SHA_DIGEST_LENGTH]);
void write_page_content(CachePage *cp, size_t pos, char * buf, size_t write_amount);
void dealloc_cache();
void dealloc_cache_frame(Cache *frame);
void cache_ttl();
void remove_page(Cache *c);
void rm_node(Cache *frame);
struct hostent * host_is_cached(char * addr);
void populate_blacklist();

int main(int argc, char ** argv) {
    int port, *connfdp;
    socklen_t clientlen=sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t tid;

    populate_blacklist();

    // Register signal handler
    if (signal(SIGINT, catch_sigint) == SIG_ERR || signal(SIGPIPE, catch_sigpipe) == SIG_ERR) {
        fprintf(stderr, "Signal handler could not be registered");
        exit(2);
    }

    if (pthread_mutex_init(&cache_lock, NULL) != 0 && pthread_mutex_init(&dns_lock, NULL) != 0) { 
        printf("\n mutex init has failed\n"); 
        return 1; 
    } 
    // Check that a port number has been received
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <port> <cache-ttl>\n", argv[0]);
        exit(1);
    }
    port = atoi(argv[1]);
    ttl = atoi(argv[2]);

    listenfd = open_listenfd(port);
    while (1) {
        connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, (struct sockaddr*) &clientaddr, &clientlen);
        pthread_create(&tid, NULL, thread, connfdp);
    }
    
    return 0;
}

/* thread routine */
void * thread(void * vargp) 
{  
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self()); 
    free(vargp);
    handle_request(connfd);
    close(connfd);
    return NULL;
}

/*
 * parse_request - receives the request from the client and 
 * parses it into the request structure
 */
Request *parse_request(char * buf, const int buf_lim) {
    Request *request = (Request *) malloc(sizeof(Request));
    request->blacklist = false;
    // Get the type of the request
    sscanf(buf, "%3s", request->reqtype);

    // Get the web address that is being requested
    sscanf(buf,"%*s %255s", request->webaddr); // TODO make formatter use macro


    request->port = 80;
    if (strnlen(request->webaddr, ADDR_LEN)) {
        char query_addr[ADDR_LEN];
        
        strcpy(query_addr, &request->webaddr[(strncmp("http://", request->webaddr, 7)) ? 0 : 7]);

        // Handle file path
        for (char *c = query_addr; *c; c++) {
            if (*c == '/') {
                *c = '\0';
                break;
            }
        }
        // Handle port
        for (char *c = query_addr; *c; c++) {
            if (*c == ':') {
                request->port = atoi(c + 1);
                *c = '\0';
                break;
            }
        }

        if (!(request->host = host_is_cached(query_addr))) {
            request->host = gethostbyname(query_addr);
            pthread_mutex_lock(&dns_lock);
            if (dns_items < 100) {
                dns[dns_items++].h = request->host;
            }
            pthread_mutex_unlock(&dns_lock);
        }

        for (int i = 0; i < blacklist_items; i++) {
            char * ip = inet_ntoa(*((struct in_addr*) request->host->h_addr_list[0])); 
            if (!strcmp(query_addr, blacklist[i].identifier) || !strcmp(ip, blacklist[i].identifier)) {
                request->blacklist = true;
            }
        }

        // Define the sockaddr
        if(request->host) {
            request->addr.sin_family = AF_INET;
            request->addr.sin_port = htons(request->port);
            request->addr.sin_addr.s_addr = *(long *) (request->host->h_addr_list[0]);
        } else {
            herror(0);
        }
    }

    // Get the http version
    sscanf(buf,"%*s %*s, %9s", request->webaddr); // TODO make formatter use macro
    return request;
}

/*
 * parse_response - collects the header from a response
 * from the remote server and parses the contentlength.
 */
Response *parse_response(int connfd) {
    Response *response = (Response *) malloc(sizeof(Response));
    char line[LINE_LEN], c = '\0';
    int idx = 0, response_idx = 0;

    while (idx < (MAXBUF / 2)) {
        if (read(connfd, &c, 1) == 1) {
            line[idx++] = c;
            if (c == '\n') {
                line[idx] = '\0';
                strcpy(response->header + response_idx, line);
                response_idx += idx;
                idx = 0;
                if (!strncmp(line, "Content-Length:", 15)) {
                    sscanf(line, "%*s %lu", &response->content_length);
                }
                if (strlen(line) <= 2) {
                    break;
                }
            }
        } else {
            break;
        }
    }

    return response;
}

/*
 * transfer_get - handles the exchange of messages between the 
 * requester and the remote server.
 */
void transfer_get(char * request_buffer, int conn_ext_fd, int connfd, size_t recieved, Cache *c) {
    size_t sent, remainder;
    char * bufptr;
    bufptr = request_buffer;
    CachePage *cp = 0;
    while ((sent = write(conn_ext_fd, bufptr, recieved))) {
        recieved -= sent;
        bufptr += sent;
    }
    Response * response = parse_response(conn_ext_fd);

    if (c) {
        cp = init_page(response);
        c->page = cp;
    }

    // Write the header
    sent = write(connfd, response->header, strlen(response->header));

    remainder = response->content_length;
    while (remainder) {
        recieved = read(conn_ext_fd, request_buffer, (remainder > MAXBUF) ? MAXBUF : remainder);
        sent = write(connfd, request_buffer, recieved);
        if (c) write_page_content(cp, response->content_length - remainder, request_buffer, recieved);
        remainder -= recieved;
    }
}

void serve_from_cache(CachePage *cp, int connfd) {
    size_t sent, remainder, total = 0;
    sent = write(connfd, cp->response->header, strlen(cp->response->header));
    remainder = cp->response->content_length;
    while(remainder) {
        sent = write(connfd, cp->page + total, (MAXBUF > remainder) ? remainder : MAXBUF);
        remainder -= sent;
        total += sent;
    }
}

/*
 * transfer_400 - sends a 400 message to @param connfd
 */
void transfer_400(int connfd) {
    char * msg = "HTTP/1.0 400 Bad Request\r\n\r\n";
    write(connfd, msg, strlen(msg));
}

/*
 * transfer_403 - sends a 403 message to @param connfd
 */
void transfer_403(int connfd) {
    char * msg = "HTTP/1.0 403 Forbidden\r\n\r\n<h1>You are forbidden from visiting this site</h1>\r\n\r\n";
    write(connfd, msg, strlen(msg));
}

/*
 * transfer_404 - sends a 404 message to @param connfd
 */
void transfer_404(int connfd) {
    char * msg = "HTTP/1.0 404 Not Found\r\n\r\n<h1>The address could not be resolved</h1>\r\n\r\n";
    write(connfd, msg, strlen(msg));
}

/*
 * handle_request - read and handle_request text lines until client closes connection
 */
void handle_request(int connfd) {
    size_t recieved;
    int conn_ext_fd;
    char line[256];
    unsigned char hash[SHA_DIGEST_LENGTH];
    char request_buffer[MAXBUF];
    memset(request_buffer, '\0', MAXBUF);

    cache_ttl();

    // Read the request
    recieved = read(connfd, request_buffer, MAXBUF);
    if (recieved) { // Content received from socket
        Request *request = parse_request(request_buffer, MAXBUF);
        if (request->host) {
            if (!request->blacklist) {
                conn_ext_fd = socket(AF_INET, SOCK_STREAM, 0);
                if (conn_ext_fd == -1) {
                    fprintf(stderr, "error opening external socket");
                } else if (!connect(conn_ext_fd, (struct sockaddr *) &(request->addr), sizeof(request->addr))) {
                    if (strcmp(request->reqtype, "GET")) {
                        transfer_400(connfd);
                    } else {
                        sscanf(request_buffer, "%255[^\n]", line);;
                        SHA1((unsigned char *) line, strlen(line), hash);
                        Cache * c = check_cache(hash);
                        if (!c) {
                            c = new_cache(hash);
                            transfer_get(request_buffer, conn_ext_fd, connfd, recieved, c);
                        } else {
                            serve_from_cache(c->page, connfd);
                            fprintf(stderr, "Served cached page for request: %s\n", line);
                        }
                    }
                } else {
                    fprintf(stderr, "Failed to connect to external address");
                }
            } else {
                transfer_403(connfd);
            }
        } else {
            transfer_404(connfd);
        }

        close(conn_ext_fd);
        free(request);
    } else { // Nothing read from socket: CLOSED
        printf("The socket has been closed.\n");
    }
}

/* 
 * open_listenfd - open and return a listening socket on port
 * Returns -1 in case of failure 
 */
int open_listenfd(int port) 
{
    int listd, optval=1;
    struct sockaddr_in serveraddr;
  
    /* Create a socket descriptor */
    if ((listd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;

    /* Eliminates "Address already in use" error from bind. */
    if (setsockopt(listd, SOL_SOCKET, SO_REUSEADDR, 
                   (const void *)&optval , sizeof(int)) < 0)
        return -1;

    /* listd will be an endpoint for all requests to port
       on any IP address for this host */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET; 
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serveraddr.sin_port = htons((unsigned short)port); 
    if (bind(listd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0)
        return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listd, LISTENQ) < 0)
        return -1;
    return listd;
} /* end open_listenfd */

/*
 * catch_sigint - function to help the program terminate gracefully in the event of 
 * sigint
 */
void catch_sigint(int signo) {
    fprintf(stderr, "\nShut down initiated. Good bye.\n");
    close(listenfd);
    dealloc_cache();
    pthread_mutex_destroy(&cache_lock);
    pthread_mutex_destroy(&dns_lock);
    exit(0);
}

/*
 * catch_sigpipe - function to terminate threads connected to no longer active sockets
 */
void catch_sigpipe(int signo) {
    pthread_exit(NULL);
}

/*
 * Gets a new cache node and places it in the cache.
 */ 
Cache *new_cache(unsigned char * buf)  {
    Cache * c = (Cache *) malloc(sizeof(Cache));
    memcpy(c->hash, buf, SHA_DIGEST_LENGTH);
    c->next = cache;
    c->ttl = time(0);
    cache = c;
    return c;
}

/*
 * init_page - populates all but the page field of a cache page
 */
CachePage *init_page(Response *response) {
    CachePage *cp = (CachePage *) malloc(sizeof(CachePage));
    
    cp->response = response;
    cp->page = (char *) malloc(response->content_length * (sizeof(char)));

    return cp;
}

/*
 * check_cache - returns a reference to a cache matching the hash
 * or null if no such page exists
 */
Cache *check_cache(unsigned char hash[SHA_DIGEST_LENGTH]) {
    Cache * c_iter = cache;
    pthread_mutex_lock(&cache_lock);
    while (c_iter) {
        if (!memcmp(c_iter->hash, hash, SHA_DIGEST_LENGTH)) {
            break;
        } else {
            c_iter = c_iter->next;
        }
    }
    pthread_mutex_unlock(&cache_lock);

    return c_iter;
}

/*
 * write_page_contnent - places the page into the cache
 */
void write_page_content(CachePage *cp, size_t pos, char * buf, size_t write_amount) {
    memcpy(cp->page + pos, buf, write_amount);
}

/*
 * cache_ttl - validates that pages in the cache have not exceeded their ttl
 */
void cache_ttl() {
    Cache * c_iter = cache;
    Cache * temp;
    pthread_mutex_lock(&cache_lock);
    while (c_iter) {
        if (c_iter->ttl + 15 < time(0)) {
            temp = c_iter;
            c_iter = c_iter->next;
            remove_page(temp);
        } else {
            c_iter = c_iter->next;
        }
    }
    pthread_mutex_unlock(&cache_lock);
}

/*
 * remove_page - removes a page from the cache
 */
void remove_page(Cache *c) {
    Cache *c_iter = cache;

    if (cache == c) {
        cache = c -> next;
        rm_node(c);
    }

    while (c_iter && c_iter->next != c) c_iter = c_iter->next;

    c_iter->next = c->next;
    rm_node(c);
}

void rm_node(Cache *frame) {
    fprintf(stderr, "Removing node from cache\n");
    if (frame->page->page) free(frame->page->page);
    if (frame->page) free(frame->page);
    if (frame) free(frame);
}

/*
 * dealloc_cache - wrapper for the recursive dealloc_cache_frame
 */
void dealloc_cache() {
    if (cache) {
        dealloc_cache_frame(cache);
    }
}

/*
 * dealloc_cache_frame - frees the entire cache
 */
void dealloc_cache_frame(Cache *frame) {
    if (frame->next) {
        dealloc_cache_frame(frame->next);
    }

    rm_node(frame);
}

struct hostent * host_is_cached(char * addr) {
    for (int i = 0; i < dns_items; i++) {
        if (!strcmp(dns[i].domain, addr)) {
            return dns[i].h;
        }
    }
    return 0;
}

void populate_blacklist() {
    FILE *file;
    file = fopen("blacklist.txt", "r");

    char * c = blacklist[blacklist_items].identifier;

    if (file) {
        while(fread(c, 1, 1, file)) {
            if (*c == '\n') {
                *c = '\0';
                blacklist_items++;
                c = blacklist[blacklist_items].identifier;
            } else {
                c++;
            }

            if (blacklist_items == 100) break;
        }
    }

    fclose(file);
}