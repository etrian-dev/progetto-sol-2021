#Makefile per la compilazione del progetto
CC = gcc -std=c99 -D_GNU_SOURCE=1
CFLAGS = -g -Wall -pedantic
HEADERS = -I .
# directory in cui sono create le librerie condivise
LIBPATH = -L ./libs
# path delle librerie condivise per hash table e utilità
UTILS   = libs/libutilities.so
HTABLE  = libs/libicl_hash.so

.PHONY: all shell_perms dirs clean cleanall test1 test2 doc

all: shell_perms dirs $(UTILS) $(HTABLE)
	# Creo il file di configurazione di default del server
	./makeconf.sh 10 32 100 server.sock server.log config.conf
	# Eseguo make nelle subdirectory contenenti il codice (hanno il proprio Makefile)
	$(MAKE) -C fs-api
	$(MAKE) -C fs-server
	$(MAKE) -C client

$(UTILS): utils.o
	$(CC) $(CFLAGS) -shared -o $@ $< -lpthread
utils.o: utils.c utils.h
	$(CC) $(CFLAGS) $(HEADERS) -c -fPIC -o $@ $< -lpthread

$(HTABLE): icl_hash.o
	$(CC) $(CFLAGS) -shared -o $@ $<
icl_hash.o: icl_hash/icl_hash.c
	$(CC) $(CFLAGS) $(HEADERS) -c -fPIC -o $@ $<

shell_perms: makeconf.sh test1.sh test2.sh statistiche.sh
	# Aggiunge permesso di esecuzione agli script
	chmod +x $^

dirs:
	#crea (se non esistono) le directory per i file oggetto e le librerie condivise
	-mkdir libs
clean:
	# rimuove gli eseguibili (se presenti)
	-rm -fr $(wildcard ./*.out)

cleanall: clean
	# rimuove tutto quello che è stato generato: file oggetto, file di log, socket...
	-rm -fr $(wildcard *.o) $(wildcard fs-server/*.o) $(wildcard fs-api/*.o) $(wildcard client/*.o) $(wildcard libs/*.so)
	-rm -fr $(wildcard *.conf) $(wildcard ./*.log) $(wildcard ./*.sock)
	-rm -fr save_reads save_writes save_writes_2 libs
test1: all
	# Creo il file di configurazione del server per il test1
	./makeconf.sh 1 128 10000 test1.sock test1.log test1.conf
	# testo il server tramite questo script
	./test1.sh test1.sock
test2: all
	# Creo il file di configurazione del server per il test2
	./makeconf.sh 4 1 10 test2.sock test2.log test2.conf
	# testo il server tramite questo script
	./test2.sh test2.sock
doc: doxygen-config
	doxygen $<
