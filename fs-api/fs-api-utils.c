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
	for(size_t i = 0; i < clients_info->capacity; i++) {
	    if(clients_info->client_id[2 * i] == -1) {
		first_free = i;
	    }
	}
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

struct reply_t *newreply(const char stat, const int nbuf, size_t *lenghts, const char **names) {
    if(nbuf < 0) {
	// deve essere un numero di buffer >= 0
	return NULL;
    }

    struct reply_t *rep = malloc(sizeof(struct reply_t));
    if(!rep) {
	return NULL;
    }
    rep->buflen = malloc(nbuf * sizeof(size_t));
    if(!(rep->buflen)) {
	free(rep);
	return NULL;
    }

    rep->status = stat;
    rep->nbuffers = nbuf;

    int no_alloc = 0;
    // duplico nomi file e scrivo loro lunghezza
    int i;
    for(i = 0; i < nbuf; i++) {
	rep->buflen[i] = lenghts[i];
	rep->fname[i] = strndup(names[i], strlen(names[i]) + 1);
	if(!(rep->fname[i])) {
	    no_alloc = 1;
	}
    }
    if(no_alloc) {
	// qualche errore di allocazione
	for(i = 0; i < nbuf; i++) {
	    if(rep->fname[i]) free(rep->fname[i]);
	}
	free(rep);
	return NULL;
    }

    return rep;
}

// Funzione per leggere dal server dei file ed opzionalmente scriverli nella directory dir
int write_swp(const int server, const char *dir, struct reply_t *rep) {
    // determino la directory corrente (assumo limite superiore alla lughezza del path)
    char *orig = malloc(BUF_BASESZ * sizeof(char));
    if(!orig) {
	return -1;
    }
    if(!getcwd(orig, BUF_BASESZ)) {
	free(orig);
	return -1;
    }

    // Apro dir per salvarli su disco
    int is_dir = 1;
    int dir_fd;
    if((dir_fd = open(dir, O_CREATEFILE)) == -1) {
	// errore di apertura/creazione directory
	is_dir = 0;
    }
    // cambio directory a quella specificata da dirname per cui i path dei file
    // che creo sono relativi a questa directory
    if(is_dir && fchdir(dir_fd) == -1) {
	// errore nel cambio di directory
	close(dir_fd);
	return -1;
    }

    // La risposta contiene il numero di file inviati dal server nel campo nbuffers
    int i = 0;
    while(i < rep->nbuffers) {
	// Leggo l'i-esimo file inviato dal server
	void *data = malloc(rep->buflen[i]);
	if(!data) {
	    // fallita allocazione buffer
	    break;
	}

	if(readn(server, data, rep->buflen[i]) == -1) {
	    free(data);
	    break;
	}

	// Se era stata fornito il path ed aperta la directory per salvare i file
	// allora devo creare il file (opero su path relativi alla directory dirname)
	if(is_dir) {
	    int file_fd;
	    if((file_fd = creat(rep->fname[i], PERMS_ALL_READ)) == -1) {
		// fallita creazione file
		break;
	    }
	    if(writen(file_fd, data, rep->buflen[i]) == -1) {
		// fallita scrittura file
		close(file_fd);
		break;
	    }
	    close(file_fd);
	    file_fd = -1;
	}
	free(data);

	i++; // aumento il numero di file ricevuti correttamente dalla api
    }

    // Se era stata aperta allora chiudo il descrittore della directory di swapout
    if(is_dir) {
	close(dir_fd);
    }

    // ripristino la directory originale
    if(chdir(orig) == -1) {
	// errore nel cambio di directory
	perror("Errore ripristino directory");
	i = -1;
    }
    free(orig);

    return i;
}
