/**
 *  \file server-utils.h
 *  \brief File header del server, contenente la definizione delle strutture dati e macro
 *
 * Questo file raccoglie la definizione di tutte le strutture dati e funzioni utilizzate
 * dai vari componenti del server
 */

#ifndef FS_SERVER_H_INCLUDED
#define FS_SERVER_H_INCLUDED

#include <pthread.h>
#include <icl_hash.h> // per hashtable
#include <utils.h> // per la coda sincronizzata
#include <time.h> // per time_t

//-----------------------------------------------------------------------------------
// Definizioni relative al parsing del file di configurazione

// Path del file di configurazione di default usato dal server */
#define CONF_PATH_DFL "config.conf"

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

/**
 *  \brief Parametri di esecuzione del server, letti dal file di configurazione
 *
 * La struttura dati contiene i vari parametri di configurazione del server
 * (numero di thread worker, quantità di memoria, ...). La struttura è il prodotto della
 * funzione parse_config()
 */
struct serv_params {
    long int thread_pool;   ///< Numero di thread worker che serviranno le richieste dei client
    long int max_memsz;     ///< Capacità massima in Mbyte che può essere occupata contemporaneamente dai file
    long int max_fcount;    ///< Massimo numero di file che possono essere presenti contemporaneamente nel server
    char *sock_path;        ///< Path del socket (locale) su cui il server accetta nuove connessioni
    char *log_path;         ///< Path del file di log del server
};

// definisco anche un valore di default per la lunghezza del path del file di configurazione
#define DFL_PATHLEN 100

/// Funzione per il parsing del file di configurazione
int parse_config(struct serv_params *params, const char *conf_fpath);

//-----------------------------------------------------------------------------------
// Creazione socket e funzioni eseguite dai thread creati dal thread manager

/// Funzione che crea il socket su cui il server accetterà le connessioni dei client
int sock_init(const char *addr);
/// Funzione eseguita dai thread worker
void *work(void *params);
/// Funzione eseguita dal thread che gestisce la terminazione
void *term_thread(void *params);
/// Funzione eseguita dal thread che effettua la scrittura del file di log
void *logging(void *params);
/// Funzione eseguita dal thread che attende la liberazione della lock
void *wait_lock(void *params);

//-----------------------------------------------------------------------------------
// Strutture dati del server per la memorizzazione dei file ed operazioni su di essi

/**
 * \brief Struttura dati che definisce un file all'interno del server
 *
 * Questa struttura contiene i dati ed i metadati di un file presente nel server
 * compresa la dimensione corrente e la variabile per effettuare il lock. Non è presente
 * il path del file, che è invece nella hashtable utilizzata per il lookup
 */
struct fs_filedata_t {
    void *data;      ///< Il buffer contenente i dati del file
    size_t size;     ///< La dimensione in numero di byte del file
    pthread_mutex_t mux_file; ///< Mutex utilizzato per la modifica del file
    pthread_cond_t mod_completed; ///< CV per controllare la modifica del file
    int modifying;   ///< Flag settata se è in corso una modifica al file (per attendere sulla CV)
    int lockedBy;    ///< Il client (al più uno) che ha la ME sul file. Se nessuno ha ME è settata a -1
    struct Queue *waiting_clients; ///< Clients in attesa di acquisire lock
    pthread_cond_t lock_free; ///< CV per aspettare che si liberi lock sul file
    int *openedBy;   ///< Array di socket per i quali questo file è aperto (possono richiedervi operazioni)
    int nopened;     ///< La dimensione (variabile) dell'array openedBy
};

/// Funzione per inserire nel fileserver un nuovo file
struct fs_filedata_t *newfile(const int client, const int flags);
// Funzione che libera la memoria occupata da una struct fs_filedata_t
void free_file(void *file);

//-----------------------------------------------------------------------------------
//Gestione del logging

/**
 * \brief Struttura utilizzata per raggruppare i parametri da passare al thread per il logging
 */
struct logging_params {
    char *log_fpath;                 // path del file nel quale effettuare il logging
    struct Queue *log_requests;      // coda nella quale le richieste di logging sono immesse
    pthread_mutex_t mux_logq;        // mutex per la coda di richieste
    pthread_cond_t new_logrequest;   // condition variable per indicare la presenza di richieste di log
};

/** \brief Struttura delle richieste di logging
 *
 * La struttura della richiesta è formata da un messaggio di log (che viene stampato senza modifiche)
 * ed un codice di errore: se error_code > 0 allora rappresenta un errore di sistema
 * (tipicamente system call che sono fallite), per cui viene tradotto opportunamente in
 * una stringa che verrà stampata nel file di log. Se error_code == 0 allora è necessario
 * effettuare il log dell'operazione, ma non si è verificato un errore (oppure è un errore
 * di utilizzo della API, per cui l'operazione richiesta viene negata). Se error_code == -1
 * e message == NULL allora è una richiesta di terminazione del thread di log, inserita
 * tipicamente dal thread manager subito prima di effettuare il join e terminare il server
 */
struct log_request {
    int error_code;
    char *message;
};

// Funzione di utilità per inserire una richiesta di log in coda, avente come messaggio
// la stringa passata come parametro e come codice di errore l'intero passato
// (se err == 0 indica che non è un errore di sistema, ma logico di utilizzo del server)
// La funzione ritorna 0 se ha successo (è stata inserita la richiesta in coda e sarà
// scritta sul file di log appena possibile), -1 altrimenti
int put_logmsg(struct logging_params *lp, const int err, char *message);

/**
 * \brief Struttura dati condivisa contenente lo stato del server
 *
 * Questa struttura dati, condivisa da tutti i thread in esecuzione nel server, contiene
 * lo stato del fileserver suddiviso in variabili, raggruppabili in sezioni. Il primo gruppo
 * di variabili mantiene i valori numerici che caratterizzano la configurazione in ogni momento
 * ed i relativi mutex per regolarne l'accesso (timestamp di avvio, files, memoria).
 * Il secondo gruppo di strutture dati immagazzina i file presenti nel server e la tabella
 * hash usata per stabilire la corrispondenza tra path e puntatori a file. Il terzo gruppo
 * di variabili immagazzina informazioni sui client connessi e lo stato di terminazione.
 * La struttura dati finale è per passare informazioni al thread di logging
 */
struct fs_ds_t {
    //-------------------------------------------------------------------------------
    /// Timestamp del tempo di avvio del server (dopo l'allocazione di questa struttura)
    time_t start_tm;

    /// Massimo numero di file che possono essere presenti nel server contemporaneamente
    /// Vale sempre la relazione max_files >= max_nfiles >= curr_files
    size_t max_files;
    /// Numero di file aperti nel server in questo momento
    size_t curr_files;
    /// Numero massimo di file memorizzati nel server durante la sua attività
    size_t max_nfiles;
    /// mutex per modificare curr_files e max_nfiles
    pthread_mutex_t mux_files;

    /// Massima capacità del server (in byte)
    /// Vale sempre la relazione max_mem >= max_used_mem >= curr_mem
    size_t max_mem;
    /// Quantità di memoria occupata dai file presenti nel server (solo segmento dati)
    size_t curr_mem;
    /// Massima quantità di memoria occupata dai file nel server durante la sua attività
    size_t max_used_mem;
    /// mutex per modificare curr_mem e max_used_mem
    pthread_mutex_t mux_mem;

    //-------------------------------------------------------------------------------
    /// Hash table condivisa nel server, che contiene la corrispondenza tra path e putatore al file
    icl_hash_t *fs_table;

    /// Coda per la comunicazione delle richieste dal thread manager ai worker threads
    struct Queue *job_queue;
    /// Lock per l'accesso in ME a job_queue
    pthread_mutex_t mux_jobq;
    /// CV per regolare l'accesso a job_queue
    pthread_cond_t new_job;

    /// Coda per l'implementazione della politica di rimpiazzamento FIFO
    struct Queue *cache_q;
    /// Lock per l'accesso in ME a cache_q
    pthread_mutex_t mux_cacheq;

    /// Conta il numero di invocazioni andate a buon file dell'algoritmo di rimpiazzamento
    size_t cache_triggered;

    //-------------------------------------------------------------------------------
    /// Struttura dati contenente informazioni sui client connessi al server
    struct client_info *active_clients;
    /// Il numero di client connessi al momento
    size_t connected_clients;
    /// Massimo numero di connessioni contemporanee durante l'esecuzione del server
    size_t max_connections;
    /// Mutex per accedere alla struttura dati che contiene le informazioni dei client connessi
    pthread_mutex_t mux_clients;


    /// Pipe per notificare al manager thread che una richiesta è stata servita da un worker
    int feedback[2];
    /// Pipe per notificare al thread manager che è stato ricevuto un segnale di terminazione
    int termination[2];
    /// Flag per indicare terminazione lenta (con SIGHUP)
    int slow_term;

    //-------------------------------------------------------------------------------
    /// Struttura dati per la gestione del logging
    struct logging_params *log_thread_config;
};

// Funzioni implementate in server-ds.c
int init_ds(struct serv_params *params, struct fs_ds_t **server_ds);
void free_serv_ds(struct fs_ds_t *server_ds);

// Definisco la terminazione veloce e lenta come interi che possano entrare in un char
// ma che non siano in conflitto con le altre richieste definite in fs-api.h
#define FAST_TERM 1
#define SLOW_TERM 2

//-----------------------------------------------------------------------------------
/**
 * \brief Struttura dati che incapsula le informazioni relative ad un client connesso
 *
 * In ogni momento possono essere connessi al server zero o più client distinti,
 * ognuno con un proprio PID. Ogni client si connette su un socket (che potrebbe essere
 * riutilizzato a seguito della disconnessione di un client). Di ogni client interessa
 * mantenere lo stato dell'ultima operazione eseguita con successo per garantire la semantica
 * di alcune operazioni della API
 */
struct client_info {
    int PID;               ///< Il PID del client, che lo identifica all'interno del server
    int socket;            ///< Il socket su cui il client è connesso (intero > 0)
    char last_op;          ///< il tipo dell'ultima operazione eseguita con successo da questo client (0 default)
    int last_op_flags;     ///< Le flags (in OR bit a bit) dell'ultima operazione eseguita con successo (0 default)
    char *last_op_path;    ///< Path del file su cui è stata compiuta l'operazione (NULL se non rilevante)
};

/// Funzione che aggiunge il client sul socket csock a quelli connessi
int add_connection(struct fs_ds_t *ds, const int csock);
/// Funzione che rimuove il client sul socket csock da quelli connessi
int rm_connection(struct fs_ds_t *ds, const int csock, const int cpid);
/// Funzione che aggiorna lo stato del client con l'ultima operazione terminata con successo
int update_client_op(struct fs_ds_t *ds, const int csock, const int cpid, const char op, const int op_flags, const char *op_path);


//-----------------------------------------------------------------------------------

/// Funzione per tentare di fare lock e riportare l'errore prima di terminare il server se fallisce
/// Se obj fosse NULL e la lock fallisce allora ho deallocato obj contenente la variabile di lock
/// (questo avviene per i file rimossi su cui altri thread stavano aspettando di avere lock)
void LOCK_OR_KILL(struct fs_ds_t *ds, pthread_mutex_t *mutex, void *obj);
/// Funzione per tentare di fare unlock e riportare l'errore prima di terminare il server se fallisce
void UNLOCK_OR_KILL(struct fs_ds_t *ds, pthread_mutex_t *mutex);

//-----------------------------------------------------------------------------------
// Operazioni sui file (implementazione delle funzionalità della API)

int api_openFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const int client_PID, int flags);
int api_closeFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const int client_PID);
int api_readFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const int client_PID);
int api_readN(struct fs_ds_t *ds, const int n, const int client_sock, const int client_PID);
int api_appendToFile(struct fs_ds_t *ds, const char *pathname,
    const int client_sock,const int client_PID, const size_t size, void *buf);
int api_writeFile(struct fs_ds_t *ds, const char *pathname,
    const int client_sock, const int client_PID, const size_t size, void *buf);
pthread_t api_lockFile(struct fs_ds_t*ds, const char *pathname, const int client_sock, const int client_PID);
int api_unlockFile(struct fs_ds_t*ds, const char *pathname, const int client_sock, const int client_PID);
int api_rmFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const int client_PID);

//-----------------------------------------------------------------------------------
// Varie funzioni di utilità implementate in api_backend-utils.c

/// Cerca il file con path pathname nel server
struct fs_filedata_t *find_file(struct fs_ds_t *ds, const char *fname);
/// Crea un file vuoto o aggiorna il file con path dato
struct fs_filedata_t *insert_file(struct fs_ds_t *ds, const char *path, const int flags, const void *buf, const size_t size, const int client);
/// Algoritmo di rimpiazzamento FIFO dei file: seleziona uno o più file come vittima
/// in modo da avere almeno newsz bytes liberi rispetto alla quantità massima di memoria
int cache_miss(struct fs_ds_t *ds, size_t newsz, struct Queue **paths, struct Queue **files);

#endif
