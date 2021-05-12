// header file contenente la dichiarazione delle funzioni della API
// che permette al client di comunicare con il server

#ifndef FS_API_H_DEFINED
#define FS_API_H_DEFINED

#include <stddef.h> // per size_t

#define SOCK_PATH_MAXLEN 108 // per socket AF_UNIX è la massima lunghezza del path al socket

// definisco enum per flags dei file nel fileserver
enum file_flags
{
	O_CREATE = 0x1,
	O_LOCK = 0x10
};

// apre la connessione al socket sockname, su cui il server sta ascoltando
int openConnection(const char *sockname, int msec, const struct timespec abstime);

// chiude la connessione a sockname
int closeConnection(const char *sockname);

// apre il file pathname (se presente nel server e solo per il client che la invia)
//int openFile(const char *pathname, int flags);

// invia al server la richiesta di lettura del file pathname
//int readFile(const char *pathname, void **buf, size_t *size);

// invia al server la richiesta di scrittura del file pathname
//int writeFile(const char *pathname, const char *dirname);

// invia al server la richiesta di concatenare al file il buffer buf
//int appendToFile(const char *pathname, void *buf, size_t size, const char *dirname);

// invia al server la richiesta di acquisire la mutua esclusione sul file pathname
//int lockFile(const char *pathname);

// invia al server la richiesta di rilasciare la mutua esclusione sul file pathname
//int unlockFile(const char *pathname);

// invia al server la richiesta di chiusura del file pathname (solo per il client che la invia)
//int closeFile(const char *pathname);

// invia al server la richiesta di rimozione del file dal server (solo se ha la lock su tale file)
//int removeFile(const char *pathname);

#endif
