#Makefile per la compilazione del progetto
CC = gcc -std=c99 -D_GNU_SOURCE=1
CFLAGS = -g -Wall -pedantic
HEADERS = -I .
# directory in cui sono create le librerie condivise
LIBPATH = -L ./libs
# path delle librerie condivise per hash table e utilità
UTILS   = libs/libutilities.so
HTABLE  = libs/libicl_hash.so

.PHONY: all shell_perms dirs clean cleanall test1 test2 test3

all: shell_perms dirs $(UTILS) $(HTABLE)
	# Creo il file di configurazione di default del server
	./makeconf.sh 10 32 100 server.sock server.log config.conf
	# Eseguo make nelle sudirectory contenenti il codice (hanno il proprio Makefile)
	$(MAKE) -C fs-api
	$(MAKE) -C fs-server
	$(MAKE) -C client

$(UTILS): utils.o
	$(CC) $(CFLAGS) -shared -o $@ $< -lpthread
	# sposto i file oggetto creati
	mv $< objs/
utils.o: utils.c utils.h dirs
	$(CC) $(CFLAGS) $(HEADERS) -c -fPIC -o $@ $< -lpthread

$(HTABLE): icl_hash.o
	$(CC) $(CFLAGS) -shared -o $@ $<
	# sposto i file oggetto creati
	mv $< objs/
icl_hash.o: icl_hash/icl_hash.c dirs
	$(CC) $(CFLAGS) $(HEADERS) -c -fPIC -o $@ $<

shell_perms: makeconf.sh test1.sh test2.sh test3.sh
	# Aggiunge permesso di esecuzione agli script
	chmod +x $^

dirs:
	#crea (se non esistono) le directory per i file oggetto e le librerie condivise
	-mkdir objs libs

clean:
	# rimuove gli eseguibili (se presenti)
	-rm -fr $(wildcard ./*.out)

cleanall: clean
	# rimuove tutto quello che è stato generato
	-rm -fr $(wildcard libs/*.so) $(wildcard objs/*.o)
	-rm -fr $(wildcard *.conf) $(wildcard ./*.log) $(wildcard ./*.sock)
	-rm -fr save_reads save_writes
test1: all
	# Creo il file di configurazione del server per il test1
	./makeconf.sh 1 128 10000 test1.sock test1.log test1.conf
	# Lancio il server in background
	valgrind --track-origins=yes --leak-check=full --show-leak-kinds=all ./fs-server.out -f test1.conf &
	# Lancio i client che testano le operazioni del server
	chmod +x test1.sh
	./test1.sh test1.sock
test2: all
	chmod +x makeconf.sh
	# Creo il file di configurazione del server per il test2
	./makeconf.sh 4 1 10 test2.sock test2.log test2.conf
	# Lancio il server in background
	./fs-server.out -f test2.conf &
	# Lancio i client che testano le operazioni del server
	chmod +x test2.sh
	./test2.sh test2.sock
test3: all
	chmod +x makeconf.sh
	# Creo il file di configurazione del server per il test3
	./makeconf.sh 8 32 100 test3.sock test3.log test3.conf
	# Lancio il server in background
	./fs-server.out -f test3.conf &
	# Lancio i client che testano le operazioni del server
	chmod +x test3.sh
	./test1.sh test3.sock
