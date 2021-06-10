// file header del server, contenente la definizione di alcune strutture dati e costanti

#ifndef FS_SERVER_H_INCLUDED
#define FS_SERVER_H_INCLUDED

#include <pthread.h>
#include <icl_hash.h> // per hashtable
#include <utils.h> // per la coda sincronizzata

//-----------------------------------------------------------------------------------
// Definizioni relative al parsing del file di configurazione

// Definisco il path del file di configurazione di default
#define CONF_PATH_DFL "./config.conf"

// struttura dati contenente i parametri del server
struct serv_params {
    long int thread_pool;   // numero di thread worker del server
    long int max_memsz;     // massima occupazione memoria dei file
    long int max_fcount;    // massimo numero di file gestiti contemporaneamente
    char *sock_path;        // path del socket su cui il server accetta connessioni
    char *log_path;         // path del file di log del server
};
// Nomi dei parametri del file di configurazione
#define TPOOL "tpool"
#define MAXMEM "maxmem"
#define MAXFILES "maxfiles"
#define SOCK_PATH "sock_path"
#define LOG_PATH "log_path"
// Valori di default per i parametri
#define TPOOL_DFL 10
#define MAXMEM_DFL 32
#define MAXFILES_DFL 100
#define SOCK_PATH_DFL "./server.sock"
#define LOG_PATH_DFL "./server.log"
// definisco anche un limite superiore (ragionevole) alla lunghezza dei path di default
#define DFL_PATHLEN 50

// Funzione per il parsing del file di configurazione
// Prova a leggere e fare il parsing del file conf_fpath (path relativo).
// Se conf_fpath è NULL o non è possibile aprirlo allora usa il file di configurazione
// di default. Ritorna 0 ed inizializza params se ha successo, -1 altrimenti
int parse_config(struct serv_params *params, const char *conf_fpath);
// Funzione che crea il socket su cui il server accetterà le connessioni da parte dei client
// Ritorna 0 in caso di successo, -1 altrimenti
int sock_init(const char *addr, const size_t len);
// Funzione eseguita dai thread worker
// Params conterrà la struttura dati del server definita in seguito
void *work(void *params);
// Funzione eseguita dal thread che gestisce la terminazione
// params conterrà la struttura dati del server definita in seguito
void *term_thread(void *params);

//-----------------------------------------------------------------------------------
// Strutture dati del server per la memorizzazione dei file
// e per la gestione delle richieste della API

// Struttura dati che contiene i dati e metadati di un file nel fileserver, ma non il path
struct fs_filedata_t {
    void *data;      // I dati contenuti nel file
    size_t size;     // La dimensione in numero di byte del file
    int *openedBy;   // Un array di socket dei client che hanno aperto questo file
    int nopened;     // La dimensione (variabile) dell'array sopra
    int lockedBy;    // Il client (al più uno) che ha la ME sul file
};
// Funzione che libera la memoria occupata da un file
void free_file(void *file); // Il parametro ha tipo void* perché viene usata in icl_hash_destroy

// Struttura dati che contiene informazioni riguardo ad un client connesso al server (attraverso il socket)
struct client_info {
    int socket;            // il socket su cui il client è connesso (intero > 0)
    char last_op;          // il tipo dell'ultima operazione eseguita con successo da questo client
    // 0 di default
    int last_op_flags;     // le flag dell'ultima operazione eseguita con successo
    // 0 di default
    char *last_op_path;    // path del file su cui l'ultima operazione è stata completata con successo
    // NULL se non rilevante
};

// Definisco la terminazione veloce e lenta come interi che possano entrare in un char
// ma che non siano in conflitto con le altre richieste definite in fs-api.h
#define FAST_TERM 1
#define SLOW_TERM 2

// Struttura dati condivisa del server
struct fs_ds_t {
    // Massimo numero di file che possono essere presenti nel server contemporaneamente
    size_t max_files;
    // Numero di file aperti nel server in questo momento
    size_t curr_files;
    // Numero massimo di file memorizzati nel server durante la sua attività
    size_t max_nfiles;
    // Vale sempre la relazione max_files >= max_nfiles >= curr_files

    // Massima capacità del server (in byte)
    size_t max_mem;
    // Quantità di memoria occupata dai file presenti nel server (solo segmento dati)
    size_t curr_mem;
    // Massima quantità di memoria occupata dai file nel server durante la sua attività
    size_t max_used_mem;
    // Vale sempre la relazione max_mem >= max_used_mem >= curr_mem

    // Hash table condivisa nel server, che contiene i file
    icl_hash_t *fs_table;
    // Mutex per l'accesso alla ht
    pthread_mutex_t mux_ht;

    // Coda per la comunicazione delle richieste dal thread manager ai worker threads
    struct Queue *job_queue;
    // Lock e cond variables per l'accesso in ME
    pthread_mutex_t mux_jobq;
    pthread_cond_t new_job;

    // Coda per l'implementazione della politica di rimpiazzamento FIFO
    struct Queue *cache_q;
    // Lock e cond variables per l'accesso in ME
    pthread_mutex_t mux_cacheq;
    pthread_cond_t new_cacheq;

    // Array contenenti informazioni sui client connessi
    struct client_info *active_clients;
    // Il numero di client connessi al momento (ovvero la dimensione dell'array sopra)
    size_t connected_clients;

    // Numero di volte che l'algoritmo di rimpiazzamento dei file è stato eseguito con successo
    size_t cache_triggered;

    // Pipe per il feedback dal worker al manager thread
    int feedback[2];

    // Pipe per la gestione della terminazione
    int termination[2];
    int worker_term;

    // File descriptor del file di log del server
    int log_fd;
    // Mutex per la scrittura in ME sul file di log
    pthread_mutex_t mux_log;
};
// Funzione che inizializza tutte le strutture dati: prende in input i parametri del server
// ed alloca e inizializza la struttura server_ds passata tramite doppio puntatore.
// Ritorna 0 se ha successo, -1 altrimenti
int init_ds(struct serv_params *params, struct fs_ds_t **server_ds);
// Funzione che libera la memoria allocata per la struttura dati
void free_serv_ds(struct fs_ds_t *server_ds);
// Funzione che aggiunge il client sul socket csock a quelli connessi
// Ritorna 0 se ha successo, -1 altrimenti
int add_connection(struct fs_ds_t *ds, const int csock);
// Funzione che rimuove il client sul socket csock da quelli connessi
// Ritorna 0 se ha successo, -1 altrimenti
int rm_connection(struct fs_ds_t *ds, const int csock);
// Funzione che aggiorna lo stato del client sul socket csock con l'ultima operazione
// terminata con successo
// Ritorna 0 se ha successo, -1 altrimenti
int update_client_op(struct fs_ds_t *ds, const int sock, const char op, const int op_flags, const char *op_path);

//-----------------------------------------------------------------------------------
// Operazioni sui file (implementazione delle funzionalità della API)

// Apre il file con path pathname (se presente) per il socket passato come parametro con le flag richieste
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_openFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, int flags);
// Chiude il file con path pathname (se presente) per il socket passato come parametro
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_closeFile(struct fs_ds_t *ds, const char *pathname, const int client_sock);
// Legge il file con path pathname (se presente nel server) e lo invia lungo il socket client_sock
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_readFile(struct fs_ds_t *ds, const char *pathname, const int client_sock);
// Legge n file nel server (quelli meno recenti per come è implementata) e li invia al client
// Se n<=0 allora legge tutti i file presenti nel server
// Se ha successo ritorna il numero di file letti, -1 altrimenti
int api_readN(struct fs_ds_t *ds, const int n, const int client_sock);
// Scrive in append al file con path pathname (se presente) il buffer buf di lunghezza size
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_appendToFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const size_t size, void *buf);
// Se l'operazione precedente del client client_sock (completata con successo) era stata
// openFile(pathname, O_CREATEFILE) allora il file pathname viene troncato (ritorna a dimensione nulla)
// altrimenti crea un nuovo file vuoto (se possibile)
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_writeFile(struct fs_ds_t *ds, const char *pathname, const int client_sock);
// Rimuove dal server il file con path pathname, se presente e non lockato da un altro socket
// Ritorna 0 se ha successo, -1 altrimenti
int api_rmFile(struct fs_ds_t *ds, const char *pathname, const int client_sock);

//-----------------------------------------------------------------------------------
// Varie funzioni di utilità implementate in api_backend-utils.c

// Cerca il file con quel nome nel server: se lo trova ritorna un puntatore ad esso
// Altrimenti ritorna NULL
struct fs_filedata_t *find_file(struct fs_ds_t *ds, const char *fname);
// Inserisce nel fileserver il file path con contenuto buf, di dimensione size, proveniente dal socket client
// Se l'inserimento riesce allora ritorna un puntatore al file originale e rimpiazza i dati con buf
// Se buf == NULL allora crea un file vuoto, ritornando il file creato
// Se l'operazione fallisce ritorna NULL
struct fs_filedata_t *insert_file(struct fs_ds_t *ds, const char *path, const void *buf, const size_t size, const int client);
// Algoritmo di rimpiazzamento dei file: rimuove uno o più file per fare spazio ad un file di dimensione
// newsz byte, in modo tale da avere una occupazione in memoria inferiore a
// ds->max_mem - newsz al termine dell'esecuzione della funzione
// Ritorna il numero di file espulsi (>=0) se il rimpiazzamento è avvenuto con successo, -1 altrimenti.
// Se la funzione ha successo inizializza e riempe con i file espulsi ed i loro path le due code passate come
// parametro alla funzione, altrimenti se l'algoritmo di rimpiazzamento fallisce
// esse non sono allocate e comunque lasciate in uno stato inconsistente
int cache_miss(struct fs_ds_t *ds, size_t newsz, struct Queue **paths, struct Queue **files);

//-----------------------------------------------------------------------------------
//Gestione del logging

// Funzione per effettuare il logging: prende come parametri
// La struttura dati del server (ds)
// Il codice di errore (corrisponderà ad errno a meno di casi particolari)
// La stringa da stampare nel file di log (null-terminated)
// La funzione ritorna 0 se ha successo, -1 se riscontra un errore (setta errno)
int logging(struct fs_ds_t *ds, int errcode, char *message);

#endif
