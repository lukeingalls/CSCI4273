CC=gcc
EXE=server
CFLAGS=-Wall -pthread

all: $(EXE)

server: server.c
	$(CC) server.c $(CFLAGS) -o $(EXE)

debug: server.c
	$(CC) server.c $(CFLAGS) -g -o $(EXE)