// header progetto
#include <icl_hash.h>
#include <server-utils.h>
// multithreading headers
#include <pthread.h>
// system call headers
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

// File contenente la gestione delle strutture dati del server

// Converto una quantità positiva di Mbyte nei corrispondenti byte moltiplicando per 2^20
// NOTA: il risultato dovrebbe essere memorizzato in un size_t per scongiurare l'assenza di
// overflow (ragionevolmente) [calcoli?]
#define MBYTE_TO_BYTE(MB) 1048576 * (MB)

// Funzione che inizializza tutte le strutture dati: prende in input i parametri del server
// e riempe la struttura passata tramite puntatore
int init_ds(struct serv_params *params, struct fs_ds_t **server_ds) {
    // controllo che siano entrambi non nulli, altrimenti ho segmentation fault
    if(!(params && server_ds)) {
        return -1;
    }

    *server_ds = calloc(1, sizeof(struct fs_ds_t));
    if(!(*server_ds)) {
        return -1;
    }

    // creo la pipe per il feedback tra thread worker e manager
    if(pipe((*server_ds)->feedback) == -1) {
        return -1;
    }
    // creo la pipe per la terminazione: l'unico thread che scrive è quello che gestisce la terminazione
    if(pipe((*server_ds)->termination) == -1) {
        return -1;
    }

    // creo la ht per la memorizzazione dei file
    // Alloco preventivamente slot per il massimo numero di file che possono essere
    // inseriti nel server. La funzione hash è quella fornita da icl_hash e la chiave
    // sarà il path, per cui string_compare va bene per confrontare le chiavi
    if(((*server_ds)->fs_table = icl_hash_create(params->max_fcount, hash_pjw, string_compare)) == NULL) {
        // errore nella creazione della ht (probabilmente malloc)
        return -1;
    }
    // inizializzo mutex
    pthread_mutex_init(&((*server_ds)->mux_ht), NULL);
    pthread_mutex_init(&((*server_ds)->mux_jobq), NULL);
    pthread_mutex_init(&((*server_ds)->mux_cacheq), NULL);
    pthread_mutex_init(&((*server_ds)->mux_log), NULL);
    // inizializzo cond variables
    pthread_cond_init(&((*server_ds)->new_job), NULL);
    pthread_cond_init(&((*server_ds)->new_cacheq), NULL);

    // Inizializzo le code
    (*server_ds)->job_queue = queue_init();
    (*server_ds)->cache_q = queue_init();
    if(!((*server_ds)->job_queue && (*server_ds)->cache_q)) {
        // errore di allocazione
        return -1;
    }

    // Setto i limiti di numero di file e memoria (il resto dei parametri sono azzerati di default)
    (*server_ds)->max_files = params->max_fcount;
    (*server_ds)->max_mem = MBYTE_TO_BYTE(params->max_memsz);

    // Tutte le strutture dati inizializzate con successo
    return 0;
}

// Funzione che libera la memoria allocata per la struttura dati
void free_serv_ds(struct fs_ds_t *server_ds) {
    if(server_ds) {
        // libero la hash table
        if(icl_hash_destroy(server_ds->fs_table, free, free_file) != 0) {
            // errore nel liberare memoria
            perror("Impossibile liberare ht");
        }
        // libero le code di job e di cache
        if(server_ds->job_queue) {
            free_Queue(server_ds->job_queue);
        }
        if(server_ds->cache_q) {
            free_Queue(server_ds->cache_q);
        }

        // libero l'array di client
        if(server_ds->active_clients) {
            free(server_ds->active_clients);
        }

        // chiudo le pipe
        if( close(server_ds->feedback[0]) == -1
                || close(server_ds->feedback[1]) == -1
                || close(server_ds->termination[0]) == -1
                || close(server_ds->termination[1] == -1)) {
            perror("Impossibile chiudere una pipe");
        }

        free(server_ds);
    }
}

void free_file(void *file) {
    struct fs_filedata_t *f = (struct fs_filedata_t *)file;
    if(f->data) {
        free(f->data);
    }
    if(f->openedBy) {
        free(f->openedBy);
    }
    free(f);
}

// Aggiunge un client a quelli connessi
// Ritorna 0 se ha successo, -1 altrimenti
int add_connection(struct fs_ds_t *ds, const int csock) {
    if(!(ds->active_clients)) {
        ds->active_clients = malloc(sizeof(struct client_info));
        if(!(ds->active_clients)) {
            return -1;
        }
    }
    else {
        struct client_info *newptr = realloc(ds->active_clients, sizeof(struct client_info) * ds->connected_clients + 1);
        if(!newptr) {
            return -1;
        }
        ds->active_clients = newptr;
    }
    ds->active_clients[ds->connected_clients].socket = csock;
    ds->active_clients[ds->connected_clients].last_op = 0;
    ds->active_clients[ds->connected_clients].last_op_flags = 0;
    ds->active_clients[ds->connected_clients].last_op_path = NULL;

    ds->connected_clients++;

    return 0;
}

// Rimuove un client da quelli connessi
// Ritorna 0 se ha successo, -1 altrimenti
int rm_connection(struct fs_ds_t *ds, const int csock) {
    if(!(ds->active_clients) || ds->connected_clients == 0) {
        // non vi era nessun client connesso
        return -1;
    }

    size_t i = 0;
    while(i < ds->connected_clients && ds->active_clients[i].socket != csock) {
        i++;
    }
    if(i == ds->connected_clients) {
        // client non trovato
        return -1;
    }
    // libero il client e sposto indietro di una posizione tutti gli altri
    if(ds->active_clients[i].last_op_path) {
        free(ds->active_clients[i].last_op_path);
    }
    for(; i < ds->connected_clients - 1; i++) {
        ds->active_clients[i] = ds->active_clients[i+1];
    }
    struct client_info *newptr = realloc(ds->active_clients, sizeof(struct client_info) * ds->connected_clients - 1);
    if(!newptr) {
        return -1;
    }
    ds->active_clients = newptr;
    ds->connected_clients--;

    return 0;
}

// Aggiorna lo stato del client connesso sul socket sock con l'ultima operazione conclusa con successo
// Ritorna 0 se ha successo, -1 altrimenti
int update_client_op(struct fs_ds_t *ds, const int sock, const char op, const int op_flags, const char *op_path) {
    size_t i;
    for(i = 0; i < ds->connected_clients; i++) {
        if(ds->active_clients[i].socket == sock) {
            ds->active_clients[i].last_op = op;
            ds->active_clients[i].last_op_flags = op_flags;
            if(op_path) {
                ds->active_clients[i].last_op_path = strndup(op_path, strlen(op_path) + 1);
                if(!ds->active_clients[i].last_op_path) {
                    return -1;
                }
            }
            else if(ds->active_clients[i].last_op_path) {
                free(ds->active_clients[i].last_op_path);
                ds->active_clients[i].last_op_path = NULL;
            }
            return 0;
        }
    }
    return -1;
}
