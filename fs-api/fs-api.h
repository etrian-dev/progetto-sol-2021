// header file contenente la dichiarazione delle funzioni della API
// che permette al client di comunicare con il server

#ifndef FS_API_H_DEFINED
#define FS_API_H_DEFINED

#include <utils.h> // per il tipo struct Queue
#include <time.h>

#define SOCK_PATH_MAXLEN 108 // per socket AF_UNIX è la massima lunghezza del path al socket

//-----------------------------------------------------------------------------------
// Struttura dati utilizzata dalla API

// La API mantiene una struttura dati interna per associare ad ogni client il proprio socket
struct conn_info {
    char *sockname;     // nome del socket (supposto univoco per tutta la sessione)
    int conn_sock;     // array di coppie (socket fd, client PID) allocato in modo contiguo
};
#define NCLIENTS_DFL 10 // il numero di slot preallocati per clients

extern struct conn_info *clients_info;

//-----------------------------------------------------------------------------------
// Funzioni di utilità della API, implementate in fs-api-utils.c

// Inizializza la struttura dati della API con il socket passato come argomento
int init_api(const char *sname);
// Funzione che ritorna il socket su cui il client che l'ha chiamata è connesso
// e -1 se il client non connesso
int isConnected(void);


// definizione delle operazioni implementate dalla API
#define CLOSE_CONN '!' // usata in closeConnection per segnalare al server di terminare la connessione col client
#define OPEN_FILE 'O'
#define CLOSE_FILE 'Q'
#define READ_FILE 'R'
#define READ_N_FILES 'N'
#define APPEND_FILE 'A'
#define WRITE_FILE 'W'
#define LOCK_FILE 'L'
#define UNLOCK_FILE 'U'
#define REMOVE_FILE 'X'
// definisco il tipo di risposta: richiesta completata con successo ('Y') oppure no ('N')
#define REPLY_YES 'Y'
#define REPLY_NO 'N'
// definisco flags per le operazioni nel fileserver
#define O_CREATEFILE 0x1
#define O_LOCKFILE 0x2

// struttura che definisce il tipo delle richieste
struct request_t {
    char type;              // tipo della richiesta: uno tra quelli definiti sopra
    int pid;                // il PID del client che effettua la richiesta
    int flags;              // flags della richiesta: O_CREATE, O_LOCK oppure nessuna (0x0)
    size_t path_len;        // lunghezza della stringa path (compreso terminatore)
    size_t buf_len;         // lunghezza del buffer buf: 0 se non utilizzata
};
// Funzione che alloca e setta i parametri di una richiesta
// Ritorna un puntatore ad essa se ha successo, NULL altrimenti
struct request_t *newrequest(const char type, const int flags, const size_t pathlen, const size_t buflen);

// struttura che definisce il tipo delle risposte
struct reply_t {
    char status;          // stato della risposta: 'Y' o 'N'
    int nbuffers;         // il numero di file (buffer) restituiti: un numero >= 0
    size_t paths_sz;      // lunghezza della stringa di path da leggere (se rilevante)
    // Il formato della stringa di path è il seguente: ciascun path è concatenato
    // e separato dal path successivo dal carattere '\n'. La stringa termina con '\0'.
};
// Funzione che alloca e setta i parametri di una risposta
// Ritorna un puntatore ad essa se ha successo, NULL altrimenti
struct reply_t *newreply(const char stat, const int nbuf, const char **names);

// Funzione per leggere dal server dei file ed opzionalmente scriverli nella directory dir
int write_swp(const int server, const char *dir, int nbufs, const size_t *sizes, const char *paths);

//-----------------------------------------------------------------------------------
// Definizione delle operazioni

// apre la connessione al socket sockname, su cui il server sta ascoltando
int openConnection(const char *sockname, int msec, const struct timespec abstime);

// chiude la connessione a sockname
int closeConnection(const char *sockname);

// apre il file pathname (se presente nel server e solo per il client che la invia)
int openFile(const char *pathname, int flags);

// invia al server la richiesta di chiusura del file pathname (solo per il client che la invia)
int closeFile(const char *pathname);

// invia al server la richiesta di lettura del file pathname, ritornando un puntatore al buffer
int readFile(const char *pathname, void **buf, size_t *size);

// legge n files qualsiasi dal server (nessun limite se n<=0) e se dirname non è NULL allora li salva in dirname
int readNFiles(int N, const char *dirname);

// invia al server la richiesta di concatenare al file il buffer buf
// eventuali file espulsi dal server sono scritti in dirname se non nulla
int appendToFile(const char *pathname, void *buf, size_t size, const char *dirname);

// invia al server la richiesta di scrittura del file pathname
// eventuali file espulsi dal server sono scritti in dirname se non nulla
int writeFile(const char *pathname, const char *dirname);

// invia al server la richiesta di acquisire la mutua esclusione sul file pathname
int lockFile(const char *pathname);

// invia al server la richiesta di rilasciare la mutua esclusione sul file pathname
int unlockFile(const char *pathname);

// invia al server la richiesta di rimozione del file dal server (solo se ha la lock su tale file)
int removeFile(const char *pathname);

#endif
