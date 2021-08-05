/**
 * \file server-ds.c
 * \brief File contenente l'implementazione delle funzioni per l'allocazione/deallocazione delle strutture dati usate dal server
 *
 * Le funzioni contenute in questo file si occupano di allocare/deallocare/modificare le strutture
 * dati del server, compresa quella che immagazzina un file e quelle che gestiscono la memorizzazione
 * delle connessioni attive e le relative operazioni
 */

// header progetto
#include <server-utils.h>
// header API
#include <fs-api.h>
// header utilità
#include <icl_hash.h>
// multithreading headers
#include <pthread.h>
// system call headers
#include <signal.h> // per SIGKILL
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Utile macro per convertire una quantità positiva di Mbyte nel corrispondente numbero di byte
/// NOTA: il risultato dovrebbe sempre essere memorizzato in un size_t per scongiurare l'overflow
/// (per dimensioni in MB ragionevolmente basse)
#define MBYTE_TO_BYTE(MB) 1048576 * (MB)

/**
 * \brief Funzione che inizializza la struttura dati principale del server (server_ds)
 *
 * La funzione riceve in input una configurazione nella struttura params, ottenuta dal
 * parsing di un file di configurazione, ed inizializza
 * con tali parametri la struttura fs_ds_t condivisa dai thread del server per effetturare
 * le operazioni implementate
 * \param [in] params La struttura dati che contiene la configurazione del server
 * \param [out] server_ds Puntatore doppio usato per restituire al chiamante la struttura dati inizializzata
 * \return Ritorna 0 se l'inizializzazione ha avuto successo, -1 altrimenti
 */
int init_ds(struct serv_params *params, struct fs_ds_t **server_ds) {
    // controllo che siano entrambi non nulli, altrimenti ho segmentation fault
    if(!(params && server_ds)) {
        return -1;
    }

    *server_ds = calloc(1, sizeof(struct fs_ds_t));
    if(!(*server_ds)) {
        return -1;
    }

    // Setto il timestamp iniziale
    (*server_ds)->start_tm = time(NULL);

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
    pthread_mutex_init(&((*server_ds)->mux_files), NULL);
    pthread_mutex_init(&((*server_ds)->mux_mem), NULL);
    pthread_mutex_init(&((*server_ds)->mux_jobq), NULL);
    pthread_mutex_init(&((*server_ds)->mux_cacheq), NULL);
    pthread_mutex_init(&((*server_ds)->mux_clients), NULL);
    // inizializzo cond variable
    pthread_cond_init(&((*server_ds)->new_job), NULL);

    // Inizializzo le code per le richieste
    (*server_ds)->job_queue = queue_init();
    (*server_ds)->cache_q = queue_init();
    if(!((*server_ds)->job_queue && (*server_ds)->cache_q)) {
        // errore di allocazione
        return -1;
    }

    // Inizializzo la struttura utilizzata dal thread di logging
    if(((*server_ds)->log_thread_config = malloc(sizeof(struct logging_params))) == NULL) {
        // fallita inizializzazione
        return -1;
    }
    // Inizializzo la coda di richieste
    if(((*server_ds)->log_thread_config->log_requests = queue_init()) == NULL) {
        // fallita inizializzazione della coda di richieste
        return -1;
    }
    // Inizializzo mutex e cond variables su tale coda
    pthread_mutex_init(&((*server_ds)->log_thread_config->mux_logq), NULL);
    pthread_cond_init(&((*server_ds)->log_thread_config->new_logrequest), NULL);
    // Copio il puntatore alla stringa contenente il path del file di log
    (*server_ds)->log_thread_config->log_fpath = strdup(params->log_path);

    // Setto i limiti di numero di file e memoria (il resto dei parametri sono azzerati di default)
    (*server_ds)->max_files = params->max_fcount;
    (*server_ds)->max_mem = MBYTE_TO_BYTE(params->max_memsz);

    // Tutte le strutture dati inizializzate con successo
    return 0;
}

/**
 * \brief Funzione che libera la memoria allocata per la struttura dati del server
 * \param [in] server_ds La struttura dati da liberare. Vengono liberate anche le strutture dati interne (code, ...) previo controllo
 */
void free_serv_ds(struct fs_ds_t *server_ds) {
    if(server_ds) {
        // libero la hash table
        if(icl_hash_destroy(server_ds->fs_table, free, free_file) != 0) {
            // errore nel liberare memoria
            perror("[SERVER] Impossibile liberare tabella hash");
        }
        // libero le code di job e di cache
        if(server_ds->job_queue) {
            free_Queue(server_ds->job_queue, free);
        }
        if(server_ds->cache_q) {
            free_Queue(server_ds->cache_q, free);
        }

        // libero l'array di client
        if(server_ds->active_clients) {
            for(size_t i = 0; i < server_ds->connected_clients; i++) {
                free(server_ds->active_clients[i].last_op_path);
            }
            free(server_ds->active_clients);
        }

        // chiudo le pipe se avevo slow_term (altrimenti erano già state chiuse)
        if(server_ds->slow_term == 1) {
            if( close(server_ds->feedback[0]) == -1
                || close(server_ds->feedback[1]) == -1
                || close(server_ds->termination[0]) == -1
                || close(server_ds->termination[1] == -1))
            {
                perror("[SERVER] Impossibile chiudere una pipe");
            }
        }

        // dealloco la struttura del thread di logging e libero la coda ivi contenuta
        // non necessario liberare il path perché è semplicemente un alias della
        // stringa in serv_params (in server.c) e quella viene deallocata comunque
        // alla terminazione del server
        if(server_ds->log_thread_config && server_ds->log_thread_config->log_requests) {
            free_Queue(server_ds->log_thread_config->log_requests, free);
        }
        if(server_ds->log_thread_config) {
            free(server_ds->log_thread_config->log_fpath);
            free(server_ds->log_thread_config);
        }

        free(server_ds);
    }
}

//-----------------------------------------------------------------------------------
// Operazioni sui file

/**
 * \brief Funzione per inserire un nuovo file nel server
 *
 * Questa funzione inserisce nel server un nuovo file ed inizializza ai valori di
 * default i suoi metadati: il file viene marcato come aperto da client, con dati NULL e
 * dimensione 0, oltre a settare la ME per client se era stata passata la flag O_LOCKFILE
 * \param [in] client Socket del client che ha richiesto la creazione del file
 * \param [in] flags OR di flags specificate, di cui interessa solo se è settata O_LOCKFILE
 * \return Ritorna un puntatore al file creato se ha successo, NULL altrimenti
 */
struct fs_filedata_t *newfile(const int client, const int flags) {
    struct fs_filedata_t *file = NULL;
    // Alloco ed inizializzo il file
    if((file = malloc(sizeof(struct fs_filedata_t))) == NULL) {
        // errore di allocazione
        return NULL;
    }
    // Inizializzo i campi della struct
    file->data = NULL;
    file->size = 0;
    pthread_mutex_init(&(file->mux_file), NULL);
    pthread_cond_init(&(file->mod_completed), NULL);
    file->modifying = 0;
    // Se devo settare mutua esclusione da parte del client che lo crea lo faccio, altrimenti -1
    if(flags & O_LOCKFILE) {
        file->lockedBy = client;
    }
    else {
        file->lockedBy = -1;
    }
    // inizializzo la coda (vuota) di clients in attesa della lock
    if((file->waiting_clients = queue_init()) == NULL) {
        free_file(file);
        return NULL;
    }
    pthread_cond_init(&(file->lock_free), NULL);
    if((file->openedBy = malloc(sizeof(int))) == NULL) {
        // errore di allocazione: libero memoria
        free_file(file);
        return NULL;
    }
    file->openedBy[0] = client; // setto il file come aperto da questo client
    file->nopened = 1; // il numero di client che ha il file aperto
    
    return file;
}

/**
 * \brief Funzione che libera la memoria occupata dal puntatore a file (fs_fileddata_t) passato come parametro
 * \param [in,out] file Puntatore al file da liberare. Il parametro ha tipo void* perché viene passata a icl_hash_destroy()
 */
void free_file(void *file) {
    struct fs_filedata_t *f = (struct fs_filedata_t *)file;
    if(f) {
        if(f->data) {
            free(f->data);
        }
        if(f->openedBy) {
            free(f->openedBy);
        }
        if(f->waiting_clients) {
            // Se necessario svuoto la coda
            struct node_t *n = NULL;
            while((n = pop(f->waiting_clients)) != NULL) {
                free(n->data);
                free(n);
            }
            free(f->waiting_clients);
        }
        // rilascio lock nel caso in cui fosse acquisita (anche se fallisce non è rilevante)
        pthread_mutex_unlock(&(f->mux_file));
        free(f);
        f = NULL;
    }
}

//-----------------------------------------------------------------------------------
// Operazioni sui client

/**
 * La funzione inserisce il client connesso sul socket csock tra quelli attivi nel server
 * e successivamente attende con read bloccante la scrittura del PID del client sul socket.
 * Il PID ottenuto serve ad identificare il client (viene usato ad esempio per settare ME su un file)
 * \param [in,out] ds La struttura dati del server, che viene aggiornata opportunamente
 * \param [in] csock Il socket assegnato dalla funzione accept_connection() al client connesso
 * \return Ritorna 0 se l'aggiunta del client ha successo, -1 altrimenti
 */
int add_connection(struct fs_ds_t *ds, const int csock) {
    if(!(ds->active_clients)) {
        ds->active_clients = malloc(sizeof(struct client_info));
        if(!(ds->active_clients)) {
            return -1;
        }
    }

    LOCK_OR_KILL(ds, &(ds->mux_clients), ds->active_clients);

    if(ds->active_clients) {
        struct client_info *newptr = realloc(ds->active_clients, sizeof(struct client_info) * (ds->connected_clients + 1));
        if(!newptr) {
            UNLOCK_OR_KILL(ds, &(ds->mux_clients));
            return -1;
        }
        ds->active_clients = newptr;
    }
    // Il client è identificato dal suo PID all'interno del server; tale valore deve essere letto
    // dal socket csock sul quale la connessione è stata appena accettata
    int cpid;
    if(readn(csock, &cpid, sizeof(int)) != sizeof(int)) {
        UNLOCK_OR_KILL(ds, &(ds->mux_clients));
        return -1;
    }
    ds->active_clients[ds->connected_clients].PID = cpid; // PID identificherà il client
    ds->active_clients[ds->connected_clients].socket = csock;
    ds->active_clients[ds->connected_clients].last_op = 0;
    ds->active_clients[ds->connected_clients].last_op_flags = 0;
    ds->active_clients[ds->connected_clients].last_op_path = NULL;
    ds->connected_clients++;

    // Se necessario aggiorno il massimo numero di client connessi contemporaneamente
    if(ds->connected_clients > ds->max_connections) {
        ds->max_connections = ds->connected_clients;
    }

    UNLOCK_OR_KILL(ds, &(ds->mux_clients));

    return 0;
}

/**
 * La funzione rimuove il client connesso sul socket csock tra quelli attivi nel server (se presente).
 * Per identificare univocamente il client serve il PID, passato tramite cpid
 * \param [in,out] ds La struttura dati del server, che viene aggiornata opportunamente
 * \param [in] csock Il socket assegnato dalla funzione accept_connection() al client connesso
 * \param [in] cpid Il PID del client connesso su csock
 * \return Ritorna 0 se la rimozione del client ha successo, -1 altrimenti
 */
int rm_connection(struct fs_ds_t *ds, const int csock, const int cpid) {
    if(!ds) {
        return -1;
    }
    LOCK_OR_KILL(ds, &(ds->mux_clients), ds->active_clients);

    // Se non vi era alcun client connesso allora non posso rimuovere csock
    if(!(ds->active_clients) || ds->connected_clients == 0) {
        return -1;
    }
    // Cerco il pid tra quelli dei client connessi
    size_t i = 0;
    while(i < ds->connected_clients && ds->active_clients[i].PID != cpid) {
        i++;
    }
    if(i == ds->connected_clients) {
        // client non trovato
        UNLOCK_OR_KILL(ds, &(ds->mux_clients));
        return -1;
    }
    // libero il client trovato
    if(ds->active_clients[i].last_op_path) {
        free(ds->active_clients[i].last_op_path);
    }
    // shifto indietro di una posizione tutti gli altri
    for(; i < ds->connected_clients - 1; i++) {
        ds->active_clients[i].socket = ds->active_clients[i+1].socket;
        ds->active_clients[i].PID = ds->active_clients[i+1].PID;
        ds->active_clients[i].last_op = ds->active_clients[i+1].last_op;
        ds->active_clients[i].last_op_flags = ds->active_clients[i+1].last_op_flags;
        ds->active_clients[i].last_op_path = ds->active_clients[i+1].last_op_path;
    }
    ds->connected_clients--;
    // Se non è connesso più alcun client allora devo liberare l'array del tutto
    if(ds->connected_clients == 0) {
        free(ds->active_clients);
        ds->active_clients = NULL;
    }
    else {
        // Altrimenti devo riallocare ad un array di dimensione minore
        struct client_info *newptr = realloc(ds->active_clients, sizeof(struct client_info) * ds->connected_clients);
        if(!newptr) {
            // fallita riallocazione
            UNLOCK_OR_KILL(ds, &(ds->mux_clients));
            return -1;
        }
        ds->active_clients = newptr;
    }
    UNLOCK_OR_KILL(ds, &(ds->mux_clients));

    return 0;
}

/**
 * Aggiorna lo stato del client connesso sul socket csock con l'ultima operazione
 * conclusa con successo e le relative flags e path del file su cui l'operazione è
 * stata eseguita (se rilevanti, altrimenti 0 e NULL rispettivamente)
 * \param [in,out] ds La struttura dati del server, che viene aggiornata opportunamente
 * \param [in] csock Il socket sul quale il client è connesso
 * \param [in] cpid Il PID del client connesso su csock
 * \param [in] op Il carattere che identifica l'ultima operazione compiuta dal client, come specificato in fs-api.h
 * \param [in] op_flags Le (eventuali) flags dell'operazione op
 * \param [in] op_path Il path del file su cui è stata effettuata l'operazione op (se rilevante)
 * \return Ritorna 0 se ha successo, -1 altrimenti
 */
int update_client_op(struct fs_ds_t *ds, const int csock, const int cpid, const char op, const int op_flags, const char *op_path) {
    size_t i;
    LOCK_OR_KILL(ds, &(ds->mux_clients), ds->active_clients);
    for(i = 0; i < ds->connected_clients; i++) {
        if(ds->active_clients[i].PID == cpid) {
            ds->active_clients[i].last_op = op;
            ds->active_clients[i].last_op_flags = op_flags;
            if(ds->active_clients[i].last_op_path) {
                free(ds->active_clients[i].last_op_path);
                ds->active_clients[i].last_op_path = NULL;
            }
            if(op_path) {
                ds->active_clients[i].last_op_path = strndup(op_path, strlen(op_path) + 1);
                if(!ds->active_clients[i].last_op_path) {
                    UNLOCK_OR_KILL(ds, &(ds->mux_clients));
                    return -1;
                }
            }
            UNLOCK_OR_KILL(ds, &(ds->mux_clients));
            return 0;
        }
    }
    UNLOCK_OR_KILL(ds, &(ds->mux_clients));
    return -1;
}
