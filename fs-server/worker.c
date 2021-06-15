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

#define OP_OK "Operazione completata con successo"
#define OP_FAIL "Operazione non consentita"

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
                // salvo la directory di lavoro corrente
                char *cwd = malloc(BUF_BASESZ * sizeof(char));
                if(!cwd) {
                    // errore di allocazione
                    return -1;
                }
                size_t dir_sz = BUF_BASESZ;
                errno = 0; // resetto errno per esaminare eventuali errori
                while(cwd && getcwd(cwd, dir_sz) == NULL) {
                    // Se errno è diventato ERANGE allora il buffer allocato non è abbastanza grande
                    if(errno == ERANGE) {
                        // rialloco cwd, con la politica di raddoppio della size
                        char *newbuf = realloc(cwd, BUF_BASESZ * 2);
                        if(newbuf) {
                            cwd = newbuf;
                            dir_sz *= 2;
                            errno = 0; // resetto errno in modo da poterlo testare dopo la guardia
                        }
                        else {
                            // errore di riallocazione
                            free(cwd);
                            return (void*)1;
                        }
                    }
                    // se si è verificato un altro errore allora esco con fallimento
                    else {
                        free(cwd);
                        return (void*)1;
                    }
                }
                // Adesso cwd contiene il path della directory corrente
                char *full_path = get_fullpath(cwd, path);
                if(full_path) {
                    free(cwd);
                    free(path);
                    path = full_path;
                }
                else {
                    pthread_kill(pthread_self(), SIGKILL);
                }
            }
            // Ora path contiene il path completo del file su cui operare
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
                // %s: effettuo il log
                snprintf(
                    msg,
                    BUF_BASESZ,
                    "[CLIENT %d] api_openFile(%s, %s|%s): %s",
                    client_sock,
                    path,
                    (request->flags & O_CREATEFILE ? "O_CREATEFILE" : "0x0"),
                    (request->flags & O_LOCKFILE ? "O_LOCKFILE" : "0x0"),
                    OP_FAIL
                );
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                // L'apertura del file ha avuto successo: effettuo il log
                snprintf(
                    msg,
                    BUF_BASESZ,
                    "[CLIENT %d] api_openFile(%s, %s|%s): %s",
                    client_sock,
                    path,
                    (request->flags & O_CREATEFILE ? "O_CREATEFILE" : "0x0"),
                    (request->flags & O_LOCKFILE ? "O_LOCKFILE" : "0x0"),
                    OP_OK
                );
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            break;
        }
        case CLOSE_FILE: { // operazione di chiusura del file
            // chiamo api_closeFile con il path appena letto
            if(api_closeFile(ds, path, client_sock) == -1) {
           		// %s: effettuo il log
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_closeFile(%s): %s", client_sock, path, OP_FAIL);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
           	}
           	else {
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_closeFile(%s): %s", client_sock, path, OP_OK);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            break;
       	}
        case READ_FILE: { // operazione di lettura di un file
            // leggo dalla ht (se presente) il file path, inviandolo lungo il socket fornito
            if(api_readFile(ds, path, client_sock) == -1) {
                // %s: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_readFile(%s): %s", client_sock, path, OP_FAIL);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            // La lettura del file ha avuto successo: logging
            else {
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_readFile(%s): %s", client_sock, path, OP_OK);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            break;
        }
        case READ_N_FILES: { // lettura di n files qualsiasi dal server
            // Il campo flags della richiesta al server è usato per specificare il numero di file da leggere
            int nfiles = -1;
            if((nfiles = api_readN(ds, request->flags, client_sock)) == -1) {
                // %s: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_readNFiles(%d): %s", client_sock, request->flags, OP_FAIL);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_readNFiles(%d): %s", client_sock, nfiles, OP_OK);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            break;
        }
        case APPEND_FILE: { // operazione di append
            // alloco un buffer per leggere i dati da concatenare
            void *buf = malloc(request->buf_len);
            if(buf) {
                if(readn(client_sock, buf, request->buf_len) == -1) {
                    if(logging(ds, errno, "api_appendToFile: Fallita lettura dati") == -1) {
                        perror("api_appendToFile: Fallita lettura dati");
                    }
                    break;
                }
                // scrivo i dati contenuti in buf alla fine del file path (se presente)
                if(api_appendToFile(ds, path, client_sock, request->buf_len, buf) == -1) {
                    // %s: logging
                    snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_appendToFile(%s): %s", client_sock, path, OP_FAIL);
                    if(logging(ds, errno, msg) == -1) {
                        perror(msg);
                    }
                }
                else {
                    // Append OK
                    snprintf(
                        msg, BUF_BASESZ,
                        "[CLIENT %d] api_appendToFile(%s) (%lu bytes): %s",
                        client_sock, path, request->buf_len, OP_OK);
                    if(logging(ds, errno, msg) == -1) {
                        perror(msg);
                    }
                }
                free(buf);
            }
            else {
                if(logging(ds, errno, "api_appendToFile: Fallita allocazione buffer dati") == -1) {
                    perror("api_appendToFile: Fallita allocazione buffer dati");
                }
                break;
            }
            break;
        }
        case WRITE_FILE: { // write file
            // alloco un buffer per leggere i dati da concatenare
            void *buf = malloc(request->buf_len);
            if(buf) {
                if(readn(client_sock, buf, request->buf_len) == -1) {
                    if(logging(ds, errno, "api_writeFile: Fallita lettura dati") == -1) {
                        perror("api_writeFile: Fallita lettura dati");
                    }
                    break;
                }
                // creo il file solo se era stato aperto con la flag O_CREATEFILE
                if(api_writeFile(ds, path, client_sock, request->buf_len, buf) == -1) {
                    // %s: logging
                    snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_writeFile(%s): %s", client_sock, path, OP_FAIL);
                    if(logging(ds, errno, msg) == -1) {
                        perror(msg);
                    }
                }
                else {
                    // write OK
                    snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_writeFile(%s) (%lu bytes): %s", client_sock, path, request->buf_len, OP_OK);
                    if(logging(ds, errno, msg) == -1) {
                        perror(msg);
                    }
                }
                free(buf);
            }
            else {
                if(logging(ds, errno, "api_writeFile: Fallita allocazione buffer dati") == -1) {
                    perror("api_writeFile: Fallita allocazione buffer dati");
                }
                break;
            }
            break;
        }
        case LOCK_FILE: { // unlock file
            // La mutua esclusione sul file viene garantita solo se il file era aperto
            // pre questo client e non era lockato da altri client
            if(api_lockFile(ds, path, client_sock) == -1) {
                // %s: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_lockFile(%s): %s", client_sock, path, OP_FAIL);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                // lock OK
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_lockFile(%s): %s", client_sock, path, OP_OK);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            break;
        }
        case UNLOCK_FILE: { // unlock file
            // La mutua esclusione viene tolta se client_sock aveva lock sul pathname richiesto
            if(api_unlockFile(ds, path, client_sock) == -1) {
                // %s: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_unlockFile(%s): %s", client_sock, path, OP_FAIL);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                // lock OK
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_unlockFile(%s): %s", client_sock, path, OP_OK);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            break;
        }
        case REMOVE_FILE: { // remove file
            // La mutua esclusione viene tolta se client_sock aveva lock sul pathname richiesto
            if(api_rmFile(ds, path, client_sock) == -1) {
                // %s: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_removeFile(%s): %s", client_sock, path, OP_FAIL);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                // lock OK
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_removeFile(%s): %s", client_sock, path, OP_OK);
                if(logging(ds, errno, msg) == -1) {
                    perror(msg);
                }
            }
            break;
        }
        default:
            snprintf(msg, BUF_BASESZ, "[WARNING] Operazione non riconosciuta proveniente dal client %d", client_sock);
            if(logging(ds, errno, msg) == -1) {
                perror(msg);
            }
        }

        if(path) {
            free(path);
            path = NULL;
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
