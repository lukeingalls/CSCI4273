/*
 * server.c - The main driver file for PA2
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>    /* pthread_create, pthread_detach */

#define MAXLINE 8192    /* max text line length */
#define MAXBUF  8192    /* max I/O buffer size */
#define LISTENQ 1024    /* second argument to listen() */
#define FILE_EXT 25     /* defines the max width of the content-type field */
#define CODE_LEN 5      /* Length of the status code */
#define HTTP_FIELD 12   /* Length of http feild */
#define CONTENT_LEN 30  /* Length of the message in header */
#define CONTENT_TYPE 15 /* adds padding for the string 'content-type: ' */

typedef struct Request {
    enum Type {
        GET = 0, 
        HEAD = 1, 
        POST = 2
    } type;
    FILE *fptr;
    char file_type[FILE_EXT];
    int version;
} Request;


int open_listenfd(int port);
void handle_request(int connfd);
void *thread(void *vargp);
Request *parse_request(char * buf, const int buf_lim);

int main(int argc, char ** argv) {
    int listenfd, port, *connfdp;
    socklen_t clientlen=sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t tid;

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
    Request *request = (Request *) malloc(sizeof(request));
    char parse_variable[MAXLINE];
    char ftype[FILE_EXT];
    /*
     * Define the type of the request. If a type is not found
     * then set it to GET
     */
    sscanf(buf, "%s ", parse_variable);
    if (!strcmp(parse_variable, "GET")) {
        request->type = GET;
    } else if (!strcmp(parse_variable, "POST")) {
        request->type = POST;
    } else if (!strcmp(parse_variable, "HEAD")) {
        request->type = HEAD;
    } else {
        request->type = GET;
    }
    /*
     * Get the path to the requested file and then open it
     */
    sscanf(buf, "%*s %s ", parse_variable);

    if (strcmp(parse_variable, "/")) { // We have the filepath
        request->fptr = fopen(parse_variable + 1, "rb");

        // Retieve file type
        sscanf(parse_variable, "%*[^.]%*c%s", ftype);
        
        if (strlen(ftype)) {
            if (!strcmp(ftype, "html") || !strcmp(ftype, "html")) {
                strcpy(request->file_type, "text/html");
            } else if (!strcmp(ftype, "txt")) {
                strcpy(request->file_type, "text/plain");
            }  else if (!strcmp(ftype, "png")) {
                strcpy(request->file_type, "image/png");
            } else if (!strcmp(ftype, "gif")) {
                strcpy(request->file_type, "image/gif");
            } else if (!strcmp(ftype, "jpg")) {
                strcpy(request->file_type, "image/jpg");
            } else if (!strcmp(ftype, "css")) {
                strcpy(request->file_type, "text/css");
            } else if (!strcmp(ftype, "js")) {
                strcpy(request->file_type, "application/javascript");
            } else {
                strcpy(request->file_type, "");
            }
        } else {
            fprintf(stderr, "ERROR: %s has no file type", parse_variable);
            strcpy(request->file_type, "text/html");
        }


    } else { // Default file path
        DIR *root;
        struct dirent *entry;
        if ((root = opendir("."))) {
            while ((entry = readdir(root))) {
                if (!strcmp(entry->d_name, "index.html")) {
                    request->fptr = fopen("index.html", "rb");
                    strcpy(request->file_type, "text/html");
                    break;
                } else if (!strcmp(entry->d_name, "index.htm")) {
                    request->fptr = fopen("index.htm", "rb");
                    strcpy(request->file_type, "text/html");
                    break;
                }
            }
        } else {
            request->fptr = 0;
        }
    }
    /*
     * Set the http version (1.0 or 1.1) with the number after the 
     * decimal point that is being set.
     */
    sscanf(buf, "%*s %*s %s ", parse_variable);
    request->version = parse_variable[2] == '1';

    return request;
}

/*
 * create_response - takes in a request structure and returns the
 * appropriate response in a c string
 */
char * create_response(Request *req) {
    char * buf;
    int file_size = 0;
    char http_version[HTTP_FIELD];
    char status_code[CODE_LEN];
    char content_message[CONTENT_LEN];
    char content_type[CONTENT_TYPE + FILE_EXT];
    char content_length[CONTENT_LEN]; 
    char *file_contents, *temp, c;

    if (req->fptr) {
        /*
         * Retrieve the size of the file in bytes
         */
        fseek(req->fptr, 0L, SEEK_END);
        file_size = ftell(req->fptr);
        rewind(req->fptr);
        /*
         * Read contents of file into file_contents
         */
        file_contents = (char *) malloc((file_size + 2) * sizeof(char));
        temp = file_contents;
        while ((c = fgetc(req->fptr)) != EOF) {
            *temp = c;
            temp++;
        }
        *temp = '\0';
    }

    strcpy(http_version, (req->version) ? "HTTP/1.1 " : "HTTP/1.0 "); // define http_version
    strcpy(status_code, (req->fptr) ? "200 " : "500 "); // define status_code
    strcpy(content_message, (req->fptr) ? "Document Follows\r\n" : "Internal Server Error\r\n");  // define content_message
    snprintf(content_type, CONTENT_TYPE + FILE_EXT, "Content-Type:%s\r\n", req->file_type); // define the content_type
    snprintf(content_length, CONTENT_LEN, "Content-Length:%d\r\n\r\n", file_size); // define the content_length

    buf = (char *) malloc(
        (
            strlen(http_version) + 
            strlen(status_code) + 
            strlen(content_message) + 
            strlen(content_type) + 
            strlen(content_length) + 
            strlen((req->fptr) ? file_contents : "") +
            3 /* Additional return, endline, and null terminator */
        ) * sizeof(char));

    if (req->fptr) {
        sprintf(buf, "%s%s%s%s%s%s\r\n", 
                        http_version, 
                        status_code, 
                        content_message, 
                        content_type, 
                        content_length, 
                        file_contents
                );
    } else {
        sprintf(buf, "%s%s%s", 
                        http_version,
                        status_code,
                        content_message
                );
    }

    if (req->fptr) free(file_contents);
    return buf;
}

/*
 * handle_request - read and handle_request text lines until client closes connection
 */
void handle_request(int connfd) {
    size_t n; 
    char request_buffer[MAXLINE];
    char *httpmsg;
    
    // Read the request
    n = read(connfd, request_buffer, MAXLINE);
    if (n) { // Content received from socket
        printf("server received the following request:\n%s\n",request_buffer);
        Request *request = parse_request(request_buffer, MAXLINE);

        // Construct the appropriate response
        httpmsg = create_response(request);
        
        strcpy(request_buffer,httpmsg);
        printf("server returning a http message with the following content.\n%s\n",request_buffer);
        write(connfd, request_buffer, strlen(httpmsg));
        
        free(request);
        free(httpmsg);
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
    int listenfd, optval=1;
    struct sockaddr_in serveraddr;
  
    /* Create a socket descriptor */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;

    /* Eliminates "Address already in use" error from bind. */
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
                   (const void *)&optval , sizeof(int)) < 0)
        return -1;

    /* listenfd will be an endpoint for all requests to port
       on any IP address for this host */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET; 
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serveraddr.sin_port = htons((unsigned short)port); 
    if (bind(listenfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0)
        return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0)
        return -1;
    return listenfd;
} /* end open_listenfd */