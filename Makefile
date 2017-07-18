all: server client
CFLAGS=--std=c99 -Wall -Wextra $(shell pkg-config --cflags libczmq)
LOADLIBES=$(shell pkg-config --libs libczmq)
server: server.o zsimpledisco.o
client: client.o zsimpledisco.o
