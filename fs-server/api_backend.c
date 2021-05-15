// header progetto
#include <utils.h>
#include <icl_hash.h> // per hashtable
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

struct fs_filedata_t *find_file(const char *fname) {
    // cerco il file nella tabella
    if(pthread_mutex_lock(&mux_ht) == -1) {
        // Fallita operazione di lock
        return NULL;
    }
    file = icl_hash_find(fs_table, buffer);
    if(pthread_mutex_unlock(&mux_ht) == -1) {
        // Fallita operazione di unlock
        return NULL;
    }
    return file;
}

struct fs_filedata_t *insert_file(const char *path, const void *buf, const size_t size, const int client) {
    // Duplico il path da usare come chiave
    char *path_cpy = strndup(path, strlen(path) + 1);
    if(!path_cpy) {
        // errore di allocazione
        return NULL;
    }
    // Alloco la struttura del file
    struct fs_filedata_t *newfile = NULL;
    if((newfile = calloc(sizeof(struct fs_filedata_t))) == NULL) {
        // errore di allocazione
        return NULL;
    }
    // Se buf è NULL allora significa che sto inserendo un nuovo file
    if(!buf) {
        newfile->size = 0; // un file appena creato avrà size 0
        if((newfile->openedBy = malloc(sizeof(int))) == NULL) {
            // errore di allocazione: libero newfile e ritorno
            free(newfile);
            return NULL;
        }
        newfile->openedBy[0] = client; // setto il file come aperto da questo client
        newfile->nopened = 1;
        newfile->data = NULL; // non ho dati da inserire
    }
    // Altrimenti devo troncare il file (sostituisco il buffer)
    else {
        // Devo copiare i dati se il file non è vuoto
        //void *buf_cpy = malloc(size); [...]
    }

    // Adesso accedo alla ht in mutua esclusione ed inserisco la entry
    if(pthread_mutex_lock(&mux_ht) == -1) {
        // Fallita operazione di lock
        return NULL;
    }
    void *esito;
    if(icl_hash_insert(fs_table, path_cpy, newfile) == NULL) {
        // errore nell'inserimento nella ht
        free(path_cpy);
        free(newfile->openedBy);
        free(newfile);
        return NULL;
    }
    if(pthread_mutex_unlock(&mux_ht) == -1) {
        // Fallita operazione di unlock
        return NULL;
    }
    return newfile; // ritorno il puntatore al file inserito
}

int openFile(const char *pathname, const int client_sock, int flags) {
    // Preparo stringa di risposta
    char reply[BUF_BASESZ];
    memset(reply, 0, BUF_BASESZ * sizeof(char));
    // Cerco il file nella tabella
    struct fs_filedata_t *file = find_file(pathname);
    if(file == NULL) {
        // file non trovato: se era stata specificata la flag di creazione lo creo
        if(flags & O_CREATE) {
            // inserisco un file vuoto (passando NULL come buffer) nella tabella
            if(insert_file(pathname, NULL, 0, client_sock)) {
                // Inserimento OK
                snprintf(reply, BUF_BASESZ, "%c:%d", 'Y', 0);
                writen(client_sock, reply, 4);
                return 0;
            }
        }
        // Altrimenti restituisco un errore
        snprintf(reply, BUF_BASESZ, "%c:%d", 'N', -1);
        writen(client_sock, reply, 5);
        return 0;
    }
    else {
        // File trovato nella tabella: devo vedere se è stato già aperto dal client
        size_t i;
        int isOpen = 0;
        while(!isOpen && i < file->nopened) {
            if(file->openedBy[i] == client_sock) {
                isOpen = 1;
            }
            i++;
        }
        if(isOpen) {
            // File già aperto: l'operazione di apertura fallisce
            snprintf(reply, BUF_BASESZ, "%c:%d", 'N', -1);
            writen(client_sock, reply, 5);
            return -1;
        }
        // Il File non era aperto da questo client: lo apro
        int *newentry = realloc(file->openedBy, file->nopened + 1);
        if(!newentry) {
            // fallita allocazione nuovo spazio per fd
            snprintf(reply, BUF_BASESZ, "%c:%d", 'N', -1);
            writen(client_sock, reply, 5);
            return -1;
        }
        // Apertura OK
        newentry[file->nopened] = client_sock;
        file->nopened++;
        file->openedBy = newentry;
        snprintf(reply, BUF_BASESZ, "%c:%d", 'Y', 0);
        writen(client_sock, reply, 4);
    }
    return 0;
}

int read_file(const char *pathname, const int client_sock) {
    // Cerco il file nella tabella
    struct fs_filedata_t *file = find_file(pathname);
    if(file == NULL) {
        // chiave non trovata => restituire errore alla API
        // TODO: reply error
    }
    // file trovato: cercare socket di questo client
    else {
        if(lockedBy != -1 && lockedBy != client_sock) {
            // il file è lockato, ma non da questo client, quindi l'operazione fallisce
            // TODO: reply error
            // TODO: log fallimento
            ;
        }
        else {
            int i, isOpen, nexit;
            i = isOpen = 0;
            nexit = 1;
            while(nexit && i < file->nopened) {
                if(file->openedBy[i] == client_sock) {
                    isOpen = 1; // aperto da questo client
                    nexit = 0;
                }
                i++;
            }

            // Se è aperto da questo client allora posso leggerlo
            if(isOpen) {
                // Ok, posso inviare il file al client lungo il socket
                // uso la writen perché sto scrivendo su un socket e so quanti byte scrivere
                if(writen(client_sock, file->data, file->size) != file->size) {
                    // La scrittura non ha scritto tutto il file: riportare sul log del server
                    // TODO: log errore
                    ;
                }
                else {
                    // La scrittura ha avuto successo
                    // TODO: log successo
                    ;
                }
            }
            else if(!isOpen) {
                // Operazione negata: il file non è stato aperto dal client
                // TODO: reply error
                // TODO: log fallimento
                ;
            }
        }
    }

    return 0;
}
