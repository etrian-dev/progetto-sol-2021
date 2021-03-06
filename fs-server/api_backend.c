/**
 * \file api_backend.c
 * \brief File contenente l'implementazione delle funzionalit√† sui file (apertura, chisura...)
 *
 * Il file contiene l'implementazione delle operazioni richieste dalla API, ovvero
 * aprire file, leggerli, scriverli...
 */

// header server
#include <server-utils.h>
// header API
#include <fs-api.h>
// header utilit√†
#include <utils.h>
#include <icl_hash.h> // per hashtable
// header multithreading
#include <pthread.h>
// system call headers
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/**
 * \brief Concatena le num stringhe in paths in un'unica stringa
 *
 * La funzione costruisce una stringa contenente i num paths dell'array di stringhe paths.
 * La precondizione √® che num >= 0 e deve essere la lunghezza di paths. Inoltre str deve
 * essere allocata dinamicamente dal chiamante in modo da poter contenere i path concatenati
 * \param [in,out] str La stringa che contiene la concatenazione dei path
 * \param [in] paths Un array di num stringhe, contenente dei path di file
 * \param [in] num Il numero di path (lunghezza dell'array)
 */
void build_pathstr(char **str, char **paths, const int num) {
    size_t offt = 0; // offset nella stringa di paths
    int i = 0;
    for(; i < num; i++) {
        size_t plen = strlen(paths[i]); // indice del terminatore dell' i-esimo path
        *str = strncat(*str, paths[i], plen); // concateno il path a str (che termina con \0)
        // inserisco il separatore (non devo farlo per l'ultimo path: str termina con '\0')
        if(i < num - 1) {
            *(*str + offt + plen) = '\n';
            *(*str + offt + plen + 1) = '\0';
            offt += plen + 1;
        }
    }
}

/**
 * \brief Tenta di aprire il file con questo path per il client che lo richiede
 *
 * Effettua l'apertura del file con path pathname per il client con PID passato come parametro, se possibile.
 * Le flags sono un OR di O_LOCKFILE e O_CREATEFILE. Le cause del fallimento dell'operazione
 * sono riportate di seguito:
 * - Il file non √® presente nel server, ma non √® stata specificata la flag O_CREATEFILE
 * - Il file √® presente nel server ed √® stata specificata la flag O_CREATEFILE
 * - Sono gi√† presenti nel server il massimo numero di file specificato dalla configurazione corrente
 * - Il file √® in stato locked per qualche altro client ed √® stata passata O_LOCKFILE
 * - Il file √® gi√† aperto da questo client
 *
 * Chiaramente, anche se l'operazione √® consentita, qualche passo della procedura potrebbe
 * fallire. Se tutto va a buon fine il file √® aggiornato come aperto dal client client_PID
 *
 * \param [in,out] ds La struttura dati condivisa del server, dove sono memorizzati i file
 * \param [in] pathname Il path del file di cui il client richiede l'apertura
 * \param [in] client_sock Il socket del client che richiede l'apertura
 * \param [in] client_PID Il PID del client che richiede l'apertura
 * \param [in] flags Le flag di apertura del client (OR di O_CREATEFILE e O_LOCKFILE)
 * \return Ritorna 0 se l'apertura/creazione viene eseguita con successo, -1 altrimenti
 */
int api_openFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const int client_PID, int flags) {
    struct reply_t *reply = NULL; // conterr√† la risposta del server
    int success = 0; // se rimane 0 allora l'operazione ha successo, altrimenti √® fallita
    int errno_saved;

    // Cerco il file nella tabella
    struct fs_filedata_t *file = find_file(ds, pathname);
    // Se il file non √® presente ed √® stata passata la flag per la creazione allora lo creo
    if(!file && (flags & O_CREATEFILE)) {
        // Se ho gi√† raggiunto il massimo numero di file memorizzabili allora la open fallisce
        // perch√© sto tentando di creare un nuovo file
        LOCK_OR_KILL(ds, &(ds->mux_files), ds);
        if(ds->curr_files == ds->max_files) {
            reply = newreply(REPLY_NO, ETOOMANYFILES, 0, NULL);
            success = -1;
        }
        UNLOCK_OR_KILL(ds, &(ds->mux_files));

        // Altrimenti posso creare un nuovo file vuoto, sul quale setto le flags (O_LOCKFILE se specificata)
        if(success == 0) {
            if(insert_file(ds, pathname, flags, NULL, 0, client_PID) == NULL) {
                // Fallita creazione del file
                reply = newreply(REPLY_NO, errno, 0, NULL);
                success = -1;
            }
            else {
                // Inserimento OK (num di file aperti aggiornato da insert_file)
                reply = newreply(REPLY_YES, 0, 0, NULL);
                // aggiorno info client con la nuova operazione completata
                if(update_client_op(ds, client_sock, client_PID, OPEN_FILE, flags, pathname) == -1) {
                    // fallito aggiornamento stato client
                    errno_saved = errno;
                    if(put_logmsg(ds->log_thread_config, errno, "Fallito aggiornamento stato client") == -1) {
                        errno = errno_saved;
                        perror("Fallito aggiornamento stato client");
                    }
                }
            }
        }
    }
    // Se il file non √® presente nel server e non √® stata passata la flag per la creazione
    // allora l'operazione fallisce
    else if(!file && !(flags & O_CREATEFILE)) {
        reply = newreply(REPLY_NO, ENOFILE, 0, NULL);
        success = -1;
    }
    // Se il file √® presente nel server ed √® stata passata la flag per la creazione allora
    // l'operazione fallisce
    else if(file && (flags & O_CREATEFILE)) {
        reply = newreply(REPLY_NO, EALREADYCREATED, 0, NULL);
        success = -1;
    }
    // Se il file esiste ed √® stata passata la flag O_LOCKFILE, ma il file √® lockato
    // da qualche altro client allora l'operazione fallisce
    else if(file && (flags & O_LOCKFILE) && (file->lockedBy != -1 && file->lockedBy != client_PID)) {
        reply = newreply(REPLY_NO, ELOCKED, 0, NULL);
        success = -1;
    }
    // File trovato nella tabella hash: devo verificare se sia gi√† stato aperto o meno
    else {
        size_t i = 0;
        int isOpen = 0;
        // Cerco questo client tra quelli che hanno aperto il file
        LOCK_OR_KILL(ds, &(file->mux_file), file);
        // Il file potrebbe essere stato eliminato durante l'attesa della lock
        if(!file) {
            isOpen = 0;
        }
        else {
            while(file->modifying == 1) {
                pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
            }
            while(!isOpen && i < file->nopened) {
                if(file->openedBy[i] == client_PID) {
                    isOpen = 1;
                }
                i++;
            }
            UNLOCK_OR_KILL(ds, &(file->mux_file));
        }

        if(isOpen) {
            // File gi√† aperto da questo client: l'operazione di apertura fallisce
            if(file) {
                reply = newreply(REPLY_NO, EALREADYOPEN, 0, NULL);
            }
            success = -1;
        }
        else {
            // Il File non era aperto da questo client: lo apro
            LOCK_OR_KILL(ds, &(file->mux_file), file);
            if(!file) {
                reply = newreply(REPLY_NO, ENOFILE, 0, NULL);
                success = -1;
            }
            else {
                // Aspetto che altre modifiche siano completate prima di procedere all'apertura
                while(file->modifying == 1) {
                    pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
                }
                file->modifying = 1;

                int *newentry = realloc(file->openedBy, sizeof(int) * (file->nopened + 1));
                if(!newentry) {
                    // fallita allocazione nuovo spazio per PID del nuovo client
                    reply = newreply(REPLY_NO, errno, 0, NULL);
                    success = -1;
                }
                else {
                    // Apertura OK: setto il file come aperto dal socket
                    file->openedBy = newentry;
                    file->openedBy[file->nopened] = client_PID;
                    file->nopened++;
                    // Se necessario setto lock (non √® in stato locked per nessun altro client)
                    if(flags & O_LOCKFILE) {
                        file->lockedBy = client_PID;
                    }
                    // aggiorno info client
                    if(update_client_op(ds, client_sock, client_PID, OPEN_FILE, flags, pathname) == -1) {
                        // fallito aggiornamento stato client
                        if(put_logmsg(ds->log_thread_config, 0, "Fallito aggiornamento stato client") == -1) {
                            perror("Fallito aggiornamento stato client");
                        }
                    }
                    // L'operazione di apertura del file ha avuto successo
                    reply = newreply(REPLY_YES, 0, 0, NULL);
                }

                file->modifying = 0;
                UNLOCK_OR_KILL(ds, &(file->mux_file));
            }
        }
    }

    // Invio la risposta al client lungo il socket
    if(reply) {
        if(writen(client_sock, reply, sizeof(*reply)) != sizeof(*reply)) {
            errno_saved = errno;
            if(put_logmsg(ds->log_thread_config, errno, "[SERVER]: Fallito invio risposta") == -1) {
                errno = errno_saved;
                perror("[SERVER]: Fallito invio risposta");
            }
            success = -1;
        }
        free(reply);
    }
    else {
        // Fallita allocazione risposta (probabilmente dovuto ad un errore di memoria)
        success = -1;
        if(put_logmsg(ds->log_thread_config, errno, "[SERVER]: Fallito invio risposta") == -1) {
            errno = errno_saved;
            perror("[SERVER]: Fallito invio risposta");
        }
    }

    return success; // 0 se successo, -1 altrimenti
}

/**
 * \brief Tenta di chiudere il file con questo path per il client che lo richiede
 *
 * Effettua la chiusura del file con path pathname per il client con PID passato come parametro, se possibile.
 * Le cause del fallimento dell'operazione sono riportate di seguito:
 * - Il file non √® presente nel server
 * - Il file √® presente nel server, ma non era aperto da questo client
 *
 * Chiaramente, anche se l'operazione √® consentita, qualche passo della procedura potrebbe
 * fallire. Se tutto va a buon fine il file viene chiuso per questo client
 *
 * \param [in,out] ds La struttura dati condivisa del server, dove sono memorizzati i file
 * \param [in] pathname Il path del file di cui il client richiede la chiusura
 * \param [in] client_sock Il socket del client che richiede la chiusura
 * \param [in] client_PID Il PID del client che richiede la chiusura
 * \return Ritorna 0 se la chiusura viene eseguita con successo, -1 altrimenti
 */
int api_closeFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const int client_PID) {
    struct reply_t *reply = NULL; // conterr√† la risposta del server
    int success = 0;
    int errno_saved;

    // Cerco il file nella tabella
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(!file) {
    	// il file non √® presente nel server (quindi non posso chiuderlo)
    	reply = newreply(REPLY_NO, ENOFILE, 0, NULL);
    	success = -1;
    }
    else {
        LOCK_OR_KILL(ds, &(file->mux_file), file);
        if(!file) {
            reply = newreply(REPLY_NO, ENOFILE, 0, NULL);
            success = -1;
        }
        else {
            while(file->modifying == 1) {
                pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
            }
            file->modifying = 1;
            // file presente: devo controllare se il client che richiede la sua chiusura lo abbia aperto
            size_t i = 0;
            while(i < file->nopened && file->openedBy[i] != client_PID) {
                i++;
            }
            if(i == file->nopened) {
                // il client non aveva aperto questo file, quindi l'operazione fallisce
                reply = newreply(REPLY_NO, ENOPENED, 0, NULL);
                success = -1;
            }
            else {
                // altrimenti il file era aperto: lo chiudo per questo socket
                for(; i < file->nopened - 1; i++) {
                    file->openedBy[i] = file->openedBy[i+1];
                }
                file->nopened--;
                // rialloco (restringo) l'array e se non √® pi√Ļ aperto da alcun client lo dealloco
                if(file->nopened == 0) {
                    free(file->openedBy);
                    file->openedBy = NULL;
                }
                else {
                    file->openedBy = realloc(file->openedBy, file->nopened * sizeof(int));
                }
                // Se il file viene chiuso dal client che aveva la mutua esclusione su di esso la resetto
                if(file->lockedBy == client_PID) {
                    file->lockedBy = -1;
                }

                // rispondo positivamente alla api
                reply = newreply(REPLY_YES, 0, 0, NULL);
            }
            file->modifying = 0;
            UNLOCK_OR_KILL(ds, &(file->mux_file));
        }
   	}

   	// Invio la risposta al client lungo il socket
    if(reply) {
        if(writen(client_sock, reply, sizeof(*reply)) != sizeof(*reply)) {
            errno_saved = errno;
            if(put_logmsg(ds->log_thread_config, errno, "[SERVER]: Fallito invio risposta") == -1) {
                errno = errno_saved;
                perror("[SERVER]: Fallito invio risposta");
            }
            success = -1;
        }
        free(reply);
    }
    else {
        // Fallita allocazione risposta (probabilmente dovuto ad un errore di memoria)
        success = -1;
        errno_saved = errno;
        if(put_logmsg(ds->log_thread_config, errno, "[SERVER]: Fallito invio risposta") == -1) {
            errno = errno_saved;
            perror("[SERVER]: Fallito invio risposta");
        }
    }

   	return success;
}

/**
 * \brief Tenta di leggere il file con questo path per il client che lo richiede
 *
 * Invia alla API il file con path pathname sul socket client_sock per il client con PID
 * passato come parametro, se possibile. Se il file non √® presente nel server o non
 * √® aperto da questo client allora l'operazione fallisce
 * \param [in,out] ds La struttura dati condivisa del server, dove sono memorizzati i file
 * \param [in] pathname Il path del file di cui il client richiede la letttura
 * \param [in] client_sock Il socket del client che richiede la lettura
 * \param [in] client_PID Il PID del client che richiede la lettura
 * \return Ritorna 0 se la lettura viene eseguita con successo, -1 altrimenti
 */
int api_readFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const int client_PID) {
    // conterr√† la risposta del server
    struct reply_t *reply = NULL;
    int success = 0;
    int errno_saved;

    // Cerco il file nella ht
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(file == NULL) {
        // chiave non trovata => ritorna errore
        reply = newreply(REPLY_NO, ENOFILE, 0, NULL);
    	success = -1;
    }
    // file trovato: guardo se era stato aperto da questo client
    else {
        int i, isOpen;
        i = isOpen = 0;

        LOCK_OR_KILL(ds, &(file->mux_file), file);
        if(!file) {
            reply = newreply(REPLY_NO, ENOFILE, 0, NULL);
            success = -1;
            isOpen = 0;
        }
        else {
            while(file->modifying == 1) {
                pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
            }

            while(!isOpen && i < file->nopened) {
                if(file->openedBy[i] == client_PID) {
                    isOpen = 1; // aperto da questo client
                }
                i++;
            }
            UNLOCK_OR_KILL(ds, &(file->mux_file));
        }

        // Se √® stato aperto da questo client allora posso leggerlo
        if(isOpen) {
            // comunico alla API che ricever√† un file
            reply = newreply(REPLY_YES, 0, 1, NULL);
            // setto manualmente la dimensione del file al posto della lunghezza dei path
            // per permettere alla API di allocare un buffer di dimensione adeguata
            reply->paths_sz = file->size;
            // aggiorno info client
            if(update_client_op(ds, client_sock, client_PID, READ_FILE, 0, pathname) == -1) {
                // fallito aggiornamento stato client
                if(put_logmsg(ds->log_thread_config, 0, "Fallito aggiornamento stato client") == -1) {
                    perror("Fallito aggiornamento stato client");
                }
            }
        }
        else if(file) {
            // Operazione negata: il client non aveva aperto il file
            reply = newreply(REPLY_NO, ENOPENED, 0, NULL);
            success = -1;
        }
    }

    // Invio la risposta al client lungo il socket
    if(reply) {
        if(writen(client_sock, reply, sizeof(*reply)) != sizeof(*reply)) {
            errno_saved = errno;
            if(put_logmsg(ds->log_thread_config, errno, "[SERVER]: Fallito invio risposta") == -1) {
                errno = errno_saved;
                perror("[SERVER]: Fallito invio risposta");
            }
            success = -1;
        }
        // Invio il file al client
        if(success == 0 && reply->status == REPLY_YES) {
            if(writen(client_sock, file->data, file->size) != file->size) {
                if(put_logmsg(ds->log_thread_config, errno, "[SERVER]: Fallito invio file") == -1) {
                    errno = errno_saved;
                    perror("[SERVER]: Fallito invio file");
                }
            }
            success = -1;
        }
        free(reply);
    }
    else {
        // Fallita allocazione risposta (probabilmente dovuto ad un errore di memoria)
        success = -1;
        errno_saved = errno;
        if(put_logmsg(ds->log_thread_config, errno, "[SERVER]: Fallito invio risposta") == -1) {
            errno = errno_saved;
            perror("[SERVER]: Fallito invio risposta");
        }
    }

    return success;
}

/**
 * \brief Tenta di leggere n files presenti nel server
 *
 * Invia alla API fino a n files presenti nel server. Se n <= 0 allora invia tutti i file
 * presenti. L'ordine di scelta dei file da inviare √® quello FIFO del tempo di inserimento
 * all'interno del server
 * \param [in,out] ds La struttura dati condivisa del server, dove sono memorizzati i file
 * \param [in] n Il numero (massimo) di files che il client ha richiesto. Se <= 0 invia tutti quelli presenti
 * \param [in] client_sock Il socket del client che richiede i file
 * \param [in] client_PID Il PID del client che richiede i file
 * \return Ritorna il numero di files inviati (>= 0) se viene eseguita con successo, -1 altrimenti
 */
int api_readN(struct fs_ds_t *ds, const int n, const int client_sock, const int client_PID) {
    struct reply_t *reply = NULL;
    int success = 0;
    int errno_saved;

    // Utilizzo la coda di path per ottenere dei file da inviare. Di conseguenza invio
    // sempre al client i file meno recenti nel server (per una maggiore efficienza,
    // dato che √® una lista concatenata con il solo forward pointer)
    LOCK_OR_KILL(ds, &(ds->mux_cacheq), ds->cache_q);
    struct node_t *curr = ds->cache_q->head; // l'indirizzo del nodo contenente il primo path
    int num_sent = 0; // num_sent conterr√† il numero di file che invier√≤ alla API
    while((n <= 0 || num_sent < n) && curr) {
        curr = curr->next;
        num_sent++;
    }
    UNLOCK_OR_KILL(ds, &(ds->mux_cacheq));

    // Alloco gli array contenenti rispettivamente:
    // puntatori ai file da inviare
    struct fs_filedata_t **files = malloc(num_sent * sizeof(struct fs_filedata_t *));
    // dimensioni dei suddetti file
    size_t *sizes = malloc(num_sent * sizeof(size_t));
    // path dei suddetti file
    char **paths = malloc(num_sent * sizeof(char*));
    char *all_paths = NULL; // conterr√† tutti i path dei file, concatenati

    if(!(files && sizes && paths)) {
        // una delle allocazioni √® fallita
        reply = newreply(REPLY_NO, errno, 0, NULL);
        success = -1;
    }
    else {
        LOCK_OR_KILL(ds, &(ds->mux_cacheq), ds->cache_q);
        curr = ds->cache_q->head;
        int i;
        for(i = 0; i < num_sent; i++) {
            // Trovo il file indicato dal path in curr
            files[i] = find_file(ds, (char*)curr->data);
            if(files[i]) {
                // Di tale file devo avere la dimensione
                sizes[i] = files[i]->size;
                // E devo anche avere un puntatore al path
                paths[i] = (char*)curr->data;
            }
            else {
                // file non trovato: lo ignoro
                num_sent--;
                i--;
            }
            curr = curr->next;
        }
        UNLOCK_OR_KILL(ds, &(ds->mux_cacheq));

        // Alloco la risposta contenente il numero di file e la lunghezza dei path concatenati
        reply = newreply(REPLY_YES, 0, num_sent, paths);
        // Alloco memoria per i path concatenati
        all_paths = calloc(reply->paths_sz, sizeof(char));
        if(reply && !all_paths) {
            // errore alloc
            success = -1;
            // devo cambiare la risposta
            if(reply) {
                reply->status = REPLY_NO;
                reply->errcode = errno;
            }
        }
        else if(reply) {
            // devo quindi concatenare i num_sent path dei file in all_paths e separarli con '\n'
            build_pathstr(&all_paths, paths, num_sent);

            // aggiorno info client
            if(update_client_op(ds, client_sock, client_PID, READ_N_FILES, 0, NULL) == -1) {
                // fallito aggiornamento stato client
                if(put_logmsg(ds->log_thread_config, 0, "Fallito aggiornamento stato client") == -1) {
                    perror("Fallito aggiornamento stato client");
                }
            }
        }
    }

    if(reply) {
        // scrivo la risposta
        if(writen(client_sock, reply, sizeof(struct reply_t)) != sizeof(struct reply_t)) {
            // errore di scrittura
            errno_saved = errno;
            if(put_logmsg(ds->log_thread_config, errno, "[SERVER]: Fallito invio risposta") == -1) {
                errno = errno_saved;
                perror("[SERVER]: Fallito invio risposta");
            }
            success = -1;
        }
        // scrivo le dimensioni dei file
        if(success == 0 && reply->status == REPLY_YES) {
            if(writen(client_sock, sizes, reply->nbuffers * sizeof(size_t)) != reply->nbuffers * sizeof(size_t)) {
                success = -1;
            }
        }
        if(success == 0 && reply->status == REPLY_YES) {
            if(writen(client_sock, all_paths, reply->paths_sz) != reply->paths_sz) {
                // errore di scrittura
                success = -1;
            }
        }
        if(success == 0 && reply->status == REPLY_YES) {
            // Infine invio sul socket tutti i file
            int i;
            for(i = 0; i < num_sent; i++) {
                if(writen(client_sock, files[i]->data, files[i]->size) != files[i]->size) {
                    // errore di scrittura file
                    success = -1;
                    break;
                }
            }
        }
        if(reply) free(reply);
    }

    // libero memoria
    if(files) free(files);
    if(sizes) free(sizes);
    if(paths) free(paths);
    if(all_paths) free(all_paths);

    if(success == -1) {
        return -1;
    }
    return num_sent; // ritorno il numero di file inviati alla API
}

/**
 * \brief Tenta di concatenare al file specificato il buffer buf
 *
 * La funzione tenta di concatenare al file con path pathname il buffer buf, di dimensione
 * size. Le seguenti condizioni provocano il fallimento dell'operazione:
 * - Il file non √® presente nel server
 * - Il file non √® aperto da questo client
 * - La dimensione del buffer da concatenare, sommata alla dimensione corrente del file,
 * supera la massima quantit√† di memoria della configurazione
 *
 * Se viene determinato che concatenare buf provocherebbe un capacity miss nel server allora
 * viene chiamato l'agoritmo di rimpiazzamento, che liberer√† la memoria necessaria ad
 * avere
 * \param [in,out] ds La struttura dati condivisa del server, dove sono memorizzati i file
 * \param [in] n Il numero (massimo) di files che il client ha richiesto. Se <= 0 invia tutti quelli presenti
 * \param [in] client_sock Il socket del client che richiede i file
 * \param [in] client_PID Il PID del client che richiede i file
 * \return Ritorna il numero di files inviati (>= 0) se viene eseguita con successo, -1 altrimenti
 */
// Scrive in append al file con path pathname (se presente) il buffer buf di lunghezza size
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_appendToFile(struct fs_ds_t *ds, const char *pathname,
    const int client_sock, const int client_PID, const size_t size, void *buf)
    {
    // conterr√† la risposta del server
    struct reply_t *reply = NULL;
    int success = 0;
    int errno_saved;

    // variabili usate nella logica di rimpiazzamento
    struct Queue *evicted = NULL;
    struct Queue *evicted_paths = NULL;
    size_t *sizes = NULL;
    char **paths = NULL;
    char *all_paths = NULL;

    int i;
    int isOpen = 0;
    // Cerco il file nella tabella
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(file == NULL) {
        // file non trovato
        reply = newreply(REPLY_NO, ENOFILE, 0, NULL);
        success = -1;
    }
    else {
        LOCK_OR_KILL(ds, &(file->mux_file), file);
        if(!file) {
            success = -1;
        }
        else {
            while(file->modifying == 1) {
                pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
            }
            file->modifying = 1;

            // file trovato: se era lockato da un altro client allora non posso modificarlo
            if(file->lockedBy != -1 && file->lockedBy != client_PID) {
                reply = newreply(REPLY_NO, ELOCKED, 0, NULL);
                success = -1;
            }
            else {
                i = 0;
                isOpen = 0;
                while(!isOpen && i < file->nopened) {
                    if(file->openedBy[i] == client_PID) {
                        isOpen = 1; // aperto da questo client
                    }
                    i++;
                }
            }
            file->modifying = 0;
            UNLOCK_OR_KILL(ds, &(file->mux_file));
        }
    }

    int nevicted = 0; // numero di file espulsi
    // Controllo che le operazioni precedenti abbiano avuto successo e che il file possa effettivamente entrare in memoria
    if(success == 0 && isOpen && size <= ds->max_mem) {
        // Se la scrittura provoca capacity miss allora chiamo l'algoritmo di rimpiazzamento
        LOCK_OR_KILL(ds, &(ds->mux_mem), ds);
        int nomem = (ds->curr_mem + size > ds->max_mem ? 1 : 0);
        UNLOCK_OR_KILL(ds, &(ds->mux_mem));
        if(nomem) {
            // i file (eventualmente) espulsi sono messi all'interno delle code
            if((nevicted = cache_miss(ds, size, &evicted_paths, &evicted)) == -1) {
                // fallita l'espulsione dei file: l'operazione di apertura fallisce
                reply = newreply(REPLY_NO, errno, 0, NULL);
                success = -1;
            }
            // Altrimenti l'espulsione ha avuto successo (e nevicted contiene il numero di file espulsi)
        }
    }
    // Operazione negata: setto la risposta adeguata solo se non era gi√† stata scelta
    else if(!reply){
        success = -1;
        reply = newreply(REPLY_NO, errno, 0, NULL);
        // Buffer di dimensione maggiore del massimo quantitativo di memoria
        if(reply && size > ds->max_mem) {
            reply->errcode = ETOOBIG;
        }
        else if(reply && file){
            reply->errcode = ENOPENED;
        }
        else if(reply){
            reply->errcode = ENOFILE;
        }
    }

    // Se il file era aperto allora lo modifico
    if(success == 0 && isOpen) {
        // Sincronizzazione interna a insert_file
        if(insert_file(ds, pathname, 0, buf, size, client_PID)) {
            // L'inserimento ha avuto successo: devo inviare eventuali files espulsi al client
            if(nevicted > 0) {
                struct node_t *str = evicted_paths->head; // coda dei path dei file espulsi
                struct node_t *fs = evicted->head; // coda dei dati dei file espulsi
                sizes = malloc(nevicted * sizeof(size_t));
                paths = malloc(nevicted * sizeof(char *));
                if(!(sizes && paths)) {
                    // fallita allocazione
                    reply = newreply(REPLY_NO, errno, 0, NULL);
                    success = -1;
                }

                if(success == 0) {
                    // scrivo le dimensioni ed i puntatori ai path
                    i = 0; // resetto i, perch√© √® stata usata in precedenza
                    while(str && fs) {
                        sizes[i] = ((struct fs_filedata_t*)fs->data)->size;
                        paths[i] = (char *)str->data;
                        str = str->next;
                        fs = fs->next;
                        i++;
                    }
                    // utilizzando le dimensioni ed i path estratti costruisco la risposta
                    reply = newreply(REPLY_YES, 0, nevicted, paths);
                    if(success == 0) {
                        all_paths = calloc(reply->paths_sz, sizeof(char));
                        if(!all_paths) {
                            // fallita allocazione
                            success = -1;
                        }
                    }
                }
                if(success == 0) {
                    // devo quindi concatenare gli nevicted path dei file in all_paths e separarli con '\n'
                    build_pathstr(&all_paths, paths, nevicted);
                    // quindi posso deallocare la lista di path
                    free_Queue(evicted_paths, free);
                    // aggiorno info client
                    if(update_client_op(ds, client_sock, client_PID, APPEND_FILE, 0, pathname) == -1) {
                        // fallito aggiornamento stato client
                        if(put_logmsg(ds->log_thread_config, 0, "Fallito aggiornamento stato client") == -1) {
                            perror("Fallito aggiornamento stato client");
                        }
                    }
                }
            }
            else {
                // nessun file da espellere, ma √® comunque necessario inviare la risposta alla API
                reply = newreply(REPLY_YES, 0, 0, NULL);
            }
        }
    }

    if(reply) {
        // scrivo la risposta sul socket del client
        if(writen(client_sock, reply, sizeof(*reply)) != sizeof(*reply)) {
            // fallita scrittura
            errno_saved = errno;
            if(put_logmsg(ds->log_thread_config, errno, "[SERVER]: Fallito invio risposta") == -1) {
                errno = errno_saved;
                perror("[SERVER]: Fallito invio risposta");
            }
            success = -1;
        }
        // scrivo le dimensioni dei file espulsi
        if(success == 0 && reply->status == REPLY_YES && reply->nbuffers > 0 && sizes) {
            if(writen(client_sock, sizes, reply->nbuffers * sizeof(size_t)) != reply->nbuffers * sizeof(size_t)) {
                // fallita scrittura
                success = -1;
            }
        }
        // scrivo i path dei file espulsi
        if(success == 0 && reply->status == REPLY_YES && reply->nbuffers > 0 && all_paths) {
            if(writen(client_sock, all_paths, reply->paths_sz) != reply->paths_sz) {
                // fallita scrittura
                success = -1;
            }
        }
        // scrivo i file espulsi
        if(success == 0 && reply->status == REPLY_YES && reply->nbuffers > 0 && evicted) {
            // Parto dal primo puntatore a file da scrivere presente nella lista e lo invio sul socket
            struct node_t *n = NULL;
            while((n = pop(evicted)) != NULL) {
                struct fs_filedata_t *file = (struct fs_filedata_t *)n->data;
                if(writen(client_sock, file->data, file->size) != file->size) {
                    // fallita scrittura
                    success = -1;
                    break;
                }
                // libero il file e passo al prossimo
                free_file(file);
                free(n);
            }
            free(evicted);
        }
        free(reply);
    }

    // posso quindi liberare gli array di supporto
    if(sizes) free(sizes);
    if(paths) free(paths);
    if(all_paths) free(all_paths);

    return success;
}

/**
 * \brief Tenta di scrivere il file pathname, con dati buf, nel server
 *
 * Implementa la funzione writeFile della API, scrivendo i dati contenuti in buf in un
 * nuovo file con path pathname se e solo se la precedente richiesta completata con successo
 * dal server proveniente dal client client_PID √® stata openFile(pathname O_CREATEFILE|O_LOCKFILE).
 * Internamente viene chiamata api_appendToFile() per effettuare la scrittura vera e propria
 * \param [in,out] ds La struttura dati condivisa del server, dove sono memorizzati i file
 * \param [in] pathname Il path del file da scrivere nel server
 * \param [in] client_sock Il descrittore del socket sul quale il client richiedente √® connesso
 * \param [in] client_PID Il PID del client che richiede l'operazione
 * \param [in] size La dimensione del buffer da scrivere
 * \param [in] buf Il buffer che andr√† a costituire la parte dati del file da scrivere
 * \return Ritorna 0 se viene eseguita con successo, -1 altrimenti
 */
int api_writeFile(struct fs_ds_t *ds, const char *pathname,
    const int client_sock, const int client_PID, const size_t size, void *buf)
    {
    // conterr√† la risposta del server
    struct reply_t *reply = NULL;
    int success = 0;
    int errno_saved;

    // controllo se l'ultima operazione di questo client (completata con successo)
    // fosse stata una openFile(pathname, O_CREATEFILE|O_LOCKFILE)
    LOCK_OR_KILL(ds, &(ds->mux_clients), ds);
    size_t pos = 0;
    while(  pos < ds->connected_clients
            && !(ds->active_clients[pos].PID == client_PID
                && ds->active_clients[pos].last_op == OPEN_FILE
                && (ds->active_clients[pos].last_op_flags == (O_CREATEFILE|O_LOCKFILE))
                && strcmp(ds->active_clients[pos].last_op_path, pathname) == 0))
    { pos++; }
    if(pos == ds->connected_clients) {
        // il client non rispetta almeno una delle condizioni sopra, quindi l'operazione √® negata
        reply = newreply(REPLY_NO, ENOPENED, 0, NULL);
        success = -1;
    }
    UNLOCK_OR_KILL(ds, &(ds->mux_clients));

    int ret;
    if(success == 0) {
        // La scrittura effettiva avviene chiamando api_appendToFile
        ret = api_appendToFile(ds, pathname, client_sock, client_PID, size, buf);
    }
    if(success != 0) {
        // Scrivo la risposta negativa
        if(reply) {
            if(writen(client_sock, reply, sizeof(*reply)) != sizeof(*reply)) {
                errno_saved = errno;
                if(put_logmsg(ds->log_thread_config, errno, "[SERVER]: Fallito invio risposta") == -1) {
                    errno = errno_saved;
                    perror("[SERVER]: Fallito invio risposta");
                }
            }
        }
        return -1;
    }
    // La risposta nel caso in cui l'operazione apppendToFile sia eseguita √® mandata da appendToFile
    // ed eventuali espulsioni di file sono fatte all'interno della funzione stessa

    return ret;
}

/**
 * \brief Tenta di assegnare la mututa esclusione sul file con path dato al client che ha effettuato la richiesta
 *
 * La funzione tenta di assegnare la mututa esclusione sul file con path pathname
 * al client richiedente.
 * Condizioni necessarie per ottenere ME:
 * - Il file con path pathname √® presente nel server
 * - Il file √® aperto dal client client_PID
 * Se nessun client ha la ME sul file richiesto, essa viene assegnata al client richiedente.
 * Se vi √® un client che detiene la ME sul file allora il client viene messo in coda di attesa
 * e viene creato un thread dedicato che aspetta di ricevere la notifica dell'unlock.
 * Nel secondo caso la risposta di lock acquisita ed il feedback al server sar√†
 * inviato dal thread di attesa (wait_lock)
 * \param [in,out] ds La struttura dati condivisa del server, dove sono memorizzati i file
 * \param [in] pathname Il path del file su cui si vuole settare ME nel server
 * \param [in] client_sock Il descrittore del socket sul quale il client richiedente √® connesso
 * \param [in] client_PID Il PID del client che richiede l'operazione
 * \return Ritorna 0 se la ME viene acquisita senza attendere, il TID del thread
 * che effettua l'attesa (intero > 0) se il client viene messo in attesa del completamento
 * dell'operazione, -1 altrimenti
 */
pthread_t api_lockFile(struct fs_ds_t*ds, const char *pathname, const int client_sock, const int client_PID) {
    int success = 0;
    // La risposta del server
    struct reply_t *rep = NULL;
    int errno_saved;

    // Cerco il file nel fileserver
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(!file) {
        rep = newreply(REPLY_NO, ENOFILE, 0, NULL);
        success = -1;
    }
    else {
        LOCK_OR_KILL(ds, &(file->mux_file), file);
        if(!file) {
            rep = newreply(REPLY_NO, ENOFILE, 0, NULL);
            success = -1;
        }
        else {
            while(file->modifying == 1) {
                pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
            }
            file->modifying = 1;

            // Se era libero o lockato da questo client posso settare la lock immediatamente
            if(file->lockedBy == -1 || file->lockedBy == client_PID) {
                file->lockedBy = client_PID;
                // L'operazione ha avuto esito positivo
                rep = newreply(REPLY_YES, 0, 0, NULL);
            }
            // Lockato da un altro client: devo attendere che si liberi
            else {
                // Metto in coda il PID del client che deve attendere la lock
                // ed anche un puntatore alla struttura dati condivisa
                int r1 = enqueue(file->waiting_clients, &client_PID, sizeof(int), client_sock);
                int r2 = enqueue(file->waiting_clients, ds, sizeof(struct fs_ds_t), -1);
                if(r1 == -1 || r2 == -1) {
                    // falliti inserimenti in coda
                    rep = newreply(REPLY_NO, errno, 0, NULL);
                    success = -1;
                }
                else {
                    // resetto flag modifica: non cambia detentore lock
                    file->modifying = 0;
                    UNLOCK_OR_KILL(ds, &(file->mux_file));
                    // Creo un altro thread detached che si incarica dell'attesa
                    pthread_t waiter_th = 0;
                    pthread_attr_t attrs;
                    pthread_attr_init(&attrs);
                    pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
                    pthread_create(&waiter_th, &attrs, wait_lock, file);
                    return waiter_th;
                }
            }

            file->modifying = 0;
            UNLOCK_OR_KILL(ds, &(file->mux_file));
            // aggiorno operazioni client
            if(success == 0) {
                update_client_op(ds, client_sock, client_PID, LOCK_FILE, O_LOCKFILE, pathname);
            }
        }
    }
    if(!rep) {
        // Se non posso inviare la risposta allora non setto la lock
        if(file) file->lockedBy = -1;
        return -1;
    }
    else {
        if(writen(client_sock, rep, sizeof(*rep)) != sizeof(*rep)) {
            errno_saved = errno;
            if(put_logmsg(ds->log_thread_config, errno, "[SERVER]: Fallito invio risposta") == -1) {
                errno = errno_saved;
                perror("[SERVER]: Fallito invio risposta");
            }
            success = -1;
        }
        free(rep);
    }

    return success;
}

/**
 * \brief Tenta di togliere la mututa esclusione sul file con path dato al client che ha effettuato la richiesta
 *
 * La funzione fa un tentativo di resettare la lock sul file con path dato a condizione che
 * il file sia stato aperto dal client e sia in stato locked per il client che effettua unlock
 * \param [in,out] ds La struttura dati condivisa del server, dove sono memorizzati i file
 * \param [in] pathname Il path del file su cui si vuole resettare ME nel server
 * \param [in] client_sock Il descrittore del socket sul quale il client richiedente √® connesso
 * \param [in] client_PID Il PID del client che richiede l'operazione
 * \return Ritorna 0 se viene eseguita con successo, -1 altrimenti
 */
int api_unlockFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const int client_PID) {
    // La risposta del server
    struct reply_t *rep = NULL;
    int success = 0;
    int errno_saved;

    // Cerco il file nel fileserver
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(!file) {
        // file non trovato
        rep = newreply(REPLY_NO, ENOFILE, 0, NULL);
        success = -1;
    }
    else {
        LOCK_OR_KILL(ds, &(file->mux_file), file);
        if(!file) {
            rep = newreply(REPLY_NO, ENOFILE, 0, NULL);
            success = -1;
        }
        else {
            while(file->modifying == 1) {
                pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
            }
            file->modifying = 1;

            if(file->lockedBy == client_PID) {
                // Se il file era lockato da questo client allora rimuovo la lock
                file->lockedBy = -1;
                // Poi devo segnalare un thread in attesa di questa lock che essa √® free
                pthread_cond_signal(&(file->lock_free));

                rep = newreply(REPLY_YES, 0, 0, NULL);
                if(!rep) {
                    // Se non posso inviare la risposta ripristino la lock e faccio fallire l'operazione
                    file->lockedBy = client_PID;
                    success = -1;
                }
            }
            else {
                // file lockato da qualche altro client
                rep = newreply(REPLY_NO, ELOCKED, 0, NULL);
                success = -1;
            }

            file->modifying = 0;
            UNLOCK_OR_KILL(ds, &(file->mux_file));

            if(success == 0) {
                update_client_op(ds, client_sock, client_PID, UNLOCK_FILE, 0, pathname);
            }
        }
    }
    if(rep) {
        if(writen(client_sock, rep, sizeof(*rep)) != sizeof(*rep)) {
            errno_saved = errno;
            if(put_logmsg(ds->log_thread_config, errno, "[SERVER]: Fallito invio risposta") == -1) {
                errno = errno_saved;
                perror("[SERVER]: Fallito invio risposta");
            }
            success = -1;
        }
        free(rep);
    }

    return success;
}

/**
 * \brief Tenta di rimuovere il file con path dato dal server
 *
 * La funzione rimuove il file con path dato se il file era aperto e in stato locked per
 * il client che ne richiede la chiusura. Se almeno una delle condizioni non √® vera allora
 * l'operazione fallisce
 * \param [in,out] ds La struttura dati condivisa del server, dove sono memorizzati i file
 * \param [in] pathname Il path del file di cui si vuole effettuare la rimozione
 * \param [in] client_sock Il descrittore del socket sul quale il client richiedente √® connesso
 * \param [in] client_PID Il PID del client che richiede l'operazione
 * \return Ritorna 0 se viene eseguita con successo, -1 altrimenti
 */
int api_rmFile(struct fs_ds_t*ds, const char *pathname, const int client_sock, const int client_PID) {
    // La risposta del server
    struct reply_t *rep = NULL;
    int success = 0;
    int errno_saved;

    // Cerco il file nel fileserver
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(!file) {
        // file non trovato
        rep = newreply(REPLY_NO, ENOFILE, 0, NULL);
        success = -1;
    }
    else {
        LOCK_OR_KILL(ds, &(file->mux_file), file);
        if(!file) {
            rep = newreply(REPLY_NO, ENOFILE, 0, NULL);
            success = -1;
        }
        else {
            while(file->modifying == 1) {
                pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
            }
            file->modifying = 1;

            if(file->lockedBy != client_PID) {
                // file non lockato da questo client
                UNLOCK_OR_KILL(ds, &(file->mux_file));
                rep = newreply(REPLY_NO, ELOCKED, 0, NULL);
                success = -1;
            }
            else {
                // Se il file era lockato da questo client allora lo rimuovo
                struct fs_filedata_t *tmptr = file; // copio in un temporaneo il puntatore
                file = NULL; // metto file a NULL
                int table = icl_hash_delete(ds->fs_table, (void*)pathname, free, NULL); // lo tolgo dalla ht
                free_file(tmptr); // dealloco il file

                // Tolgo anche il path dalla coda
                LOCK_OR_KILL(ds, &(ds->mux_cacheq), ds->cache_q);
                struct node_t *curr = ds->cache_q->head;
                struct node_t *prev = NULL;
                int cache = -1;
                while(curr) {
                    if(strcmp((char*)curr->data, pathname) == 0) {
                        struct node_t *tmp = curr;
                        if(prev) {
                            // sto rimuovendo un altro elemento della lista (non la testa)
                            prev->next = curr->next;
                        }
                        else {
                            // sto rimuovendo la testa della lista
                            ds->cache_q->head = curr->next;
                        }
                        if(curr == ds->cache_q->tail) {
                            // Se sto rimuovendo la coda devo aggiornare la coda
                            ds->cache_q->tail = prev;
                        }
                        // libero tmp (curr)
                        free(tmp->data);
                        free(tmp);
                        tmp = NULL;
                        cache = 0; // setto per indicare che ho rimosso con successo dalla cache
                        break;
                    }
                    prev = curr;
                    curr = curr->next;
                }
                UNLOCK_OR_KILL(ds, &(ds->mux_cacheq));

                // Aggiorno numero file nel server
                LOCK_OR_KILL(ds, &(ds->mux_files), ds);
                ds->curr_files--;
                UNLOCK_OR_KILL(ds, &(ds->mux_files));

                if(table == 0) {
                    // tolto con successo dalla hashtable
                    rep = newreply(REPLY_YES, 0, 0, NULL);
                }
                else {
                    // impossibile togliere il file dalla ht
                    if(put_logmsg(ds->log_thread_config, 0, "[SERVER] api_rmFile: Consistenza hash table persa") == -1) {
                        perror("[SERVER] api_rmFile: Consistenza hash table persa");
                    }
                }
                if(cache == -1) {
                    // consistenza della cache non garantita!
                    // Non termino il server ma effettuo il log dell'evento
                    if(put_logmsg(ds->log_thread_config, 0, "[SERVER] api_rmFile: Consistenza cache persa") == -1) {
                        perror("[SERVER] api_rmFile: Consistenza cache persa");
                    }
                }
            }
        }
    }

    if(rep) {
        if(writen(client_sock, rep, sizeof(*rep)) != sizeof(*rep)) {
            errno_saved = errno;
            if(put_logmsg(ds->log_thread_config, errno, "[SERVER]: Fallito invio risposta") == -1) {
                errno = errno_saved;
                perror("[SERVER]: Fallito invio risposta");
            }
            success = -1;
        }
        free(rep);
    }

    return success;
}
