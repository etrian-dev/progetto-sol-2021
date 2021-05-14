CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I .. -I .

LIBPATH = -Wl,-rpath=./libs -L ../libs
LIBS = -lfs-api -lutilities -lpthread

SERVER = ../fs-server.out

$(SERVER): parse_config.c server.c logging.c worker.c queue.c
	$(CC) $(CFLAGS) $(HEADERS) $(LIBPATH) -o $@ $^ $(LIBS)
