// header file contenente la dichiarazione delle funzioni della API
// che permette al client di comunicare con il server

#ifndef FS_API_H_DEFINED
#define FS_API_H_DEFINED

#include <stddef.h> // per size_t
#include <time.h>

#define SOCK_PATH_MAXLEN 108 // per socket AF_UNIX è la massima lunghezza del path al socket

// La API mantiene una struttura dati interna per associare ad ogni client il proprio socket
struct conn_info {
	char *sockname;     // nome del socket (supposto univoco per tutta la sessione)
	int *client_id;     // array di coppie (socket fd, client PID) allocato in modo contiguo
	// NOTA: la regola è di mettere prima il fd del socket, poi il PID del client
	size_t capacity;    // mantiene la capacità dell'array sovrastante
	size_t count;       // conta il numero di client connessi in questo momento
};

#define NCLIENTS_DFL 10 // il numero di slot preallocati per clients

extern struct conn_info *clients_info;

// Inizializza la struttura dati della API con il socket passato come argomento
int init_api(const char *sname);
int add_client(const int conn_fd, const int pid);
int rm_client(const int pid);
int isConnected(const int pid);

// funzione di utilità per convertire msec in un delay specificato secondo timespec
void get_delay(const int msec, struct timespec *delay);

//-----------------------------------------------------------------------------------

// definisco i tipi di operazioni
#define OPEN_FILE 'O'
#define CLOSE_FILE 'Q'
#define CREATE_FILE 'C'
#define LOCK_FILE 'L'
#define UNLOCK_FILE 'U'
#define READ_FILE 'R'
#define WRITE_FILE 'W'
#define APPEND_FILE 'A'
#define REMOVE_FILE 'X'

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

// invia al server la richiesta di scrittura del file pathname
int writeFile(const char *pathname, const char *dirname);

// invia al server la richiesta di concatenare al file il buffer buf
//int appendToFile(const char *pathname, void *buf, size_t size, const char *dirname);

// invia al server la richiesta di acquisire la mutua esclusione sul file pathname
//int lockFile(const char *pathname);

// invia al server la richiesta di rilasciare la mutua esclusione sul file pathname
//int unlockFile(const char *pathname);

// invia al server la richiesta di rimozione del file dal server (solo se ha la lock su tale file)
//int removeFile(const char *pathname);

#endif
