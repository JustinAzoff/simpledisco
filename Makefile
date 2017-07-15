all: server client
CFLAGS=-Wall -Wextra $(shell pkg-config --cflags libczmq) --std=c99
LDFLAGS= $(shell pkg-config --libs libczmq)
server: server.c zsimpledisco.c
client: client.c zsimpledisco.c
