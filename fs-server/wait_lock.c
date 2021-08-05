/**
 * \file wait_lock.c
 * \brief File contenente l'implementazione della funzione eseguita dal thread che attende di acquisire lock
 */

// header server
#include <server-utils.h>
// header API
#include <fs-api.h>
// header utilità
#include <utils.h>
// header sistema
#include <unistd.h>
// header std
#include <errno.h>
#include <stdlib.h>

/**
 * La funzione è eseguita da un thread detached che prende come parametro il file, estrae
 * dalla coda di client in attesa della lock il PID e socket di un client quando viene
 * segnalata la liberazione della lock
 */
void *wait_lock(void *params) {
    size_t success = 0;
    struct fs_filedata_t *file = (struct fs_filedata_t *)params;
    pthread_mutex_lock(&(file->mux_file));
    
    while(file->lockedBy != -1) {
        pthread_cond_wait(&(file->lock_free), &(file->mux_file));
    }
    // setto flag modifica
    file->modifying = 1;

    // Adesso posso estrarre il client in attesa (coppia di PID e socket)
    int PID, sock;
    struct node_t *req = NULL;
    if((req = pop(file->waiting_clients)) == NULL) {
        // fallita estrazione client
        return (void*)-1;
    }
    // estraggo anche il puntatore alla struttura dati
    struct node_t *req2 = pop(file->waiting_clients);
    struct fs_ds_t *server_ds = (struct fs_ds_t *)req2->data;
    PID = *(int*)req->data;
    sock = req->socket;
    // setto lock con PID client
    file->lockedBy = PID;
    // resetto flag di modifica del file
    file->modifying = 0;

    // posso togliere mutex
    pthread_mutex_unlock(&(file->mux_file));

    // invio la risposta alla chiamata API che era in attesa
    struct reply_t *rep = newreply(REPLY_YES, 0, 0, NULL);
    if(rep) {
        if(writen(sock, rep, sizeof(*rep)) != sizeof(*rep)) {
            int errno_saved = errno;
            if(put_logmsg(server_ds->log_thread_config, errno, "[SERVER]: Fallito invio risposta") == -1) {
                errno = errno_saved;
                perror("[SERVER]: Fallito invio risposta");
            }
            success = -1;
        }
        free(rep);
    }
    else {
        success = -1;
    }

    // Prima di terminare devo inviare al server il feedback di completamento dell'operazione
    if(write(server_ds->feedback[1], &sock, sizeof(sock)) == -1) {
        if(put_logmsg(server_ds->log_thread_config, errno, "Fallito invio feedback al server") == -1) {
            perror("Fallito invio feedback al server");
        }
    }
    
    // posso liberare le richieste, perché ho già salvato il socket
    free(req->data);
    free(req2->data);
    free(req);
    free(req2);

    // A questo punto il thread può terminare (0 o -1 a seconda dell'esito)
    return (void*)success;
}
