// header client
#include <utils.h>
#include <client.h>
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

void init_params(struct client_opts *params);


// Funzione che processa argv per ottenere le opzioni passate al client (usa la funzione getopt)
// Ritorna 0 se ha successo (e riempie i campi della struttura params)
// Altrimenti ritorna -1
int get_client_options(int nargs, char **args, struct client_opts *params) {
    // inizializzo la struttura con i valori di default
    init_params(params);

    if(nargs > 1) {
	// disabilita la scrittura su standard error di eventuali errori di parsing delle opzioni
	opterr = 0;

	// processa tutti gli argomenti, tranne -W, -r, -l, -u, -c
	int i = 1;
	int opchar;
	while((opchar = getopt(nargs, args, CLIENT_OPSTRING) != -1) {
	    switch(opchar) {
		case 'h': // trovata l'opzione -h, quindi sarà mostrato il messaggio di help
		    params->help_on = 1;
		    break;
		case 'f': // l'argomento di f è il path del socket per comunicare con il server
		    if(string_dup(params->fs_socket, optarg) == -1) {
			// errore nella duplicazione
			return -1;
		    }
		    break;
		case 'w': // l'argomento di w è la directory in cui cercare i file da scrivere
		    // il numero massimo di file da scrivere è determinato successivamente
		    if(string_dup(params->dir_write, optarg) == -1) {
			// errore nella duplicazione
			return -1;
		    }
		    break;
		case 'D': // l'argomento di -D è la directory in cui salvare i file vittima dello swapout
		    // siccome deve essere usata insieme a -w o -W la validazione di
		    // questa opzione avviene solo quando tutti gli argomenti sono stati processati
		    if(string_dup(params->dir_swapout, optarg) == -1) {
			// errore nella duplicazione
			return -1;
		    }
		    break;
		case 'R': // l'argomento di -R è il numero massimo di file da leggere
		    int retcode = isNumber(optarg, &(params->max_read));
		    if(retcode == 1) {
			// optarg non è un numero
			// TODO: qualche messaggio di errore
			return -1;
		    }
		    if(retcode == 2) {
			// optarg è un numero, ma causa overflow/underflow
			// TODO: qualche messaggio di errore
			return -1;
		    }

	    }
	}

	// abilita nuovamente l'output degli errori di getopt su stderr
	opterr = 1;
    }

    return 0;
}

void init_params(struct client_opts *params) {
    if(params) {
	// prima resetto tutto a zero
	memset(params, 0, sizeof(*params));
	// poi setto alcuni parametri al valore di default
	params->help_on = 0;
	params->max_write = -1;
	params->max_read = -1;
	params->prints_on = 0;
    }
    // TODO: riporta errore
}
