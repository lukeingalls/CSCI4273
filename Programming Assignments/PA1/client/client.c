/* 
 * udpclient.c - A simple UDP client
 * usage: udpclient <host> <port>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

#define BUFSIZE 1024

/* 
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
}

/*
 * sendAllTo - wraps the sendto function and ensures that all data is transmitted (or results in an error)
 */
int sendAllTo(int sockfd, char *buf, int buf_len, int flags, struct sockaddr_in * sockAddr, socklen_t addrlen) {
    int n;
    int total = 0;
    while (total < buf_len) {
        n = sendto(sockfd, buf + total, buf_len - total, 0, (struct sockaddr *) sockAddr, addrlen);
        if (n < 0) { break; }
        total += n;
    }

    buf_len = total;

    return (n==-1) ? -1 : 0;
}

int main(int argc, char **argv) {
    int sockfd, portno, n;
    int serverlen;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char *hostname;
    char buf[BUFSIZE], parsed_command[10];
    /* check command line arguments */
    if (argc != 3) {
       fprintf(stderr,"usage: %s <hostname> <port>\n", argv[0]);
       exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr)); // Set all of server addr to 0
    serveraddr.sin_family = AF_INET; // Set family
    bcopy((char *)server->h_addr, 
	  (char *)&serveraddr.sin_addr.s_addr, server->h_length); // Copy server->h_addr to serveraddr.sin_addr.s_addr (length: server->h_length)
    serveraddr.sin_port = htons(portno); // copy port to serveraddr
    serverlen = sizeof(serveraddr);

    while (1) {
        /* get a message from the user */
        bzero(buf, BUFSIZE); //reset the buffer
        printf("Command: ");
        fgets(buf, BUFSIZE, stdin);

        sscanf(buf, "%s ", parsed_command);
        // /* send the message to the server */
        n = sendAllTo(sockfd, buf, strlen(buf), 0, &serveraddr, serverlen);
        if (n < 0) {
            error("Error in sendAllTo");
        }
        if (!strcmp("get", parsed_command)) {
        } else if (!strcmp("put", parsed_command)) {
        } else if (!strcmp("delete", parsed_command)) {
        } else if (!strcmp("ls", parsed_command)) {
            n = recvfrom(sockfd, buf, BUFSIZE, 0, &serveraddr, &serverlen);
            printf("Received %d bytes\n", strlen(buf));
            if (n < 0) error("ERROR in recvfrom");
            printf("Received message from server:\n%s\n", buf);
        } else if (!strcmp("exit", parsed_command)) {
            printf("Good bye!\n");
            close(sockfd);
            break;
        } else {
            n = recvfrom(sockfd, buf, BUFSIZE, 0, &serveraddr, &serverlen);
            if (n < 0) error("ERROR in recvfrom");
            printf("Received message from server:\n%s\n", buf);
        }
        /* print the server's reply */

    }
    return 0;
}
