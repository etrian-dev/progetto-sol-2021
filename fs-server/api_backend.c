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

int read_file(const char *pathname, const int client_sock) {
    // cerco il file nella tabella
    if(pthread_mutex_lock(&mux_ht) == -1) {
        // Fallita operazione di lock
        // TODO: reply error
    }
    file = icl_hash_find(fs_table, buffer);
    if(pthread_mutex_unlock(&mux_ht) == -1) {
        // Fallita operazione di unlock
        // TODO: reply error
    }

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
