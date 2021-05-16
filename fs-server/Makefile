CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I . -I .. -I ../icl_hash

LIBPATH = -Wl,-rpath=./libs -L ../libs
LIBS = -lfs-api -lutilities -licl_hash -lpthread

SERVER = ../fs-server.out

$(SERVER): parse_config.c logging.c worker.c api_backend.c server-ds.c server.c
	$(CC) $(CFLAGS) $(HEADERS) $(LIBPATH) -o $@ $^ $(LIBS)
