#Makefile per la compilazione del progetto
CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I .

LIBPATH = -L ./libs

UTILS = libs/libutilities.so

.PHONY: all clean cleanall server fs-api

all: $(UTILS) fs-api server
	cd fs-api && $(MAKE)
	cd fs-server && $(MAKE)
	cd client && $(MAKE)
$(UTILS): utils.o
	$(CC) $(CFLAGS) -shared -o $@ $< -lpthread
utils.o: utils.c utils.h
	$(CC) $(CFLAGS) $(HEADERS) -c -fPIC -o $@ $< -lpthread
clean:
	-rm -fr $(wildcard *.out)
cleanall: clean
	-rm $(wildcard libs/*.so)

