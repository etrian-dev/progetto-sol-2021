// header progetto
#include <utils.h>
#include <server-utils.h>
#include <fs-api.h>
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
        struct request_t *request = (struct request_t *)elem->data;

        // Sulla base dell'operazione richiesta chiamo la corrispondente funzione del backend che la implementa
        switch(request->type) {
            case 'O': // operazione di apertura di un file
                // Se la richiesta è di creazione di un file controllo di non aver
                // raggiunto il massimo numero di file memorizzabili contemporaneamente nel server
                if(api_openFile(server_ds, request->path, client_sock, request->flags) == -1) {
                    // Operazione non consentita: logging
                    if(log(server_ds, errno, "openFile: Operazione non consentita") == -1) {
                        perror("openFile: Operazione non consentita");
                    }
                }
                else {
                    // L'apertura del file ha avuto successo: logging
                    if(log(server_ds, errno, "openFile: Operazione riuscita") == -1) {
                        perror("openFile: Operazione riuscita");
                    }
                }
                break;
            case 'R': // operazione di lettura di un file
                if(api_readFile(server_ds, request->path, client_sock) == -1) {
                    // Operazione non consentita: logging
                    if(log(server_ds, errno, "readFile: Operazione non consentita") == -1) {
                        perror("readFile: Operazione non consentita");
                    }
                }
                // La lettura del file ha avuto successo: logging
                else {
                    if(log(server_ds, errno, "readFile: Operazione riuscita") == -1) {
                        perror("readFile: Operazione riuscita");
                    }
                }
                break;
            case 'A': // operazione di append
                if(api_appendToFile(server_ds, request->path, client_sock, request->buf_len, request->buf, request->dir_swp) == -1) {
                    // Operazione non consentita: logging
                    if(log(server_ds, errno, "appendToFile: Operazione non consentita") == -1) {
                        perror("appendToFile: Operazione non consentita");
                    }
                }
                else {
                    // Append OK
                    if(log(server_ds, errno, "appendFile: Operazione riuscita") == -1) {
                        perror("appendFile: Operazione riuscita");
                    }
                }
                break;
            case 'W':
            case 'L': // lock file
            case 'U': // unlock file
            case 'C': // remove file
            default:
                break;
        }

        free(request->path);
        free(request);

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
