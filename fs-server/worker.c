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
        if(pthread_mutex_unlock(&(server_ds->mux_jobq)) == -1) {
            // Fallita operazione di unlock
            return NULL;
        }

        int client_sock = elem->socket; // il socket è passato dal manager all'interno della struttura

        // persing della richiesta (dichiaro qui le variabili usate da tutti rami dello switch per comodità
        char operation;
        int flags = 0x0;
        char *path = calloc(BUF_BASESZ, sizeof(char));
        // Tento di estrarre l'operazione da fare, le eventuali flags ed il path
        if(sscanf((char*)elem->data, "%c:%d:%[^:]", &operation, &flags, path) != 3) {
            // errore nel parsing: la richiesta non rispetta il formato dato
            //cleanup_worker(elem);
            // TODO: reply error
            printf("scanf error\n");
        }
        // Sulla base dell'operazione richiesta termino il suo parsing e poi chiamo
        // la corrispondente funzione del backend che la implementa
        switch(operation) {
            case 'O': // operazione di apertura di un file
                if(openFile(server_ds, path, client_sock, flags) == -1) {
                    // Operazione non consentita: logging
                    if(log(server_ds, errno, "openFile: Operazione non consentita") == -1) {
                        perror("openFile: Operazione non consentita");
                    }
                }
                // L'apertura del file ha avuto successo: logging
                if(log(server_ds, errno, "openFile: Operazione riuscita") == -1) {
                    perror("openFile: Operazione riuscita");
                }
                break;
            case 'R': // operazione di lettura: il buffer contiene il pathname
                if(readFile(server_ds, path, client_sock) == -1) {
                    // Operazione non consentita: logging
                    if(log(server_ds, errno, "readFile: Operazione non consentita") == -1) {
                        perror("readFile: Operazione non consentita");
                    }
                }
                // L'apertura del file ha avuto successo: logging
                if(log(server_ds, errno, "readFile: Operazione riuscita") == -1) {
                    perror("readFile: Operazione riuscita");
                }
                break;
            case 'W':
            case 'A': // append to file
            case 'L': // lock file
            case 'U': // unlock file
            case 'C': // remove file
            default:
                break;
        }

        printf("Aperto %s\n", path);

        free(path);

        if(pthread_mutex_lock(&(server_ds->mux_feedback)) == -1) {
            if(log(server_ds, errno, "Fallito lock feedback") == -1) {
                perror("Fallito lock feedback");
            }
        }

        // Quindi deposito il socket servito nella pipe di feedback
        if(write(server_ds->feedback[1], (void*)&client_sock, 4) == -1) {
            if(log(server_ds, errno, "Fallito invio feedback al server") == -1) {
                perror("Fallito invio feedback al server");
            }
        }

        if(pthread_mutex_unlock(&(server_ds->mux_feedback)) == -1) {
            if(log(server_ds, errno, "Fallito unlock feedback") == -1) {
                perror("Fallito unlock feedback");
            }
        }

        free(elem);
    }
}
