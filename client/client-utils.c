// header client
#include <client.h>
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
        params->oplist = queue_init();
        if(!(params->oplist)) {
            free(params);
            return NULL;
        }
    }
    return params;
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
            case 'h': // trovata l'opzione -h, quindi sarà mostrato il messaggio di uso
                if(params->help_on) {
                    // Non è consentito ripetere -h
                    return -1;
                }
                params->help_on = 1;
                break;
            case 'f': // l'argomento di -f è il path del socket per comunicare con il server
                if(string_dup(&(params->fs_socket), optarg) == -1) {
                    // errore nella duplicazione del path
                    return -1;
                }
                break;
            case 'p': // la presenza dell'opzione -p fa stampare al client tutte le operazioni su stdout
                if(params->prints_on) {
                    // Non è consentito ripetere -p
                    return -1;
                }
                params->prints_on = 1;
                break;
            case 't': {
                // l'argormento di -t è il delay (espresso in millisecondi) tra le richeste
                // per cui dovrà essere un intero >= 0
                long int delay;
                int ret = isNumber(optarg, &delay);
                if(ret == 0 && delay >= 0) {
                    params->rdelay;
                }
                else {
                    return -1;
                }
                break;
            }
            case 'r': {
                // leggo la lista di file richiesti
                if(process_filelist(params->oplist, optarg, 'r') == -1) {
                    return -1;
                }
                // Se la prossima opzione è -d allora scrivo la directory
                if(args[optind] != NULL && args[optind + 1] != NULL && strncmp(args[optind], "-d", 2) == 0) {
                    if(string_dup(&(((struct operation*)params->oplist->head)->dirname), args[optind + 1]) == -1) {
                        return -1;
                    }
                }
                else if(args[optind] != NULL && strncmp(args[optind], "-d", 2) == 0) {
                    // errore di sintassi: -d senza argomento
                    return -1;
                }
                break;
            }
            case 'R': {
                struct operation *op = malloc(sizeof(struct operation));
                if(!op) {
                    return -1;
                }
                op->type = 'R';
                // L'argomento è opzionale, per cui devo controllare se dopo -R vi sia un intero >= 0
                long int n;
                int retcode = isNumber(optarg, &n);
                if(retcode == 0) {
                    if(n < 0) {
                        // non posso avere argomento negativo
                        free(op);
                        return -1;
                    }
                    else if(n == 0) {
                        // se è 0 significa nessun limite (-1)
                        op->max_read = -1;
                    }
                    else {
                        // Altrimenti ho un limite finito > 0
                        op->max_read = n;
                    }
                }
                // Controllo se è l'opzione -d, se sì, setto dirname
                else if(strncmp(optarg, "-d", 2) == 0 ) {
                    if(string_dup(&(op->dirname), optarg) == -1) {
                        free(op);
                        return -1;
                    }
                    break; // esco per evitare di sovrascrivere dirname con il prossimo if
                }
                // Altrimenti procedo come per READ_FILE
                // Se la prossima opzione è -d allora scrivo la directory
                if(args[optind] != NULL && args[optind + 1] != NULL && strncmp(args[optind], "-d", 2) == 0) {
                    if(string_dup(&(op->dirname), args[optind + 1]) == -1) {
                        free(op);
                        return -1;
                    }
                }
                else if(args[optind] != NULL && strncmp(args[optind], "-d", 2) == 0) {
                    // errore di sintassi: -d senza argomento
                    free(op);
                    return -1;
                }

                // Inserisco op nella coda
                if(enqueue(params->oplist, op, sizeof(*op), -1) == -1) {
                    free(op);
                    return -1;
                }
                break;
            }
            // -w ed -a hanno un preprocessing identico a -W, ma distinguo in seguito cambiando il tipo
            // di operazione scelta
            case 'a':
            case 'w':
            case 'W': {
                // leggo la lista di file richiesti
                if(process_filelist(params->oplist, optarg, 'W') == -1) {
                    return -1;
                }
                // Se la prossima opzione è -D allora scrivo la directory
                // Identica a READ_FILE, ma con -D
                if(args[optind] != NULL && args[optind + 1] != NULL && strncmp(args[optind], "-D", 2) == 0) {
                    if(string_dup(&(((struct operation*)params->oplist->head)->dirname), args[optind + 1]) == -1) {
                        return -1;
                    }
                }
                else if(args[optind] != NULL && strncmp(args[optind], "-D", 2) == 0) {
                    // errore di sintassi: -d senza argomento
                    return -1;
                }

                // Ora distinguo tra le operazioni
                struct operation *op = (struct operation*)params->oplist->head;
                if(opchar == 'a') {
                    op->type = 'a';
                }
                else if(opchar == 'w') {
                    op->type = 'w';
                    // Se c'è anche l'argomento opzionale lo converto ad intero e setto il parametro
                    long int nw;
                    if(op->flist->head->next && isNumber(op->flist->head->next->data, &nw) == 0 && nw >= 0) {
                        // per come ho scritto il client se è illimitato devo scrivere un valore < 0 in max_write
                        op->max_write = (nw == 0 ? -1 : nw);
                        // Devo anche rimuovere dalla lista l'argomento, altrimenti verrebbe scambiato come directory
                        free(op->flist->head->next->data);
                        free(op->flist->head->next);
                        op->flist->head->next = NULL;
                    }
                    else if(op->flist->head->next == NULL) {
                        // Massimo numero file non specificato: resta illimitato di default
                        op->max_write = -1;
                    }
                    else {
                        // Se non è un argomento valido allora tutta la richiesta deve essere rifiutata
                        fprintf(stderr, "Richiesta -w %s rifiutata\n", (char*)op->flist->head->data);
                        struct node_t *tmp = pop(params->oplist);
                        free(tmp->data);
                        free(tmp);
                    }
                }
                break;
            }
            default:
                if(optopt != 'd' && optopt != 'D') {
                    return -1;
                }
            }
        }
    }

    return 0;
}

int process_filelist(struct Queue *ops, char *arg, char op_type) {
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

void free_client_opt(struct client_opts *options) {
    free(options->fs_socket);
    if(options->oplist) {
        struct node_t *n = NULL;
        while((n = pop(options->oplist))) {
            free_Queue(((struct operation *)n)->flist);
            free(n);
        }
        free(options->oplist);
    }
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
    char *basedir_abspath = NULL;
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
