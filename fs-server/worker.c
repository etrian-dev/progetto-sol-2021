// header progetto
#include <utils.h>
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

void cleanup_worker(struct node_t *node);

// worker thread

void *work(void *ds) {
    struct fs_ds_t *server_ds = (struct fs_ds_t *)ds;
    struct node_t *elem;

    while(1) {
        // prendo mutex sulla coda di richieste
        if(pthread_mutex_lock(&(server_ds->mux_jobq)) == -1) {
            // Fallita operazione di lock
            return NULL;
        }
        // aspetto tramite la variabile di condizione che arrivi una richiesta
        while((elem = pop(server_ds->job_queue)) == NULL) {
            pthread_cond_wait(&(server_ds->new_job), &(server_ds->mux_jobq));
        }
        // persing della richiesta (dichiaro qui le variabili usate da tutti rami dello switch per comodità
        char operation;
        int flags = 0x0;
        int client_sock = -1;
        char *path = NULL;
        // Prima devo determinare il tipo di richiesta, perché da ciò dipende il resto della stringa
        if(sscanf((char*)elem->data, "%c:", &operation) != 1) {
            // errore nel parsing: la richiesta non rispetta il formato dato
            //cleanup_worker(elem);
            // TODO: reply error
            ;
        }
        // Sulla base dell'operazione richiesta termino il suo parsing e poi chiamo
        // la corrispondente funzione del backend che la implementa
        switch(operation) {
            case 'O': // operazione di apertura di un file
                // devo estrarre le flag di apertura, il socket ed il path
                if(sscanf((char*)elem->data, "%*c:%d:%d:%s", &flags, &client_sock, path) != 3) {
                     // errore nel parsing: la richiesta non rispetta il formato dato
                    //cleanup_worker(elem);
                    // TODO: reply error
                    ;
                }
                if(openFile(server_ds, path, client_sock, flags) == -1) {
                    // Operazione non consentita: logging
                    if(log(server_ds->log_fd, errno, "openFile: Operazione non consentita") == -1) {
                        perror("openFile: Operazione non consentita");
                    }
                }
                // L'apertura del file ha avuto successo: logging
                if(log(server_ds->log_fd, errno, "openFile: Operazione riuscita") == -1) {
                    perror("openFile: Operazione riuscita");
                }
                break;
            case 'R': // operazione di lettura: il buffer contiene il pathname
                // devo estrarre il path ed il socket del client
                if(sscanf((char*)elem->data, "%*c:%d:%s", &client_sock, path) != 2) {
                     // errore nel parsing: la richiesta non rispetta il formato dato
                    //cleanup_worker(elem);
                    // TODO: reply error
                    ;
                }
                if(readFile(server_ds, path, client_sock) == -1) {
                    // Operazione non consentita: logging
                    if(log(server_ds->log_fd, errno, "readFile: Operazione non consentita") == -1) {
                        perror("readFile: Operazione non consentita");
                    }
                }
                // L'apertura del file ha avuto successo: logging
                if(log(server_ds->log_fd, errno, "readFile: Operazione riuscita") == -1) {
                    perror("readFile: Operazione riuscita");
                }
                break;
            case 'W':
            case 'A': // append to file
            case 'L': // lock file
            case 'U': // unlock file
            case 'C': // remove file
            default:
                ;
        }
        cleanup_worker(elem);
    }
}

void cleanup_worker(struct node_t *node) {
   ;
}
