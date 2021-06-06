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

void clean(void *p1, void *p2) {
    if(p1) free(p1);
    if(p2) free(p2);
}

int term_worker(struct fs_ds_t *ds, fd_set *set, const int maxfd);

// worker thread
void *work(void *params) {
    struct fs_ds_t *ds = (struct fs_ds_t *)params;
    struct node_t *elem = NULL;
    struct request_t *request = NULL;
    char *path = NULL;
    int client_sock = -1;

    // ascolto il socket di terminazione
    fd_set term_fd;
    FD_ZERO(&term_fd);
    FD_SET(ds->termination[0], &term_fd);
    int nfd = ds->termination[0] + 1;

    int term = 0;
    while(term == 0) {

        term = term_worker(ds, &term_fd, nfd);
        if(term == -1) {
            // errore della select interna
            return (void*)1;
        }
        if(term == 1) {
            // terminazione veloce: esco direttamente
            break;
        }
        // terminazione lenta o nessuna terminazione: leggo le richieste in coda

        // prendo mutex sulla coda di richieste
        if(pthread_mutex_lock(&(ds->mux_jobq)) == -1) {
            // Fallita operazione di lock
            return NULL;
        }
        // aspetto tramite la variabile di condizione che arrivi una richiesta
        while((elem = pop(ds->job_queue)) == NULL) {
            // Controllo se ho terminazione lenta o veloce
            term = term_worker(ds, &term_fd, nfd);
            if(term == -1) {
                // errore della select interna
                return (void*)1;
            }
            if(term == 1 || term == 2) {
                break;
            }

            // Altrimenti non devo terminare (term == 0) allora mi metto in attesa sulla coda
            pthread_cond_wait(&(ds->new_job), &(ds->mux_jobq));
        }
        if(pthread_mutex_unlock(&(ds->mux_jobq)) == -1) {
            // Fallita operazione di unlock
            return NULL;
        }

        client_sock = elem->socket; // il socket del client da servire
        request = (struct request_t *)elem->data; // la richiesta inviata dal client
        
        char msg[BUF_BASESZ];
        memset(msg, 0, BUF_BASESZ * sizeof(char));

        // A seconda dell'operazione richiesta chiama la funzione che la implementa
        // e si occupa di fare tutto l'I/O ed i controlli internamente
        switch(request->type) {
        case CLOSE_CONN: { // operazione di chiusura della connessione da parte di un client
            if(rm_connection(ds, client_sock) == -1) {
                // Fallita rimozione client
                snprintf(msg, BUF_BASESZ, "Fallita rimozione client con socket %d", client_sock);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            close(client_sock); // chiudo anche il socket su cui si era connesso (in realtà lo fa anche il client stesso)
            break;
        }
        case OPEN_FILE: { // operazione di apertura di un file
            // leggo dal socket il path del file da aprire
            path = malloc(request->path_len * sizeof(char));
            if(!path) {
                if(logging(ds, errno, "openFile: Fallita allocazione path") == -1) {
                    perror("openFile: Fallita allocazione path");
                }
                break;
            }
            if(readn(client_sock, path, request->path_len) == -1) {
                if(logging(ds, errno, "openFile: Fallita lettura path") == -1) {
                    perror("openFile: Fallita lettura path");
                }
                free(path);
                break;
            }
            // chiamo api_openfile con il path appena letto e le flag che erano state settate nella richiesta
            if(api_openFile(ds, path, client_sock, request->flags) == -1) {
                // Operazione non consentita: effettuo il log
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] openFile(%s): Operazione non consentita", client_sock, path);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                // L'apertura del file ha avuto successo: effettuo il log
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] openFile(%s): Operazione completata con successo", client_sock, path);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            free(path);
            break;
        }
        case CLOSE_FILE: { // operazione di chiusura del file
       		// leggo dal socket il path del file da chiudere
            path = malloc(request->path_len * sizeof(char));
            if(!path) {
                if(logging(ds, errno, "closeFile: Fallita allocazione path") == -1) {
                    perror("closeFile: Fallita allocazione path");
                }
                break;
            }
            if(readn(client_sock, path, request->path_len) == -1) {
                if(logging(ds, errno, "closeFile: Fallita lettura path") == -1) {
                    perror("closeFile: Fallita lettura path");
                }
                free(path);
                break;
            }
            // chiamo api_closeFile con il path appena letto
            if(api_closeFile(ds, path, client_sock) == -1) {
           		// Operazione non consentita: effettuo il log
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] closeFile(%s): Operazione non consentita", client_sock, path);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
           	}
           	else {
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] closeFile(%s): Operazione completata con successo", client_sock, path);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            free(path);
            break;
       	}
        case READ_FILE: { // operazione di lettura di un file
            // leggo dal socket il path del file da leggere
            path = malloc(request->path_len * sizeof(char));
            if(!path) {
                if(logging(ds, errno, "readFile: Fallita allocazione path") == -1) {
                    perror("readFile: Fallita allocazione path");
                }
                break;
            }
            if(readn(client_sock, path, request->path_len) == -1) {
                if(logging(ds, errno, "readFile: Fallita lettura path") == -1) {
                    perror("readFile: Fallita lettura path");
                }
                free(path);
                break;
            }
            // leggo dalla ht (se presente) il file path, inviandolo lungo il socket fornito
            if(api_readFile(ds, path, client_sock) == -1) {
                // Operazione non consentita: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] readFile(%s): Operazione non consentita", client_sock, path);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            // La lettura del file ha avuto successo: logging
            else {
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] readFile(%s): Operazione completata con successo", client_sock, path);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            free(path);
            break;
        }
        case READ_N_FILES: { // lettura di n files qualsiasi dal server
            // Il campo flags della richiesta al server è usato per specificare il numero di file da leggere
            int nfiles = -1;
            if((nfiles = api_readN(ds, request->flags, client_sock)) == -1) {
                // Operazione non consentita: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] readNFiles(%d): Operazione non consentita", client_sock, request->flags);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] readNFiles(%s): Operazione completata con successo", client_sock, path);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            break;
        }
        case APPEND_FILE: { // operazione di append
            // leggo dal socket il path del file da modificare ed i dati da ricevere
            path = malloc(request->path_len * sizeof(char));
            void *buf = malloc(request->buf_len);
            if(!path || !buf) {
                if(logging(ds, errno, "appendToFile: Fallita allocazione path o buffer dati") == -1) {
                    perror("appendToFile: Fallita allocazione path o buffer dati");
                }
                // cleanup
                clean(path, buf);
                break;
            }
            if(readn(client_sock, path, request->path_len) == -1) {
                if(logging(ds, errno, "appendToFile: Fallita lettura path") == -1) {
                    perror("appendToFile: Fallita lettura path");
                }
                // cleanup
                clean(path, buf);
                break;
            }
            if(readn(client_sock, buf, request->buf_len) == -1) {
                if(logging(ds, errno, "appendToFile: Fallita lettura dati") == -1) {
                    perror("appendToFile: Fallita lettura dati");
                }
                // cleanup
                clean(path, buf);
                break;
            }
            // scrivo i dati contenuti in buf alla fine del file path (se presente)
            if(api_appendToFile(ds, path, client_sock, request->buf_len, buf) == -1) {
                // Operazione non consentita: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] appendToFile(%s): Operazione non consentita", client_sock, path);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                // Append OK
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] appendToFile(%s) (%lu bytes): Operazione completata con successo", client_sock, path, request->buf_len);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            // libero memoria
            clean(path, buf);
            break;
        }
        case WRITE_FILE: { // write file
            // leggo dal socket il path del file da modificare
            path = malloc(request->path_len * sizeof(char));
            void *buf = malloc(request->buf_len);
            if(!path || !buf) {
                if(logging(ds, errno, "writeFile: Fallita allocazione path o buffer dati") == -1) {
                    perror("writeFile: Fallita allocazione path o buffer dati");
                }
                // cleanup
                clean(path, buf);
                break;;
            }
            if(readn(client_sock, path, request->path_len) == -1) {
                if(logging(ds, errno, "writeFile: Fallita lettura path") == -1) {
                    perror("writeFile: Fallita lettura path");
                }
                // cleanup
                clean(path, buf);
                break;
            }
            // creo il file solo se era stato aperto con la flag O_CREATEFILE
            if(api_writeFile(ds, path, client_sock) == -1) {
                // Operazione non consentita: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] writeFile(%s): Operazione non consentita", client_sock, path);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                // write OK
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] writeFile(%s): Operazione completata con successo", client_sock, path);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            clean(path, buf);
            break;
        }
        case 'L': // lock file
        case 'U': // unlock file
        case 'C': // remove file
        default:
            break;
        }

        // Quindi deposito il socket servito nella pipe di feedback
        if(write(ds->feedback[1], &client_sock, sizeof(client_sock)) == -1) {
            if(logging(ds, errno, "Fallito invio feedback al server") == -1) {
                perror("Fallito invio feedback al server");
            }
        }
    }

    // libero la memoria allocata dal worker
    if(request) free(request);
    if(elem) free(elem);
    if(path) free(path);

    return (void*)0;
}

int term_worker(struct fs_ds_t *ds, fd_set *set, const int maxfd) {
    int term;
    fd_set set_cpy = *set;
    struct timeval timeout = {0, 1}; // aspetta per 1ms
    // il valore di ritorno di select è il numero di file descriptor pronti
    int retval = select(maxfd, &set_cpy, NULL, NULL, &timeout);
    if(retval == -1) {
        return -1;
    }
    else if(retval == 1) {
        // Controllo se terminazione lenta o veloce
        if(read(ds->termination[0], &term, sizeof(term)) == -1) {
            perror("Impossibile leggere tipo di terminazione");
            exit(1);
        }
        return term;
    }
    // non devo terminare
    return 0;
}
