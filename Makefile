all: server client
CFLAGS=--std=c99 -Wall -Wextra $(shell pkg-config --cflags libczmq)
LOADLIBES=$(shell pkg-config --libs libczmq)
server: server.o server_cmd.o keygen_cmd.o zsimpledisco.o
client: client.o zsimpledisco.o

server.static:
	cc -o server server.c server_cmd.c zsimpledisco.c -static-libstdc++ -static -static-libgcc -Wall -Wextra -DCZMQ_BUILD_DRAFT_API=1 -DZMQ_BUILD_DRAFT_API=1 $(shell pkg-config --cflags --libs libczmq) -l pthread -lstdc++ -lm
