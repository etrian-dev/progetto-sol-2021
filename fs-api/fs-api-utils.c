// header api
#include <fs-api.h>
// syscall headers
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
// std headers
#include <time.h>
#include <string.h>
#include <errno.h>

// Implementazione di alcune funzioni di utilità per l'API

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
	memeset(clients_info->client_id, -1, NCLIENTS_DFL * 2 * sizeof(int));
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
    clients_info->client_id[2 * i] = -1;
    clients_info->client_id[2 * i + 1] = -1;
    // aggiorno il numero di client connessi
    client_info->count--;

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
