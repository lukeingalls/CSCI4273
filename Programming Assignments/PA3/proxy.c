/*
 * server.c - The main driver file for PA2
 */
#include "proxy.h"

// Socket listening for connections
int listenfd;
Cache * cache = 0;

void catch_sigint(int signo);
void catch_sigpipe(int signo);
int open_listenfd(int port);
void handle_request(int connfd);
void *thread(void *vargp);
Request *parse_request(char * buf, const int buf_lim);
Cache *new_cache();
CachePage *init_page(unsigned char * buf, size_t buf_len, size_t resp_len);
Cache *check_cache(unsigned char hash[SHA_DIGEST_LENGTH]);
void write_page_content(CachePage *cp, size_t pos, char * buf);
void dealloc_cache();
void dealloc_cache_frame(Cache *frame);

int main(int argc, char ** argv) {
    int port, *connfdp;
    socklen_t clientlen=sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t tid;

    // Register signal handler
    if (signal(SIGINT, catch_sigint) == SIG_ERR) {
        fprintf(stderr, "Signal handler could not be registered");
        exit(2);
    }

    signal(SIGPIPE, catch_sigpipe);

    // Check that a port number has been received
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }
    port = atoi(argv[1]);

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

        request->host = gethostbyname(query_addr);

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

    fprintf(stderr, "The header is: %s\n", response->header);

    return response;
}

/*
 * transfer_get - handles the exchange of messages between the 
 * requester and the remote server.
 */
void transfer_get(char * request_buffer, int conn_ext_fd, int connfd, size_t recieved) {
    size_t sent, remainder;
    char * bufptr;
    bufptr = request_buffer;
    while ((sent = write(conn_ext_fd, bufptr, recieved))) {
        recieved -= sent;
        bufptr += sent;
    }
    Response * response = parse_response(conn_ext_fd);
    // Write the header
    sent = write(connfd, response->header, strlen(response->header));

    remainder = response->content_length;
    while (remainder) {
        recieved = read(conn_ext_fd, request_buffer, (remainder > MAXBUF) ? MAXBUF : remainder);
        sent = write(connfd, request_buffer, recieved);
        remainder -= recieved;
    }

    free(response);
}

/*
 * transfer_400 - sends a 400 message to @param connfd
 */
void transfer_400(int connfd) {
    char * msg = "HTTP/1.0 400 Bad Request\r\n\r\n";
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
    char request_buffer[MAXBUF];
    memset(request_buffer, '\0', MAXBUF);


    // Read the request
    recieved = read(connfd, request_buffer, MAXBUF);
    if (!strncmp(request_buffer, "GET /ajax/libs/jquery/1.4/jquery.min.js undefined", 20)) {
        fprintf(stderr, "request issued");
    }
    // fprintf(stderr, "%s\n\n", request_buffer);
    if (recieved) { // Content received from socket
        Request *request = parse_request(request_buffer, MAXBUF);
        if (request->host) {
            conn_ext_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (conn_ext_fd == -1) {
                fprintf(stderr, "error opening external socket");
            } else if (!connect(conn_ext_fd, (struct sockaddr *) &(request->addr), sizeof(request->addr))) {
                if (strcmp(request->reqtype, "GET")) {
                    transfer_400(connfd);
                } else {
                    transfer_get(request_buffer, conn_ext_fd, connfd, recieved);
                }
            } else {
                fprintf(stderr, "Failed to connect to external address");
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
Cache *new_cache()  {
    Cache * c = (Cache *) malloc(sizeof(Cache));
    c->next = cache;
    return c;
}

/*
 * init_page - populates all but the page field of a cache page
 */
CachePage *init_page(unsigned char * buf, size_t buf_len, size_t resp_len) {
    CachePage *cp = (CachePage *) malloc(sizeof(CachePage));
    SHA1(buf, buf_len, cp->hash);
    
    memcpy(cp->header, buf, buf_len);
    cp->length = resp_len;

    cp->page = (char *) malloc(resp_len * (sizeof(char)));

    return cp;
}

/*
 * check_cache - returns a reference to a cache matching the hash
 * or null if no such page exists
 */
Cache *check_cache(unsigned char hash[SHA_DIGEST_LENGTH]) {
    Cache * c_iter = cache;

    while (c_iter) {
        if (!memcmp(c_iter->page->hash, hash, SHA_DIGEST_LENGTH)) {
            break;
        } else {
            c_iter = c_iter->next;
        }
    }

    return c_iter;
}

/*
 * write_page_contnent - places the page into the cache
 */
void write_page_content(CachePage *cp, size_t pos, char * buf) {
    memcpy(cp->page + pos, buf, (cp->length - pos > MAXBUF) ? MAXBUF : cp->length - pos);
}

/*
 * dealloc_cache - wrapper for the recursive dealloc_cache_frame
 */
void dealloc_cache() {
    dealloc_cache_frame(cache);
}

/*
 * dealloc_cache_frame - frees the entire cache
 */
void dealloc_cache_frame(Cache *frame) {
    if (frame->next) {
        dealloc_cache_frame(frame->next);
    }
    free(frame->page->page);
    free(frame->page);
    free(frame);
}
