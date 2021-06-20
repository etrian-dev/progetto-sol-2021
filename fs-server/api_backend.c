// header server
#include <server-utils.h>
// header API
#include <fs-api.h>
// header utilità
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

// Costruisce una stringa contenente i num pathname in paths (str deve essere già allocata)
void build_pathstr(char **str, const char **paths, const int num) {
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

// Apre il file con path pathname (se presente) per il client con le flag passate come parametro
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_openFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const int client_PID, int flags) {
    // conterrà la risposta del server
    struct reply_t *reply = NULL;
    int success = 0;

    // Cerco il file nella tabella
    struct fs_filedata_t *file = find_file(ds, pathname);

    // Se il file non è presente ed è stata passata la flag per la creazione allora lo creo
    if(!file && (flags & O_CREATEFILE)) {
        // Se ho già raggiunto il massimo numero di file memorizzabili allora la open fallisce
        // perché sto tentando di creare un nuovo file
        LOCK_OR_KILL(ds, &(ds->mux_files), ds);
        if(ds->curr_files == ds->max_files) {
            reply = newreply(REPLY_NO, 0, NULL);
            success = -1;
        }
        UNLOCK_OR_KILL(ds, &(ds->mux_files));

        // Altrimenti posso crearlo
        if(success == 0) {
            // inserisco un file vuoto (passando NULL come buffer) nella hash table
            // e le flags (solo O_LOCKFILE rilevante per settare lock)
            if(insert_file(ds, pathname, flags, NULL, 0, client_PID) == NULL) {
                reply = newreply(REPLY_NO, 0, NULL);
                if(logging(ds, 0, "openFile: impossibile creare il file") == -1) {
                    perror("openFile: impossibile creare il file");
                }
                success = -1;
            }
            else {
                // Inserimento OK (num di file aperti aggiornato da insertFile)
                reply = newreply(REPLY_YES, 0, NULL);
                // aggiorno info client
                if(update_client_op(ds, client_sock, client_PID, OPEN_FILE, flags, pathname) == -1) {
                    // fallito aggiornamento stato client
                    if(logging(ds, 0, "Fallito aggiornamento stato client") == -1) {
                        perror("Fallito aggiornamento stato client");
                    }
                }
            }
        }
    }
    // Se il file non è presente nel server e non è stata passata la flag per la creazione
    // oppure se il file è presente ed è stata passata la flag per la creazione
    // oppure se il file esiste ed è lockato da qualche altro client
    // allora l'operazione di apertura/creazione fallisce
    else if((!file && !(flags & O_CREATEFILE)) || (file && (flags & O_CREATEFILE)) || (file->lockedBy != -1)) {
        reply = newreply(REPLY_NO, 0, NULL);
        if(logging(ds, 0, "openFile: impossibile aprire/creare il file") == -1) {
            perror("openFile: impossibile aprire/creare il file");
        }
        success = -1;
    }
    // File trovato nella tabella
    else {
        size_t i = 0;
        int isOpen = 0;
        // Cerco questo socket tra quelli che hanno aperto il file
        LOCK_OR_KILL(ds, &(file->mux_file), file);
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

        if(isOpen) {
            // File già aperto da questo client: l'operazione di apertura fallisce
            reply = newreply(REPLY_NO, 0, NULL);
            if(logging(ds, 0, "openFile: file già aperto dal client") == -1) {
                perror("openFile: file già aperto dal client");
            }
            success = -1;
        }
        else {
            // Il File non era aperto da questo client: lo apro
            LOCK_OR_KILL(ds, &(file->mux_file), file);
            // Aspetto che altre modifiche siano completate prima di procedere all'apertura
            while(file->modifying == 1) {
                pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
            }
            file->modifying = 1;

            int *newentry = realloc(file->openedBy, sizeof(int) * (file->nopened + 1));
            if(!newentry) {
                // fallita allocazione nuovo spazio per fd
                reply = newreply(REPLY_NO, 0, NULL);
                if(logging(ds, 0, "openFile: apertura file non riuscita") == -1) {
                    perror("openFile: apertura file non riuscita");
                }
                success = -1;
            }
            else {
                // Apertura OK: setto il file come aperto dal socket
                file->openedBy = newentry;
                file->openedBy[file->nopened] = client_PID;
                file->nopened++;
                // Se necessario setto lock (non è in stato locked per nessun altro client)
                if(flags & O_LOCKFILE) {
                    file->lockedBy = client_PID;
                }
                // aggiorno info client
                if(update_client_op(ds, client_sock, client_PID, OPEN_FILE, flags, pathname) == -1) {
                    // fallito aggiornamento stato client
                    if(logging(ds, 0, "Fallito aggiornamento stato client") == -1) {
                        perror("Fallito aggiornamento stato client");
                    }
                }

                reply = newreply(REPLY_YES, 0, NULL);
            }

            file->modifying = 0;
            UNLOCK_OR_KILL(ds, &(file->mux_file));
        }
    }

    // Invio la risposta al client lungo il socket
    if(reply) {
        if(writen(client_sock, reply, sizeof(*reply)) != sizeof(*reply)) {
            success = -1;
        }
        free(reply);
    }
    else {
        // Fallita allocazione risposta (probabilmente dovuto ad un errore di memoria)
        success = -1;
    }

    return success; // 0 se successo, -1 altrimenti
}

// Chiude il file con path pathname (se presente) per il socket passato come parametro
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_closeFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const int client_PID) {
	// conterrà la risposta del server
    struct reply_t *reply = NULL;
    int success = 0;

    // Cerco il file nella tabella
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(!file) {
    	// il file non è presente nel server (quindi non posso chiuderlo)
    	reply = newreply(REPLY_NO, 0, NULL);
    	success = -1;
    }
    else {
        LOCK_OR_KILL(ds, &(file->mux_file), file);
        while(file->modifying == 1) {
            pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
        }
        file->modifying = 1;

   		// file presente: devo controllare se il client che richiede la sua chiusura
        // lo abbia aperto in precedenza
        size_t i = 0;
   		while(i < file->nopened && file->openedBy[i] != client_PID) {
   			i++;
   		}
   		if(i == file->nopened) {
   			// il client non aveva aperto questo file, quindi l'operazione fallisce
   			reply = newreply(REPLY_NO, 0, NULL);
   			success = -1;
   		}
        else {
            // altrimenti il file era aperto: lo chiudo per questo socket
            for(; i < file->nopened - 1; i++) {
                file->openedBy[i] = file->openedBy[i+1];
            }
            file->nopened--;
            // rialloco (restringo) l'array e se non è più aperto da alcun client lo dealloco
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
            reply = newreply(REPLY_YES, 0, NULL);
        }
        file->modifying = 0;
        UNLOCK_OR_KILL(ds, &(file->mux_file));
   	}

   	if(reply) {
   		if(writen(client_sock, reply, sizeof(*reply)) != sizeof(*reply)) {
   			// fallita scrittura sul socket
            success = -1;
   		}
   		free(reply);
   	}
    else {
        success = -1;
    }

   	return success;
}

// Legge il file con path pathname (se presente) e lo invia al client client_sock
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_readFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const int client_PID) {
    // conterrà la risposta del server
    struct reply_t *reply = NULL;
    int success = 0;

    // Cerco il file nella ht
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(file == NULL) {
        // chiave non trovata => ritorna errore
        reply = newreply(REPLY_NO, 0, NULL);
        if(logging(ds, 0, "readFile: file non trovato") == -1) {
            perror("readFile: file non trovato");
        }
        success = -1;
    }
    // file trovato: guardo se era stato aperto da questo client
    else {
        int i, isOpen;
        i = isOpen = 0;

        LOCK_OR_KILL(ds, &(file->mux_file), file);
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

        // Se è stato aperto da questo client allora posso leggerlo
        if(isOpen) {
            reply = newreply(REPLY_YES, 1, NULL);
            // aggiorno info client
            if(update_client_op(ds, client_sock, client_PID, READ_FILE, 0, pathname) == -1) {
                // fallito aggiornamento stato client
                if(logging(ds, 0, "Fallito aggiornamento stato client") == -1) {
                    perror("Fallito aggiornamento stato client");
                }
            }
            // setto manualmente la dimensione del file al posto della lunghezza dei path
            // per permettere alla API di allocare un buffer di dimensione adeguata
            if(reply) {
                reply->paths_sz = file->size;
                if(writen(client_sock, reply, sizeof(*reply)) != sizeof(*reply)) {
                    // fallito invio risposta
                    success = -1;
                }
                if(success != 0 || writen(client_sock, file->data, file->size) != file->size) {
                    success = -1;
                }
                free(reply);
                return 0; // ritorno subito per evitare di interferire con il test successivo
            }
        }
        else {
            // Operazione negata: il client non aveva aperto il file
            reply = newreply(REPLY_NO, 0, NULL);
            if(logging(ds, 0, "readFile: il file non era aperto") == -1) {
                perror("readFile: il file non era aperto");
            }
            success = -1;
        }
    }

    // Scrivo la risposta (è REPLY_NO, altrimenti è già stata inviata)
    if(reply) {
        if(writen(client_sock, reply, sizeof(*reply)) != sizeof(*reply)) {
            // fallito invio risposta
            success = -1;
        }
        free(reply);
    }
    success = -1;

    return success;
}

// Legge n file nel server (quelli meno recenti per come è implementata) e li invia al client
// Se n<=0 allora legge tutti i file presenti nel server
// Se ha successo ritorna il numero di file letti, -1 altrimenti
int api_readN(struct fs_ds_t *ds, const int n, const int client_sock, const int client_PID) {
    // Utilizzo la coda di path per ottenere dei file da inviare. Di conseguenza invio
    // sempre al client i file meno recenti nel server (per una maggiore efficienza,
    // dato che è una lista concatenata con il solo forward pointer)
    int success = 0;

    LOCK_OR_KILL(ds, &(ds->mux_cacheq), ds->cache_q);
    struct node_t *curr = ds->cache_q->head; // l'indirizzo del nodo contenente il primo path
    int num_sent = 0; // num_sent conterrà il numero di file che invierò alla API
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
    char *all_paths = NULL; // conterrà tutti i path dei file, concatenati
    struct reply_t *rep = NULL;

    if(!(files && sizes && paths)) {
        // una delle allocazioni è fallita
        success = -1;
    }
    else {
        LOCK_OR_KILL(ds, &(ds->mux_cacheq), ds->cache_q);
        curr = ds->cache_q->head;
        int i;
        for(i = 0; i < num_sent; i++) {
            // Trovo il file indicato dal path in curr
            files[i] = find_file(ds, (char*)curr->data);
            // Di tale file devo avere la dimensione
            sizes[i] = files[i]->size;
            // E devo anche avere un puntatore al path
            paths[i] = (char*)curr->data;
            curr = curr->next;
        }
        UNLOCK_OR_KILL(ds, &(ds->mux_cacheq));

        // Alloco la risposta contenente il numero di file e la lunghezza dei path concatenati
        rep = newreply(REPLY_YES, num_sent, paths);
        if(!rep) {
            success = -1;
        }

        all_paths = calloc(rep->paths_sz, sizeof(char));
        if(!all_paths) {
            // errore alloc
            success = -1;
            // devo cambiare la risposta
            if(rep) {
                rep->status = REPLY_NO;
            }
        }
        else {
            // devo quindi concatenare i num_sent path dei file in all_paths e separarli con '\n'
            build_pathstr(&all_paths, paths, num_sent);
        }


        if(rep) {
            // scrivo la risposta
            if(writen(client_sock, rep, sizeof(struct reply_t)) != sizeof(struct reply_t)) {
                // errore di scrittura
                success = -1;
            }
            // scrivo le dimensioni dei file
            if(success == 0 && rep->status == REPLY_YES) {
                if(writen(client_sock, sizes, rep->nbuffers * sizeof(size_t)) != rep->nbuffers * sizeof(size_t)) {
                    success = -1;
                }
            }
            if(success == 0 && rep->status == REPLY_YES) {
                if(writen(client_sock, all_paths, rep->paths_sz) != rep->paths_sz) {
                    // errore di scrittura
                    success = -1;
                }
            }
            if(success == 0) {
                // Infine invio sul socket tutti i file
                for(i = 0; i < num_sent; i++) {
                    if(writen(client_sock, files[i]->data, files[i]->size) != files[i]->size) {
                        // errore di scrittura file
                        success = -1;
                        break;
                    }
                }
            }
            if(success == 0) {
                // aggiorno info client
                if(update_client_op(ds, client_sock, client_PID, READ_N_FILES, 0, NULL) == -1) {
                    // fallito aggiornamento stato client
                    if(logging(ds, 0, "Fallito aggiornamento stato client") == -1) {
                        perror("Fallito aggiornamento stato client");
                    }
                }
            }
        }
    }

    // libero memoria
    if(files) free(files);
    if(sizes) free(sizes);
    if(paths) free(paths);
    if(all_paths) free(all_paths);
    if(rep) free(rep);

    if(success == -1) {
        return -1;
    }
    return num_sent; // ritorno il numero di file inviati alla API
}

// Scrive in append al file con path pathname (se presente) il buffer buf di lunghezza size
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_appendToFile(struct fs_ds_t *ds, const char *pathname,
    const int client_sock, const int client_PID, const size_t size, void *buf)
    {
    // conterrà la risposta del server
    struct reply_t *reply = NULL;
    int success = 0;

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
        if(logging(ds, 0, "api_appendToFile: file non trovato") == -1) {
            perror("api_appendToFile: file non trovato");
        }
        success = -1;
    }
    else {
        LOCK_OR_KILL(ds, &(file->mux_file), file);
        while(file->modifying == 1) {
            pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
        }
        file->modifying = 1;

        // file trovato: se era lockato da un altro client allora non posso modificarlo
        if(file->lockedBy != -1 && file->lockedBy != client_PID) {
            reply = newreply(REPLY_NO, 0, NULL);
            if(logging(ds, 0, "api_appendToFile: file lockato da un altro client") == -1) {
                perror("api_appendToFile: file lockato da un altro client");
            }
            success = -1;
        }
        else {
            i = 0;
            isOpen = 0; // flag per determinare se il file era aperto
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
                reply = newreply(REPLY_NO, 0, NULL);
                success = -1;
            }
            // Altrimenti l'espulsione ha avuto successo (e nevicted contiene il numero di file espulsi)
        }
    }
    // Operazione negata
    else {
        success = -1;
        // Buffer di dimensione maggiore del massimo quantitativo di memoria
        if(size > ds->max_mem) {
            if(logging(ds, 0, "api_appendToFile: buffer troppo grande") == -1) {
                perror("api_appendToFile: buffer troppo grande");
            }
        }
        else {
            if(logging(ds, 0, "api_appendToFile: il file non era stato aperto") == -1) {
                perror("api_appendToFile: il file non era stato aperto");
            }
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
                    success = -1;
                }
                if(success == 0) {
                    // scrivo le dimensioni ed i puntatori ai path
                    i = 0; // resetto i, perché è stata usata in precedenza
                    while(str && fs) {
                        sizes[i] = ((struct fs_filedata_t*)fs->data)->size;
                        paths[i] = (char *)str->data;
                        str = str->next;
                        fs = fs->next;
                        i++;
                    }
                    // utilizzando le dimensioni ed i path estratti costruisco la risposta
                    if((reply = newreply(REPLY_YES, nevicted, paths)) == NULL) {
                        // fallita allocazione risposta: fallisce anche l'invio dei file
                        success = -1;
                    }
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
                    free_Queue(evicted_paths);
                    // aggiorno info client
                    if(update_client_op(ds, client_sock, client_PID, APPEND_FILE, 0, pathname) == -1) {
                        // fallito aggiornamento stato client
                        if(logging(ds, 0, "Fallito aggiornamento stato client") == -1) {
                            perror("Fallito aggiornamento stato client");
                        }
                    }
                }
            }
            else {
                // nessun file da espellere, ma è comunque necessario inviare la risposta alla API
                reply = newreply(REPLY_YES, 0, NULL);
            }
        }
    }
    else {
        // Aggiornamento del file fallito
        reply = newreply(REPLY_NO, 0, NULL);
        success = -1;
    }

    if(reply) {
        // scrivo la risposta sul socket del client
        if(writen(client_sock, reply, sizeof(*reply)) != sizeof(*reply)) {
            // fallita scrittura
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

// Se l'operazione precedente del client client_PID (completata con successo) era stata
// openFile(pathname, O_CREATEFILE|O_LOCKFILE) allora il file pathname viene troncato e
// viene scritto il contenuto di buf, di dimensione size
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_writeFile(struct fs_ds_t *ds, const char *pathname,
    const int client_sock, const int client_PID, const size_t size, void *buf)
    {
    int success = 0;
    // conterrà la risposta del server
    struct reply_t *reply = NULL;
    // controllo se l'ultima operazione di questo client (completata con successo)
    // fosse stata una openFile(pathname, O_CREATEFILE|O_LOCKFILE)
    size_t pos = 0;
    while(  pos < ds->connected_clients
            && !(ds->active_clients[pos].PID == client_PID
                && ds->active_clients[pos].last_op == OPEN_FILE
                && (ds->active_clients[pos].last_op_flags == (O_CREATEFILE|O_LOCKFILE))
                && strcmp(ds->active_clients[pos].last_op_path, pathname) == 0))
    { pos++; }
    if(pos == ds->connected_clients) {
        // il client non rispetta almeno una delle condizioni sopra, quindi l'operazione è negata
        reply = newreply(REPLY_NO, 0, NULL);
        success = -1;
    }

    int ret;
    if(success == 0) {
        // La scrittura effettiva avviene chiamando api_appendToFile
        ret = api_appendToFile(ds, pathname, client_sock, client_PID, size, buf);
    }
    if(success != 0 || ret != 0) {
        // Scrivo la risposta negativa
        if(reply) {
            writen(client_sock, reply, sizeof(*reply));
        }
        return -1;
    }
    // La risposta nel caso in cui l'operazione completi con successo è mandata da appendToFile
    // ed eventuali espulsioni di file sono fatte all'interno della funzione stessa

    return 0;
}

// Assegna, se possibile, la mutua esclusione sul file con path pathname al client client_PID
// Ritorna 0 se ha successo, -1 altrimenti
int api_lockFile(struct fs_ds_t*ds, const char *pathname, const int client_sock, const int client_PID) {
    // La risposta del server
    struct reply_t *rep = NULL;

    // Cerco il file nel fileserver
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(!file) {
        // file non trovato: setto a -1 il secondo campo per indicare che il file non è nel server
        rep = newreply(REPLY_NO, -1, NULL);
    }
    else {
        LOCK_OR_KILL(ds, &(file->mux_file), file);
        while(file->modifying == 1) {
            pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
        }
        file->modifying = 1;

        // Se era lockato da questo client o era libero setto lock
        if(file->lockedBy == -1 || file->lockedBy == client_PID) {
            file->lockedBy = client_PID;
            // L'operazione ha avuto esito positivo
            rep = newreply(REPLY_YES, 0, NULL);
        }
        else {
            // Lockato da un altro client
            rep = newreply(REPLY_NO, 0, NULL);
        }

        file->modifying = 0;
        UNLOCK_OR_KILL(ds, &(file->mux_file));

    }
    if(!rep) {
        // Se non posso inviare la risposta allora non setto la lock
        file->lockedBy = -1;
        return -1;
    }
    else {
        update_client_op(ds, client_sock, client_PID, LOCK_FILE, O_LOCKFILE, pathname);
        if(writen(client_sock, rep, sizeof(*rep)) != sizeof(*rep)) {
            return -1;
        }
        free(rep);
    }

    return 0;
}
// Toglie la mutua esclusione sul file pathname (solo se era lockato da client_PID)
// Ritorna 0 se ha successo, -1 altrimenti
int api_unlockFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const int client_PID) {
    // La risposta del server
    struct reply_t *rep = NULL;

    // Cerco il file nel fileserver
    struct fs_filedata_t *file = find_file(ds, pathname);

    LOCK_OR_KILL(ds, &(file->mux_file), file);
    while(file->modifying == 1) {
        pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
    }
    file->modifying = 1;

    if(file->lockedBy == client_PID) {
        // Se il file era lockato da questo client allora rimuovo la lock
        file->lockedBy = -1;

        rep = newreply(REPLY_YES, 0, NULL);
        if(!rep) {
            // Se non posso inviare la risposta ripristino la lock e faccio fallire l'operazione
            file->lockedBy = client_PID;
            return -1;
        }
    }
    else {
        // file non trovato o lockato da qualche altro client
        rep = newreply(REPLY_NO, 0, NULL);
    }

    file->modifying = 0;
    UNLOCK_OR_KILL(ds, &(file->mux_file));

    if(rep) {
        update_client_op(ds, client_sock, client_PID, UNLOCK_FILE, 0, pathname);
        if(writen(client_sock, rep, sizeof(*rep)) != sizeof(*rep)) {
            free(rep);
            return -1;
        }
        free(rep);
    }

    return 0;
}

// Rimuove dal server il file con path pathname, se presente e lockato da client_PID
// Ritorna 0 se ha successo, -1 altrimenti
int api_rmFile(struct fs_ds_t*ds, const char *pathname, const int client_sock, const int client_PID) {
    // La risposta del server
    struct reply_t *rep = NULL;

    // Cerco il file nel fileserver
    struct fs_filedata_t *file = find_file(ds, pathname);

    if(!file) {
        // file non trovato
        rep = newreply(REPLY_NO, 0, NULL);
    }
    else {

        LOCK_OR_KILL(ds, &(file->mux_file), file);
        while(file->modifying == 1) {
            pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
        }
        file->modifying = 1;

        if(file->lockedBy != client_PID) {
            // file non lockato da questo client
            rep = newreply(REPLY_NO, 0, NULL);
            UNLOCK_OR_KILL(ds, &(file->mux_file)); // operazione non consentita
        }
        else {
            // Se il file era lockato da questo client allora lo rimuovo (prima tolgo lock)
            struct fs_filedata_t *tmptr = file; // copio in un temporaneo il puntatore
            file = NULL; // metto file a NULL
            //pthread_mutex_unlock(&(file->mux_file)); // rilascio lock
            free_file(tmptr); // dealloco file
            int table = icl_hash_delete(ds->fs_table, (void*)pathname, free, NULL); // lo tolgo dalla ht
            // lo tolgo anche dalla coda
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

            if(table == 0) {
                rep = newreply(REPLY_YES, 0, NULL);
            }
            else {
                rep = newreply(REPLY_NO, 0, NULL);
            }

            if(cache == -1) {
                // consistenza della cache non garantita!
                // Non termino il server ma effettuo il log dell'evento
                if(logging(ds, 0, "[SERVER] api_rmFile: Consistenza cache persa") == -1) {
                    perror("[SERVER] api_rmFile: Consistenza cache persa");
                }
            }
        }
    }

    if(!rep) {
        return -1;
    }
    else {
        if(writen(client_sock, rep, sizeof(*rep)) != sizeof(*rep)) {
            free(rep);
            return -1;
        }
        free(rep);
    }

    return 0;
}
