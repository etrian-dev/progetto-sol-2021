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

#define NPOS_DFL 10

// inizializza i parametri a valori di default
void init_params(struct client_opts *params) {
    if(params) {
	// prima resetto tutto a zero
	memset(params, 0, sizeof(*params));
	// I valori numerici di default in questo caso sono tutti 0 => inutile settarli per via della memset
	// alloco puntatori per file da leggere/scrivere etc riservando di default un certo numero di posizioni
	params->write_list = calloc(NPOS_DFL, sizeof(char*));
	params->read_list = calloc(NPOS_DFL, sizeof(char*));
	params->lock_list = calloc(NPOS_DFL, sizeof(char*));
	params->unlock_list = calloc(NPOS_DFL, sizeof(char*));
	params->rm_list = calloc(NPOS_DFL, sizeof(char*));
	// un eventuale errore di allocazione degli array non comporta errori fatali in quanto
	// rimangono comunque NULL i puntatori, per cui devo solo assicurarmi di allocarli al bisogno
    }
    // TODO: riporta errore
}


// Funzione che processa argv per ottenere le opzioni passate al client (usa la funzione getopt)
// Ritorna 0 se ha successo (e riempie i campi della struttura params)
// Altrimenti ritorna -1
int get_client_options(int nargs, char **args, struct client_opts *params) {
    // faccio una copia degli argomenti
    char **args_dup = calloc(nargs, sizeof(char*));
    if(!args_dup) {
	// errno settato da calloc
	return -1;
    }

    int i;
    for(i = 0; i < nargs; i++) {
	args_dup[i] = strndup(args[i], strlen(args[i]));
	if(!args_dup) {
	    // TODO: funzione cleanup
	    return -1;
	}
    }

    // inizializzo la struttura con i valori di default
    init_params(params);

    // flag per determinare se determinate opzioni sono ammissibili in funzione di altre
    int has_w, has_r;
    has_w = has_r = 0;

    if(nargs > 1) {
	// processa tutti gli argomenti, tranne -W, -r, -l, -u, -c
	int i = 1;
	int opchar;
	int retcode;
	while((opchar = getopt(nargs, args, CLIENT_OPSTRING)) != -1) {
	    switch(opchar) {
		case 'h': // trovata l'opzione -h, quindi sarà mostrato il messaggio di help
		    params->help_on = 1;
		    break;
		case 'f': // l'argomento di f è il path del socket per comunicare con il server
		    if(string_dup(&(params->fs_socket), optarg) == -1) {
			// errore nella duplicazione
			return -1;
		    }
		    break;
		case 'w': // l'argomento di w è la directory in cui cercare i file da scrivere
		    // il numero massimo di file da scrivere è determinato successivamente
		    if(string_dup(&(params->dir_write), optarg) == -1) {
			// errore nella duplicazione
			return -1;
		    }
		    // Esiste almeno una richiesta di scrittura => Se compare -D è ok
		    has_w = 1;
		    break;
		case 'D': // l'argomento di -D è la directory in cui salvare i file vittima dello swapout
		    // siccome deve essere usata insieme a -w o -W la validazione di
		    // questa opzione avviene solo quando tutti gli argomenti sono stati processati
		    if(string_dup(&(params->dir_swapout), optarg) == -1) {
			// errore nella duplicazione
			return -1;
		    }
		    break;
		case 'd': // l'argomento di -d è la directory in cui salvare i file letti dal server
		    // siccome deve essere usata insieme a -r o -R la validazione di
		    // questa opzione avviene solo quando tutti gli argomenti sono stati processati
		    if(string_dup(&(params->dir_save_reads), optarg) == -1) {
			// errore nella duplicazione
			return -1;
		    }
		    break;
		case 'R': // l'argomento di -R è il numero massimo di file da leggere
		    // Se l'argomento non è fornito (optarg NULL) rimane al valore di default (-1)
		    retcode = isNumber(optarg, &(params->max_read));
		    if(retcode == 1) {
			// optarg non è un numero intero
			printf("\"-R %s\" non è corretta: \"%s\" non è un numero\n", optarg, optarg);
			return -1;
		    }
		    if(retcode == 2) {
			// optarg è un numero, ma causa overflow/underflow
			printf("\"-R %s\" non è corretta: \"%s\" causa overflow/underflow\n", optarg, optarg);
			return -1;
		    }
		    if(params->max_read < 0) {
			// optarg è un numero intero, ma negativo e quindi non posso accettarlo
			printf("\"-R %s\" non è corretta: l'argomento deve essere >= 0\n", optarg);
			return -1;
		    }
		    // Esiste almeno una richiesta di lettura => Se compare -d è ok
		    has_r = 1;
		    break;
		case 't': // l'argomento (intero, positivo) di -t è il delay tra le richieste
		    retcode = isNumber(optarg, &(params->rdelay));
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
		    break;
		case 'p': // la presenza dell'opzione -p fa stampare al client tutte le operazioni su stdout
		    params->prints_on = 1;
		    break;
		// situazioni di errore
		case ':': // parametro mancante in un'opzione che lo richiede
		    if(optopt != 'R') { // il parametro per -R è opzionale
			printf("Parametro mancante per l'opzione %c\n", optopt);
			return -1;
		    }
		    break;
		default: // opzione non riconosciuta
		    printf("Opzione %c non riconosciuta\n", optopt);
		    return -1;
	    }
	}

	// TODO: parsing di -W, -r, -l, -u, -c, che sono state permutate al termine di argv
	// TODO: cleanup degli argomenti parzialmente processati se vi sono errori in process_filelist
	i = 0;
	int update_i;
	while(i < nargs) {
	    size_t arg_len = strlen(args[i]);
	    if(strncmp(args[i], "-W", arg_len) == 0) {
		// -W prende in input una lista di file (almeno uno) da scrivere in write_arr
		// ed opzionalmente al termine un intero n
		; // gestisco in modo diverso dagli altri
	    }
	    else if(strncmp(args[i], "-r", arg_len) == 0) {
		if((update_i = process_filelist(params->read_list, args_dup, i + 1)) == -1) {
		    // errore da riportare su stderr, ma continuo a processare
		    fprintf(stderr, "Errore: %s\n", strerror(errno)); // TODO: migliorare
		}
		i = update_i;
		// Esiste almeno una richiesta di lettura => Se compare -d è ok
		has_r = 1;
	    } else if(strncmp(args[i], "-l", arg_len) == 0) {
		if((update_i = process_filelist(params->lock_list, args_dup, i + 1)) == -1) {
		    // errore da riportare su stderr, ma continuo a processare
		    fprintf(stderr, "Errore: %s\n", strerror(errno)); // TODO: migliorare
		}
		i = update_i;
	    } else if(strncmp(args[i], "-u", arg_len) == 0) {
		if((update_i = process_filelist(params->unlock_list, args_dup, i + 1)) == -1) {
		    // errore da riportare su stderr, ma continuo a processare
		    fprintf(stderr, "Errore: %s\n", strerror(errno)); // TODO: migliorare
		}
		i = update_i;
	    }
	    else if(strncmp(args[i], "-c", arg_len) == 0) {
		if((update_i = process_filelist(params->rm_list, args_dup, i + 1)) == -1) {
		    // errore da riportare su stderr, ma continuo a processare
		    fprintf(stderr, "Errore: %s\n", strerror(errno)); // TODO: migliorare
		}
		i = update_i;
	    }
	    i++;
	}

	//// -D può essere non nullo sse compaiono richieste di scrittura
	//if(params->dir_swapout && has_w) {
	    //// errore nello specificare le opzioni: lo comunico all'utente e stampo messaggio di help
	    //params->help_on = 1;
	    //return -1;
	//}
	//// Stessa cosa, ma per lettura
	//if(params->dir_save_reads && has_r) {
	    //// errore nello specificare le opzioni: lo comunico all'utente e stampo messaggio di help
	    //params->help_on = 1;
	    //return -1;
	//}
    }

    return 0;
}

int process_filelist(char **files, char **args, const int ipos) {
    // Se l'array files non era stato allocato (quindi deve essere NULL) lo alloco adesso con NPOS_DFL posizioni
    if(!files) {
	files = calloc(NPOS_DFL, sizeof(char*));
	// Se anche adesso non sono in grado di allocare memoria allora non era un fallimento temporaneo
	// quindi riporto l'errore (il main del client terminerà)
	if(!files) {
	    // errore (fatale) di allocazione
	    return -1;
	}
    }

    // TODO: magari controllare che ipos sia un indice valido
    int i = ipos;
    // leggo stringhe da argv dopo quella contenente l'opzione finchè
    // non ho terminato argv o la stringa considerata non è un'altra opzione
    while(args[i] && strchr(args[i], '-') == NULL) {
	// duplico l'argomento
	files[i] = strndup(args[i], strlen(args[i]));
	if(files[i] == NULL) {
	    // errore nella duplicazione dell'argomento
	    return -1;
	}

	// TODO: per il momento assumo un numero max di parametri pari a NPOS_DFL
	// In seguito potrei prevedere gestione dell'espansione dinamica dell'array

	i++;
    }

    // Tutti i path letti con successo => ritorno l'indice della prossima opzione
    // da cui riparte il parsing
    return i - 1;
    // ritorno i - 1 in quanto get_client_options incrementerà i prima di testare nuovamente la condizione
    // TODO: magari controintuitivo e cambiare comportamento del loop di get_client_options
}
