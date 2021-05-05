// header progetto
#include <server-utils.h>
// multithreading headers
#include <pthread.h>
// system call headers
#include <sys/types.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

// Questo file contiene l'implementazione della funzione che effettua il parsing del file
// di configurazione "config.txt" e riempe una struttura contenente tutti i parametri
// del server settati opportunamente
int parse_config(struct serv_params *params) {
	// apro il file di configurazione in lettura, usando il path definito nell'header server-utils.h
	int conf_fd = -1;
	if((conf_fd = open(CONF_PATH, O_RDONLY)) == -1) {
		// errore nell'apertura del file
		// TODO: scrittura nel file di log
		return -1;
	}

	// alloco un buffer per la lettura
	char *buf = malloc(BUF_BASESZ * sizeof(char));
	if(!buf) {
		// errore nell'allocazione di memoria
		// TODO: scrittura nel file di log
		return -1;
	}
	int buf_sz = BUF_BASESZ; // questa variabile manterrà la dimensione del buffer

	int bytes_read = 0;
	// parsing secondo il formato specificato
	while((bytes_read = read(conf_fd, buf, buf_sz)) == -1) { // TODO: readn?
		if(rialloca_buffer(&buf, buf_sz * 2) == -1) {
			// errore nella riallocazione del buffer
			// TODO: scrittura nel file di log
			// TODO: cleanup function
			return -1;
		}
	}

	return 0;
}
