// header client
#include <utils.h>
#include <client.h>
#include <fs-api.h> // per le definizioni dei caratteri corrispondenti alle operazioni
// system call headers
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <dirent.h> // per visita directory
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
	params->nread = -1;

	params->oplist = calloc(1, sizeof(struct Queue));
	// un eventuale errore di allocazione non comporta errori fatali in quanto
	// rimangono comunque NULL i puntatori, per cui è necessario soltanto controllare
	// di allocarli prima di usarli
    }
}

// Funzione che processa argv per ottenere le opzioni passate al client (usa la funzione getopt)
// Ritorna 0 se ha successo (e riempie i campi della struttura params)
// Altrimenti ritorna -1
int get_client_options(int nargs, char **args, struct client_opts *params) {
    // inizializzo la struttura con i valori di default ed alloca code
    init_params(params);

    // flag per determinare se determinate opzioni sono ammissibili in funzione di altre
    int has_w, has_r;
    has_w = has_r = 0;

    if(nargs > 1) {
	// processa tutte le opzioni da riga di comando
	int opchar;
	int retcode;
	while((opchar = getopt(nargs, args, CLIENT_OPSTRING)) != -1) {
	    switch(opchar) {
		case 'h': // trovata l'opzione -h, quindi sarà mostrato il messaggio di help
		    params->help_on = 1;
		    break;
		case 'f': // l'argomento di f è il path del socket per comunicare con il server
		    if(string_dup(&(params->fs_socket), optarg) == -1) {
			// errore nella duplicazione del path
			return -1;
		    }
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
		    retcode = isNumber(optarg, &(params->nread));
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
		    if(params->nread < 0) {
			// optarg è un numero intero, ma negativo e quindi non posso accettarlo
			printf("\"-R %s\" non è corretta: l'argomento deve essere >= 0\n", optarg);
			return -1;
		    }
		    // Esiste almeno una richiesta di lettura => Se compare -d è ok
		    has_r = 1;
		    break;
		case 'r': // l'argomento dell'opzione -r è una lista di file (almeno 1)
		    if(process_filelist(params->oplist, optarg, READ_FILE) == -1) {
			// errore da riportare su stderr, ma continuo a processare
			fprintf(stderr, "Errore: %s\n", strerror(errno)); // TODO: migliorare
		    }
		    // Esiste almeno una richiesta di lettura => Se compare -d è ok
		    has_r = 1;
		    break;
		case 'W': // l'argomento dell'opzione -W è una lista di file (almeno 1)
		    if(process_filelist(params->oplist, optarg, WRITE_FILE) == -1) {
			// errore da riportare su stderr, ma continuo a processare
			fprintf(stderr, "Errore: %s\n", strerror(errno)); // TODO: migliorare
		    }
		    // Esiste almeno una richiesta di scrittura => Se compare -D è ok
		    has_w = 1;
		    break;
		case 'w': { // l'argomento dell'opzione -w è una directory (ed opzionalmente un intero)
		    char *save_stat = NULL;
		    char *dirname = strtok_r(optarg, ",", &save_stat);
		    char *maxfiles = strtok_r(NULL, " ", &save_stat);
		    long int nfiles = 0;

		    if(string_dup(&(params->dir_write), optarg) == -1) {
			// errore nella duplicazione
			return -1;
		    }
		    if(maxfiles && isNumber(maxfiles, &nfiles) == 0 && nfiles >= 0) {
			params->nwrite = nfiles;
		    }
		    // Se il massimo numero di file è minore di 0 o non specificato assumo nessun limite
		    if(nfiles < 0) {
			fprintf(stderr, "Warning: #file da leggere in %s non limitato\n", dirname); // TODO: migliorare
		    }
		    // argomento opzionale mancante => resta max_write = 0 di default

		    // Esiste almeno una richiesta di scrittura => Se compare -D è ok
		    has_w = 1;
		    break;
		}
		case 'a': // l'argomento dell'opzione -a è una lista di coppie dest, src
		    if(process_filelist(params->oplist, optarg, APPEND_FILE) == -1) {
			// errore da riportare su stderr, ma continuo a processare
			fprintf(stderr, "Errore: %s\n", strerror(errno)); // TODO: migliorare
		    }
		    break;
		case 't': // l'argomento (intero, positivo) di -t è il delay tra le richieste
		    retcode = isNumber(optarg, &(params->rdelay));
		    if(retcode == 1) {
			// optarg non è un numero
			printf("\"-t %s\" non è corretta: \"%s\" non è un numero\n", optarg, optarg);
			return -1;
		    }
		    if(retcode == 2) {
			// optarg è un numero, ma causa overflow/underflow
			printf("\"-t %s\" non è corretta: \"%s\" causa overflow/underflow\n", optarg, optarg);
			return -1;
		    }
		    break;
		case 'p': // la presenza dell'opzione -p fa stampare al client tutte le operazioni su stdout
		    params->prints_on = 1;
		    break;
		// TODO: -l, -u, -c
		// situazioni di errore
		case ':': // parametro mancante in un'opzione che lo richiede
		    if(optopt != 'R') { // il parametro per -R è opzionale, quindi questo messaggio lo stampo solo per le altre opzioni
			printf("Parametro mancante per l'opzione %c\n", optopt);
			return -1;
		    }
		    break;
		default: // opzione non riconosciuta
		    printf("Opzione %c non riconosciuta\n", optopt);
		    return -1;
	    }
	}

	// -D può essere non nullo sse compaiono richieste di scrittura
	if(params->dir_swapout && !has_w) {
	    // errore nello specificare le opzioni: lo comunico all'utente e stampo messaggio di help
	    fprintf(stderr, "Errore: nessun file da scrivere specificato, quindi -D non deve comparire\n"); // TODO: migliorare
	    params->help_on = 1;
	    return -1;
	}
	// Stessa cosa, ma per lettura
	if(params->dir_save_reads && !has_r) {
	    // errore nello specificare le opzioni: lo comunico all'utente e stampo messaggio di help
	    fprintf(stderr, "Errore: nessun file da leggere specificato, quindi -d non deve comparire\n"); // TODO: migliorare
	    params->help_on = 1;
	    return -1;
	}
    }

    return 0;
}

int process_filelist(struct Queue *ops, char *arg, char op_type) {
    // Se la lista di files non era stata allocata (quindi è NULL) provo ad allocarla adesso
    if(!ops) {
	if((ops = queue_init()) == NULL) {
	    // errore di allocazione non recuperabile: ritorno -1
	    return -1;
	}
    }

    // creo una nuova operazione, che aggiungerò in coda alla lista
    struct operation *newop = malloc(sizeof(struct operation));
    if(!newop) {
	// errore di allocazione
	return -1;
    }
    // creo la lista di file
    newop->flist = queue_init();
    if(!(newop->flist)) {
	// errore di allocazione
	return -1;
    }
    // assegno il tipo di operazione
    newop->type = op_type;

    // parsing della stringa arg, i cui token sono separati da ','
    char *save_stat = NULL;
    char *token = strtok_r(arg, ",", &save_stat);
    while(token) {
	// aggiungo il token alla coda
	// non è rilevante memorizzare alcun socket, per cui l'ultimo argomento può essere ignorato
	if(enqueue(newop->flist, token, strlen(token) + 1, 0) == -1) {
	    // fallito inserimento in coda di un file: notifico l'utente su stderr
	    fprintf(stderr, "Fallito inserimento del file \"%s\" in coda\n", token);
	}
	// Ottengo il successivo token nella lista
	token = strtok_r(NULL, ",", &save_stat);
    }

    // Aggiungo l'operazione in coda a ops
    if(enqueue(ops, newop, sizeof(struct operation), 0) == -1) {
	// fallito inserimento in coda di un file: notifico l'utente su stderr
	fprintf(stderr, "Fallito inserimento della lista di file nella coda\n");
    }

    // Lista di file parsata senza errori
    return 0;
}

// libera una coda di code
void free_QQ(struct Queue *qq) {
    struct node_t *el = NULL;
    while((el = pop(qq)) != NULL) {
	free_Queue((struct Queue *)el);
    }
    free(qq);
}

void free_client_opt(struct client_opts *options) {
    free(options->fs_socket);
    free(options->dir_write);
    free(options->dir_save_reads);
    free(options->dir_swapout);
    free(options);
}

long int visit_dir(struct Queue *q, const char *basedir, long int nleft) {
    long int i = 0; // contatore del numero di file visitati
    char *path = NULL;
    struct dirent *entry;
    struct stat info;
    // Provo ad aprire la directory specificata
    DIR *dw = opendir(basedir);
    if(!dw) {
        // errore
        return -1;
    }
    else {
        do {
            // Resetto errno per discriminare errore di lettura
            errno = 0;
            entry = readdir(dw); // WARNING: MT-unsafe
            // se errno cambia allora ho avuto un errore di lettura
            if(!entry && errno != 0) {
                // errore
                return -1;
            }
            // Ignoro le entry "." e ".."
            else if(strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0){
                // Se entry è a sua volta una directory devo visitarla ricorsivamente
                // Per determinare ciò potrei usare un campo di dirent non-POSIX, oppure stat
                if(stat(entry->d_name, &info) == -1) {
                    // errore
                    return -1;
                }
                if(S_ISDIR(info.st_mode)) {
                    // Allora ricorsivamente visito la directory

		    // Devo concatenare il path base con il nome della directory
		    char *fpath = get_fullpath(basedir, entry->d_name);
		    if(!fpath) {
			// impossibile ottenere path assoluto
			return -1;
		    }

                    long int left = (nleft <= 0 ? nleft : nleft - i);
                    long int res = visit_dir(q, fpath, left);
                    if(res != -1) {
			free(fpath);
			i += res; // aggiorno il numero di file letti
		    }
		    // Se res era -1 c'è stato un errore nella chiamata
		    // ricorsiva, ma posso continuare la visita della directory
                }
                else {
                    // Altrimenti è il path di un file, per cui lo inserisco in coda
		    char *fpath = get_fullpath(basedir, entry->d_name);
		    if(!fpath) {
			// impossibile ottenere path assoluto
			return -1;
		    }

                    int res = enqueue(q, fpath, strlen(fpath) + 1, -1);
		    // Se ritorna errore non esco con -1, ma continuo a leggere la directory
		    // TEMP
		    if(res == -1) {
			printf("Fallito enqueue di %s\n", fpath);
		    }
		    free(fpath);

		    // Incremento il numero di file processati
		    i++;
		}
            }
        } while(entry && i < nleft); // se entry è NULL allora ho terminato la visita (ricorsiva) della directory

	// quindi chiudo la directory
	if(closedir(dw) == -1) {
	    // errore di chiusura della directory
	    return -1;
	}
    }

    return i; // ritorno il numero di file trovati e messi in coda
}
