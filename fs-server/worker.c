// header server
#include <server-utils.h>
// header API
#include <fs-api.h>
// header utilità
#include <utils.h>
// multithreading headers
#include <pthread.h>
// system call headers
#include <unistd.h>
#include <signal.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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

    int term = 0;
    while(1) {
        // prendo mutex sulla coda di richieste
        if(pthread_mutex_lock(&(ds->mux_jobq)) == -1) {
            // Fallita operazione di lock
            return NULL;
        }
        // aspetto tramite la variabile di condizione che arrivi una richiesta
        while((elem = pop(ds->job_queue)) == NULL) {
            // Se non ci sono richieste e era stato ricevuto SIGHUP termino il worker
            if(ds->slow_term == 1) {
                pthread_mutex_unlock(&(ds->mux_jobq));
                return NULL;
            }
            pthread_cond_wait(&(ds->new_job), &(ds->mux_jobq));
        }
        if(pthread_mutex_unlock(&(ds->mux_jobq)) == -1) {
            // Fallita operazione di unlock
            return NULL;
        }

        client_sock = elem->socket; // il socket del client da servire
        request = (struct request_t *)elem->data; // la richiesta inviata dal client
        // posso liberare la memoria occupata dalla richiesta
        free(elem);

        // Controllo se la richiesta ricevuta sia di terminazione
        if(request->type == FAST_TERM) {
            free(request);
            break;
        }

        char msg[BUF_BASESZ];
        memset(msg, 0, BUF_BASESZ * sizeof(char));

        // A seconda dell'operazione richiesta chiama la funzione che la implementa
        // e si occupa di fare tutto l'I/O ed i controlli internamente
        switch(request->type) {
        case CLOSE_CONN: { // operazione di chiusura della connessione da parte di un client
            // tolgo questo socket dalla struttura dati del server
            if(rm_connection(ds, client_sock) == -1) {
                // Fallita rimozione client
                snprintf(msg, BUF_BASESZ, "Fallita rimozione client con socket %d", client_sock);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                // chiudo anche il socket su cui si era connesso (in realtà lo fa anche il client stesso)
                close(client_sock);
                // Inverto il segno se la richiesta è stata completata con successo
                client_sock = -client_sock;
            }
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
        case REMOVE_FILE: { // remove file
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
        default:
            break;
        }

        free(request); // libero la richiesta servita

        // Quindi deposito il socket servito nella pipe di feedback
        if(write(ds->feedback[1], &client_sock, sizeof(client_sock)) == -1) {
            if(logging(ds, errno, "Fallito invio feedback al server") == -1) {
                perror("Fallito invio feedback al server");
            }
        }
    }

    return (void*)0;
}

