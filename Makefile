#Makefile per la compilazione del progetto

CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I .

SERVER = fs-server.out
CLIENT = client.out
API = libfs-api.so

.PHONY: all clean cleanall

all: $(SERVER) $(API)

$(SERVER): parse_config.c server.c utils.c logging.c
	$(CC) $(CFLAGS) $(HEADERS) -o $@ $^

$(API): fs-api.c fs-api.h
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $<
