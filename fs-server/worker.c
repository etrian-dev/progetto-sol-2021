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

// worker thread
void *work(void *params) {
    struct fs_ds_t *ds = (struct fs_ds_t *)params;
    struct node_t *elem = NULL;
    struct request_t *request = NULL;
    char *path = NULL;
    int client_sock = -1;
    while(1) {
        // prendo mutex sulla coda di richieste
        if(pthread_mutex_lock(&(ds->mux_jobq)) == -1) {
            // Fallita operazione di lock
            return NULL;
        }
        // aspetto tramite la variabile di condizione che arrivi una richiesta
        while((elem = pop(ds->job_queue)) == NULL) {
            // Se non ho richieste in coda ed è in corso la procedura di terminazione lenta
            // allora termino il worker
            if(ds->slow_term == 1) {
                if(pthread_mutex_unlock(&(ds->mux_jobq)) == -1) {
                    // Fallita operazione di unlock
                    pthread_kill(pthread_self(), SIGKILL);
                }
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

        // Leggo il path del file su cui operare (se necessario) e lo trasformo in path assoluto (se non lo è)
        if((request->type != READ_N_FILES || request->type != CLOSE_CONN) && request->path_len > 0) {
            path = malloc(request->path_len * sizeof(char));
            if(!path) {
                // Impossibile allocare memoria per il path: termino il server
                if(logging(ds, errno, "Impossibile allocare memoria per il path da leggere") == -1) {
                    perror("Impossibile allocare memoria per il path da leggere");
                }
                break;
            }
            if(readn(client_sock, path, request->path_len) != request->path_len) {
                if(logging(ds, errno, "Fallita lettura path") == -1) {
                    perror("Fallita lettura path");
                }
                free(path);
                break;
            }
            // Ora ho il path
            if(path[0] != '/') {
                // TODO: creare path assoluto e riassegnarlo a path
            }
        }

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
            // leggo dal socket  i dati da ricevere
            void *buf = malloc(request->buf_len);
            if(!buf) {
                if(logging(ds, errno, "appendToFile: Fallita allocazione buffer dati") == -1) {
                    perror("appendToFile: Fallita allocazione buffer dati");
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
            clean(path, NULL);
            break;
        }
        case LOCK_FILE: { // unlock file
            // La mutua esclusione sul file viene garantita solo se il file era aperto
            // pre questo client e non era lockato da altri client
            if(api_lockFile(ds, path, client_sock) == -1) {
                // Operazione non consentita: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] lockFile(%s): Operazione non consentita", client_sock, path);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                // lock OK
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] lockFile(%s): Operazione completata con successo", client_sock, path);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            clean(path, NULL);
            break;
        }
        case UNLOCK_FILE: { // unlock file
            // La mutua esclusione viene tolta se client_sock aveva lock sul pathname richiesto
            if(api_unlockFile(ds, path, client_sock) == -1) {
                // Operazione non consentita: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] unlockFile(%s): Operazione non consentita", client_sock, path);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                // lock OK
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] unlockFile(%s): Operazione completata con successo", client_sock, path);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            clean(path, NULL);
            break;
        }
        case REMOVE_FILE: { // remove file
            // La mutua esclusione viene tolta se client_sock aveva lock sul pathname richiesto
            if(api_rmFile(ds, path, client_sock) == -1) {
                // Operazione non consentita: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] removeFile(%s): Operazione non consentita", client_sock, path);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                // lock OK
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] removeFile(%s): Operazione completata con successo", client_sock, path);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            clean(path, NULL);
            break;
        }
        default:
            snprintf(msg, BUF_BASESZ, "[WARNING] Operazione non riconosciuta proveniente dal client %d", client_sock);
            if(logging(ds, errno, msg) == -1) {
                perror(msg);
            }
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
