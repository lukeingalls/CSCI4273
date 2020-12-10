/*
 * DFS.c - The implementation for a distributed file server
 */
#include "../DF.h"
#include "DFS.h"

// Socket listening for connections
int listenfd, dns_items = 0, blacklist_items = 0;

/**
 * \brief Catches sigint and gracefully terminates the program
 **/
void catch_sigint(int signo);
/**
 * \brief Catches sigpipe and terminates the thread corresponding to broken pipe
 **/
void catch_sigpipe(int signo);
/**
 * \brief Creates the socket for incoming connections
 * \returns -1 in case of failure. Open socket otherwise.
 **/
int open_listenfd(int port);
/**
 * \brief Initiates a new thread and calls \b handle_request
 **/
void *thread(void *vargp);
/**
 * \brief Essentially the equivalent to int main for the threads.
 * Will take the request and invoke what is necessary to handle the request.
 **/
void handle_request(int connfd);
/**
 * \brief Populates the list representing the valid credentials
 * \param creds_file the name of the file storing the creds. Should be dfs.conf by default.
 **/
void readCreds(char creds_file[]);
/**
 * \brief Utility function for inserting cred into the linked list
 **/
int insertCred(CREDS *c);
/**
 * \brief Utility function that checks whether a cred is represented in the cred list
 * \returns True if the cred is in the cred list and false otherwise
 **/
int credExists(CREDS *c);
/**
 * \brief Utility function for creating a creds object
 **/
CREDS *createCred(char uname[USERNAME_LEN], char passw[PASSWORD_LEN]);
/**
 * \brief Utility to clean up the cred nodes on exit.
 **/
void freeCreds(CREDNODE *cn);
/**
 * \brief returns an open dirent node to the user's folder
 * If the folder did not already exist it will be created.
 **/
DIR *openUDir(CREDS *c);
/**
 * Handles the receipt of a file
 **/
void receiveFile(CREDS *c, char filename[], size_t file_len, int connfd);
/**
 * \brief takes the creds and the filename passed to get the relative path to the file
 **/
char *getFilename(CREDS *c, char filename[], char part);
/**
 * Ack user - tells the client whether creds were accepted
 **/
void ackUser(char valid, int connfd);
void sendList(DIR *udir, int connfd);
void sendFile(CREDS *c, char filename[], int connfd);

CREDNODE *users = 0;

int main(int argc, char **argv)
{
    int port, *connfdp;
    socklen_t clientlen = sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t tid;

    // Register signal handler
    if (signal(SIGINT, catch_sigint) == SIG_ERR || signal(SIGPIPE, catch_sigpipe) == SIG_ERR)
    {
        fprintf(stderr, "Signal handler could not be registered");
        exit(2);
    }

    // Check that a port number has been received
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <port> <cred_conf>\n", argv[0]);
        exit(1);
    }
    readCreds(argv[2]);

    port = atoi(argv[1]);

    listenfd = open_listenfd(port);
    while (1)
    {
        connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        pthread_create(&tid, NULL, thread, connfdp);
    }

    return 0;
}

void *thread(void *vargp)
{
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self());
    free(vargp);
    handle_request(connfd);
    close(connfd);
    return NULL;
}

void handle_request(int connfd)
{
    size_t recieved, file_len;
    char request_buffer[MAXBUF], command[10], filename[30];
    CREDS c;
    DIR *udir;

    while ((recieved = read(connfd, request_buffer, MAXBUF)))
    {
        // recieved = read(connfd, request_buffer, MAXBUF);
        if (recieved)
        { // Content received from socket
            printf("%s\n", request_buffer);
            sscanf(request_buffer, "%s %s %s", command, c.username, c.password);
            //Authenticate the user
            if (credExists(&c))
            {
                ackUser(true, connfd);
                udir = openUDir(&c);
                switch (command[0])
                {
                case 'g':
                    sscanf(request_buffer, "%*s %*s %*s %s", filename);
                    sendFile(&c, filename, connfd);
                    break;
                case 'l':
                    sendList(udir, connfd);
                    break;
                case 'p':
                    sscanf(request_buffer, "%*s %*s %*s %s %lu", filename, &file_len);
                    receiveFile(&c, filename, file_len, connfd);
                    break;
                }
                closedir(udir);
            }
            else
            {
                ackUser(false, connfd);
                printf("Invalid cred\n");
            }
        }
        else
        { // Nothing read from socket: CLOSED
            printf("The socket has been closed.\n");
            return;
        }
    }
}

int open_listenfd(int port)
{
    int listd, optval = 1;
    struct sockaddr_in serveraddr;

    /* Create a socket descriptor */
    if ((listd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;

    /* Eliminates "Address already in use" error from bind. */
    if (setsockopt(listd, SOL_SOCKET, SO_REUSEADDR,
                   (const void *)&optval, sizeof(int)) < 0)
        return -1;

    /* listd will be an endpoint for all requests to port
       on any IP address for this host */
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)port);
    if (bind(listd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
        return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listd, LISTENQ) < 0)
        return -1;
    return listd;
} /* end open_listenfd */

void catch_sigint(int signo)
{
    fprintf(stderr, "\nShut down initiated. Good bye.\n");
    close(listenfd);
    freeCreds(users);
    exit(0);
}

void catch_sigpipe(int signo)
{
    fprintf(stderr, "Sigpipe\n");
    pthread_exit(NULL);
}

void readCreds(char creds_file[])
{
    FILE *conf = fopen(creds_file, "r");
    char temp[USERNAME_LEN + PASSWORD_LEN], uname[USERNAME_LEN], passw[PASSWORD_LEN];
    if (conf)
    {
        while (fgets(temp, USERNAME_LEN + PASSWORD_LEN, conf))
        {
            sscanf(temp, "%s %s", uname, passw);
            CREDS *c = createCred(uname, passw);
            if (!insertCred(c))
            {
                free(c); //cred already exists
            }
        }
        fclose(conf);
    }
    else
    {
        fprintf(stderr, "Creds file { %s } does not exist\n", creds_file);
        exit(EXIT_FAILURE);
    }
}

int insertCred(CREDS *c)
{
    if (credExists(c))
    {
        return false;
    }
    else
    {
        CREDNODE *cnode = (CREDNODE *)malloc(sizeof(CREDNODE));
        cnode->user = c;
        cnode->next = users;
        users = cnode;
        return true;
    }
}

int credExists(CREDS *c)
{
    CREDNODE *t_user = users;
    while (t_user)
    {
        if (
            !strcmp(t_user->user->username, c->username) &&
            !strcmp(t_user->user->password, c->password))
        {
            return true;
        }
        else
        {
            t_user = t_user->next;
        }
    }
    return false;
}

CREDS *createCred(char uname[USERNAME_LEN], char passw[PASSWORD_LEN])
{
    CREDS *c = (CREDS *)malloc(sizeof(CREDS));
    strcpy(c->username, uname);
    strcpy(c->password, passw);
    return c;
}

void freeCreds(CREDNODE *cn)
{
    if (cn)
    {
        freeCreds(cn->next);
        if (cn->user)
            free(cn->user);
        free(cn);
    }
}

DIR *openUDir(CREDS *c)
{
    DIR *dir = opendir(c->username);
    if (!dir)
    {
        int status = mkdir(c->username, 0777);
        if (status != -1)
        {
            dir = opendir(c->username);
        }
    }
    return dir;
}

void receiveFile(CREDS *c, char filename[], size_t file_len, int connfd)
{
    char part;
    read(connfd, &part, sizeof(char));
    char *fname = getFilename(c, filename, part);
    FILE *f = fopen(fname, "wb");
    char buf[MAXBUF];
    size_t r, written;
    if (f)
    {
        while (file_len)
        {
            if ((r = read(connfd, buf, MAXBUF)))
            {
                written = fwrite(buf, 1, r, f);
                fprintf(stderr, "%lu\n", written);
                file_len -= written;
                fprintf(stderr, "%lu\n", file_len);
            }
            else
            {
                fprintf(stderr, "Failed to read anything\n");
                break;
            }
        }
        fclose(f);
    }
    else
    {
        printf("File: %s failed to open\n", fname);
    }
    free(fname);
}

char *getFilename(CREDS *c, char filename[], char part)
{
    char *buf = (char *)malloc(sizeof(char) * 100);
    sprintf(buf, "./%s/.%s.%d", c->username, filename, (int)part);
    return buf;
}

void ackUser(char valid, int connfd)
{
    send(connfd, &valid, sizeof(valid), 0);
}

// This is mostly borrowed from PA1
void sendList(DIR *udir, int connfd)
{
    int pos = 0;
    char lsBody[MAXBUF];
    struct dirent *entry;
    if (udir)
    {
        // Prepare string
        while ((entry = readdir(udir)))
        {
            if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
            {
                pos += snprintf(lsBody + pos, MAXBUF, "%s\n", entry->d_name);
            }
        }
        // Forward it to user
        write(connfd, lsBody, pos);
    }
}

void sendFile(CREDS *c, char filename[], int connfd)
{
    char fullfilename[100], buf[MAXBUF];
    sprintf(fullfilename, "./%s/%s", c->username, filename);
    FILE *f = fopen(fullfilename, "rb");
    printf("Sending back file %s\n", fullfilename);
    if (f)
    {
        if (fseek(f, 0, SEEK_END))
        {
            fprintf(stderr, "File failed top open or could not get file size!\n");
            return;
        }
        else
        {
            unsigned long filesize = ftell(f);
            ssize_t read;
            rewind(f);

            fprintf(stderr, "file size: %lu\n", filesize);
            write(connfd, &filesize, sizeof(filesize));

            if ((read = fread(buf, 1, (filesize > MAXBUF) ? MAXBUF : filesize, f)))
            {
                write(connfd, buf, read);
            }
            buf[read] = '\0';
            fprintf(stderr, "content: %s", buf);
        }
        fclose(f);
    }
}
