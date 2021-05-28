CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I . -I .. -I ../icl_hash -I ../fs-api

LIBPATH = -Wl,-rpath=./libs -L ../libs
LIBS = -lfs-api -lutilities -licl_hash -lpthread

SERVER = ../fs-server.out

$(SERVER): server.o worker.o api_backend.o api_backend-utils.o term_thread.o sock_init.o server-ds.o parse_config.o logging.o
	$(CC) $(CFLAGS) $(HEADERS) $(LIBPATH) -o $@ $^ $(LIBS)
	-mv $(wildcard *.o ../objs)
server.o: server.c
	$(CC) $(CFLAGS) $(HEADERS) $(LIBPATH) -c -o $@ $<
worker.o: worker.c
	$(CC) $(CFLAGS) $(HEADERS) $(LIBPATH) -c -o $@ $<
api_backend.o: api_backend.c
	$(CC) $(CFLAGS) $(HEADERS) $(LIBPATH) -c -o $@ $^
api_backend-utils.o: api_backend-utils.c
	$(CC) $(CFLAGS) $(HEADERS) $(LIBPATH) -c -o $@ $^
term_thread.o: term_thread.c
	$(CC) $(CFLAGS) $(HEADERS) $(LIBPATH) -c -o $@ $<
sock_init.o: sock_init.c
	$(CC) $(CFLAGS) $(HEADERS) $(LIBPATH) -c -o $@ $<
server-ds.o: server-ds.c
	$(CC) $(CFLAGS) $(HEADERS) $(LIBPATH) -c -o $@ $<
parse_config.o: parse_config.c
	$(CC) $(CFLAGS) $(HEADERS) $(LIBPATH) -c -o $@ $<
logging.o: logging.c
	$(CC) $(CFLAGS) $(HEADERS) $(LIBPATH) -c -o $@ $<
