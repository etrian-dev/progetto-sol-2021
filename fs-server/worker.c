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

void cleanup(struct node_t *node);

// worker thread

void *work(void *queue) {
    struct node_t *elem;
    struct fs_filedata_t *file;

    while(1) {
        // prendo mutex sulla coda di richieste
        if(pthread_mutex_lock(&mux_jobq) == -1) {
            // Fallita operazione di lock
            return NULL;
        }
        // aspetto tramite la variabile di condizione che arrivi una richiesta
        while((elem = pop(&job_queue)) == NULL) {
            pthread_cond_wait(&new_job, &mux_jobq);
        }
        // persing della richiesta, il cui formato è <OP>:<socket>:<string>\0
        char operation;
        int client_sock;
        char *buffer;
        if(sscanf((char*)elem->data, "%c:%d:%s", &operation, &client_sock, buffer) != 3) {
            // errore nel parsing: la richiesta non rispetta il formato dato
            cleanup(elem);
            // TODO: reply error
        }

        // Sceglie l'operazione e chiama la funzione che la realizza
        switch(operation) {
            case 'O': // operazione di apertura di un file
            case 'R': // operazione di lettura: il buffer contiene il pathname
                read_file(buffer, client_sock);
                break;
            case 'W':
            case 'A': // append to file
            case 'L': // lock file
            case 'U': // unlock file
            case 'C': // remove file
        }
        cleanup(elem);
    }
}

void cleanup(struct node_t *node) {
    free(node->data);
    free(node);
}
