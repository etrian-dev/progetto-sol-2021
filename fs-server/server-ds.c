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


// Funzione che inizializza tutte le strutture dati: prende in input i parametri del server
// e riempe la struttura passata tramite puntatore
int init_ds(struct serv_params *params, struct fs_ds_t **server_ds) {
    // controllo che siano entrambi non nulli, altrimenti ho segmentation fault
    if(!(params && server_ds)) {
        return -1;
    }

    *server_ds = malloc(sizeof(struct fs_ds_t));
	if(!(*server_ds)) {
		return -1;
	}

    // creo anche la pipe per il feedback tra thread worker e manager
	if(pipe((*server_ds)->feedback) == -1) {
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
    // inizializzo mutex e cond variables
    pthread_mutex_init(&((*server_ds)->mux_jobq), NULL);
    pthread_mutex_init(&((*server_ds)->mux_cacheq), NULL);
    pthread_cond_init(&((*server_ds)->new_job), NULL);
    pthread_cond_init(&((*server_ds)->new_cacheq), NULL);
    // Inizializzo le code
    (*server_ds)->job_queue = queue_init();
    (*server_ds)->cache_q = queue_init();
    if(!((*server_ds)->job_queue && (*server_ds)->cache_q)) {
        // errore di allocazione
        return -1;
    }

    // Tutte le strutture dati inizializzate con successo
    return 0;
}
