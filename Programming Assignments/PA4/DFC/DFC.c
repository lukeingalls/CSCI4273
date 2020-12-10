#include "../DF.h"
#include "DFC.h"

/**
 * Stores the username and password for the client
 **/
CREDS creds;

/**
 * \brief Reads in the information from the conf file
 * \param dfs an array of DFS objects
 * \param config_filename c-string of the name of the config file
 **/
void readConf(DFS dfs[DFS_LEN], char config_filename[]);
/**
 * \brief Connects client to servers
 * \param dfs array of DFS objects
 **/
void connDFS(DFS dfs[DFS_LEN]);
/**
 * \brief Closes client connections
 * \param dfs array of DFS objects
 **/
void closeDFS(DFS dfs[DFS_LEN]);
/**
 * \brief sends appropriate header info to the remote servers
 **/
void sendCommand(DFS dfs[DFS_LEN], char command[10], char file[], unsigned long file_size);
/**
 * \brief Sends the file to the servers
 **/
void sendFile(DFS dfs[DFS_LEN], FILE *f, unsigned long filesize);
/**
 * \brief writes len items from buf to connfd
 **/
int writeAll(char buf[], int len, int connfd);
/**
 * \brief handles operations relevant to sending a put command
 **/
void hput(DFS dfs[DFS_LEN], FILE *f, char file_name[], size_t file_size);
int recvAck(DFS dfs[DFS_LEN]);
void catch_sigpipe(int signo);


void menu()
{
    printf("===== Please select an option =====\n");
    printf("1) Get <file>\n");
    printf("2) List\n");
    printf("3) Put <file>\n");
    printf("4) Quit\n");
}

int main(int argc, char *argv[])
{
    // Holds the info for connecting to servers
    DFS dfs[DFS_LEN];
    char selection[100], file[30];

    if (argc != 2)
    {
        fprintf(stderr, "Usage: ./DFC <config_file>\n");
        exit(EXIT_FAILURE);
    }

    signal(SIGPIPE, catch_sigpipe);

    readConf(dfs, argv[1]);
    connDFS(dfs);

    while (1)
    {
        menu();
        fgets(selection, 100, stdin);
        switch (selection[0])
        {
        case '1':
        case 'G':
        case 'g':
            sscanf(selection, "%*s %s", file);
            sendCommand(dfs, "get", file, 0);
            if (recvAck(dfs)) {

            } else {
                printf("Did not present valid creds!\n");
            }
            break;
        case '2':
        case 'L':
        case 'l':
            sendCommand(dfs, "list", "", 0);
            if (recvAck(dfs)) {

            } else {
                printf("Did not present valid creds!\n");
            }
            break;
        case '3':
        case 'P':
        case 'p':
            //TODO: get file lenght and define a protocol to send the file
            sscanf(selection, "%*s %s", file);
            FILE *f = fopen(file, "rb");
            if (!f || fseek(f, 0, SEEK_END))
            {
                fprintf(stderr, "File failed top open or could not get file size!\n");
                continue;
            }
            else
            {
                unsigned long filesize = ftell(f);
                rewind(f);
                sendCommand(dfs, "put", file, filesize);
                if (recvAck(dfs)) {

                } else {
                    printf("Did not present valid creds!\n");
                }
                sendFile(dfs, f, filesize);
                // hput(dfs, f, file, filesize);
            }
            break;
        case '4':
        case 'Q':
        case 'q':
            //TODO: close out of the server connections
            break;
        default:
            printf("For fuck's sake enter an actual command\n");
            break;
        }

        // quit the loop
        if (
            selection[0] == 'Q' ||
            selection[0] == 'q' ||
            selection[0] == '4')
        {
            break;
        }
    }

    closeDFS(dfs);
    return 0;
}

void readConf(DFS dfs[DFS_LEN], char config_filename[])
{
    FILE *conf = fopen(config_filename, "r");
    int index = 0;
    char temp[100];

    if (conf)
    {
        while (fgets(temp, 100, conf))
        {
            if (index < DFS_LEN)
            {
                sscanf(temp, "%19s", dfs[index].server_ident);
                sscanf(temp, "%*s %[^:]", dfs[index].url);
                sscanf(temp, "%*s %*[^:]%*c%d", &dfs[index].port);
                index++;
            }
            else
            {
                if (index == DFS_LEN)
                {
                    // trim off the newline character
                    strncpy(creds.username, temp, strlen(temp) - 1);
                    index++;
                }
                else
                {
                    strcpy(creds.password, temp);
                }
            }
        }
        fclose(conf);
    }
    else
    {
        fprintf(stderr, "Config file { %s } does not exist\n", config_filename);
        exit(EXIT_FAILURE);
    }
}

void connDFS(DFS dfs[DFS_LEN])
{
    // check whether any server is active
    int any_active = false;

    for (DFS *d = &dfs[0]; d < dfs + 4; d++)
    {
        d->active = true;
        if ((d->host = gethostbyname(d->url)) == 0)
        {
            herror("gethostbyname");
            d->active = false;
        }
        if (d->active && (d->connfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        {
            perror("socket");
            d->active = false;
        }
        if (d->active)
        {
            d->addr.sin_family = AF_INET;
            d->addr.sin_port = htons((unsigned short)d->port);
            d->addr.sin_addr.s_addr = *(long *)d->host->h_addr_list[0];
            if (connect(d->connfd, (struct sockaddr *)&(d->addr), sizeof(d->addr)))
            {
                d->active = false;
                fprintf(stderr, "Failed to connect to server %s\n", d->server_ident);
            } else { // connection worked
                struct timeval timeout;
                timeout.tv_sec = 1;
                timeout.tv_usec = 0;
                if (setsockopt (d->connfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
                                sizeof(timeout)) < 0)
				    perror("setsockopt failed\n");
            }
        }

        any_active |= d->active;
    }

    if (!any_active)
    {
        fprintf(stderr, "There are no active DFS Servers!\n");
        exit(EXIT_FAILURE);
    }
}

void sendCommand(DFS dfs[DFS_LEN], char command[10], char file[], unsigned long file_size)
{
    char buf[MAXBUF];
    if (!strcmp(command, "list"))
    { //list
        sprintf(buf, "%s %s %s\n", command, creds.username, creds.password);
    }
    else if (!file_size)
    { //get
        sprintf(buf, "%s %s %s %s\n", command, creds.username, creds.password, file);
    }
    for (int i = 0; i < DFS_LEN; i++)
    {
        if (file_size)
        { //put
            unsigned long relative_len = (file_size + (file_size + i) % 4) / 4;
            sprintf(buf, "%s %s %s %s %lu\n", command, creds.username, creds.password, file, relative_len);
        }
        if (dfs[i].active && !write(dfs[i].connfd, buf, (strlen(buf) >= MAXBUF) ? MAXBUF : strlen(buf) + 1))
        {
            dfs[i].active = false;
        }
    }
}

void closeDFS(DFS dfs[DFS_LEN])
{
    for (DFS *d = &dfs[DFS_LEN - 1]; d >= dfs; d--)
    {
        if (d->connfd != -1)
            close(d->connfd);
    }
}

void sendFile(DFS dfs[DFS_LEN], FILE *f, unsigned long file_size)
{
    unsigned long relative_len;
    char buf[MAXBUF];
    ssize_t sent, read;

    for (int i = 0; i < DFS_LEN; i++)
    {
        for (
            relative_len = (file_size + (file_size + i) % 4) / 4;
            relative_len > 0;
            relative_len -= sent)
        {
            if ((read = fread(buf, 1, (relative_len > MAXBUF) ? MAXBUF : relative_len, f)))
            {
                // if (!(sent = writeAll(buf, read, dfs[i].connfd))) {
                //     printf("Cannot write to the socket %d\n", dfs[i].connfd);
                //     break;
                // }
                write(dfs[i].connfd, buf, read);
                break;
            } else {
                break;
            }
        }
    }
}

int writeAll(char buf[], int len, int connfd) {
    size_t pos = 0;
    ssize_t sent;
    while(len) {
        if ((sent = write(connfd, buf + pos, len)) != -1) {
            len -= sent;
            pos += sent;
        } else {
            return 0;
        }
    }
    return pos;
} 

void catch_sigpipe(int signo) {
    fprintf(stderr, "caught sigpipe\n");
}

int recvAck(DFS dfs[DFS_LEN])
{
    int resp = 0, temp;
    ssize_t t;
    for (int i = 0; i < DFS_LEN; i++)
    {
        if ((t = read(dfs[i].connfd, &temp, sizeof(char))))
        {
            if (t != -1) {
                resp |= temp;
            }
        }
    }
    return resp;
}