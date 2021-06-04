// file header del server che contiene la definizione di cose utili al server

#ifndef FS_SERVER_H_INCLUDED
#define FS_SERVER_H_INCLUDED

#include <stddef.h> // per il tipo size_t
#include <icl_hash.h> // per hashtable
#include <utils.h> // per la coda sincronizzata

//-----------------------------------------------------------------------------------
// Parsing del file di configurazione

// Definisco il path di default del file di configurazione
#define CONF_PATH_DFL "./config.txt"

// struttura contenente i parametri del server
struct serv_params {
    long int thread_pool;   // numero di thread worker del server
    long int max_memsz;     // massima occupazione memoria dei file
    long int max_fcount;    // massimo numero di file gestiti contemporaneamente
    char *sock_path;        // path del socket server
    char *log_path;         // path del file di log
};

// i field del file di configurazione sono stringhe definite a tempo di compilazione
#define TPOOL "tpool"
#define MAXMEM "maxmem"
#define MAXFILES "maxfiles"
#define SOCK_PATH "sock_path"
#define LOG_PATH "log_path"
// valori di default per i parametri
#define TPOOL_DFL 10
#define MAXMEM_DFL 1024
#define MAXFILES_DFL 10
#define SOCK_PATH_DFL "./server.sock"
#define LOG_PATH_DFL "./server.log"
// definisco anche un limite superiore (ragionevole) alla lunghezza dei path di default
#define DFL_PATHLEN 50

// funzione per il parsing del file di configurazione
int parse_config(struct serv_params *params, const char *conf_fpath);

// Inizializza il socket su cui il server accetterà le connessioni da parte dei client
int sock_init(const char *addr, const size_t len);

//-----------------------------------------------------------------------------------
// Strutture dati del server per la memorizzazione dei file e gestione delle richieste della API

// Struttura dati che contiene i dati e metadati di un file nel fileserver, ma non il path
struct fs_filedata_t {
    void *data;      // I dati contenuti nel file
    size_t size;     // La dimensione in numero di byte del file
    int *openedBy;   // Un array di socket dei client che hanno aperto questo file
    int nopened;     // La dimensione (variabile) dell'array sopra
    int lockedBy;    // Il client (al più uno) che ha la ME sul file
};
// dichiaro anche la funzione per liberare la struttura
void free_file(void *file);

// Dichiaro qui tutte le strutture dati condivise del server
struct fs_ds_t {
    // Massimo numero di file aperti in ogni istante nel server
    size_t max_files;
    // Numero di file aperti nel server
    size_t curr_files;
    // Numero massimo di file memorizzati nel server durante la sua attività
    size_t max_nfiles;

    // Massima capacità del server (in byte)
    size_t max_mem;
    // Quantità di memoria occupata in ogni istante dal server
    size_t curr_mem;
    // Massima quantità di memoria occupata dai file nel server durante la sua attività
    size_t max_used_mem;

    // hash table condivisa nel server
    icl_hash_t *fs_table;
    // mutex per l'accesso alla ht
    pthread_mutex_t mux_ht;

    // coda per la comunicazione delle richieste ai worker
    struct Queue *job_queue;
    // lock e cond variables per l'accesso in ME
    pthread_mutex_t mux_jobq;
    pthread_cond_t new_job;

    // coda per l'implementazione della politica di rimpiazzamento FIFO
    struct Queue *cache_q;
    // lock e cond variables per l'accesso in ME
    pthread_mutex_t mux_cacheq;
    pthread_cond_t new_cacheq;

    // numero di volte che l'algoritmo di rimpiazzamento dei file è stato chiamato
    size_t cache_triggered;

    // pipe per il feedback dal worker al manager
    int feedback[2];

    // Pipe per la gestione della terminazione
    int termination[2];

    int log_fd; // file descriptor del file di log
    pthread_mutex_t mux_log;
};

// Funzione che inizializza tutte le strutture dati: prende in input i parametri del server
// e riempe la struttura passata tramite puntatore
int init_ds(struct serv_params *params, struct fs_ds_t **server_ds);
// Funzione che libera la memoria allocata per la struttura dati
void free_serv_ds(struct fs_ds_t *server_ds);

// Funzione eseguita dal thread che gestisce la terminazione
void *term_thread(void *params);

// Funzione eseguita dal worker
void *work(void *params);

//-----------------------------------------------------------------------------------
// Operazioni sui file

// Apre il file con path pathname (se presente) per il socket passato come parametro con le flag richieste
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_openFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, int flags);

// Legge il file con path pathname (se presente nel server) e lo invia lungo il socket client_sock
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_readFile(struct fs_ds_t *ds, const char *pathname, const int client_sock);


// Legge n file nel server (quelli meno recenti per come è implementata) e li invia al client
// Se n<=0 allora legge tutti i file presenti nel server
// Se ha successo ritorna 0, -1 altrimenti
int api_readN(struct fs_ds_t *ds, const int n, const int client_sock);

// Scrive in append al file con path pathname (se presente) il buffer buf di lunghezza size
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_appendToFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const size_t size, char *buf);

// Se l'operazione precedente del client client_sock (completata con successo) era stata
// openFile(pathname, O_CREATEFILE) allora il file pathname viene troncato (ritorna a dimensione nulla)
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_writeFile(struct fs_ds_t *ds, const char *pathname, const int client_sock);

// Varie funzioni di utilità implementate in api_backend-utils.c

// Cerca il file con quel nome nel server: se lo trova ritorna un puntatore ad esso
// Altrimenti ritorna NULL
struct fs_filedata_t *find_file(struct fs_ds_t *ds, const char *fname);
// Inserisce nel fileserver il file path con contenuto buf, di dimensione size, proveniente dal socket client
// Se l'inserimento riesce allora ritorna un puntatore al file originale e rimpiazza i dati con buf
// Se buf == NULL allora crea un file vuoto, ritornando il file stesso
// Se l'operazione fallisce ritorna NULL
struct fs_filedata_t *insert_file(struct fs_ds_t *ds, const char *path, const void *buf, const size_t size, const int client);

// Algoritmo di rimpiazzamento dei file: rimuove uno o più file per fare spazio ad un file di dimensione
// newsz byte, in modo tale da avere una occupazione in memoria inferiore a ds->max_mem - newsz al termine
// Ritorna il numero di file espulsi (>0) se il rimpiazzamento è avvenuto con successo, -1 altrimenti.
// Se la funzione ha successo inizializza e riempe con i file espulsi ed i loro path le due code passate come 
// parametro alla funzione, altrimenti se l'algoritmo di rimpiazzamento fallisce
// esse non sono allocate e comunque lasciate in uno stato inconsistente
int cache_miss(struct fs_ds_t *ds, size_t newsz, struct Queue **paths, struct Queue **files);

//-----------------------------------------------------------------------------------
//Gestione del logging

// Funzione per effettuare il logging: prende come parametri
// 1) il file descriptor (deve essere già aperto in scrittura) del file di log
// 2) il codice di errore (errno)
// 3) la stringa da stampare nel file (null-terminated)
// La funzione ritorna:
// 0 se ha successo
// -1 se riscontra un errore (settato errno)
int logging(struct fs_ds_t *ds, int errcode, char *message);


#endif
