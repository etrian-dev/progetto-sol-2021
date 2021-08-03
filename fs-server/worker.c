/**
 * \file worker.c
 * \brief File contenente l'implementazione della funzione eseguita dai thread worker
 *
 * Tutti i thread worker creati dal thread manager eseguono la funzione work() qui
 * implementata per servire le richieste dei client connessi
 */

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

#define OP_OK "OK"
#define OP_FAIL "FAILED"

/**
 * Un thread worker compie ciclicamente le seguenti operazioni:
 * - Estrae dalla coda di job una richiesta (attende se la coda è vuota)
 * - Esamina la richiesta
 *  - Se di terminazione veloce allora termina
 *  - Altrimenti sceglie la opportuna funzione della API che si occupa di gestire la richiesta
 * - Quando la gestione della richiesta è terminata viene scritto nella pipe di feedback il descrittore del socket servito
 *
 * Ogni thread tiene anche traccia del numero di richieste servite da quando è stato
 * creato (che abbiano avuto successo o meno), per poi scriverlo nel file di log prima di terminare
 * \param [in] params Un puntatore generico che è in realtà un puntatore alla struttura dati del server (fs_ds_t)
 * \return Ritorna 0 se il thread è terminato normalmente (a seguito di un segnale catturato dal server), -1 altrimenti
 */
void *work(void *params) {
    size_t served = 0; // numero di richieste servite da questo worker

    struct fs_ds_t *ds = (struct fs_ds_t *)params;
    struct node_t *elem = NULL;
    struct request_t *request = NULL;
    char *path = NULL;
    int client_sock = -1;

    char msg[BUF_BASESZ];
    memset(msg, 0, BUF_BASESZ * sizeof(char));

    int term = 0; // flag per terminazione: assicura che prima di terminare il thread si abbia il log
    while(!term) {
        // prendo mutex sulla coda di richieste
        LOCK_OR_KILL(ds, &(ds->mux_jobq), ds->job_queue);
        // aspetto tramite la variabile di condizione che arrivi una richiesta
        while((elem = pop(ds->job_queue)) == NULL) {
            // Se non ho richieste in coda ed è in corso la procedura di terminazione lenta
            // allora termino il worker
            if(ds->slow_term == 1) {
                term = 1;
                break;
            }
            pthread_cond_wait(&(ds->new_job), &(ds->mux_jobq));
        }
        UNLOCK_OR_KILL(ds, &(ds->mux_jobq));
        // Se nel while era stata decisa la terminazione allora esco anche da questo while
        if(term) {
            break;
        }

        client_sock = elem->socket; // il socket del client da servire
        request = (struct request_t *)elem->data; // la richiesta inviata dal client
        // posso liberare la memoria occupata dalla richiesta
        free(elem);

        // Controllo se la richiesta ricevuta sia di terminazione veloce
        if(request->type == FAST_TERM) {
            // in tal caso esco dal while
            free(request);
            break;
        }

        // Leggo il path del file su cui operare (se necessario) e lo trasformo in path assoluto (se non lo è)
        if((request->type != READ_N_FILES || request->type != CLOSE_CONN) && request->path_len > 0) {
            path = malloc(request->path_len * sizeof(char));
            if(!path) {
                // Impossibile allocare memoria per il path: scarto la richiesta
                if(put_logmsg(ds->log_thread_config, errno, "[WORKER]: Impossibile allocare memoria") == -1) {
                    fprintf(stderr, "[MANAGER THREAD]: Impossibile allocare memoria\n[SERVER]: Impossibile effettuare logging\n");
                }
                free(request);
                continue;
            }
            if(readn(client_sock, path, request->path_len) != request->path_len) {
                if(put_logmsg(ds->log_thread_config, errno, "[MANAGER THREAD]: Fallita lettura con il path fornito") == -1) {
                    fprintf(stderr, "[MANAGER THREAD]: Fallita lettura file con il path fornito\n[SERVER]: Impossibile effettuare logging\n");
                }
                free(path);
                free(request);
                continue;
            }
            // Ora ho il path
            if(path[0] != '/') {
                // salvo la directory di lavoro corrente
                char *cwd = malloc(BUF_BASESZ * sizeof(char));
                if(!cwd) {
                    // errore di allocazione
                    return (void*)1;
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

        int write_feedback = 1; // flag per scegliere di mandare feedback o meno (utile sono per api_lockfile)
        // A seconda dell'operazione richiesta chiama la funzione che la implementa
        // e si occupa di fare tutto l'I/O ed i controlli internamente
        switch(request->type) {
        case CLOSE_CONN: { // operazione di chiusura della connessione da parte di un client
            // tolgo questo socket dalla struttura dati del server
            if(rm_connection(ds, client_sock, request->pid) == -1) {
                // Fallita rimozione client
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] Disconnessione dal socket %d: %s", request->pid, client_sock, OP_FAIL);
                if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                // Connessione con il client chiusa con successo
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] Disconnessione dal socket %d: %s", request->pid, client_sock, OP_OK);
                if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                    perror(msg);
                }
                // chiudo il socket su cui si era connesso (in realtà lo fa anche il client stesso)
                close(client_sock);
                // Inverto il segno se la richiesta è stata completata con successo
                client_sock = -client_sock;
            }
            // Quando un client si disconnette tutti i file sui quali aveva la mutua esclusione la perdono
            int i = 0;
            icl_entry_t *tmp_el = NULL;
            void *key = NULL;
            struct fs_filedata_t *file = NULL;
            icl_hash_foreach(ds->fs_table, i, tmp_el, key, file) {
                LOCK_OR_KILL(ds, &(file->mux_file), file);
                while(file->modifying == 1) {
                    pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
                }
                file->modifying = 1;
                if(file->lockedBy == request->pid) {
                    file->lockedBy = -1;
                }
                // segnalo a tutti i thread sospesi che la lock è adesso libera
                pthread_cond_broadcast(&(file->lock_free));
                file->modifying = 0;
                UNLOCK_OR_KILL(ds, &(file->mux_file));
            }

            // Se era l'ultimo client connesso ed era in corso la terminazione
            // allora devo terminare il server, pertanto segnalo
            // la coda (che sarà vuota) a tutti i worker e poi termino il thread
            LOCK_OR_KILL(ds, &(ds->mux_clients), ds->active_clients);
            if(ds->connected_clients == 0 && ds->slow_term == 1) {
                term = 1; // il worker dovrà terminare
            }
            UNLOCK_OR_KILL(ds, &(ds->mux_clients));
            break;
        }
        case OPEN_FILE: { // operazione di apertura di un file
            // chiamo api_openfile con il path appena letto e le flag che erano state settate nella richiesta
            if(api_openFile(ds, path, client_sock, request->pid, request->flags) == -1) {
                // openFile non consentita: effettuo il log
                snprintf(
                    msg,
                    BUF_BASESZ,
                    "[CLIENT %d] api_openFile(%s, %s|%s): %s",
                    request->pid,
                    path,
                    (request->flags & O_CREATEFILE ? "O_CREATEFILE" : "0x0"),
                    (request->flags & O_LOCKFILE ? "O_LOCKFILE" : "0x0"),
                    OP_FAIL
                );
                if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                // L'apertura del file ha avuto successo: effettuo il log
                snprintf(
                    msg,
                    BUF_BASESZ,
                    "[CLIENT %d] api_openFile(%s, %s|%s): %s",
                    request->pid,
                    path,
                    (request->flags & O_CREATEFILE ? "O_CREATEFILE" : "0x0"),
                    (request->flags & O_LOCKFILE ? "O_LOCKFILE" : "0x0"),
                    OP_OK
                );
                if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                    perror(msg);
                }
            }
            break;
        }
        case CLOSE_FILE: { // operazione di chiusura del file
            // chiamo api_closeFile con il path appena letto
            if(api_closeFile(ds, path, client_sock, request->pid) == -1) {
           		// closeFile non consentita: effettuo il log
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_closeFile(%s): %s", request->pid, path, OP_FAIL);
                if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                    perror(msg);
                }
           	}
           	else {
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_closeFile(%s): %s", request->pid, path, OP_OK);
                if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                    perror(msg);
                }
            }
            break;
       	}
        case READ_FILE: { // operazione di lettura di un file
            // leggo dalla ht (se presente) il file path, inviandolo lungo il socket fornito
            if(api_readFile(ds, path, client_sock, request->pid) == -1) {
                // readFile non consentita: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_readFile(%s): %s", request->pid, path, OP_FAIL);
                if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                    perror(msg);
                }
            }
            // La lettura del file ha avuto successo: logging
            else {
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_readFile(%s): %s", request->pid, path, OP_OK);
                if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                    perror(msg);
                }
            }
            break;
        }
        case READ_N_FILES: { // lettura di n files qualsiasi dal server
            // Il campo flags della richiesta al server è usato per specificare il numero di file da leggere
            int nfiles = -1;
            if((nfiles = api_readN(ds, request->flags, client_sock, request->pid)) == -1) {
                // readNFiles non consentita: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_readNFiles(%d): %s", request->pid, request->flags, OP_FAIL);
                if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_readNFiles(%d): %s", request->pid, nfiles, OP_OK);
                if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
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
                    if(put_logmsg(ds->log_thread_config, errno, "api_appendToFile: Fallita lettura dati") == -1) {
                        perror("api_appendToFile: Fallita lettura dati");
                    }
                    break;
                }
                // scrivo i dati contenuti in buf alla fine del file path (se presente)
                if(api_appendToFile(ds, path, client_sock, request->pid, request->buf_len, buf) == -1) {
                    // appendToFile non consentita: logging
                    snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_appendToFile(%s): %s", request->pid, path, OP_FAIL);
                    if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                        perror(msg);
                    }
                }
                else {
                    // Append OK
                    snprintf(
                        msg, BUF_BASESZ,
                        "[CLIENT %d] api_appendToFile(%s) (%lu bytes): %s",
                        request->pid, path, request->buf_len, OP_OK);
                    if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                        perror(msg);
                    }
                }
                free(buf);
            }
            else {
                if(put_logmsg(ds->log_thread_config, errno, "api_appendToFile: Fallita allocazione buffer dati") == -1) {
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
                    if(put_logmsg(ds->log_thread_config, errno, "api_writeFile: Fallita lettura dati") == -1) {
                        perror("api_writeFile: Fallita lettura dati");
                    }
                    break;
                }
                // scrivo il file letto dal socket
                if(api_writeFile(ds, path, client_sock, request->pid, request->buf_len, buf) == -1) {
                    // writeFile fallita: logging
                    snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_writeFile(%s): %s", request->pid, path, OP_FAIL);
                    if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                        perror(msg);
                    }
                }
                else {
                    // write OK
                    snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_writeFile(%s) (%lu bytes): %s", request->pid, path, request->buf_len, OP_OK);
                    if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                        perror(msg);
                    }
                }
                free(buf);
            }
            else {
                if(put_logmsg(ds->log_thread_config, errno, "api_writeFile: Fallita allocazione buffer dati") == -1) {
                    perror("api_writeFile: Fallita allocazione buffer dati");
                }
                break;
            }
            break;
        }
        case LOCK_FILE: { // unlock file
            // La mutua esclusione sul file viene garantita solo se il file era aperto
            // per questo client e non era lockato da altri client
            pthread_t result;
            // Se è stato ritornato -1 allora operazione non consentita
            if((result = api_lockFile(ds, path, client_sock, request->pid)) == -1) {
                // lockFile non consentita: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_lockFile(%s): %s", request->pid, path, OP_FAIL);
                if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                    perror(msg);
                }
            }
            // Se ritornato 0 allora lock acquisita
            else if(result == 0) {
                // lock OK
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_lockFile(%s): %s", request->pid, path, OP_OK);
                if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                    perror(msg);
                }
            }
            // Altrimenti è stato creato il thread per l'attesa
            else {
                // Se il client è stato messo in attesa dell'acquisizione della lock
                // allora non devo mandare subito il feedback
                write_feedback = 0;
            }
            break;
        }
        case UNLOCK_FILE: { // unlock file
            // La mutua esclusione viene tolta se client_sock aveva lock sul pathname richiesto
            if(api_unlockFile(ds, path, client_sock, request->pid) == -1) {
                // unlockFile non consentita: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_unlockFile(%s): %s", request->pid, path, OP_FAIL);
                if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                // unlock OK
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_unlockFile(%s): %s", request->pid, path, OP_OK);
                if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                    perror(msg);
                }
            }
            break;
        }
        case REMOVE_FILE: { // remove file
            // La mutua esclusione viene tolta se client_sock aveva lock sul pathname richiesto
            if(api_rmFile(ds, path, client_sock, request->pid) == -1) {
                // removeFile non consentita: logging
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_rmFile(%s): %s", request->pid, path, OP_FAIL);
                if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                    perror(msg);
                }
            }
            else {
                // remove OK
                snprintf(msg, BUF_BASESZ, "[CLIENT %d] api_rmFile(%s): %s", request->pid, path, OP_OK);
                if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                    perror(msg);
                }
            }
            break;
        }
        default:
            snprintf(msg, BUF_BASESZ, "[SERVER] Operazione non riconosciuta proveniente dal client %d", client_sock);
            if(put_logmsg(ds->log_thread_config, errno, msg) == -1) {
                perror(msg);
            }
        }

        if(path) {
            free(path);
            path = NULL;
        }
        free(request); // libero la richiesta servita

        // Quindi deposito il socket servito nella pipe di feedback
        if(write_feedback == 1) {
            if(write(ds->feedback[1], &client_sock, sizeof(client_sock)) == -1) {
                if(put_logmsg(ds->log_thread_config, errno, "Fallito invio feedback al server") == -1) {
                    perror("Fallito invio feedback al server");
                }
            }
        }
        // Aumento il numero di richieste servite da questo worker
        served++;
    }
    // stampo nel file di log in numero di richieste servite da questo thread prima di uscire
    snprintf(msg, BUF_BASESZ, "[WORKER %lu]: Servite %lu richieste", pthread_self(), served);
    if(put_logmsg(ds->log_thread_config, 0, msg) == -1) {
        perror(msg);
    }

    return (void*)0;
}
