#Makefile per la compilazione del progetto
CC = gcc
CFLAGS = -g -Wall -pedantic
HEADERS = -I .

LIBPATH = -L ./libs

UTILS   = libs/libutilities.so
HTABLE  = libs/libicl_hash.so

.PHONY: all clean cleanall test1 test2 test3

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
	-rm -fr $(wildcard libs/*.so) $(wildcard objs/*.o) $(wildcard *.conf)

test1: all
	chmod +x makeconf.sh
	# Creo il file di configurazione del server
	./makeconf.sh 1 128 10000 test1.sock test1.log test1.conf
	# Lancio il server in background
	valgrind -q --leak-check=full --show-leak-kinds=all ./fs-server.out -f test1.conf &
	# Lancio i client che testano le operazioni del server
	chmod +x test1.sh
	./test1.sh test1.sock
test2: all
	chmod +x makeconf.sh
	# Creo il file di configurazione del server
	./makeconf.sh 1 128 10000 test1.sock test1.log test1.conf
	# Lancio il server in background
	valgrind -q --leak-check=full --show-leak-kinds=all ./fs-server.out -f test1.conf &
	# Lancio i client che testano le operazioni del server
	chmod +x test1.sh
	./test1.sh test1.sock
test3: all
	chmod +x makeconf.sh
	# Creo il file di configurazione del server
	./makeconf.sh 1 128 10000 test1.sock test1.log test1.conf
	# Lancio il server in background
	valgrind -q --leak-check=full --show-leak-kinds=all ./fs-server.out -f test1.conf &
	# Lancio i client che testano le operazioni del server
	chmod +x test1.sh
	./test1.sh test1.sock
