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
#include <sys/time.h> // per struct timeval della select
#include <unistd.h>
#include <signal.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

void clean(void *p1, void *p2, void *p3) {
    if(p1) free(p1);
    if(p2) free(p2);
    if(p3) free(p3);
}

// worker thread
void *work(void *params) {
    struct fs_ds_t *ds = (struct fs_ds_t *)params;
    struct node_t *elem = NULL;
    struct request_t *request = NULL;
    char *path = NULL;
    int client_sock = -1;

    // ascolto il spcket di terminazione
    fd_set term_fd, term_fd_cpy;
    FD_ZERO(&term_fd);
    FD_SET(ds->termination[0], &term_fd);
    int nfd = ds->termination[0] + 1;

    char term[1]; *term = '0';
    while(term[0] == '0') {
        term_fd_cpy = term_fd;
        struct timeval timeout = {0, 1}; // aspetta per 1ms
        // il valore di ritorno di select è il numero di file descriptor pronti
        int retval = select(nfd, &term_fd_cpy, NULL, NULL, &timeout);
        if(retval == -1) {
            perror("Select worker fallita");
        }
        else if(retval == 1) {
            // Controllo se terminazione lenta o veloce
            if(pthread_mutex_lock(&(ds->mux_term)) == -1) {
                perror("Fallita acquisizione ME su terminazione");
                exit(1);
            }

            if(read(ds->termination[0], term, 1) == -1) {
                perror("Impossibile leggere tipo di terminazione");
                exit(1);
            }

            if(pthread_mutex_unlock(&(ds->mux_term)) == -1) {
                perror("Fallito rilascio ME su terminazione");
                exit(1);
            }

            // terminazione veloce: non servo alcuna richiesta e termino il thread
            if(term[0] == '1') {
                break;
            }
            else if(term[0] != '2') {
                printf("[WORKER] term = %c\n", term[0]);
                break;
            }
            // terminazione lenta: finisco di servire questa richiesta, ma esco
            // quando testo di nuovo la condizione del while
        }


        // prendo mutex sulla coda di richieste
        if(pthread_mutex_lock(&(ds->mux_jobq)) == -1) {
            // Fallita operazione di lock
            return NULL;
        }
        // aspetto tramite la variabile di condizione che arrivi una richiesta
        while((elem = pop(ds->job_queue)) == NULL) {
            pthread_cond_wait(&(ds->new_job), &(ds->mux_jobq));
        }
        if(pthread_mutex_unlock(&(ds->mux_jobq)) == -1) {
            // Fallita operazione di unlock
            return NULL;
        }

        client_sock = elem->socket; // il socket è passato dal manager all'interno della struttura
        request = (struct request_t *)elem->data;

        // Sulla base dell'operazione richiesta chiamo la corrispondente funzione del backend che la implementa
        switch(request->type) {
            case 'O': { // operazione di apertura di un file
                // Se la richiesta è di creazione di un file controllo di non aver
                // raggiunto il massimo numero di file memorizzabili contemporaneamente nel server

                // leggo dal socket il path del file da aprire
                path = malloc(request->path_len * sizeof(char));
                if(!path) {
                    // cleanup
                    continue;
                }
                if(readn(client_sock, path, request->path_len) == -1) {
                    // cleanup
                    continue;
                }

                if(api_openFile(ds, path, client_sock, request->flags) == -1) {
                    // Operazione non consentita: logging
                    if(log(ds, errno, "openFile: Operazione non consentita") == -1) {
                        perror("openFile: Operazione non consentita");
                    }
                }
                else {
                    // L'apertura del file ha avuto successo: logging
                    if(log(ds, errno, "openFile: Operazione riuscita") == -1) {
                        perror("openFile: Operazione riuscita");
                    }
                }
                free(path);
                break;
            }
            case 'R': { // operazione di lettura di un file
                // leggo dal socket il path del file da leggere
                path = malloc(request->path_len * sizeof(char));
                if(!path) {
                    // cleanup non necessario
                    continue;
                }
                if(readn(client_sock, path, request->path_len) == -1) {
                    // cleanup
                    clean(path, NULL, NULL);
                    continue;
                }

                if(api_readFile(ds, path, client_sock) == -1) {
                    // Operazione non consentita: logging
                    if(log(ds, errno, "readFile: Operazione non consentita") == -1) {
                        perror("readFile: Operazione non consentita");
                    }
                }
                // La lettura del file ha avuto successo: logging
                else {
                    if(log(ds, errno, "readFile: Operazione riuscita") == -1) {
                        perror("readFile: Operazione riuscita");
                    }
                }
                free(path);
                break;
            }
            case 'A': {// operazione di append
                // leggo dal socket il path del file da modificare
                path = malloc(request->path_len * sizeof(char));
                void *buf = malloc(request->buf_len);
                char *swp = malloc(request->dir_swp_len * sizeof(char));
                if(!path || !buf || !swp) {
                    // cleanup
                    clean(path, buf, swp);
                    continue;
                }
                if(readn(client_sock, path, request->path_len) == -1) {
                    // cleanup
                    clean(path, buf, swp);
                    continue;
                }
                if(readn(client_sock, buf, request->buf_len) == -1) {
                    // cleanup
                    clean(path, buf, swp);
                    continue;
                }
                if(readn(client_sock, swp, request->dir_swp_len) == -1) {
                    // cleanup
                    clean(path, buf, swp);
                    continue;
                }

                if(api_appendToFile(ds, path, client_sock, request->buf_len, buf, swp) == -1) {
                    // Operazione non consentita: logging
                    if(log(ds, errno, "appendToFile: Operazione non consentita") == -1) {
                        perror("appendToFile: Operazione non consentita");
                    }
                }
                else {
                    // Append OK
                    if(log(ds, errno, "appendFile: Operazione riuscita") == -1) {
                        perror("appendFile: Operazione riuscita");
                    }
                }
                clean(path, buf, swp);
                break;
            }
            case 'W':
            case 'L': // lock file
            case 'U': // unlock file
            case 'C': // remove file
            default:
                break;
        }

        // libero la richiesta
        free(elem->data);
        free(elem);

        if(pthread_mutex_lock(&(ds->mux_feedback)) == -1) {
            if(log(ds, errno, "Fallito lock feedback") == -1) {
                perror("Fallito lock feedback");
            }
        }

        // Quindi deposito il socket servito nella pipe di feedback
        if(write(ds->feedback[1], (void*)&client_sock, 4) == -1) {
            if(log(ds, errno, "Fallito invio feedback al server") == -1) {
                perror("Fallito invio feedback al server");
            }
        }

        if(pthread_mutex_unlock(&(ds->mux_feedback)) == -1) {
            if(log(ds, errno, "Fallito unlock feedback") == -1) {
                perror("Fallito unlock feedback");
            }
        }
    }

    // libero la memoria allocata dal worker
    free(request);
    if(path) {
        free(path);
    }

    return (void*)0;
}
