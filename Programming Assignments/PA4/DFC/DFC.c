#include "../DF.h"
#include "DFC.h"

/**
 * Stores the username and password for the client
 **/
CREDS creds;

FNODE *head = 0;

int table[DFS_LEN][DFS_LEN][2] = {
    { {4, 1}, {1, 2}, {2, 3}, {3, 4} },
    { {1, 2}, {2, 3}, {3, 4}, {4, 1} },
    { {2, 3}, {3, 4}, {4, 1}, {1, 2} },
    { {3, 4}, {4, 1}, {1, 2}, {2, 3} },
};



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

int recvAck(DFS dfs[DFS_LEN]);
void catch_sigpipe(int signo);
void recvList(DFS dfs[DFS_LEN]);
void freeCreds(FNODE *cn);
FNODE *createCred(char fname[100]);
FNODE *fileExists(FNODE *f);
int insertNode(FNODE *f);
void printList();
void get(DFS dfs[DFS_LEN], char file[]);
int hashmod(FILE *f);

void menu()
{
    printf("===== Please select an option =====\n");
    printf("1) Get <file>\n");
    printf("2) List\n");
    printf("3) Put <file>\n");
    printf("4) Quit\n");
}

void sendPutCommand(DFS dfs[DFS_LEN], int index, int hash, char file[], unsigned long file_size)
{
    char buf[MAXBUF];
    for (int i = 0; i < DFS_LEN; i++)
    {
        unsigned long relative_len = (file_size + ((file_size + i)) % 4) / 4;
        sprintf(buf, "%s %s %s %s %lu\n", "put", creds.username, creds.password, file, relative_len);
        // printf("%s\n", buf);
        write(dfs[table[hash][i][index] - 1].connfd, buf, (strlen(buf) >= MAXBUF) ? MAXBUF : strlen(buf) + 1);
        // if (write(dfs[table[hash][i][index] - 1].connfd, buf, (strlen(buf) >= MAXBUF) ? MAXBUF : strlen(buf) + 1) <= 0)
        // {
        //     printf("Skipped server %s\n", dfs[table[hash][i][index]].server_ident);
        // }
    }
}

int recvHashAck(DFS dfs[DFS_LEN], int hash, int index)
{
    int resp = 0, temp;
    ssize_t t;
    for (int i = 0; i < DFS_LEN; i++)
    {
        int idx = table[hash][i][index] - 1;
        if ((t = read(dfs[idx].connfd, &temp, sizeof(char))) > 0)
        {
            resp |= temp;
        }
    }
    return resp;
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

    // signal(SIGPIPE, catch_sigpipe);
    signal(SIGPIPE, SIG_IGN);

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
            get(dfs, file);
            return 0;
            break;
        case '2':
        case 'L':
        case 'l':
            freeCreds(head);
            head = 0;
            sendCommand(dfs, "list", "", 0);
            if (recvAck(dfs))
            {
                recvList(dfs);
                printList();
            }
            else
            {
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
                int hash = hashmod(f);
                for (int iterations = 0; iterations < 2; iterations++) {
                    sendPutCommand(dfs, iterations, hash, file, filesize);
                    // sendCommand(dfs, "put", file, filesize);
                    if (recvHashAck(dfs, hash, iterations))
                    {
                        sendFile(dfs, f, filesize);
                    }
                    else
                    {
                        printf("Did not present valid creds!\n");
                    }
                }
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
            }
            else
            { // connection worked
                struct timeval timeout;
                timeout.tv_sec = 1;
                timeout.tv_usec = 0;
                if (setsockopt(d->connfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
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
    char part;
    int hash = hashmod(f);
    
    for (int i = 0; i < DFS_LEN; i++)
    {
        part = i;
        int idx = table[hash][i][0] - 1;
        // printf("Idx: %d, part: %d\n", idx, (int) part);
        write(dfs[idx].connfd, &part, sizeof(char));
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
                // printf("Relative length: %lu\n", relative_len);
                write(dfs[idx].connfd, buf, read);
                break;
            }
            else
            {
                break;
            }
        }
    }
}

int writeAll(char buf[], int len, int connfd)
{
    size_t pos = 0;
    ssize_t sent;
    while (len)
    {
        if ((sent = write(connfd, buf + pos, len)) != -1)
        {
            len -= sent;
            pos += sent;
        }
        else
        {
            return 0;
        }
    }
    return pos;
}

void catch_sigpipe(int signo)
{
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
            if (t != -1)
            {
                resp |= temp;
            }
        }
    }
    return resp;
}

void recvList(DFS dfs[DFS_LEN])
{
    char buf[MAXBUF], fname[100];
    int len, part;
    ssize_t t, pos;
    for (int i = 0; i < DFS_LEN; i++)
    {
        pos = 0;
        if ((t = read(dfs[i].connfd, buf, MAXBUF)) != -1)
        {
            while (t && t != pos)
            {
                sscanf(buf + pos, "%*c%s ", fname);
                len = strlen(fname);
                pos += len + 2;
                part = atoi(fname + len - 1);
                fname[len - 2] = '\0';
                FNODE *x, *f = createCred(fname);
                if ((x = fileExists(f)))
                {
                    free(f);
                    f = x;
                }
                else
                {
                    insertNode(f);
                }
                f->part_loc[part] = dfs[i].connfd;
            }
        }
    }
}

int insertNode(FNODE *f)
{
    if (fileExists(f))
    {
        return false;
    }
    else
    {
        f->next = head;
        head = f;
        return true;
    }
}

FNODE *fileExists(FNODE *f)
{
    FNODE *t_user = head;
    while (t_user)
    {
        if (!strcmp(t_user->file_name, f->file_name))
        {
            return t_user;
        }
        else
        {
            t_user = t_user->next;
        }
    }
    return 0;
}

FNODE *createCred(char fname[100])
{
    FNODE *c = (FNODE *)malloc(sizeof(FNODE));
    strncpy(c->file_name, fname, 99);
    for (int i = 0; i < DFS_LEN; i++)
    {
        c->part_loc[i] = -1;
    }
    return c;
}

void freeCreds(FNODE *cn)
{
    if (cn)
    {
        freeCreds(cn->next);
        free(cn);
    }
}

void printList()
{
    FNODE *t_user = head;
    while (t_user)
    {
        if (
            t_user->part_loc[0] != -1 &&
            t_user->part_loc[1] != -1 &&
            t_user->part_loc[2] != -1 &&
            t_user->part_loc[3] != -1)
        {
            printf("%s\n", t_user->file_name);
        }
        else
        {
            printf("%s [incomplete]\n", t_user->file_name);
        }
        t_user = t_user->next;
    }
}

FNODE *findNode(char fname[100])
{
    FNODE *t_user = head;
    while (t_user)
    {
        if (!strcmp(t_user->file_name, fname))
        {
            return t_user;
        }
        else
        {
            t_user = t_user->next;
        }
    }
    return 0;
}

void receiveFile(FILE *f, size_t file_len, int connfd)
{
    char buf[MAXBUF];
    size_t r, written;
    if (f)
    {
        // while (file_len)
        // {
            if ((r = read(connfd, buf, file_len)) > 0)
            {
                written = fwrite(buf, 1, r, f);
                file_len -= written;
            }
            else
            {
                fprintf(stderr, "Failed to read anything\n");
                // break;
            }
        // }
    }
}

void sendOneCommand(int connfd, char file[])
{
    char buf[MAXBUF];
    sprintf(buf, "get %s %s %s\n", creds.username, creds.password, file);
    for (int i = 0; i < DFS_LEN; i++)
    {
        write(connfd, buf, (strlen(buf) >= MAXBUF) ? MAXBUF : strlen(buf) + 1);
    }
}

int recvOneAck(int connfd)
{
    int resp = 0, temp;
    ssize_t t;
    if ((t = read(connfd, &temp, sizeof(char))))
    {
        if (t != -1)
        {
            resp |= temp;
        }
    }
    return resp;
}

void get(DFS dfs[DFS_LEN], char file[])
{
    char buf[MAXBUF];

    // First get the current list info
    // ===============================
    freeCreds(head);
    head = 0;
    sendCommand(dfs, "list", "", 0);
    if (recvAck(dfs))
    {
        recvList(dfs);
        // ===============================
        FNODE *f = findNode(file);
        if (
            f &&
            f->part_loc[0] != -1 &&
            f->part_loc[1] != -1 &&
            f->part_loc[2] != -1 &&
            f->part_loc[3] != -1)
        {
            FILE *fout = fopen(f->file_name, "wb");
            if (fout)
            {
                unsigned long size;
                for (int i = 0; i < DFS_LEN; i++)
                {
                    sprintf(buf, ".%s.%d", file, i);
                    sendOneCommand(f->part_loc[i], buf);
                    recvOneAck(f->part_loc[i]);
                    if (read(f->part_loc[i], &size, MAXBUF) > 0)
                    {
                        if (size) receiveFile(fout, size, f->part_loc[i]);
                    }
                }
                fclose(fout);
            }
        }
        else
        {
            printf("The file { %s } is unavailible!\n", file);
            return;
        }
    } else {
        printf("Your credentials are invalid!\n");
    }
}

int hashmod(FILE *f) {
    unsigned char c[MD5_DIGEST_LENGTH];
    MD5_CTX mdContext;
    int bytes;
    unsigned char data[1024];

    MD5_Init (&mdContext);
    while ((bytes = fread (data, 1, 1024, f)) != 0)
        MD5_Update (&mdContext, data, bytes);
    MD5_Final (c,&mdContext);

    rewind(f);

    return c[MD5_DIGEST_LENGTH - 1] % 4;
}