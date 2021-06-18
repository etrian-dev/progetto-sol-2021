// header api
#include <fs-api.h>
// header utilità
#include <utils.h>
// syscall headers
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
// std headers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <errno.h>

// Implementazione di alcune funzioni di utilità per l'API
struct conn_info *clients_info;

// inizializza la struttura dati della API per la connessione al socket so sname
int init_api(const char *sname) {
    if((clients_info = malloc(sizeof(struct conn_info))) == NULL) {
        // errore di allocazione
        return -1;
    }
    if((clients_info->sockname = strndup(sname, strlen(sname) + 1)) == NULL) {
        // errore di allocazione
        return -1;
    }
    // prealloco un certo numero di slot nell'array per i client che li richiederanno
    if((clients_info->client_id = malloc(2 * sizeof(int) * NCLIENTS_DFL)) == NULL) {
        // errore di allocazione, ma non fatale: alloco memoria al bisogno
        clients_info->capacity = 0; // però devo settare a 0 la capacità
    }
    else {
        // setto ogni entry dell'array a -1 per indicare che sono libere
        memset(clients_info->client_id, -1, NCLIENTS_DFL * 2 * sizeof(int));
        clients_info->capacity = NCLIENTS_DFL; // setto anche il numero di slot allocati
    }
    // Inizializzo anche il numero di client connessi
    clients_info->count = 0;
    return 0;
}

int add_client(const int conn_fd, const int pid) {
    size_t first_free;

    // guardo se devo espandere l'array di id
    if(clients_info->capacity == clients_info->count) {
        clients_info->capacity++;
        int *dummy = realloc(clients_info->client_id, clients_info->capacity * 2 * sizeof(int));
        if(!dummy) {
            // riallocazione fallita: errore fatale
            return -1;
        }
        clients_info->client_id = dummy;
        // la prima posizione libera sarà quindi l'ultima
        first_free = clients_info->capacity - 1;
    }
    else {
        // devo determinare la prima posizione libera
        size_t i = 0;
        while(i < clients_info->capacity && clients_info->client_id[2 * i] != -1) {
            i++;
        }
        first_free = i;
    }

    // inserisco prima il socket della connessione e dopo il PID del chiamante per distinguere i client
    clients_info->client_id[2 * first_free] = conn_fd;
    clients_info->client_id[2 * first_free + 1] = pid;
    clients_info->count++; // devo anche aggiornare il numero di client connessi
    return 0;
}

int rm_client(const int pid) {
    // Cerco il PID nell'array: se esiste allora ritorno la posizione, altrimenti -1
    int i = 0;
    while(i < clients_info->capacity && clients_info->client_id[2 * i + 1] != pid) {
        i++;
    }
    if(i == clients_info->capacity) {
        // PID non trovato: impossibile rimuovere il client
        return -1;
    }
    // salvo il socket di questo client
    int sock = clients_info->client_id[2 * i];
    // libero questa posizione del'array e ne sovrascivo i contenuti
    clients_info->client_id[2 * i] = -1;
    clients_info->client_id[2 * i + 1] = -1;
    // aggiorno il numero di client connessi
    clients_info->count--;

    // chiudo il socket del client disconnesso
    if(close(sock) == -1) {
        // errore di chiusura del socket: riporto l'errore al client (ma lo slot è libero)
        return -1;
    }

    // Rimozione connessione OK
    return 0;
}

int isConnected(void) {
    int pid = getpid(); // prendo il pid del client chiamante
    // Cerco il PID nell'array: se esiste allora ritorno la posizione, altrimenti -1
    int i = 0;
    while(i < clients_info->capacity) {
        // l'array è di coppie ordinate di interi (socket,PID)
        if(clients_info->client_id[2 * i + 1] == pid) {
            // Trovato: ritorno il socket
            return clients_info->client_id[2 * i];
        }
        i++;
    }
    // Non trovato
    return -1;
}

// funzione di utilità per convertire msec in un delay specificato secondo timespec
void get_delay(const int msec, struct timespec *delay) {
    // dato che il delay è specificato in ms devo convertirlo a ns per scriverlo in timespec
    // Se il delay in ms è troppo grande (>=1000) per essere convertito in ns provo a convertire in secondi
    // Se non gestissi questi due casi potrei avere overflow su tv_nsec
    if(msec >= 1000) {
        delay->tv_sec = (time_t)(msec / 1000); // con la divisione intera ottengo il numero di secondi in msec ms
        delay->tv_nsec = (msec % 1000) * 1000000; // il resto è convertito a ns e non posso avere overflow
    }
    else {
        delay->tv_nsec = msec * 1000000;
    }
}

struct request_t *newrequest(const char type, const int flags, const size_t pathlen, const size_t buflen) {

    struct request_t *req = malloc(sizeof(struct request_t));
    if(!req) {
        return NULL;
    }
    memset(req, 0, sizeof(struct request_t)); // per evitare errori di campi non inizializzati
    req->type = type;
    req->pid = getpid();
    req->flags = flags;
    req->path_len = pathlen;
    req->buf_len = buflen;

    return req;
}

struct reply_t *newreply(const char stat, const int nbuf, const char **names) {
    if(nbuf < 0) {
        // il numero di buffer non può essere < 0
        return NULL;
    }

    struct reply_t *rep = malloc(sizeof(struct reply_t));
    if(!rep) {
        return NULL;
    }
    memset(rep, 0, sizeof(struct reply_t)); // per evitare errori di campi non inizializzati
    rep->status = stat;
    rep->nbuffers = nbuf;
    rep->paths_sz = 0;
	// calcolo la lunghezza della stringa di path
	if(names && nbuf > 0) {
		for(int i = 0; i < nbuf; i++) {
			// conto anche il carattere di terminazione della stringa '\n'
			rep->paths_sz += strlen(names[i]) + 1;
		}
	}

    return rep;
}

// Funzione per leggere dal server dei file ed opzionalmente scriverli nella directory dir
int write_swp(const int server, const char *dir, int nbufs, const size_t *sizes, const char *paths) {
    char *paths_cpy = strndup(paths, strlen(paths) + 1); // devo copiare paths perché strtok modifica la stringa
    if(!paths_cpy) {
        return -1;
    }

    // Rispettivamente saranno la directory di lavoro corrente e il path assoluto alla
    // directory nella quale scrivere gli nbufs di cui sono fornite dimensioni e pathname
    char *cwd = NULL; // cwd la mantengo per ripristinarla al termine della funzione
    char *dir_abspath = NULL;
    // Ottengo il path assoulto della directory
    if(dir != NULL && dir[0] != '/') {
        // salvo la directory di lavoro corrente
        cwd = malloc(BUF_BASESZ * sizeof(char));
        if(!cwd) {
            // errore di allocazione
            return -1;
        }
        size_t dir_sz = BUF_BASESZ;
        errno = 0; // resetto errno per esaminare eventuali errori
        while(cwd && getcwd(cwd, dir_sz) == NULL) {
            // Se errno è diventato ERANGE allora il buffer allocato non è abbastanza grande
            if(errno == ERANGE) {
                // rialloco cwd, con la politica di raddoppio della size
                char *newbuf = realloc(cwd, BUF_BASESZ * 2);
                if(newbuf) {
                    cwd = newbuf;
                    dir_sz *= 2;
                    errno = 0; // resetto errno in modo da poterlo testare dopo la guardia
                }
                else {
                    // errore di riallocazione
                    free(cwd);
                    return -1;
                }
            }
            // se si è verificato un altro errore allora esco con fallimento
            else {
                free(cwd);
                return -1;
            }
        }
        // Adesso cwd contiene il path della directory corrente
        dir_abspath = get_fullpath(cwd, dir);
        if(!dir_abspath) {
            free(cwd);
            free(paths_cpy);
            return -1;
        }
    }

    if(dir){
        if(!dir_abspath || chdir(dir_abspath) == -1) {
            perror("fs-api-utils.c: write_swp(): chdir error");
            free(cwd);
            free(dir_abspath);
            free(paths_cpy);
            return -1;
        }
    }

    // La risposta contiene il numero di file inviati dal server nel campo nbuffers
    int i = 0;
    char *state = NULL;
    // tokenizzo per ottenere tutti i pathname contenuti in paths
    char *abs_filepath = strtok_r(paths_cpy, "\n", &state);
    while(i < nbufs) {
        // Leggo l'i-esimo file inviato dal server
        void *data = malloc(sizes[i]);
        if(!data) {
            // fallita allocazione buffer
            break;
        }

        if(readn(server, data, sizes[i]) != sizes[i]) {
            free(data);
            break;
        }

        // Se era stata fornito il path allora devo creare il file (opero su path relativi alla directory)
        if(dir) {
            int file_fd;
            // Il nome del file da scrivere sarà dir_abspath/abs_filepath
            char *fname = strrchr(abs_filepath, '/');
            fname = fname + 1; // Il nome del file non contiene '/' iniziale
            if((file_fd = creat(fname, PERMS_ALL_READ)) == -1) {
                // fallita creazione file
                break;
            }
            if(writen(file_fd, data, sizes[i]) != sizes[i]) {
                // fallita scrittura file
                close(file_fd);
                break;
            }
            close(file_fd);
            file_fd = -1;
        }
        free(data);

        abs_filepath = strtok_r(NULL, "\n", &state); // aggiorno il token
        i++; // aumento il numero di file ricevuti correttamente dalla api
    }

    // ripristino la directory originale
    if(cwd) {
        if(chdir(cwd) == -1) {
            // errore nel cambio di directory
            perror("fs-api-utils.c: write_swp(): chdir(): errore ripristino directory");
            i = -1;
        }
    }

    // libero memoria
    free(paths_cpy);
    if(cwd) free(cwd);
    if(dir_abspath) free(dir_abspath);

    return i;
}
