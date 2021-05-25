#Makefile per la compilazione del progetto
CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I .

LIBPATH = -L ./libs

UTILS   = libs/libutilities.so
HTABLE  = libs/libicl_hash.so

.PHONY: all clean test1 cleanall fs-api server client

all: $(UTILS) $(HTABLE)
	#crea (se non esistono) le directory per i file oggetto e le librerie condivise
	-mkdir objs libs
	$(MAKE) -C fs-api
	$(MAKE) -C fs-server
	$(MAKE) -C client
$(UTILS): utils.o
	$(CC) $(CFLAGS) -shared -o $@ $< -lpthread
	mv $< objs/
utils.o: utils.c utils.h
	$(CC) $(CFLAGS) $(HEADERS) -c -fPIC -o $@ $< -lpthread

$(HTABLE): icl_hash.o
	$(CC) $(CFLAGS) -shared -o $@ $<
	mv $< objs/
icl_hash.o: icl_hash/icl_hash.c
	$(CC) $(CFLAGS) $(HEADERS) -c -fPIC -o $@ $<

clean:
	-rm -fr $(wildcard ./*.out)
cleanall: clean
	-rm -fr $(wildcard libs/*.so) $(wildcard objs/*.o)

test1:
	chmod +x test1.sh
	./test1.sh
