// header api
#include <fs-api.h>
#include <utils.h>
// syscall headers
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
// std headers
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

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
    int cIdx = -1;
    if((cIdx = isConnected(pid)) == -1) {
        // client non trovato, quindi non posso rimuoverlo
        return -1;
    }
    // prendo il socket di questo client
    int sock = clients_info->client_id[2 * cIdx];

    // libero questa posizione del'array
    clients_info->client_id[2 * cIdx] = -1;
    clients_info->client_id[2 * cIdx + 1] = -1;
    // aggiorno il numero di client connessi
    clients_info->count--;

    // chiudo il socket
    if(close(sock) == -1) {
        // errore di chiusura del socket: riporto l'errore al client (ma lo slot è libero)
        return -1;
    }

    // Rimozione connessione OK
    return 0;
}

int isConnected(const int pid) {
    // Cerco il PID fornito nell'array: se esiste allora libero la posizione
    int i = 0;
    while(i < clients_info->capacity) {
        if(clients_info->client_id[2 * i + 1] == pid) {
            // Trovato: ritorno la posizione
            return i;
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
    memset(req, 0, sizeof(struct request_t));
    req->type = type;
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
    rep->status = stat;
    rep->nbuffers = nbuf;
    rep->paths_sz = 0;
	// calcolo la lunghezza della stringa di path
	if(names && nbuf > 0) {
		int i;
		for(i = 0; i < nbuf; i++) {
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

    // ottengo il path assoluto di dir (se già non lo è)
    char *dir_abspath = NULL;
    if(dir && dir[0] != '/') {
        dir_abspath = get_fullpath(orig, dir);
    }
    else if(dir && dir[0] == '.'){
        dir_abspath = &dir[2];
    }
    else {
        dir_abspath = dir;
    }

    // cambio directory a quella specificata da dir per cui i path dei file
    // che creo sono relativi a questa directory
    if(!dir_abspath || chdir(dir_abspath) == -1) {
        // errore nel cambio di directory
        free(orig);
        free(dir_abspath);
        return -1;
    }

    // La risposta contiene il numero di file inviati dal server nel campo nbuffers
    int i = 0;
    char *state = NULL;
    // tokenizzo per ottenere tutti i pathname contenuti in paths
    char *fname = strtok_r(paths_cpy, "\n", &state);
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

        // SOLO DEBUG
        fprintf(stdout, "Ricevuto il file %s di %lu bytes\n", fname, sizes[i]);

        // Se era stata fornito il path allora devo creare il file (opero su path relativi alla directory)
        if(dir) {
            int file_fd;
            if((file_fd = creat(fname, PERMS_ALL_READ)) == -1) {
                // fallita creazione file
                break;
            }
            if(writen(file_fd, data, sizes[i]) == -1) {
                // fallita scrittura file
                close(file_fd);
                break;
            }
            close(file_fd);
            file_fd = -1;
        }
        free(data);

        fname = strtok_r(NULL, "\n", &state); // aggiorno il token
        i++; // aumento il numero di file ricevuti correttamente dalla api
    }

    // ripristino la directory originale
    if(chdir(orig) == -1) {
        // errore nel cambio di directory
        perror("Errore ripristino directory");
        i = -1;
    }
    free(orig);
    free(dir_abspath);

    return i;
}
