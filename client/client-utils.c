// header client
#include <client.h>
// header API
#include <fs-api.h> // per le definizioni dei caratteri corrispondenti alle operazioni
// header utilità
#include <utils.h>
// system call headers
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h> // per visita directory
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Definisco il numero di client di default (se necessario espando il loro numero)
#define NPOS_DFL 10

// inizializza i parametri a valori di default
struct client_opts *init_params(void) {
    struct client_opts *params = calloc(1, sizeof(struct client_opts));
    if(params) {
        params->oplist = calloc(1, sizeof(struct Queue));
        // un eventuale errore di allocazione non comporta errori fatali in quanto
        // rimangono comunque NULL i puntatori, per cui è necessario soltanto controllare
        // di allocarli prima di usarli
    }

    return params;
}

// Alloca ed inizializza i parametri di una nuova operazione, ritornando un puntatore ad essa
struct operation *newop(const char t, const long int max, struct Queue *list, char *dir, char *swp) {
    struct operation *op = calloc(1, sizeof(struct operation));
    if(!op) {
        return NULL;
    }
    op->type = t;
    op->max_n = max;
    if(dir) {
        if(string_dup(&(op->dir_op), dir) == -1) {
            free(op);
            return NULL;
        }
    }
    if(swp) {
        if(string_dup(&(op->dir_swp), swp) == -1) {
            free(op->dir_op);
            free(op);
            return NULL;
        }
    }
    if(list) {
        // Se è già stata preparata una coda allora assegno direttamente il puntatore
        op->flist = list;
    }
    else {
        // alloco una nuova coda vuota
        op->flist = queue_init();
        if(!(op->flist)) {
            free(op->dir_op);
            free(op->dir_swp);
            free(op);
            return NULL;
        }
    }
    return op;
}

// libera l'operazione op
void free_op(struct operation **op) {
    if(*op) {
        if((*op)->dir_op) free((*op)->dir_op);
        if((*op)->dir_swp) free((*op)->dir_swp);
        if((*op)->flist) free_Queue((*op)->flist, free);
        free(*op);
        *op = NULL;
    }
}

// Funzione che processa argv per ottenere le opzioni passate al client (usa la funzione getopt)
// Ritorna 0 se ha successo (e riempie i campi della struttura params)
// Altrimenti ritorna -1
int get_client_options(int nargs, char **args, struct client_opts *params) {
    if(nargs > 1) {
        // processa tutte le opzioni da riga di comando
        int opchar;
        int retcode;
        while((opchar = getopt(nargs, args, CLIENT_OPSTRING)) != -1) {
            switch(opchar) {
            case 'h': // trovata l'opzione -h, quindi sarà mostrato il messaggio di help
                params->help_on = 1;
                return 0; // non è necessario proseguire con il processing
            case 'f': // l'argomento di f è il path del socket per comunicare con il server
                if(string_dup(&(params->fs_socket), optarg) == -1) {
                    // errore nella duplicazione del path del socket
                    return -1;
                }
                break;
            case 'D':   // l'argomento di -D è la directory in cui salvare i file vittima dello swapout
            case 'd': { // l'argomento di -d è la directory in cui salvare i file letti
                // gestisco le due opzioni insieme, perché entrambe devono seguire -w o -W
                // oppure -r o -R rispettivamente
                int success = 0;
                if(params->oplist->tail && params->oplist->tail->data) {
                    struct operation *last_op = (struct operation *)params->oplist->tail->data;
                    if(opchar == 'D' && (last_op->type == 'w' || last_op->type == WRITE_FILE)) {
                        if(string_dup(&(last_op->dir_swp), optarg) == -1) {
                            // errore nella duplicazione della directory di salvataggio
                            success = -1;
                        }
                    }
                    else if(opchar == 'd' && (last_op->type == READ_FILE || last_op->type == READ_N_FILES)) {
                        if(string_dup(&(last_op->dir_swp), optarg) == -1) {
                            // errore nella duplicazione della directory di salvataggio
                            success = -1;
                        }
                    }
                    else {
                        // L'operazione precedente non era -w, -W, -r o -R, per cui non va bene avere -D o -d
                        success = -1;
                    }
                }
                if(success == -1) {
                    return success;
                }
                break;
            }
            case 'R': { // l'argomento di -R è il numero massimo di file da leggere
                // Se l'argomento non è fornito (optarg NULL) rimane al valore di default (-1)
                // Se l'argomento è in realtà un'opzione (ovvero -R non ha argomenti)
                // allora deve rimanere il valore di default
                struct operation *read_N_op = NULL;
                if(optarg && optarg[0] != '-') {
                    long int num;
		            retcode = isNumber(optarg, &num);
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
		            if(num < 0) {
		                // optarg è un numero intero, ma negativo e quindi non posso accettarlo
		                printf("\"-R %s\" non è corretta: l'argomento deve essere >= 0\n", optarg);
		                return -1;
		            }
                    // Altrimenti l'argomento num è ok: creo l'operazione
                    read_N_op = newop(READ_N_FILES, num, NULL, NULL, NULL);
		        }
                else {
                    read_N_op = newop(READ_N_FILES, -1, NULL, NULL, NULL);
                }
                // poi metto l'operazione in coda
                if(!read_N_op) {
                    return -1;
                }
                if(enqueue(params->oplist, read_N_op, sizeof(struct operation), -1) == -1) {
                    // fallito inserimento in coda
                    free_op(&read_N_op);
                    return -1;
                }
                break;
            }
            case 'r':   // l'argomento dell'opzione -r è una lista di file (almeno 1)
            case 'a':   // l'argomento dell'opzione -a è una lista di coppie di file (almeno una coppia)
            case 'W':   // l'argomento dell'opzione -W è una lista di file (almeno 1)
            case 'l':   // l'argomento dell'opzione -l è una lista di file (almeno 1)
            case 'u':   // l'argomento dell'opzione -u è una lista di file (almeno 1)
            case 'c': { // l'argomento dell'opzione -c è una lista di file (almeno 1)
                char type;
                switch(opchar) {
                case 'r': type = READ_FILE; break;
                case 'a': type = APPEND_FILE; break;
                case 'W': type = WRITE_FILE; break;
                case 'l': type = LOCK_FILE; break;
                case 'u': type = UNLOCK_FILE; break;
                case 'c': type = REMOVE_FILE; break;
                }
                if(process_filelist(params->oplist, optarg, type) == -1) {
                    return -1;
                }
                break;
            }
            case 'w': { // l'argomento dell'opzione -w è una directory (ed opzionalmente un intero)
                char *save_stat = NULL;
                char *dirname = strtok_r(optarg, ",", &save_stat);
                char *maxfiles = strtok_r(NULL, " ", &save_stat);
                long int nfiles = 0;
                struct operation *op_wdir = NULL;

                retcode = isNumber(maxfiles, &nfiles);
                if(maxfiles && retcode == 0 && nfiles > 0) {
                    op_wdir = newop('w', nfiles, NULL, dirname, NULL);
                }
                else if(maxfiles && retcode == 0 && nfiles == 0) {
                    op_wdir = newop('w', -1, NULL, dirname, NULL);
                }
                else {
                    return -1;
                }
                // metto in coda l'operazione
                if(!op_wdir) {
                    return -1;
                }
                if(enqueue(params->oplist, op_wdir, sizeof(struct operation), -1) == -1) {
                    // fallito inserimento in coda
                    free_op(&op_wdir);
                    return -1;
                }
                break;
            }
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
            // situazioni di errore
            case ':': // parametro mancante in un'opzione che lo richiede
                printf("Parametro mancante per l'opzione %c\n", optopt);
                return -1;
            default: // opzione non riconosciuta
                printf("Opzione %c non riconosciuta\n", optopt);
                return -1;
            }
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
    struct operation *op = newop(op_type, -1, NULL, NULL, NULL);
    if(!op) {
        // errore di allocazione
        return -1;
    }
    // parsing della stringa arg, i cui token sono separati da ','
    char *save_stat = NULL;
    char *token = strtok_r(arg, ",", &save_stat);
    while(token) {
        // aggiungo il token alla coda
        // non è rilevante memorizzare alcun socket, per cui l'ultimo argomento può essere ignorato
        if(enqueue(op->flist, token, strlen(token) + 1, 0) == -1) {
            // fallito inserimento in coda di un file: notifico l'utente su stderr
            fprintf(stderr, "[%d]: Fallito inserimento del file \"%s\" in coda\n", getpid(), token);
        }
        // Ottengo il successivo token nella lista
        token = strtok_r(NULL, ",", &save_stat);
    }
    // Aggiungo l'operazione in coda a ops
    if(enqueue(ops, op, sizeof(struct operation), 0) == -1) {
        // fallito inserimento in coda di un file: notifico l'utente su stderr
        fprintf(stderr, "[%d]: Fallito inserimento della lista di file nella coda\n", getpid());
    }
    // Lista di file parsata senza errori
    return 0;
}

void free_client_opt(struct client_opts *options) {
    free(options->fs_socket);
    struct node_t *n = NULL;
    while((n = pop(options->oplist)) != NULL) {
        struct operation *op = (struct operation *)n->data;
        free_op(&op);
        free(n);
    }
    if(options->oplist) free(options->oplist);
    free(options);
}

long int visit_dir(struct Queue *pathlist, struct Queue *datalist, const char *basedir, long int nleft) {
    long int nfiles = 0; // contatore del numero di file visitati
    struct dirent *entry = NULL;
    struct stat info;

    // salvo la directory di lavoro corrente
    char *orig = malloc(BUF_BASESZ * sizeof(char));
    if(!orig) {
        // errore di allocazione
        return -1;
    }
    size_t dir_sz = BUF_BASESZ;
    errno = 0; // resetto errno per esaminare eventuali errori
    while(orig && getcwd(orig, dir_sz) == NULL) {
        // Se errno è diventato ERANGE allora il buffer allocato non è abbastanza grande
        if(errno == ERANGE) {
            // rialloco orig, con la politica di raddoppio della size
            char *newbuf = realloc(orig, BUF_BASESZ * 2);
            if(newbuf) {
                orig = newbuf;
                dir_sz *= 2;
                errno = 0; // resetto errno in modo da poterlo testare dopo la guardia
            }
            else {
                // errore di riallocazione
                free(orig);
                return -1;
            }
        }
        // se si è verificato un altro errore allora esco con fallimento
        else {
            free(orig);
            return -1;
        }
    }
    // Adesso orig contiene il path della directory corrente

    // ottengo il path assoluto di basedir (se già non lo è)
    const char *basedir_abspath;
    if(basedir && basedir[0] != '/') {
        basedir_abspath = get_fullpath(orig, basedir);
    }
    else if(basedir && basedir[0] == '.'){
        basedir_abspath = &basedir[2];
    }
    else {
        basedir_abspath = basedir;
    }

    // cambio directory corrente a basedir
    if(!basedir_abspath || chdir(basedir_abspath) == -1) {
        // impossibile cambiare directory di lavoro
        free(orig);
        return -1;
    }

    // Apro basedir_abspath per scorrerla
    DIR *dw = opendir(basedir_abspath);
    if(!dw) {
        fprintf(stderr, "What: %s\n", strerror(errno));
        // errore apertura directory
        return -1;
    }
    // Directory aperta con successo
    else {
        do {
            // Resetto errno per discriminare errore di lettura da fine directory
            errno = 0;
            entry = readdir(dw); // NOTA: dovrebbe essere threadsafe in glibc, ma non garantito da POSIX
            // se errno cambia allora ho avuto un errore di lettura
            if(!entry && errno != 0) {
                // errore lettura directory entry
                return -1;
            }
            // Ignoro le entry "." e ".."
            else if(entry && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                // Se entry è a sua volta una directory devo visitarla ricorsivamente
                // Per determinare ciò uso stat (2).
                //La directory di lavoro è basedir_abspath, per cui è come se passassi a stat il path basedir_abspath/entry->d_name
                if(stat(entry->d_name, &info) == -1) {
                    // errore di lettura informazioni file
                    return -1;
                }
                // Con la seguente macro testa se il file sia una directory
                if(S_ISDIR(info.st_mode)) {
                    // Allora ricorsivamente visito la directory
                    // Il path sarà interpretato dalla chiamata ricorsiva come relativo a basedir_abspath

                    // Se ho già letto nfiles ed ho un limite allora ne restano da leggere al più
                    // nleft - nfiles. Altrimenti non ho un limite (nleft < 0) e quindi nleft nella chiamata
                    // ricorsiva può rimanere invariato
                    long int left = (nleft < 0 ? nleft : nleft - nfiles);
                    long int res = visit_dir(pathlist, datalist, entry->d_name, left);
                    // Se la chiamata ricorsiva ha successo devo aggiornare il numero di file letti
                    if(res != -1) {
                        nfiles += res; // aggiorno il numero di file letti
                    }
                    // Se res == -1 c'è stato un errore nella chiamata
                    // ricorsiva, ma posso continuare la visita della directory
                }
                else {
                    int fail = 0;
                    int ffd = -1;
                    // Altrimenti è il path di un file, per cui lo inserisco in coda
                    // In questo caso non uso path relativi per rendere più agevole la gestione da parte del server
                    char *fpath = get_fullpath(basedir_abspath, entry->d_name);
                    if(!fpath) {
                        fail = 1;
                    }
                    // apro il file e ne copio il contenuto in un buffer, per poi inserirlo
                    // la size è nota dalla stat
                    if((ffd = open(entry->d_name, O_RDONLY)) == -1) {
                        fail = 1;
                    }
                    void *data = malloc(info.st_size);
                    if(!data) {
                        fail = 1;
                    }
                    if(!fail) {
                        if(readn(ffd, data, info.st_size) != info.st_size) {
                            fail = 1;
                        }
                    }
                    // Inserisco il path assoluto del file nella coda pathlist (la stessa per ogni chiamata ricorsiva)
                    // ed i dati del file nella coda datalist
                    // Se ritornano errore non esco, ma continuo a leggere la directory (tuttavia stampo a schermo l'errore)
                    if(enqueue(pathlist, fpath, strlen(fpath) + 1, -1) == -1) {
                        fprintf(stderr, "Fallito enqueue di \"%s\": %s\n", fpath, strerror(errno));
                        fail = 1;
                    }
                    if(fail || enqueue(datalist, data, info.st_size, -1) == -1) {
                        fprintf(stderr, "Fallito enqueue dei dati di \"%s\": %s\n", fpath, strerror(errno));
                        fail = 1;
                    }
                    // libero memoria
                    if(ffd != -1) close(ffd);
                    if(fpath) free(fpath);
                    if(data) free(data);

                    if(fail) {
                        continue;
                    }
                    // Incremento il numero di file visitati
                    nfiles++;
                }
            }
        } while(entry && (nleft < 0 || nfiles < nleft)); // se entry è NULL (e errno non è cambiato) allora ho terminato la visita della directory

        // quindi chiudo la directory
        if(closedir(dw) == -1) {
            // errore di chiusura della directory
            return -1;
        }

        // ripristino la directory originale (nelle chiamate ricorsive sarebbe ".." ma
        // sfruttare questa entry richiederebbe una gestione più articolata
        if(chdir(orig) == -1) {
            // impossibile cambiare directory di lavoro
            free(orig);
            return -1;
        }
        free(orig);
    }

    return nfiles; // ritorno il numero di file trovati e messi in coda
}
